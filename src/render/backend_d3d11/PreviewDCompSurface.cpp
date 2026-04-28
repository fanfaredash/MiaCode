#include "render/backend_d3d11/PreviewDCompSurface.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewChartReviewLayerState.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewGuideLayerState.h"
#include "core/scene/PreviewHeadLayerState.h"
#include "core/scene/PreviewJudgeEffectLayerState.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"
#include "core/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "core/scene/PreviewMuriActionLayerState.h"
#include "core/scene/PreviewMuriPadLayerState.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"

#include <QPainter>
#include "core/scene/PreviewSlideMotionLayerState.h"
#include "core/scene/PreviewTouchHoldLayerState.h"
#include "core/scene/PreviewTouchJudgeLayerState.h"
#include "core/scene/PreviewTouchLayerState.h"
#include "core/scene/PreviewTrackLayerState.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>

namespace miacode::preview::dcomp {

namespace {

void logSurface(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("render/backend_d3d11/surface"),
        payload);
}

}  // namespace

PreviewDCompSurface::PreviewDCompSurface(QObject* parent)
    : QObject(parent)
{
    // Render-thread-driven publishing. Each Present on the render thread
    // fires `presented` carrying its emit timestamp; we re-enter
    // onRendererPresented on the GUI thread (which records dispatch
    // latency, then calls onRuntimeFrameStateChanged to build + publish
    // a snapshot that's vsync-aligned). Paired with the gating in
    // onRuntimeFrameStateChanged, this makes playback publishes uniformly
    // spaced at the display refresh interval — which is what fixed the
    // playhead-delta variance the diagnostic exposed.
    connect(&renderer_, &PreviewDCompRenderer::presented, this,
            &PreviewDCompSurface::onRendererPresented,
            Qt::QueuedConnection);

    // Worker-thread HUD rebuild. The watcher lives on the GUI thread;
    // its `finished` signal is delivered here by Qt's auto-connection,
    // and onHudRebuildFinished swaps the produced QImage into hudImage_.
    connect(&hudRebuildWatcher_,
            &QFutureWatcher<QSharedPointer<QImage>>::finished,
            this,
            &PreviewDCompSurface::onHudRebuildFinished);
}

PreviewDCompSurface::~PreviewDCompSurface()
{
    detach();
}

void PreviewDCompSurface::attachToWindow(QQuickWindow* window)
{
    if (window_ == window) {
        return;
    }
    detach();
    window_ = window;
    if (window_ == nullptr) {
        return;
    }
    logSurface("attach",
               QStringLiteral("window=0x%1 visible=%2 width=%3 height=%4")
                   .arg(reinterpret_cast<quintptr>(window), 0, 16)
                   .arg(window->isVisible() ? 1 : 0)
                   .arg(window->width())
                   .arg(window->height()));

    connect(window_, &QQuickWindow::sceneGraphInitialized, this,
            &PreviewDCompSurface::onWindowSceneGraphInitialized,
            Qt::DirectConnection);
    connect(window_, &QQuickWindow::widthChanged, this,
            &PreviewDCompSurface::onWindowGeometryChanged);
    connect(window_, &QQuickWindow::heightChanged, this,
            &PreviewDCompSurface::onWindowGeometryChanged);
    connect(window_, &QQuickWindow::visibilityChanged, this,
            &PreviewDCompSurface::onWindowVisibilityChanged);
    // Top-level HWND mode also needs to follow editor position changes
    // — moving the editor must move the popup that hosts the chart.
    // QWindow::xChanged / yChanged fire on the GUI thread when the
    // user drags the editor. The slot reapplies the tracked item's
    // screen coords through MoveWindow.
    connect(window_, &QWindow::xChanged, this,
            &PreviewDCompSurface::onWindowGeometryChanged);
    connect(window_, &QWindow::yChanged, this,
            &PreviewDCompSurface::onWindowGeometryChanged);
    // Phase 4e — DPI / multi-monitor handling. screenChanged fires
    // when the user drags the window between monitors with different
    // DPI scaling. effectiveDevicePixelRatio updates on the new
    // screen, so we need to re-derive the swap chain pixel size and
    // visual transform from the tracked item's logical bounds × new
    // DPR. screenChanged on QQuickWindow fires on the GUI thread.
    connect(window_, &QQuickWindow::screenChanged, this,
            [this](QScreen*) {
                logSurface("screen_changed",
                           QStringLiteral("dpr=%1")
                               .arg(window_ != nullptr
                                    ? window_->effectiveDevicePixelRatio()
                                    : 0.0));
                applyTrackedItemGeometry();
            });
    connect(window_, &QObject::destroyed, this, &PreviewDCompSurface::detach);

    // Diagnostic: tally Qt's per-vsync Presents so logPresentRateDiagnostic()
    // can compare them to DComp's. DirectConnection so the increment runs
    // on the QSG render thread without a queued event allocation. The
    // atomic is read from the GUI thread by the periodic logger.
    qtFrameSwapDiagConnection_ = connect(
        window_, &QQuickWindow::frameSwapped, this,
        [this]() {
            qtFrameSwapCount_.fetch_add(1, std::memory_order_relaxed);
        },
        Qt::DirectConnection);
    if (!presentRateDiagTimer_.isActive()) {
        presentRateDiagTimer_.setInterval(1000);
        presentRateDiagTimer_.setTimerType(Qt::CoarseTimer);
        QObject::disconnect(&presentRateDiagTimer_, nullptr, this, nullptr);
        connect(&presentRateDiagTimer_, &QTimer::timeout, this,
                &PreviewDCompSurface::logPresentRateDiagnostic);
        lastDiagDCompFrames_ = 0;
        lastDiagQtFrames_ = 0;
        lastDiagNs_ = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
        presentRateDiagTimer_.start();
    }

    // If the window is already initialised + visible, init right away.
    if (window_->isSceneGraphInitialized()) {
        initialiseIfReady();
    }
}

void PreviewDCompSurface::setLayerFlags(
    miacode::preview::scene::PreviewRenderLayerFlags flags)
{
    if (layerFlags_ == flags) return;
    layerFlags_ = flags;
    onRuntimeFrameStateChanged();  // republish so the change shows immediately
}

void PreviewDCompSurface::setRuntime(PreviewRuntime* runtime)
{
    if (runtime_ == runtime) {
        return;
    }
    if (runtimeFrameStateConnection_) {
        QObject::disconnect(runtimeFrameStateConnection_);
        runtimeFrameStateConnection_ = QMetaObject::Connection();
    }
    runtime_ = runtime;
    renderPlayheadInitialized_ = false;
    if (runtime_ != nullptr) {
        // Phase 4-perf-fix: QueuedConnection, NOT Direct. The runtime's
        // tick path looks like
        //   1. setPlayheadSeconds(audioSecond, /*requestUpdate=*/true)
        //         → frameState.playhead = audioSecond, emit signal
        //   2. visualSecond = applyVisualClockSmoothing(audioSecond, …)
        //   3. setPlayheadSeconds(visualSecond, /*requestUpdate=*/false)
        //         → frameState.playhead = visualSecond, NO emit
        // A DirectConnection slot fires inside step 1, so it captures
        // the audio-time playhead — without the +16.67ms visual
        // lookahead the legacy QSG path got because it reads
        // frameState() at QSG render time (after step 3 settled).
        // Result: DComp lags audio by one vsync vs legacy. Queueing
        // defers the slot until after the tick completes; by then
        // step 3 has overwritten the playhead with the
        // smoothing-adjusted, lookahead-biased value, so the snapshot
        // captures what legacy would have rendered.
        runtimeFrameStateConnection_ = QObject::connect(
            runtime_, &PreviewRuntime::frameStateChanged, this,
            &PreviewDCompSurface::onRuntimeFrameStateChanged,
            Qt::QueuedConnection);
        // Publish an initial snapshot so the renderer has valid data
        // before the first frameStateChanged fires (e.g. during the
        // window's pre-playback idle phase).
        onRuntimeFrameStateChanged();
        logSurface("runtime_attached",
                   QStringLiteral("runtime=0x%1")
                       .arg(reinterpret_cast<quintptr>(runtime), 0, 16));
    } else {
        logSurface("runtime_detached");
    }
}

