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
#include "preview/scene/PreviewSlideMotionLayerState.h"
#include "preview/scene/PreviewTouchHoldLayerState.h"
#include "preview/scene/PreviewTouchJudgeLayerState.h"
#include "preview/scene/PreviewTouchLayerState.h"
#include "preview/scene/PreviewTrackLayerState.h"

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
    runtime_ = nullptr;
    teardownCore();
    window_ = nullptr;
}

void PreviewDCompSurface::onRuntimeFrameStateChanged()
{
    if (runtime_ == nullptr) {
        return;
    }
    const auto& state = runtime_->frameState();

    PreviewDCompFrameStateSnapshot snapshot;
    snapshot.revision = ++snapshotRevision_;
    snapshot.playheadSeconds = state.playheadSeconds;
    snapshot.sceneLogicalSize = currentClientPixelSize();
    snapshot.playing = false;

    // Phase 3.3a: build the backdrop + track layer descriptors on the
    // GUI thread and stash them in the snapshot. The render thread
    // walks them after publish. We use the QQuickWindow's logical size
    // for the playfield rect rather than a placeholder geometry — Phase
    // 4 replaces this with a per-placeholder rect, but for the Phase 1
    // top-left demo region the swap chain itself drives the projection,
    // and we just need *some* representative rect to feed the layer
    // builders.
    const QSize logicalSize = window_ != nullptr
        ? QSize(window_->width(), window_->height())
        : QSize(800, 800);
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

    // Backdrop (z=1 in the legacy stack). Snapshot owns its own QImage
    // copy so the render thread never reads runtime-mutated QImage state
    // — see Phase 3.3c-fix commit notes for why this matters.
    if (!state.assets.outlineImage.isNull()) {
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
    pushCircleBatch(scene::buildPreviewMuriPadLayerState(state, playfieldRect).circles);

    // Muri action (z=3) — solid-colour ellipses (Phase 3.5a).
    pushCircleBatch(scene::buildPreviewMuriActionLayerState(state, playfieldRect).circles);

    // Judge firework (z=4) — Phase 3.5c. Uses the windowed firework
    // layer cursor for activation timing.
    pushFireworkBatch(scene::buildPreviewJudgeFireworkLayerState(
        state, windowed(preparedCache_.judgeFireworkLayer(), judgeFireworkCursor_),
        playfieldRect));

    // Guide (z=5)
    pushSpriteBatch(scene::buildPreviewGuideLayerSprites(
        state, windowed(preparedCache_.guideLayer(), guideCursor_), playfieldRect));

    // Track (z=6)
    {
        auto layerState = scene::buildPreviewTrackLayerState(
            state, windowed(preparedCache_.slideLikeLayer(), trackCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Slide motion (z=7)
    {
        auto layerState = scene::buildPreviewSlideMotionLayerState(
            state, windowed(preparedCache_.slideLikeLayer(), slideMotionCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Judge effect (z=8)
    {
        auto layerState = scene::buildPreviewJudgeEffectLayerState(
            state, windowed(preparedCache_.judgeEffectLayer(), judgeEffectCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Touch judge (z=9)
    pushSpriteBatch(scene::buildPreviewTouchJudgeLayerState(
        state, windowed(preparedCache_.touchJudgeLayer(), touchJudgeCursor_), playfieldRect).sprites);

    // Head (z=10) — passes the asset cache so tinted base+overlay
    // composites are deduped across frames.
    {
        auto layerState = scene::buildPreviewHeadLayerState(
            state, windowed(preparedCache_.headLayer(), headCursor_), playfieldRect,
            &headRenderAssetCache_);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Touch (z=11)
    {
        auto layerState = scene::buildPreviewTouchLayerState(
            state, windowed(preparedCache_.touchLayer(), touchCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        appendOwnedImages(layerState.ownedImages);
    }

    // Touch hold (z=12) — sprites first, then arcs; legacy QSG renders
    // them as separate child nodes inside the same layer slot, with
    // arcs above sprites visually.
    {
        auto layerState = scene::buildPreviewTouchHoldLayerState(
            state, windowed(preparedCache_.touchHoldLayer(), touchHoldCursor_), playfieldRect);
        pushSpriteBatch(layerState.sprites);
        pushArcBatch(layerState.arcs);
        appendOwnedImages(layerState.ownedImages);
    }

    // Chart review (z=13) — special: not marker-windowed; uses
    // preparedEvents collected from the chart_review layer cursor.
    {
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
    {
        QVector<MuriJudgeSpriteEvent> activeEvents;
        QVector<int> activeMarkerIndices;
        preparedCache_.collectMaimuriDxJudgeData(
            maimuriDxJudgeCursor_.activePreparedIndices,
            &activeEvents, &activeMarkerIndices);
        scene::PreviewActiveMarkerView allMarkers(state.noteMarkers);
        pushSpriteBatch(scene::buildPreviewMaimuriDxJudgeLayerSprites(
            state, allMarkers, activeEvents, playfieldRect));
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
}

void PreviewDCompSurface::onWindowGeometryChanged()
{
    if (!initialised_) {
        // First geometry signal often arrives before the SG is fully
        // initialised. Try to bring up the surface if we now have a real
        // size + HWND.
        initialiseIfReady();
        return;
    }
    const QSize clientPx = currentClientPixelSize();
    if (clientPx.width() <= 0 || clientPx.height() <= 0) {
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
