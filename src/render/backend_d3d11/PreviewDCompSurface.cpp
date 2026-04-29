#include "render/backend_d3d11/PreviewDCompSurface.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewSceneGeometry.h"

#include "sources/chart/BackdropSource.h"
#include "sources/chart/ChartReviewSource.h"
#include "sources/chart/GuideSource.h"
#include "sources/chart/HeadSource.h"
#include "sources/chart/HudSource.h"
#include "sources/chart/JudgeEffectSource.h"
#include "sources/chart/JudgeFireworkSource.h"
#include "sources/chart/MaimuriDxJudgeSource.h"
#include "sources/chart/MuriActionSource.h"
#include "sources/chart/MuriPadSource.h"
#include "sources/chart/SlideMotionSource.h"
#include "sources/chart/StageBackgroundSource.h"
#include "sources/chart/TouchHoldSource.h"
#include "sources/chart/TouchJudgeSource.h"
#include "sources/chart/TouchSource.h"
#include "sources/chart/TrackSource.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QQuickWindow>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
// Monotonic, high-resolution timestamp in nanoseconds. Same clock as
// the render thread's `presented(emittedAtNs)` payload (steady_clock).
// Used for diagnostics and for the renderPlayheadSeconds_ delta when
// the publish was driven directly (no render-thread emit timestamp).
inline qint64 monotonicNanoseconds()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

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
    // latency, then calls buildAndPublishSnapshot to produce a snapshot
    // that's vsync-aligned). Paired with the gating in
    // onRuntimeFrameStateChanged, this makes playback publishes uniformly
    // spaced at the display refresh interval — which is what fixed the
    // playhead-delta variance the diagnostic exposed.
    //
    // Connection captured into rendererPresentedConnection_ so detach()
    // can disconnect it BEFORE renderer_.stop() joins; without that,
    // a queued `presented` emit posted right before stop() returns can
    // dispatch on a half-destroyed surface during shutdown (cross-thread
    // UAF — produced the 2 GB process dumps).
    rendererPresentedConnection_ = connect(
        &renderer_, &PreviewDCompRenderer::presented, this,
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
    // Disconnect the render thread's `presented` signal BEFORE detach()
    // (which calls teardownCore() / renderer_.stop() / joins the thread).
    // Without this, any queued emit posted right before stop() returns
    // can dispatch on a half-destroyed surface during shutdown — that's
    // the cross-thread UAF that produced the 2 GB process dumps.
    //
    // Done in the destructor (not in detach()) because detach() is also
    // called from attachToWindow() during a window-switch — and the
    // connection is set up ONCE in the constructor, so disconnecting it
    // on every detach would leave the surface deaf to the render thread
    // after the first re-attach.
    if (rendererPresentedConnection_) {
        QObject::disconnect(rendererPresentedConnection_);
        rendererPresentedConnection_ = QMetaObject::Connection();
    }
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
        lastDiagNs_ = monotonicNanoseconds();
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
    // NOTE: rendererPresentedConnection_ is intentionally NOT
    // disconnected here. It's set up once in the constructor and the
    // destructor handles the disconnect (see ~PreviewDCompSurface).
    // detach() is called from both the destructor *and* attachToWindow
    // (during window switch) — disconnecting here would leave the
    // surface deaf to the render thread after the first re-attach.
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

    // emittedAtNs = 0 → no render-thread Present timestamp is available
    // (this is a runtime-driven or direct call). buildAndPublishSnapshot
    // falls back to the current monotonic now in that case.
    buildAndPublishSnapshot(0);
}