void PreviewDCompSurface::detach()
{
    // Wait for any in-flight HUD rebuild to finish before tearing
    // down — the worker captures stateCopy by value so it doesn't
    // dereference into our memory, but the watcher's `finished` slot
    // fires on this object and would crash if it ran post-destruction.
    if (hudRebuildWatcher_.isRunning()) {
        hudRebuildWatcher_.waitForFinished();
    }
    hudRebuildInFlight_ = false;

    if (presentRateDiagTimer_.isActive()) {
        presentRateDiagTimer_.stop();
    }
    if (qtFrameSwapDiagConnection_) {
        QObject::disconnect(qtFrameSwapDiagConnection_);
        qtFrameSwapDiagConnection_ = QMetaObject::Connection();
    }
    if (window_) {
        disconnect(window_, nullptr, this, nullptr);
    }
    if (runtimeFrameStateConnection_) {
        QObject::disconnect(runtimeFrameStateConnection_);
        runtimeFrameStateConnection_ = QMetaObject::Connection();
    }
    setTrackedItem(nullptr);
    runtime_ = nullptr;
    teardownCore();
#ifdef Q_OS_WIN
    // Phase 4-perf-test (Option 3): pair with the lazy-create in
    // currentParentHwnd. teardownCore released DComp's references
    // to this HWND already; we own the Win32 handle so destroy it
    // here. Has to be after teardownCore (DComp expects the parent
    // HWND alive while the visual tree is being shut down).
    if (childHwnd_ != nullptr) {
        ::DestroyWindow(reinterpret_cast<HWND>(childHwnd_));
        childHwnd_ = nullptr;
        logSurface("child_hwnd_destroyed");
    }
#endif
    window_ = nullptr;
}

