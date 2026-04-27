#include "preview/dcomp/PreviewDCompSurface.h"

#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewActiveMarkerView.h"
#include "preview/scene/PreviewChartReviewLayerState.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewGuideLayerState.h"
#include "preview/scene/PreviewHeadLayerState.h"
#include "preview/scene/PreviewJudgeEffectLayerState.h"
#include "preview/scene/PreviewJudgeFireworkLayerState.h"
#include "preview/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "preview/scene/PreviewMuriActionLayerState.h"
#include "preview/scene/PreviewMuriPadLayerState.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"

#include <QPainter>
#include "preview/scene/PreviewSlideMotionLayerState.h"
#include "preview/scene/PreviewTouchHoldLayerState.h"
#include "preview/scene/PreviewTouchJudgeLayerState.h"
#include "preview/scene/PreviewTouchLayerState.h"
#include "preview/scene/PreviewTrackLayerState.h"

#include <QDateTime>
#include <QQuickItem>
#include <QQuickWindow>

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
        QStringLiteral("preview/dcomp/surface"),
        payload);
}

// Phase 1 placement: 200×200 red square pinned 16px in from the top-left
// of the client area. A fixed offset is enough to verify the visual tree
// works; Phase 4 replaces this with a placeholder-driven transform.
constexpr int kTestRectInsetPx = 16;
constexpr int kTestRectBaseSidePx = 200;

QSize testRectSize(QSize clientSize)
{
    // Scale the rectangle proportionally to the window so the resize
    // demo per plan §3 Phase 1 is visible. Cap so it doesn't dominate
    // the entire window on small client areas.
    if (clientSize.width() <= 0 || clientSize.height() <= 0) {
        return { kTestRectBaseSidePx, kTestRectBaseSidePx };
    }
    const int side = std::min(
        std::max(64, std::min(clientSize.width(), clientSize.height()) / 4),
        kTestRectBaseSidePx);
    return { side, side };
}

}  // namespace

PreviewDCompSurface::PreviewDCompSurface(QObject* parent)
    : QObject(parent)
{
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
    connect(window_, &QObject::destroyed, this, &PreviewDCompSurface::detach);

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
    if (runtime_ != nullptr) {
        // DirectConnection so the GUI thread builds the snapshot and
        // publishes it inline with the signal — no event-loop hop. Both
        // slot and signal live on the GUI thread, so DirectConnection
        // is safe.
        runtimeFrameStateConnection_ = QObject::connect(
            runtime_, &PreviewRuntime::frameStateChanged, this,
            &PreviewDCompSurface::onRuntimeFrameStateChanged,
            Qt::DirectConnection);
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
    window_ = nullptr;
}

void PreviewDCompSurface::onRuntimeFrameStateChanged()
{
    if (runtime_ == nullptr) {
        return;
    }

    // Phase 4b-perf: throttle snapshot rebuild to ~60 Hz (the DComp
    // render thread's max consume rate). The runtime can fire
    // frameStateChanged faster than that — audio events, multiple
    // updates per visual frame, etc. — and any intermediate publishes
    // get overwritten before the render thread reads them. Skipping
    // the work entirely when the previous publish is < 16 ms old
    // halves the GUI-thread layer-build cost in pathological cases
    // without affecting the rendered frame rate.
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    if (lastPublishNs_ != 0 && (nowNs - lastPublishNs_) < 14'000'000LL) {
        return;
    }
    lastPublishNs_ = nowNs;

    const auto& state = runtime_->frameState();

    PreviewDCompFrameStateSnapshot snapshot;
    snapshot.revision = ++snapshotRevision_;
    snapshot.playheadSeconds = state.playheadSeconds;
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
    const double playheadSeconds = state.playheadSeconds;
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
    if (enabled(scene::HudLayer)) {
        constexpr qint64 kHudRebuildIntervalNs = 200LL * 1000LL * 1000LL;
        const bool needsRebuild =
            !hudImage_
            || hudImage_->size() != logicalSize
            || lastHudRebuildNs_ == 0
            || (nowNs - lastHudRebuildNs_) >= kHudRebuildIntervalNs;
        if (needsRebuild) {
            auto fresh = QSharedPointer<QImage>::create(
                logicalSize, QImage::Format_RGBA8888_Premultiplied);
            fresh->fill(Qt::transparent);
            QPainter p(fresh.data());
            miacode::preview::hud::paintPreviewHudOverlay(
                p, state, logicalSize, layerFlags_);
            p.end();
            hudImage_ = fresh;
            lastHudRebuildNs_ = nowNs;
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
}

bool PreviewDCompSurface::isActive() const
{
    return initialised_;
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
    // Phase 4a: when a QML target item is tracked the visual follows
    // the item; otherwise we fall back to the Phase-1 demo region.
    if (trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
        return;
    }
    // From Phase 2: route resize through the renderer so the swap chain
    // ResizeBuffers happens on the render thread between presents (the
    // only safe spot per DXGI). The visual transform is independent — we
    // can apply it from the GUI thread immediately because it only writes
    // to DComp's IDCompositionVisual, not the swap chain.
    const QSize rectSize = testRectSize(clientPx);
    if (rectSize != core_.swapChainPixelSize()) {
        renderer_.requestResize(rectSize);
    }
    core_.setVisualTransform(kTestRectInsetPx, kTestRectInsetPx, rectSize);
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
    if (!trackedItem_->isVisible()) return;
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
    core_.setVisualTransform(xPx, yPx, pixelSize);

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

    const QSize rectSize = testRectSize(clientPx);

#ifdef Q_OS_WIN
    if (!core_.initialise(reinterpret_cast<HWND>(parentHwnd), rectSize)) {
        logSurface("init_failed");
        return false;
    }
#else
    Q_UNUSED(parentHwnd);
    Q_UNUSED(rectSize);
    return false;
#endif

    core_.setVisualTransform(kTestRectInsetPx, kTestRectInsetPx, rectSize);
    initialised_ = true;
    logSurface("initialised",
               QStringLiteral("client_w=%1 client_h=%2 rect_w=%3 rect_h=%4")
                   .arg(clientPx.width()).arg(clientPx.height())
                   .arg(rectSize.width()).arg(rectSize.height()));
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
    return reinterpret_cast<void*>(window_->winId());
#else
    return nullptr;
#endif
}

}  // namespace miacode::preview::dcomp