void PreviewDCompSurface::buildAndPublishSnapshot(qint64 emittedAtNs)
{
    if (runtime_ == nullptr) {
        return;
    }

    // Note: gap and dispatch-latency diagnostics are recorded in
    // onRendererPresented (the dedicated slot for the renderer signal)
    // so they observe the full Qt::QueuedConnection dispatch path. By
    // the time we reach here from a direct call we no longer have the
    // signal context.

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
    const qint64 nowNs = monotonicNanoseconds();
    lastPublishNs_ = nowNs;
    QElapsedTimer tickBuildTimer;
    tickBuildTimer.start();

    const auto& state = runtime_->frameState();

    // Render-thread playhead clock. When the call originates from
    // onRendererPresented, emittedAtNs is the render thread's monotonic
    // (steady_clock) timestamp captured immediately after Present(1, 0)
    // returned — i.e., the actual vsync moment. Using it for the
    // playhead delta means renderPlayheadSeconds_ advances by exactly
    // the vsync interval, immune to:
    //   - Qt::QueuedConnection dispatch latency (typically 0.5-3 ms,
    //     can spike to 10 ms under contention),
    //   - the ms-quantisation that QDateTime::currentMSecsSinceEpoch
    //     used to introduce (0.67 ms variance at 60 Hz),
    //   - GUI-thread scheduler jitter.
    //
    // For non-render-thread callers (setRuntime, the runtime-driven
    // path that survives the early return above when not actively
    // rendering), emittedAtNs is 0 and we fall back to nowNs. The fall-
    // back path is rare (paused state) and stability there matters
    // less, since the renderer is re-presenting the same snapshot
    // anyway.
    //
    // The runtime's playheadSeconds (state.playheadSeconds) is the
    // audio-time anchor: small drifts are corrected gradually by
    // advancing the render clock independently, large drifts (seek,
    // pause/resume, > 50 ms) snap so we don't render motion across
    // a discontinuity.
    const qint64 playheadSampleNs = (emittedAtNs > 0) ? emittedAtNs : nowNs;
    constexpr double kRenderPlayheadSnapSeconds = 0.050;
    constexpr double kFixedNominal60HzSeconds = 1.0 / 60.0;
    // Pause detection: if state.playheadSeconds (the audio-time anchor
    // from PreviewRuntime, after smoothing + lookahead) hasn't changed
    // since last call, audio is paused — visual playhead must NOT
    // advance, otherwise the fixed-nominal +16.67 ms / frame plus the
    // 50 ms drift snap produces a 4-frame oscillation visible as
    // flicker on per-playhead animations (judge effects, slide motion,
    // etc.). The first call (NaN) falls through to the playback path.
    const double currentAudioSeconds = state.playheadSeconds;
    const bool audioStalled =
        !std::isnan(lastObservedAudioSeconds_)
        && currentAudioSeconds == lastObservedAudioSeconds_;
    lastObservedAudioSeconds_ = currentAudioSeconds;
    if (!renderPlayheadInitialized_) {
        renderPlayheadSeconds_ = currentAudioSeconds;
        renderPlayheadLastSampleNs_ = playheadSampleNs;
        renderPlayheadInitialized_ = true;
    } else if (audioStalled) {
        // Pause: lock visual to audio anchor; no advance, no drift
        // accumulation, no snap-induced backward jumps. Each snapshot
        // carries the exact audio time, so paused-state animations
        // are visually frozen on a stable frame.
        renderPlayheadSeconds_ = currentAudioSeconds;
        renderPlayheadLastSampleNs_ = playheadSampleNs;
    } else {
        // Playback path.
        //
        // Render-thread-driven (emittedAtNs > 0): advance by a hardcoded
        // 60 Hz vsync interval. Empirically drives playhead_delta_stats
        // stddev_ms to 0.000 on the 60 Hz test hardware. Hardcoded for
        // now — two prior attempts to plumb the user's Video-Settings
        // selection (60 / 120 / Display Refresh) through PreviewRuntime
        // stalled the render thread mid-session for reasons not yet
        // pinned. Phase 5 polish reattempts plumbing with more care.
        //
        // Direct/runtime-driven (emittedAtNs == 0, e.g. setRuntime seed):
        // measured wall-clock advance bounded to [0, 100 ms] so a long
        // stall doesn't race forward.
        //
        // Drift snap (>50 ms) catches seek and sustained missed-vsync.
        double advanceSeconds;
        if (emittedAtNs > 0) {
            advanceSeconds = kFixedNominal60HzSeconds;
        } else {
            const qint64 elapsedNs =
                playheadSampleNs - renderPlayheadLastSampleNs_;
            const double elapsedSeconds =
                static_cast<double>(elapsedNs) / 1.0e9;
            advanceSeconds = qBound(0.0, elapsedSeconds, 0.100);
        }
        renderPlayheadSeconds_ += advanceSeconds;
        renderPlayheadLastSampleNs_ = playheadSampleNs;
        const double drift = currentAudioSeconds - renderPlayheadSeconds_;
        if (qAbs(drift) > kRenderPlayheadSnapSeconds) {
            renderPlayheadSeconds_ = currentAudioSeconds;
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

    // Phase 2c — OBS-style source/compositor walk. The compositor owns
    // an ordered list of IPreviewSource instances (chart sprite layers,
    // HUD overlay) and contributes each enabled source's descriptors in
    // z-order. Each source owns at most one batch type; the few that
    // emit multiple (track, slide motion, head, touch, touch_hold) push
    // them in their own contributeToSnapshot. Walks the *entire* legacy
    // layer set (StageBackground … HudLayer) — same draw output as the
    // pre-refactor inline code, just dispatched via virtual calls.
    //
    // The surface still owns the per-frame state the cursored sources
    // depend on — preparedCache_, the eleven cursor members,
    // headRenderAssetCache_, and the four HUD watcher fields — because
    // those are tied to the surface's lifetime, not the compositor's.
    // Sources hold non-owning pointers; lazy-initialised on the first
    // build so all surface members are constructed first.
    ensureCompositorInitialized();
    const qreal dpr = (window_ != nullptr
                       && window_->effectiveDevicePixelRatio() > 0.0)
                          ? window_->effectiveDevicePixelRatio()
                          : 1.0;
    miacode::render::PreviewBuildContext ctx{
        state,
        logicalSize,
        stageRect,
        playfieldRect,
        layerFlags_,
        renderPlayheadSeconds_,
        dpr,
    };
    compositor_.buildSnapshot(ctx, snapshot);

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

void PreviewDCompSurface::ensureCompositorInitialized()
{
    if (compositorInitialized_) {
        return;
    }
    compositorInitialized_ = true;

    // Register every chart layer in legacy z-order. Compositor sorts on
    // registerSource(), so the registration order doesn't matter for
    // drawing — only zOrder() does. We still register in z-ascending
    // order for readability and to make a future stable-z layer (two
    // sources at the same z, like touch_hold sprites + arcs if we ever
    // split that source) keep its pre-refactor order.
    using namespace miacode::sources::chart;
    compositor_.registerSource(std::make_unique<StageBackgroundSource>());
    compositor_.registerSource(std::make_unique<BackdropSource>());
    compositor_.registerSource(std::make_unique<MuriPadSource>());
    compositor_.registerSource(std::make_unique<MuriActionSource>());
    compositor_.registerSource(std::make_unique<JudgeFireworkSource>(
        &preparedCache_, &judgeFireworkCursor_));
    compositor_.registerSource(std::make_unique<GuideSource>(
        &preparedCache_, &guideCursor_));
    compositor_.registerSource(std::make_unique<TrackSource>(
        &preparedCache_, &trackCursor_));
    compositor_.registerSource(std::make_unique<SlideMotionSource>(
        &preparedCache_, &slideMotionCursor_));
    compositor_.registerSource(std::make_unique<JudgeEffectSource>(
        &preparedCache_, &judgeEffectCursor_));
    compositor_.registerSource(std::make_unique<TouchJudgeSource>(
        &preparedCache_, &touchJudgeCursor_));
    compositor_.registerSource(std::make_unique<HeadSource>(
        &preparedCache_, &headCursor_, &headRenderAssetCache_));
    compositor_.registerSource(std::make_unique<TouchSource>(
        &preparedCache_, &touchCursor_));
    compositor_.registerSource(std::make_unique<TouchHoldSource>(
        &preparedCache_, &touchHoldCursor_));
    compositor_.registerSource(std::make_unique<ChartReviewSource>(
        &preparedCache_, &chartReviewCursor_));
    compositor_.registerSource(std::make_unique<MaimuriDxJudgeSource>(
        &preparedCache_, &maimuriDxJudgeCursor_));
    // HUD source last — z=15 > all chart layers. Holds non-owning
    // pointers to four surface-owned fields the rebuild path mutates;
    // the watcher's `finished` signal stays connected to the surface
    // (which writes hudImage_ back). Single-threaded GUI access.
    compositor_.registerSource(std::make_unique<HudSource>(
        &hudImage_, &lastHudRebuildNs_, &hudRebuildWatcher_,
        &hudRebuildInFlight_));

    logSurface("compositor_initialized",
               QStringLiteral("source_count=%1").arg(compositor_.sourceCount()));
}

void PreviewDCompSurface::onRendererPresented(qint64 emittedAtNs)
{
    // monotonicNanoseconds() uses the same clock as the render thread's
    // emittedAtNs, so subtracting them gives the true GUI-thread
    // dispatch latency in nanoseconds.
    const qint64 nowNs = monotonicNanoseconds();

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

    // Forward the render-thread Present timestamp into the publish
    // helper. The helper uses it to advance renderPlayheadSeconds_ by
    // the actual vsync interval (immune to dispatch jitter), instead
    // of the GUI-thread-side wall-clock delta which would inherit it.
    buildAndPublishSnapshot(emittedAtNs);
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
    const qint64 nowNs = monotonicNanoseconds();
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
    const qint64 nowNs = monotonicNanoseconds();
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