void PreviewDCompSurface::onRuntimeFrameStateChanged()
{
    if (runtime_ == nullptr) {
        return;
    }

    // Vsync-aligned publishing. When the render thread is actively
    // rendering (playback is on, not paused), it fires `presented` once
    // per Present — that signal drives the publish at uniform display-
    // refresh-rate intervals. While that's the case, suppress publishes
    // triggered by PreviewRuntime::frameStateChanged: those fire at
    // GUI-tick scheduler jitter (15-19 ms gaps with occasional cluster
    // pairs sub-ms apart), which is exactly the source of the playhead-
    // delta variance the user perceives as judder. The render thread's
    // signal is uniform; let it be the sole publish trigger during play.
    //
    // The `sender()` check distinguishes the two paths: when this slot
    // is invoked from the renderer's `presented` signal, sender() is
    // &renderer_, the gate is bypassed, and we publish. When invoked
    // from PreviewRuntime::frameStateChanged, sender() is runtime_, the
    // gate fires during playback and skips. Direct calls (e.g. from
    // setRuntime() to seed an initial snapshot) have sender() == nullptr
    // and bypass the gate as intended.
    const QObject* eventSource = sender();
    const bool fromRuntime = (eventSource == runtime_.data());
    if (fromRuntime && renderer_.isActivelyRendering()) {
        return;
    }
    // Note: gap and dispatch-latency diagnostics are recorded in
    // onRendererPresented (the dedicated slot for the renderer signal)
    // so they observe the full Qt::QueuedConnection dispatch path. By
    // the time we reach here from a direct call, sender() is nullptr
    // and the source-discriminator logic that used to live here would
    // mis-attribute every call.

    // No coalescing throttle. The original code clamped this slot to
    // ~71 Hz (14 ms floor) to "halve the build cost in pathological
    // cases", and a follow-up dropped it to 4 ms hoping queue
    // clustering would stop. Both versions kept dropping ~25 % of
    // legitimate 60 Hz ticks — Qt::QueuedConnection clusters two
    // consecutive events with sub-millisecond inter-call gaps when
    // the GUI thread becomes idle after being briefly busy, and even
    // a 1 ms floor would have rejected those.
    //
    // The tick_build_stats diagnostic settled the trade-off: avg
    // build cost is 0.3-0.7 ms, max ~10 ms during cache rebuilds.
    // Doing every emit's work — even at 100/s — is < 5 % of one CPU
    // core. Versus the alternative of dropping 25 % of publishes,
    // which manifests as 44 Hz motion on a 60 Hz display (the
    // user-visible judder this work is targeting). Trivial CPU cost
    // beats wrong-rate publishes.
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    lastPublishNs_ = nowNs;
    QElapsedTimer tickBuildTimer;
    tickBuildTimer.start();

    const auto& state = runtime_->frameState();

    // Render-thread playhead clock: advance by wall-clock time elapsed
    // since the last publish, then drift-correct against the runtime's
    // audio-time playhead. When `presented` drives this slot at uniform
    // vsync intervals, renderPlayheadSeconds_ advances by uniform amounts
    // per sample regardless of GUI-tick scheduler jitter — fixing the
    // 6-8 ms stddev playhead_delta_stats was reporting. The runtime's
    // playheadSeconds (state.playheadSeconds) is the audio-time anchor:
    // small drifts are corrected gradually by reading wall-clock elapsed,
    // large drifts (seek, pause/resume, > 50 ms) snap so we don't render
    // motion across a discontinuity.
    constexpr double kRenderPlayheadSnapSeconds = 0.050;
    if (!renderPlayheadInitialized_) {
        renderPlayheadSeconds_ = state.playheadSeconds;
        renderPlayheadLastSampleNs_ = nowNs;
        renderPlayheadInitialized_ = true;
    } else {
        const qint64 elapsedNs = nowNs - renderPlayheadLastSampleNs_;
        const double elapsedSeconds = static_cast<double>(elapsedNs) / 1.0e9;
        // Bound the step to [0, 100 ms] so a long stall (e.g., system
        // sleep / GC pause) doesn't make us race forward by seconds when
        // we wake; the drift snap below catches up cleanly instead.
        const double cappedElapsed = qBound(0.0, elapsedSeconds, 0.100);
        renderPlayheadSeconds_ += cappedElapsed;
        renderPlayheadLastSampleNs_ = nowNs;
        const double drift = state.playheadSeconds - renderPlayheadSeconds_;
        if (qAbs(drift) > kRenderPlayheadSnapSeconds) {
            renderPlayheadSeconds_ = state.playheadSeconds;
        }
    }

    PreviewDCompFrameStateSnapshot snapshot;
    snapshot.revision = ++snapshotRevision_;
    snapshot.playheadSeconds = renderPlayheadSeconds_;
    snapshot.playing = false;

    // Phase 4a: when a QML target item is tracked, the scene's logical
    // size is the item's bounding rect (matches the legacy QSG path —
    // PreviewQuickSceneRoot passes boundingRect().size() to all layer
    // builders as renderSize). Without a tracked item we fall back to
    // the QQuickWindow's logical size; that's the Phase-3 demo path
    // where the whole window scene squeezes into the 200x200 swap
    // chain, used for visual sanity checks before Phase 4 wired up
    // the placeholder geometry.
    QSize logicalSize;
    if (trackedItem_ != nullptr
        && trackedItem_->width() > 0.0
        && trackedItem_->height() > 0.0) {
        logicalSize = QSize(qRound(trackedItem_->width()),
                             qRound(trackedItem_->height()));
    }
    if (logicalSize.width() <= 0 || logicalSize.height() <= 0) {
        logicalSize = window_ != nullptr
            ? QSize(window_->width(), window_->height())
            : QSize(800, 800);
    }
    snapshot.sceneLogicalSize = logicalSize;
    const double layoutSquareScale = state.render.layoutSquareScale > 0.0
        ? state.render.layoutSquareScale
        : 1.0;
    const QRectF stageRect = miacode::preview::scene::stageRectForSize(logicalSize);
    const QRectF playfieldRect = miacode::preview::scene::playfieldRectForStage(
        stageRect, layoutSquareScale);

    // Phase 3.4: prepared-scene cache + per-layer cursor windowing.
    // Mirrors PreviewQuickSceneRoot::updatePaintNode lines 566-592 — sync
    // the cache (rebuild when chart content changes), reset every cursor
    // on rebuild, then incrementally advance each cursor to the current
    // playhead. This is what makes per-frame layer assembly cheap; the
    // simpler full-marker view used in Phase 3.3a walks every marker on
    // every frame, which scales badly for long charts.
    namespace scene = miacode::preview::scene;
    const bool cacheRebuilt = preparedCache_.sync(state);
    if (cacheRebuilt) {
        guideCursor_.reset();
        headCursor_.reset();
        trackCursor_.reset();
        slideMotionCursor_.reset();
        judgeEffectCursor_.reset();
        judgeFireworkCursor_.reset();
        touchCursor_.reset();
        touchJudgeCursor_.reset();
        touchHoldCursor_.reset();
        chartReviewCursor_.reset();
        maimuriDxJudgeCursor_.reset();
    }
    // Use the render-thread playhead clock (uniform 16.67 ms per sample
    // when driven by `presented` at vsync) so cursor advancement and
    // sprite positions match the playhead value the snapshot carries —
    // not the jittery state.playheadSeconds that the GUI tick wrote.
    const double playheadSeconds = renderPlayheadSeconds_;
    scene::syncPreviewLayerWindowCursor(preparedCache_.guideLayer(), playheadSeconds, &guideCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.headLayer(), playheadSeconds, &headCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.slideLikeLayer(), playheadSeconds, &trackCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.slideLikeLayer(), playheadSeconds, &slideMotionCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.judgeEffectLayer(), playheadSeconds, &judgeEffectCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.judgeFireworkLayer(), playheadSeconds, &judgeFireworkCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.touchLayer(), playheadSeconds, &touchCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.touchJudgeLayer(), playheadSeconds, &touchJudgeCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.touchHoldLayer(), playheadSeconds, &touchHoldCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.chartReviewLayer(), playheadSeconds, &chartReviewCursor_);
    scene::syncPreviewLayerWindowCursor(preparedCache_.maimuriDxJudgeLayer(), playheadSeconds, &maimuriDxJudgeCursor_);

    const auto windowed = [&](const auto& layer, const scene::PreviewLayerWindowCursor& cursor) {
        return scene::PreviewActiveMarkerView(state.noteMarkers, layer, cursor);
    };
    const auto appendOwnedImages =
        [&](const QVector<QSharedPointer<QImage>>& images) {
            for (const auto& image : images) {
                snapshot.retainedImages.append(image);
            }
        };
    // Phase 3.5a: tagged-batch pushers. Each layer pushes one or more
    // batches recording its primitive type + range; the pipeline then
    // walks `snapshot.batches` in order, switching shaders per type. An
    // empty layer pushes nothing.
    using BatchType = PreviewDCompFrameStateSnapshot::BatchType;
    const auto pushSpriteBatch = [&](const scene::PreviewSpriteDescriptors& sprites) {
        if (sprites.isEmpty()) return;
        PreviewDCompFrameStateSnapshot::DrawBatch batch;
        batch.type = BatchType::Sprites;
        batch.firstIndex = static_cast<qint32>(snapshot.sprites.size());
        batch.count = static_cast<qint32>(sprites.size());
        snapshot.sprites.append(sprites);
        snapshot.batches.append(batch);
    };
    const auto pushCircleBatch = [&](const scene::PreviewCircleDescriptors& circles) {
        if (circles.isEmpty()) return;
        PreviewDCompFrameStateSnapshot::DrawBatch batch;
        batch.type = BatchType::Circles;
        batch.firstIndex = static_cast<qint32>(snapshot.circles.size());
        batch.count = static_cast<qint32>(circles.size());
        snapshot.circles.append(circles);
        snapshot.batches.append(batch);
    };
    const auto pushArcBatch = [&](const scene::PreviewArcDescriptors& arcs) {
        if (arcs.isEmpty()) return;
        PreviewDCompFrameStateSnapshot::DrawBatch batch;
        batch.type = BatchType::Arcs;
        batch.firstIndex = static_cast<qint32>(snapshot.arcs.size());
        batch.count = static_cast<qint32>(arcs.size());
        snapshot.arcs.append(arcs);
        snapshot.batches.append(batch);
    };
    const auto pushFireworkBatch = [&](const scene::PreviewJudgeFireworkLayerState& fw) {
        if (!fw.active) return;
        PreviewDCompFrameStateSnapshot::DrawBatch batch;
        batch.type = BatchType::Fireworks;
        batch.firstIndex = static_cast<qint32>(snapshot.fireworks.size());
        batch.count = 1;
        snapshot.fireworks.append(fw);
        snapshot.batches.append(batch);
    };

    // Phase 3.6 — gate each layer on layerFlags_. Mirrors PreviewQuickSceneRoot's
    // updateLayerSlotProfiled(... previewRenderLayerEnabled(...)) checks.
    const auto enabled = [this](scene::PreviewRenderLayer layer) {
        return scene::previewRenderLayerEnabled(layerFlags_, layer);
    };

    // Stage background (z=0) — Phase 4c. Picks the first non-null
    // image from media.resolvedStageImage / mediaFrame /
    // retainedVideoFallbackFrame, fits it into stageRect by either
    // contain or cover (per state.render.backgroundScaleMode), and
    // pushes it as a sprite. cacheable mirrors the legacy
    // resolvedStageImageCacheable flag — true for static images that
    // recur frame-to-frame, false for video/dynamic frames so they
    // run through the per-frame transient compartment.
    //
    // Skipped when separate-surface external media is active (the
    // legacy QSG path also short-circuits in that case — the media
    // is rendered by an external HWND/QQuickWindow instead). Skipped
    // when no media is available so the dark canvasBg from the
    // embeddedPreviewFrame Rectangle shows through DComp's
    // transparent clear.
    if (enabled(scene::StageBackgroundLayer)) {
        const auto& media = state.media;
        const bool usesExternalMedia =
            media.presentationMode
                == scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem;
        QImage bgImage;
        bool bgCacheable = true;
        if (!usesExternalMedia) {
            if (!media.resolvedStageImage.isNull()) {
                bgImage = media.resolvedStageImage;
                bgCacheable = media.resolvedStageImageCacheable;
            } else if (!media.mediaFrame.isNull()) {
                bgImage = media.mediaFrame;
                bgCacheable = true;
            } else if (!media.retainedVideoFallbackFrame.isNull()) {
                bgImage = media.retainedVideoFallbackFrame;
                bgCacheable = false;  // last-known fallback, treat as transient
            }
        }
        if (!bgImage.isNull() && bgImage.width() > 0 && bgImage.height() > 0) {
            const bool fitContain =
                state.render.backgroundScaleMode == PreviewBackgroundScaleMode::FitContain;
            const QRectF targetRect = scene::mediaTargetRect(
                bgImage.size(), stageRect, fitContain);
            if (targetRect.width() > 0.0 && targetRect.height() > 0.0) {
                auto bgPtr = QSharedPointer<QImage>::create(bgImage);
                snapshot.retainedImages.append(bgPtr);
                scene::PreviewSpriteDescriptor bg;
                bg.image = bgPtr.data();
                bg.center = targetRect.center();
                bg.width = targetRect.width();
                bg.height = targetRect.height();
                bg.rotationDegrees = 0.0;
                bg.opacity = 1.0;
                bg.effect = scene::PreviewAnimatedSpriteEffect::None;
                bg.cacheable = bgCacheable;
                scene::PreviewSpriteDescriptors batch;
                batch.append(bg);
                pushSpriteBatch(batch);
            }
        }
    }

    // Backdrop (z=1 in the legacy stack). Snapshot owns its own QImage
    // copy so the render thread never reads runtime-mutated QImage state
    // — see Phase 3.3c-fix commit notes for why this matters.
    if (enabled(scene::BackdropLayer) && !state.assets.outlineImage.isNull()) {
        auto backdropImage =
            QSharedPointer<QImage>::create(state.assets.outlineImage);
        snapshot.retainedImages.append(backdropImage);
        scene::PreviewSpriteDescriptor backdrop;
        backdrop.image = backdropImage.data();
        backdrop.center = playfieldRect.center();
        backdrop.width = playfieldRect.width();
        backdrop.height = playfieldRect.height();
        backdrop.rotationDegrees = 0.0;
        backdrop.opacity = 1.0;
        backdrop.effect = scene::PreviewAnimatedSpriteEffect::None;
        backdrop.cacheable = true;
        scene::PreviewSpriteDescriptors backdropBatch;
        backdropBatch.append(backdrop);
        pushSpriteBatch(backdropBatch);
    }

    // The remaining layers push batches in the same back-to-front order
    // as PreviewQuickSceneRoot::updatePaintNode — z-order matters
    // because the pipeline issues draws in batch order with
    // premultiplied-alpha blending, so later batches paint over earlier
    // ones. Layer flags (which the user can toggle) aren't honoured
    // yet; Phase 3.6 wires them up alongside pixel-parity checks.

    // Muri pad (z=2) — solid-colour ellipses (Phase 3.5a).
    if (enabled(scene::MuriPadStateLayer)) {
        pushCircleBatch(scene::buildPreviewMuriPadLayerState(state, playfieldRect).circles);
    }

    // Muri action (z=3) — solid-colour ellipses (Phase 3.5a).
    if (enabled(scene::MuriActionLayer)) {
        pushCircleBatch(scene::buildPreviewMuriActionLayerState(state, playfieldRect).circles);
    }

    // Judge firework (z=4) — Phase 3.5c. Uses the windowed firework
    // layer cursor for activation timing.
    if (enabled(scene::JudgeFireworkLayer)) {
        pushFireworkBatch(scene::buildPreviewJudgeFireworkLayerState(
            state, windowed(preparedCache_.judgeFireworkLayer(), judgeFireworkCursor_),
            playfieldRect));
    }

    // Guide (z=5)
    if (enabled(scene::GuideLayer)) {
        pushSpriteBatch(scene::buildPreviewGuideLayerSprites(
            state, windowed(preparedCache_.guideLayer(), guideCursor_), playfieldRect));
    }

    // Track (z=6)
    if (enabled(scene::TrackLayer)) {
        auto layerState = scene::buildPreviewTrackLayerState(
            state, windowed(preparedCache_.slideLikeLayer(), trackCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Slide motion (z=7)
    if (enabled(scene::SlideMotionLayer)) {
        auto layerState = scene::buildPreviewSlideMotionLayerState(
            state, windowed(preparedCache_.slideLikeLayer(), slideMotionCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Judge effect (z=8)
    if (enabled(scene::JudgeLayer)) {
        auto layerState = scene::buildPreviewJudgeEffectLayerState(
            state, windowed(preparedCache_.judgeEffectLayer(), judgeEffectCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Touch judge (z=9)
    if (enabled(scene::JudgeTouchLayer)) {
        pushSpriteBatch(scene::buildPreviewTouchJudgeLayerState(
            state, windowed(preparedCache_.touchJudgeLayer(), touchJudgeCursor_), playfieldRect).sprites);
    }

    // Head (z=10) — passes the asset cache so tinted base+overlay
    // composites are deduped across frames.
    if (enabled(scene::HeadLayer)) {
        auto layerState = scene::buildPreviewHeadLayerState(
            state, windowed(preparedCache_.headLayer(), headCursor_), playfieldRect,
            &headRenderAssetCache_);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Touch (z=11)
    if (enabled(scene::TouchLayer)) {
        auto layerState = scene::buildPreviewTouchLayerState(
            state, windowed(preparedCache_.touchLayer(), touchCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Touch hold (z=12) — sprites first, then arcs; legacy QSG renders
    // them as separate child nodes inside the same layer slot, with
    // arcs above sprites visually.
    if (enabled(scene::TouchHoldLayer)) {
        auto layerState = scene::buildPreviewTouchHoldLayerState(
            state, windowed(preparedCache_.touchHoldLayer(), touchHoldCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        pushArcBatch(layerState.arcs);
        appendOwnedImages(layerState.ownedImages);
    }

    // Chart review (z=13) — special: not marker-windowed; uses
    // preparedEvents collected from the chart_review layer cursor.
    if (enabled(scene::ChartReviewLayer)) {
        scene::PreviewChartReviewPreparedEvents preparedEvents;
        preparedCache_.collectChartReviewEvents(
            chartReviewCursor_.activePreparedIndices, &preparedEvents);
        pushSpriteBatch(scene::buildPreviewChartReviewLayerSprites(
            state, playfieldRect, &preparedEvents));
    }

    // Maimuri DX judge (z=14) — uses the SIMPLE full-marker view
    // (PreviewQuickMaimuriDxJudgeLayer.cpp does the same) because the
    // maimuriDxJudgeLayer cursor windows the *event* list, not markers.
    // The cursor still feeds collectMaimuriDxJudgeData for the events
    // themselves.
    if (enabled(scene::MaimuriDxJudgeLayer)) {
        QVector<MuriJudgeSpriteEvent> activeEvents;
        QVector<int> activeMarkerIndices;
        preparedCache_.collectMaimuriDxJudgeData(
            maimuriDxJudgeCursor_.activePreparedIndices,
            &activeEvents, &activeMarkerIndices);
        scene::PreviewActiveMarkerView allMarkers(state.noteMarkers);
        pushSpriteBatch(scene::buildPreviewMaimuriDxJudgeLayerSprites(
            state, allMarkers, activeEvents, playfieldRect));
    }

    // Phase 4f — HUD overlay rendered via QPainter into a sprite.
    // Throttled at ~5 Hz (200 ms) so we don't pay the rasterisation
    // cost every frame; the rendered text is stable between rebuilds
    // and the texture cache hits on the unchanged QImage cacheKey.
    // Pushed last so it draws on top of everything (mirrors the
    // legacy z=2 placement of PreviewQuickHudLayer above the chart).
    //
    // Rasterise at PHYSICAL pixel size (logical × DPR), not logical:
    // DComp's viewport is physical pixels, so a logical-size texture
    // gets upscaled by DPR through bilinear filtering and the text
    // looks blurry. Sprite still covers the canvas at logical bounds;
    // the bigger texture maps 1:1 to physical viewport pixels =
    // crisp text. paintPreviewHudOverlay's font scaling already
    // hinges on shortSide / kHudReferenceShortSide (1024), so feeding
    // it the larger size auto-scales fonts to the right pixel size.
    if (enabled(scene::HudLayer)) {
        const qreal hudDpr = window_ != nullptr
            && window_->effectiveDevicePixelRatio() > 0.0
                ? window_->effectiveDevicePixelRatio() : 1.0;
        const QSize hudPixelSize(
            qMax(1, qRound(logicalSize.width() * hudDpr)),
            qMax(1, qRound(logicalSize.height() * hudDpr)));
        constexpr qint64 kHudRebuildIntervalNs = 200LL * 1000LL * 1000LL;
        const bool needsRebuild =
            !hudImage_
            || hudImage_->size() != hudPixelSize
            || lastHudRebuildNs_ == 0
            || (nowNs - lastHudRebuildNs_) >= kHudRebuildIntervalNs;
        if (needsRebuild && !hudRebuildInFlight_) {
            // Post the rebuild to a worker thread. Cost on the GUI hot
            // path is one PreviewFrameState copy (cheap due to Qt COW
            // containers + std::shared_ptr refcounts) plus a
            // QtConcurrent::run task post (~µs). The synchronous
            // QPainter rasterisation that used to live here moves off
            // the GUI thread entirely; the result lands in
            // onHudRebuildFinished.
            const scene::PreviewFrameState stateCopy = state;
            const auto layerFlagsCopy = layerFlags_;
            const QSize hudPixelSizeCopy = hudPixelSize;
            hudRebuildInFlight_ = true;
            lastHudRebuildNs_ = nowNs;
            auto future = QtConcurrent::run(
                [stateCopy, layerFlagsCopy, hudPixelSizeCopy]() {
                    QElapsedTimer workerTimer;
                    workerTimer.start();
                    auto fresh = QSharedPointer<QImage>::create(
                        hudPixelSizeCopy,
                        QImage::Format_RGBA8888_Premultiplied);
                    fresh->fill(Qt::transparent);
                    QPainter p(fresh.data());
                    miacode::preview::hud::paintPreviewHudOverlay(
                        p, stateCopy, hudPixelSizeCopy, layerFlagsCopy);
                    p.end();
                    return fresh;
                });
            hudRebuildWatcher_.setFuture(future);
        }
        if (hudImage_ && !hudImage_->isNull()) {
            snapshot.retainedImages.append(hudImage_);
            scene::PreviewSpriteDescriptor hud;
            hud.image = hudImage_.data();
            hud.center = QPointF(logicalSize.width() / 2.0,
                                  logicalSize.height() / 2.0);
            hud.width = logicalSize.width();
            hud.height = logicalSize.height();
            hud.rotationDegrees = 0.0;
            hud.opacity = 1.0;
            hud.effect = scene::PreviewAnimatedSpriteEffect::None;
            hud.cacheable = true;
            scene::PreviewSpriteDescriptors batch;
            batch.append(hud);
            pushSpriteBatch(batch);
        }
    }

    if ((snapshot.revision % 30) == 0 || snapshot.revision <= 5) {
        logSurface("snapshot_published",
                   QStringLiteral("revision=%1 sprites=%2 circles=%3 arcs=%4 fireworks=%5 batches=%6 retained=%7 playhead=%8 logical=%9x%10 cache_rebuilt=%11")
                       .arg(snapshot.revision)
                       .arg(snapshot.sprites.size())
                       .arg(snapshot.circles.size())
                       .arg(snapshot.arcs.size())
                       .arg(snapshot.fireworks.size())
                       .arg(snapshot.batches.size())
                       .arg(snapshot.retainedImages.size())
                       .arg(snapshot.playheadSeconds, 0, 'f', 3)
                       .arg(logicalSize.width()).arg(logicalSize.height())
                       .arg(cacheRebuilt ? 1 : 0));
    }

    renderer_.publishSnapshot(snapshot);

    const qint64 buildNs = tickBuildTimer.nsecsElapsed();
    ++tickBuildCount_;
    tickBuildSumNs_ += buildNs;
    if (buildNs > tickBuildMaxNs_) {
        tickBuildMaxNs_ = buildNs;
    }
}

bool PreviewDCompSurface::isActive() const
{
    return initialised_;
}

void PreviewDCompSurface::onRendererPresented(qint64 emittedAtNs)
{
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;

    // Inter-call gap on the renderer-driven slot path. Render thread
    // fires `presented` ~16.67 ms apart (Present(1, 0) is vsync-paced);
    // any deviation here comes from the GUI thread sitting on the
    // queued event because it was busy with something else.
    if (lastPresentedSlotNs_ != 0) {
        const qint64 gapNs = nowNs - lastPresentedSlotNs_;
        ++presentedGapCount_;
        presentedGapSumNs_ += gapNs;
        if (gapNs > presentedGapMaxNs_) presentedGapMaxNs_ = gapNs;
        if (presentedGapMinNs_ == 0 || gapNs < presentedGapMinNs_) {
            presentedGapMinNs_ = gapNs;
        }
        if (gapNs > 20'000'000LL) ++presentedGapLongCount_;
        if (gapNs < 8'000'000LL) ++presentedGapShortCount_;
    }
    lastPresentedSlotNs_ = nowNs;

    // Dispatch latency: how long the queued event sat before the slot
    // ran. A healthy GUI thread dispatches < 1 ms after emit. Anything
    // larger means the GUI thread was blocked — that's the actual
    // duration we're trying to attribute to a specific blocker.
    const qint64 latencyNs = nowNs - emittedAtNs;
    ++presentedLatencyCount_;
    presentedLatencySumNs_ += latencyNs;
    if (latencyNs > presentedLatencyMaxNs_) presentedLatencyMaxNs_ = latencyNs;
    if (presentedLatencyMinNs_ == 0 || latencyNs < presentedLatencyMinNs_) {
        presentedLatencyMinNs_ = latencyNs;
    }
    if (latencyNs > 5'000'000LL) ++presentedLatencyLongCount_;

    // Direct call into the publish path. sender() is nullptr inside,
    // which is correct: it's neither the runtime nor the renderer
    // signal frame, just a method call on the same object.
    onRuntimeFrameStateChanged();
}

void PreviewDCompSurface::onHudRebuildFinished()
{
    hudRebuildInFlight_ = false;
    if (!hudRebuildWatcher_.future().isValid()
        || hudRebuildWatcher_.future().resultCount() == 0) {
        return;
    }
    QSharedPointer<QImage> fresh = hudRebuildWatcher_.future().result();
    if (!fresh || fresh->isNull()) {
        return;
    }
    // Approximate per-rebuild stats: elapsed = now - last task start.
    // We can't time the worker thread directly without piping a
    // duration through the future, but lastHudRebuildNs_ was set when
    // the task was posted and the watcher fires when the task
    // returns, so this is a reasonable upper-bound proxy. Real wall-
    // clock time spent on the worker is what matters for confirming
    // the GUI hot path is unblocked, which the existing
    // tick_build_stats max_ms (now ≪ 5 ms) already shows.
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    const qint64 elapsedNs = lastHudRebuildNs_ != 0
        ? (nowNs - lastHudRebuildNs_) : 0;
    ++hudRebuildCount_;
    hudRebuildSumNs_ += elapsedNs;
    if (elapsedNs > hudRebuildMaxNs_) {
        hudRebuildMaxNs_ = elapsedNs;
    }
    hudImage_ = fresh;
}

void PreviewDCompSurface::onWindowSceneGraphInitialized()
{
    initialiseIfReady();
    tryDiscoverTrackedItem();
}

void PreviewDCompSurface::onWindowGeometryChanged()
{
    if (!initialised_) {
        // First geometry signal often arrives before the SG is fully
        // initialised. Try to bring up the surface if we now have a real
        // size + HWND.
        initialiseIfReady();
        tryDiscoverTrackedItem();
        return;
    }
    const QSize clientPx = currentClientPixelSize();
    if (clientPx.width() <= 0 || clientPx.height() <= 0) {
        return;
    }
    tryDiscoverTrackedItem();
    // The visual follows the tracked QML preview pane. If the pane
    // hasn't been discovered yet (early in QML construction), fall
    // back to covering the full client area so something visible
    // exists; applyTrackedItemGeometry will narrow it on the next
    // tick once the pane is found.
    if (trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
        return;
    }
    if (clientPx != core_.swapChainPixelSize()) {
        renderer_.requestResize(clientPx);
    }
    core_.setVisualTransform(0, 0, clientPx);
}

void PreviewDCompSurface::onWindowVisibilityChanged()
{
    if (window_ == nullptr) {
        return;
    }
    if (window_->isVisible() && !initialised_) {
        initialiseIfReady();
    }
    tryDiscoverTrackedItem();

    // Phase 4e — pause the render thread when the host window is
    // hidden or minimised. The DComp visual can't be visible to the
    // user in those states, so spinning on the frame-latency
    // waitable is wasted CPU/GPU. visibility() reads as Hidden /
    // Minimized / Maximized / FullScreen / AutomaticVisibility /
    // Windowed; pause for Hidden + Minimized only.
    const QWindow::Visibility vis = window_->visibility();
    const bool shouldPause = (vis == QWindow::Hidden)
                          || (vis == QWindow::Minimized);
    if (initialised_) {
        renderer_.setPaused(shouldPause);
    }
}

void PreviewDCompSurface::onTrackedItemGeometryChanged()
{
    applyTrackedItemGeometry();
}

void PreviewDCompSurface::tryDiscoverTrackedItem()
{
    if (trackedItem_ != nullptr) return;
    if (window_ == nullptr) return;
    QQuickItem* found = window_->findChild<QQuickItem*>(
        QStringLiteral("preview_dcomp_track_target"));
    if (found != nullptr) {
        setTrackedItem(found);
    }
}

void PreviewDCompSurface::setTrackedItem(QQuickItem* item)
{
    if (trackedItem_ == item) return;
    for (auto& c : trackedItemConnections_) {
        QObject::disconnect(c);
    }
    trackedItemConnections_.clear();
    trackedItem_ = item;
    if (trackedItem_ == nullptr) {
        logSurface("track_target_cleared");
        return;
    }
    // Position-, size-, and visibility-affecting signals on the item.
    // The item's mapToScene depends on its parent chain too — ancestor
    // moves can change the scene-space origin without firing on the
    // tracked item. For simplicity we re-read on every published
    // snapshot (renderer thread reads stable swap-chain state); GUI
    // ancestor moves in a stable layout are rare.
    auto track = [this](QMetaObject::Connection c) {
        if (c) trackedItemConnections_.append(c);
    };
    track(QObject::connect(trackedItem_, &QQuickItem::xChanged,
                            this, &PreviewDCompSurface::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::yChanged,
                            this, &PreviewDCompSurface::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::widthChanged,
                            this, &PreviewDCompSurface::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::heightChanged,
                            this, &PreviewDCompSurface::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::visibleChanged,
                            this, &PreviewDCompSurface::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::parentChanged,
                            this, &PreviewDCompSurface::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::windowChanged,
                            this, &PreviewDCompSurface::onTrackedItemGeometryChanged));
    logSurface("track_target_attached",
               QStringLiteral("item=0x%1 w=%2 h=%3")
                   .arg(reinterpret_cast<quintptr>(trackedItem_.data()), 0, 16)
                   .arg(trackedItem_->width())
                   .arg(trackedItem_->height()));
    applyTrackedItemGeometry();
}

void PreviewDCompSurface::applyTrackedItemGeometry()
{
    if (!initialised_) return;
    if (trackedItem_ == nullptr || window_ == nullptr) return;
    if (trackedItem_->window() != window_.data()) return;
    // Don't gate on isVisible: in DComp-exclusive mode QML deliberately
    // hides the tracked QQuickItem so the QSG sync phase skips it,
    // but the bounding rect (anchors-driven, follows the parent
    // chain) is still authoritative for where DComp must paint.
    // Skipping geometry updates when invisible would leave the swap
    // chain frozen at its last position even as the user resizes.
    const qreal itemW = trackedItem_->width();
    const qreal itemH = trackedItem_->height();
    if (itemW <= 0.0 || itemH <= 0.0) return;

    const QPointF topLeftScene = trackedItem_->mapToScene(QPointF(0, 0));
    const qreal dpr = window_->effectiveDevicePixelRatio() > 0.0
        ? window_->effectiveDevicePixelRatio() : 1.0;
    const int xPx = qRound(topLeftScene.x() * dpr);
    const int yPx = qRound(topLeftScene.y() * dpr);
    const QSize pixelSize(qRound(itemW * dpr), qRound(itemH * dpr));
    if (pixelSize.width() <= 0 || pixelSize.height() <= 0) return;

    if (pixelSize != core_.swapChainPixelSize()) {
        renderer_.requestResize(pixelSize);
    }
    // Top-level HWND mode: the popup HWND owns the DComp visual tree
    // and is positioned via MoveWindow in SCREEN-relative coordinates
    // (mapToGlobal + DPR). The DComp visual stays at (0, 0) inside
    // the popup's client area. In legacy in-place mode, the visual
    // is parented to the QQuickWindow's HWND directly and we use the
    // visual transform to position it in client coordinates.
#ifdef Q_OS_WIN
    if (childHwnd_ != nullptr) {
        const QPointF globalLogical = window_->mapToGlobal(topLeftScene);
        const int globalXPx = qRound(globalLogical.x() * dpr);
        const int globalYPx = qRound(globalLogical.y() * dpr);
        ::MoveWindow(reinterpret_cast<HWND>(childHwnd_),
                     globalXPx, globalYPx,
                     pixelSize.width(), pixelSize.height(),
                     TRUE);
        core_.setVisualTransform(0, 0, pixelSize);
    } else {
        core_.setVisualTransform(xPx, yPx, pixelSize);
    }
#else
    core_.setVisualTransform(xPx, yPx, pixelSize);
#endif

    // Phase 4a diagnostic: log the geometry decision sparingly so we
    // can confirm the tracked item's reported bounds match the visible
    // legacy preview frame. Only log when the values change to avoid
    // flooding on layout settle.
    static thread_local int s_lastX = INT_MIN;
    static thread_local int s_lastY = INT_MIN;
    static thread_local QSize s_lastSize;
    if (s_lastX != xPx || s_lastY != yPx || s_lastSize != pixelSize) {
        s_lastX = xPx;
        s_lastY = yPx;
        s_lastSize = pixelSize;
        logSurface("track_target_geometry",
                   QStringLiteral("scene_x=%1 scene_y=%2 item_w=%3 item_h=%4 dpr=%5 px_x=%6 px_y=%7 px_w=%8 px_h=%9 obj=%10")
                       .arg(topLeftScene.x(), 0, 'f', 2)
                       .arg(topLeftScene.y(), 0, 'f', 2)
                       .arg(itemW, 0, 'f', 2)
                       .arg(itemH, 0, 'f', 2)
                       .arg(dpr, 0, 'f', 2)
                       .arg(xPx).arg(yPx)
                       .arg(pixelSize.width()).arg(pixelSize.height())
                       .arg(trackedItem_->objectName()));
    }
}

bool PreviewDCompSurface::initialiseIfReady()
{
    if (initialised_) {
        return true;
    }
    if (window_ == nullptr) {
        return false;
    }

    void* parentHwnd = currentParentHwnd();
    if (parentHwnd == nullptr) {
        // Window not yet exposed at the platform layer. Try again on the
        // next signal.
        logSurface("init_deferred", QStringLiteral("reason=null_hwnd"));
        return false;
    }

    const QSize clientPx = currentClientPixelSize();
    if (clientPx.width() <= 0 || clientPx.height() <= 0) {
        logSurface("init_deferred", QStringLiteral("reason=zero_size"));
        return false;
    }

#ifdef Q_OS_WIN
    if (!core_.initialise(reinterpret_cast<HWND>(parentHwnd), clientPx)) {
        logSurface("init_failed");
        return false;
    }
#else
    Q_UNUSED(parentHwnd);
    return false;
#endif

    core_.setVisualTransform(0, 0, clientPx);
    initialised_ = true;
    logSurface("initialised",
               QStringLiteral("client_w=%1 client_h=%2")
                   .arg(clientPx.width()).arg(clientPx.height()));
    // Phase 2: start the dedicated render thread that paces on the
    // FRAME_LATENCY_WAITABLE_OBJECT and drives the swap chain. From now on
    // all rendering happens off the GUI thread.
    if (!renderer_.start(&core_)) {
        logSurface("renderer_start_failed");
    }
    return true;
}

void PreviewDCompSurface::teardownCore()
{
    if (initialised_) {
        // Stop the render thread BEFORE tearing down Core — Core owns the
        // D3D11 device and swap chain that the renderer uses, and the
        // waitable handle is closed during shutdown. stop() joins the
        // thread so we know it's not touching the resources during
        // shutdown.
        renderer_.stop();
        core_.shutdown();
        initialised_ = false;
        logSurface("teardown");
    }
}

QSize PreviewDCompSurface::currentClientPixelSize() const
{
    if (window_ == nullptr) {
        return {};
    }
    const qreal dpr = window_->effectiveDevicePixelRatio() > 0.0
                          ? window_->effectiveDevicePixelRatio()
                          : 1.0;
    return QSize(qRound(window_->width() * dpr),
                 qRound(window_->height() * dpr));
}

void* PreviewDCompSurface::currentParentHwnd() const
{
#ifdef Q_OS_WIN
    if (window_ == nullptr) {
        return nullptr;
    }
    // If we already created our top-level popup HWND, reuse it.
    if (childHwnd_ != nullptr) {
        return childHwnd_;
    }
    if (miacode::debug_options::previewDCompTopLevelHwndEnabled()) {
        // Top-level borderless transparent owned popup. WS_POPUP gives
        // us a window with no caption, no border, no system menu. The
        // extended styles together make it click-through
        // (WS_EX_TRANSPARENT), focus-stealing-proof (WS_EX_NOACTIVATE),
        // alpha-capable (WS_EX_LAYERED — required for transparent
        // composition), and absent from the taskbar / Alt-Tab list
        // (WS_EX_TOOLWINDOW). The QQuickWindow's HWND is the OWNER,
        // not the parent, which means the popup stays above the
        // editor and follows its minimise/restore lifecycle without
        // being clipped to the editor's client area (which is what
        // would happen with WS_CHILD).
        const HWND owner = reinterpret_cast<HWND>(window_->winId());
        if (owner == nullptr) {
            return nullptr;
        }
        const HWND popup = ::CreateWindowExW(
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED
                | WS_EX_TOOLWINDOW,
            L"STATIC",
            L"",
            WS_POPUP,
            0, 0, 1, 1,
            owner, nullptr,
            ::GetModuleHandleW(nullptr), nullptr);
        if (popup == nullptr) {
            const DWORD err = ::GetLastError();
            logSurface("toplevel_hwnd_create_failed",
                       QStringLiteral("err=%1").arg(err));
            return reinterpret_cast<void*>(owner);
        }
        // Fully opaque alpha — the chart content itself comes from
        // DComp's premultiplied compositing. The layered window flag
        // is required to coexist with the editor's swap chain
        // without producing a black box.
        ::SetLayeredWindowAttributes(popup, 0, 255, LWA_ALPHA);
        ::ShowWindow(popup, SW_SHOWNOACTIVATE);
        const_cast<PreviewDCompSurface*>(this)->childHwnd_ = popup;
        logSurface("toplevel_hwnd_created",
                   QStringLiteral("owner=0x%1 popup=0x%2")
                       .arg(reinterpret_cast<quintptr>(owner), 0, 16)
                       .arg(reinterpret_cast<quintptr>(popup), 0, 16));
        return reinterpret_cast<void*>(popup);
    }
    return reinterpret_cast<void*>(window_->winId());
#else
    return nullptr;
#endif
}

void PreviewDCompSurface::logPresentRateDiagnostic()
{
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    const qint64 dcompFrames = renderer_.framesRendered();
    const qint64 qtFrames =
        qtFrameSwapCount_.load(std::memory_order_relaxed);
    const qint64 deltaNs = nowNs - lastDiagNs_;
    const double deltaSec = deltaNs > 0 ? static_cast<double>(deltaNs) / 1.0e9
                                        : 1.0;
    const double dcompFps =
        static_cast<double>(dcompFrames - lastDiagDCompFrames_) / deltaSec;
    const double qtFps =
        static_cast<double>(qtFrames - lastDiagQtFrames_) / deltaSec;
    logSurface("present_rates",
               QStringLiteral(
                   "dcomp_fps=%1 qt_fps=%2 dcomp_total=%3 qt_total=%4 "
                   "interval_ms=%5 quiesce_qsg=%6 toplevel_hwnd=%7")
                   .arg(dcompFps, 0, 'f', 1)
                   .arg(qtFps, 0, 'f', 1)
                   .arg(dcompFrames)
                   .arg(qtFrames)
                   .arg(static_cast<double>(deltaNs) / 1.0e6, 0, 'f', 1)
                   .arg(miacode::debug_options::previewDCompQuiesceQsgEnabled() ? 1 : 0)
                   .arg(miacode::debug_options::previewDCompTopLevelHwndEnabled() ? 1 : 0));

    // Per-tick snapshot-build timing (GUI thread). If avg_ms is high
    // (≳5 ms), the snapshot build is the GUI thread bottleneck. If
    // avg_ms is low but publishes/sec is well below 60, the slot is
    // being dispatched late (queued-event latency, GUI thread doing
    // other work) and the fix is on the dispatch side.
    const double avgBuildMs =
        tickBuildCount_ > 0
            ? static_cast<double>(tickBuildSumNs_)
                  / static_cast<double>(tickBuildCount_) / 1.0e6
            : 0.0;
    const double maxBuildMs = static_cast<double>(tickBuildMaxNs_) / 1.0e6;
    logSurface(
        "tick_build_stats",
        QStringLiteral(
            "publishes=%1 avg_build_ms=%2 max_build_ms=%3 interval_ms=%4")
            .arg(tickBuildCount_)
            .arg(avgBuildMs, 0, 'f', 3)
            .arg(maxBuildMs, 0, 'f', 3)
            .arg(static_cast<double>(deltaNs) / 1.0e6, 0, 'f', 1));
    tickBuildCount_ = 0;
    tickBuildSumNs_ = 0;
    tickBuildMaxNs_ = 0;

    // Inter-call gap distribution. avg ≈ 16.67 ms = GUI thread is
    // dispatching presented signals on time. avg > 17 ms or
    // long_gap_count > 0 means the GUI thread was blocked by some
    // other event (sync handoff, paint, timeline tick) for at least
    // one vsync. short_gap_count counts cluster events (queue drain
    // catching up after a stall) — that's the symptom we're tracing.
    const double avgGapMs = presentedGapCount_ > 0
        ? static_cast<double>(presentedGapSumNs_)
            / static_cast<double>(presentedGapCount_) / 1.0e6
        : 0.0;
    const double maxGapMs =
        static_cast<double>(presentedGapMaxNs_) / 1.0e6;
    const double minGapMs =
        static_cast<double>(presentedGapMinNs_) / 1.0e6;
    logSurface(
        "presented_gap_stats",
        QStringLiteral(
            "samples=%1 avg_ms=%2 min_ms=%3 max_ms=%4 long_gaps=%5 "
            "short_gaps=%6")
            .arg(presentedGapCount_)
            .arg(avgGapMs, 0, 'f', 3)
            .arg(minGapMs, 0, 'f', 3)
            .arg(maxGapMs, 0, 'f', 3)
            .arg(presentedGapLongCount_)
            .arg(presentedGapShortCount_));
    presentedGapCount_ = 0;
    presentedGapSumNs_ = 0;
    presentedGapMaxNs_ = 0;
    presentedGapMinNs_ = 0;
    presentedGapLongCount_ = 0;
    presentedGapShortCount_ = 0;

    // Dispatch latency: time the queued `presented` event spent waiting
    // before its slot ran. avg < 1 ms means GUI thread is responsive;
    // long_count > 0 means it was blocked by another event for at
    // least 5 ms when the signal was queued — the duration of that
    // block IS the size of long-tailed latency outliers.
    const double avgLatencyMs = presentedLatencyCount_ > 0
        ? static_cast<double>(presentedLatencySumNs_)
            / static_cast<double>(presentedLatencyCount_) / 1.0e6
        : 0.0;
    const double maxLatencyMs =
        static_cast<double>(presentedLatencyMaxNs_) / 1.0e6;
    const double minLatencyMs =
        static_cast<double>(presentedLatencyMinNs_) / 1.0e6;
    logSurface(
        "presented_latency_stats",
        QStringLiteral(
            "samples=%1 avg_ms=%2 min_ms=%3 max_ms=%4 long_count=%5")
            .arg(presentedLatencyCount_)
            .arg(avgLatencyMs, 0, 'f', 3)
            .arg(minLatencyMs, 0, 'f', 3)
            .arg(maxLatencyMs, 0, 'f', 3)
            .arg(presentedLatencyLongCount_));
    presentedLatencyCount_ = 0;
    presentedLatencySumNs_ = 0;
    presentedLatencyMaxNs_ = 0;
    presentedLatencyMinNs_ = 0;
    presentedLatencyLongCount_ = 0;

    // HUD rebuild timing. The block runs synchronously inside
    // onRuntimeFrameStateChanged every 200 ms (≈ 5/s), so each entry
    // here is one full QPainter rasterise. If max_ms is high (5+ ms)
    // and rate is ~5/s, this is the dominant residual latency source
    // and the fix is to move it to a worker thread.
    const double avgHudMs = hudRebuildCount_ > 0
        ? static_cast<double>(hudRebuildSumNs_)
            / static_cast<double>(hudRebuildCount_) / 1.0e6
        : 0.0;
    const double maxHudMs =
        static_cast<double>(hudRebuildMaxNs_) / 1.0e6;
    logSurface(
        "hud_rebuild_stats",
        QStringLiteral(
            "rebuilds=%1 avg_ms=%2 max_ms=%3 interval_ms=%4")
            .arg(hudRebuildCount_)
            .arg(avgHudMs, 0, 'f', 3)
            .arg(maxHudMs, 0, 'f', 3)
            .arg(static_cast<double>(deltaNs) / 1.0e6, 0, 'f', 1));
    hudRebuildCount_ = 0;
    hudRebuildSumNs_ = 0;
    hudRebuildMaxNs_ = 0;

    lastDiagDCompFrames_ = dcompFrames;
    lastDiagQtFrames_ = qtFrames;
    lastDiagNs_ = nowNs;
}

}  // namespace miacode::preview::dcomp
