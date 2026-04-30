#include "render/backend_d3d11/TimelineRenderView.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include "sources/timeline/TimelineGridSource.h"
#include "sources/timeline/TimelineHeaderSource.h"
#include "sources/timeline/TimelineNotesSource.h"
#include "sources/timeline/TimelineOverlaySource.h"
#include "sources/timeline/TimelineWaveformSource.h"

#include <QQuickItem>
#include <QQuickWindow>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace miacode::preview::dcomp {

namespace {

void logTimelineView(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("render/backend_d3d11/timeline_view"),
        payload);
}

}  // namespace

TimelineRenderView::TimelineRenderView(QObject* parent)
    : QObject(parent)
{
    // Same shape as PreviewDCompSurface — the renderer's `presented`
    // signal drives a re-publish of the cached scene state at vsync
    // rate when the timeline state hasn't otherwise changed (so a
    // dragged playhead line keeps moving smoothly even if the bridge's
    // renderStateChanged fires irregularly).
    rendererPresentedConnection_ = connect(
        &renderer_, &PreviewDCompRenderer::presented, this,
        &TimelineRenderView::onRendererPresented,
        Qt::QueuedConnection);
}

TimelineRenderView::~TimelineRenderView()
{
    // See PreviewDCompSurface::~PreviewDCompSurface for the rationale —
    // disconnect BEFORE detach() runs renderer_.stop() / joins the
    // thread; otherwise a queued `presented` emit posted right before
    // the join completes could dispatch on a half-destroyed view.
    if (rendererPresentedConnection_) {
        QObject::disconnect(rendererPresentedConnection_);
        rendererPresentedConnection_ = QMetaObject::Connection();
    }
    detach();
}

void TimelineRenderView::attachToWindow(QQuickWindow* window)
{
    if (window_ == window) {
        return;
    }
    detach();
    window_ = window;
    if (window_ == nullptr) {
        return;
    }
    logTimelineView("attach",
                    QStringLiteral("window=0x%1 visible=%2 width=%3 height=%4")
                        .arg(reinterpret_cast<quintptr>(window), 0, 16)
                        .arg(window->isVisible() ? 1 : 0)
                        .arg(window->width())
                        .arg(window->height()));

    connect(window_, &QQuickWindow::sceneGraphInitialized, this,
            &TimelineRenderView::onWindowSceneGraphInitialized,
            Qt::DirectConnection);
    connect(window_, &QQuickWindow::widthChanged, this,
            &TimelineRenderView::onWindowGeometryChanged);
    connect(window_, &QQuickWindow::heightChanged, this,
            &TimelineRenderView::onWindowGeometryChanged);
    connect(window_, &QQuickWindow::visibilityChanged, this,
            &TimelineRenderView::onWindowVisibilityChanged);
    // Top-level HWND mode also needs to follow editor position changes.
    connect(window_, &QWindow::xChanged, this,
            &TimelineRenderView::onWindowGeometryChanged);
    connect(window_, &QWindow::yChanged, this,
            &TimelineRenderView::onWindowGeometryChanged);
    connect(window_, &QQuickWindow::screenChanged, this,
            [this](QScreen*) {
                applyTrackedItemGeometry();
            });
    connect(window_, &QObject::destroyed, this, &TimelineRenderView::detach);

    if (window_->isSceneGraphInitialized()) {
        initialiseIfReady();
    }
}

void TimelineRenderView::setSceneState(
    const miacode::timeline::TimelineSceneState& state)
{
    // Cache only — DO NOT publish immediately. The renderer's
    // `presented` signal (vsync-paced, max 60-120 Hz depending on
    // display) drives buildAndPublishSnapshot; the cached state is
    // picked up on the next vsync.
    //
    // Publishing immediately + on presented effectively doubled the
    // publish rate during playback (TimelineQuickStateBridge fires
    // renderStateChanged ~60 Hz when the playhead line moves; the
    // renderer also fires presented ~60 Hz). With each publish
    // rebuilding a snapshot of all timeline primitives — including
    // the QPainter text-label rasterise on cache misses — that
    // doubled GUI-thread work. The chart preview's renderer was
    // observed Present-stalling for up to 2.8s under this load
    // (verification screenshot: Present=11.9 FPS during play).
    sceneState_ = state;
    sceneStateValid_ = true;
}

void TimelineRenderView::setTrackedQuickItem(QQuickItem* item)
{
    setTrackedItem(item);
}

void TimelineRenderView::detach()
{
    if (window_) {
        disconnect(window_, nullptr, this, nullptr);
    }
    // rendererPresentedConnection_ stays — see destructor / chart-surface
    // notes for why detach() doesn't disconnect it.
    setTrackedItem(nullptr);
    teardownCore();
#ifdef Q_OS_WIN
    if (childHwnd_ != nullptr) {
        ::DestroyWindow(reinterpret_cast<HWND>(childHwnd_));
        childHwnd_ = nullptr;
        logTimelineView("child_hwnd_destroyed");
    }
#endif
    window_ = nullptr;
}

bool TimelineRenderView::isActive() const
{
    return initialised_;
}

void TimelineRenderView::onWindowSceneGraphInitialized()
{
    const bool wasInitialized = initialised_;
    initialiseIfReady();
    tryDiscoverTrackedItem();
    // Phase 3c-fix5 — when initialiseIfReady transitions from
    // false → true here, the previous setTrackedQuickItem call
    // (which arrived earlier via TimelineQuickItem's windowChanged
    // slot) ran applyTrackedItemGeometry while !initialised_ was
    // true, so it short-circuited at the !initialised_ guard. Now
    // that init succeeded, re-apply the geometry so the popup
    // sizes/positions to the tracked item bounds. Without this the
    // popup stays at the default (0, 0) + currentClientPixelSize
    // (full window) — which is exactly what produced the user's
    // "popup at the wrong place" verification screenshot.
    if (!wasInitialized && initialised_ && trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
    }
}

void TimelineRenderView::onWindowGeometryChanged()
{
    if (!initialised_) {
        initialiseIfReady();
        tryDiscoverTrackedItem();
        // Phase 3c-fix5 — same recovery as onWindowSceneGraphInitialized:
        // if init transitioned to true on this signal, the popup needs
        // its geometry applied now that the trackedItem (set earlier
        // from TimelineQuickItem's windowChanged) has a valid render
        // target.
        if (initialised_ && trackedItem_ != nullptr) {
            applyTrackedItemGeometry();
        }
        return;
    }
    const QSize clientPx = currentClientPixelSize();
    if (clientPx.width() <= 0 || clientPx.height() <= 0) {
        return;
    }
    tryDiscoverTrackedItem();
    if (trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
        return;
    }
    if (clientPx != core_.swapChainPixelSize()) {
        renderer_.requestResize(clientPx);
    }
    core_.setVisualTransform(0, 0, clientPx);
}

void TimelineRenderView::onWindowVisibilityChanged()
{
    if (window_ == nullptr) {
        return;
    }
    const bool wasInitialized = initialised_;
    if (window_->isVisible() && !initialised_) {
        initialiseIfReady();
    }
    tryDiscoverTrackedItem();
    // Phase 3c-fix5 — covers the third entry point where init can
    // transition from false → true. Same recovery as the other two
    // (sceneGraphInitialized, geometryChanged): apply tracked-item
    // geometry now that the popup is ready to receive it.
    if (!wasInitialized && initialised_ && trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
    }
    const QWindow::Visibility vis = window_->visibility();
    const bool shouldPause = (vis == QWindow::Hidden)
                          || (vis == QWindow::Minimized);
    if (initialised_) {
        renderer_.setPaused(shouldPause);
    }
}

void TimelineRenderView::onRendererPresented(qint64 emittedAtNs)
{
    Q_UNUSED(emittedAtNs);
    if (!sceneStateValid_) {
        return;
    }
    // Re-publish the cached state at vsync rate. Cheap because Qt's
    // COW vectors share data with the previous publish — the
    // PreviewDCompFrameStateSnapshot is rebuilt but its descriptor
    // vectors don't allocate fresh storage when nothing changed.
    buildAndPublishSnapshot();
}

void TimelineRenderView::onTrackedItemGeometryChanged()
{
    applyTrackedItemGeometry();
}

void TimelineRenderView::tryDiscoverTrackedItem()
{
    if (trackedItem_ != nullptr) return;
    if (window_ == nullptr) return;
    QQuickItem* found = window_->findChild<QQuickItem*>(
        QStringLiteral("timeline_dcomp_track_target"));
    if (found != nullptr) {
        setTrackedItem(found);
    }
}

void TimelineRenderView::setTrackedItem(QQuickItem* item)
{
    if (trackedItem_ == item) return;
    for (auto& c : trackedItemConnections_) {
        QObject::disconnect(c);
    }
    trackedItemConnections_.clear();
    trackedItem_ = item;
    if (trackedItem_ == nullptr) {
        logTimelineView("track_target_cleared");
        return;
    }
    auto track = [this](QMetaObject::Connection c) {
        if (c) trackedItemConnections_.append(c);
    };
    track(QObject::connect(trackedItem_, &QQuickItem::xChanged,
                            this, &TimelineRenderView::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::yChanged,
                            this, &TimelineRenderView::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::widthChanged,
                            this, &TimelineRenderView::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::heightChanged,
                            this, &TimelineRenderView::onTrackedItemGeometryChanged));
    track(QObject::connect(trackedItem_, &QQuickItem::visibleChanged,
                            this, &TimelineRenderView::onTrackedItemGeometryChanged));
    logTimelineView("track_target_set",
                    QStringLiteral("obj=%1").arg(trackedItem_->objectName()));
    applyTrackedItemGeometry();
}

void TimelineRenderView::applyTrackedItemGeometry()
{
    if (window_ == nullptr || trackedItem_ == nullptr || !initialised_) {
        return;
    }
    const QPointF topLeftScene = trackedItem_->mapToScene(QPointF(0.0, 0.0));
    const qreal itemW = trackedItem_->width();
    const qreal itemH = trackedItem_->height();
    if (itemW <= 0.0 || itemH <= 0.0) {
        return;
    }
    const qreal dpr = window_->effectiveDevicePixelRatio() > 0.0
                          ? window_->effectiveDevicePixelRatio()
                          : 1.0;
    const int xPx = qRound(topLeftScene.x() * dpr);
    const int yPx = qRound(topLeftScene.y() * dpr);
    const QSize pixelSize(qMax(1, qRound(itemW * dpr)),
                           qMax(1, qRound(itemH * dpr)));
    if (pixelSize != core_.swapChainPixelSize()) {
        renderer_.requestResize(pixelSize);
    }
#ifdef Q_OS_WIN
    // Phase 3c-fix10 — log geometry values regardless of mode (top-level
    // popup vs in-place). With Phase 3c-fix8's in-place mode, childHwnd_
    // is always null for the timeline view, so the previous log inside
    // the `if (childHwnd_ != nullptr)` branch never fired.
    {
        const QPointF windowGlobal = window_->mapToGlobal(topLeftScene);
        const QPointF itemGlobal = trackedItem_->mapToGlobal(QPointF(0.0, 0.0));
        static thread_local int s_lastX = INT_MIN;
        static thread_local int s_lastY = INT_MIN;
        static thread_local QSize s_lastSize;
        if (s_lastX != xPx || s_lastY != yPx
            || s_lastSize != pixelSize) {
            s_lastX = xPx;
            s_lastY = yPx;
            s_lastSize = pixelSize;
            logTimelineView(
                "track_target_geometry",
                QStringLiteral(
                    "scene_x=%1 scene_y=%2 item_w=%3 item_h=%4 dpr=%5 "
                    "scene_xPx=%6 scene_yPx=%7 "
                    "item_global=%8,%9 win_global=%10,%11 "
                    "px_w=%12 px_h=%13 mode=%14 obj=%15")
                    .arg(topLeftScene.x(), 0, 'f', 2)
                    .arg(topLeftScene.y(), 0, 'f', 2)
                    .arg(itemW, 0, 'f', 2)
                    .arg(itemH, 0, 'f', 2)
                    .arg(dpr, 0, 'f', 2)
                    .arg(xPx).arg(yPx)
                    .arg(qRound(itemGlobal.x() * dpr))
                    .arg(qRound(itemGlobal.y() * dpr))
                    .arg(qRound(windowGlobal.x() * dpr))
                    .arg(qRound(windowGlobal.y() * dpr))
                    .arg(pixelSize.width()).arg(pixelSize.height())
                    .arg(childHwnd_ != nullptr ? "popup" : "inplace")
                    .arg(trackedItem_->objectName()));
        }
    }
    if (childHwnd_ != nullptr) {
        const QPointF globalLogical = trackedItem_->mapToGlobal(QPointF(0.0, 0.0));
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
}

void TimelineRenderView::buildAndPublishSnapshot()
{
    if (!sceneStateValid_) {
        return;
    }
    ensureCompositorInitialized();

    // Phase 3c-fix9 — defensive geometry resync on every publish. The
    // QML layout settles AFTER our windowChanged slot runs in the
    // common startup path; the width/heightChanged signals we hook up
    // in setTrackedItem cover later changes, but the very first
    // setTrackedQuickItem call sees layout-uninitialized
    // width()/height() values (often the parent's tentative bounds),
    // which produce an oversized initial popup that only corrects
    // itself when a window resize / zoom-restore re-fires the
    // geometry signals (Figure 1 vs Figure 2 of the user's
    // verification screenshots). Re-applying geometry here is cheap
    // — setVisualTransform is a DComp transform write — and ensures
    // the popup stays in sync regardless of which signal we missed.
    if (initialised_ && trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
    }

    PreviewDCompFrameStateSnapshot snapshot;
    snapshot.revision = ++snapshotRevision_;
    snapshot.playheadSeconds = sceneState_.visibleStartSecond;  // not animated
    snapshot.playing = false;

    // Logical size = the tracked item's bounds (or window's if the
    // item isn't tracked yet). Same fallback as the chart surface.
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
            : QSize(800, 200);
    }
    snapshot.sceneLogicalSize = logicalSize;

    // Empty PreviewFrameState since the timeline doesn't read it. The
    // build context's `frameState` is a const-ref so we need a stable
    // empty object; thread-local static keeps it cheap.
    static thread_local const miacode::preview::scene::PreviewFrameState
        kEmptyFrameState{};
    const qreal dpr = (window_ != nullptr
                       && window_->effectiveDevicePixelRatio() > 0.0)
                          ? window_->effectiveDevicePixelRatio()
                          : 1.0;
    miacode::render::PreviewBuildContext ctx{
        kEmptyFrameState,
        logicalSize,
        QRectF(0, 0, logicalSize.width(), logicalSize.height()),
        QRectF(0, 0, logicalSize.width(), logicalSize.height()),
        miacode::preview::scene::kPreviewAllRenderLayers,
        0.0,
        dpr,
        &sceneState_,
    };
    compositor_.buildSnapshot(ctx, snapshot);

    if ((snapshot.revision % 60) == 0 || snapshot.revision <= 5) {
        logTimelineView(
            "snapshot_published",
            QStringLiteral(
                "revision=%1 timeline_rects=%2 timeline_lines=%3 batches=%4 "
                "logical=%5x%6")
                .arg(snapshot.revision)
                .arg(snapshot.timelineRects.size())
                .arg(snapshot.timelineLines.size())
                .arg(snapshot.batches.size())
                .arg(logicalSize.width())
                .arg(logicalSize.height()));
    }
    renderer_.publishSnapshot(snapshot);
}

bool TimelineRenderView::initialiseIfReady()
{
    if (initialised_) {
        return true;
    }
    if (window_ == nullptr) {
        return false;
    }
    void* parentHwnd = currentParentHwnd();
    if (parentHwnd == nullptr) {
        logTimelineView("init_deferred", QStringLiteral("reason=null_hwnd"));
        return false;
    }
    const QSize clientPx = currentClientPixelSize();
    if (clientPx.width() <= 0 || clientPx.height() <= 0) {
        logTimelineView("init_deferred", QStringLiteral("reason=zero_size"));
        return false;
    }
#ifdef Q_OS_WIN
    if (!core_.initialise(reinterpret_cast<HWND>(parentHwnd), clientPx)) {
        logTimelineView("init_failed");
        return false;
    }
#else
    Q_UNUSED(parentHwnd);
    return false;
#endif
    core_.setVisualTransform(0, 0, clientPx);
    initialised_ = true;
    logTimelineView("initialised",
                    QStringLiteral("client_w=%1 client_h=%2")
                        .arg(clientPx.width()).arg(clientPx.height()));
    if (!renderer_.start(&core_)) {
        logTimelineView("renderer_start_failed");
    }
    return true;
}

void TimelineRenderView::teardownCore()
{
    if (initialised_) {
        renderer_.stop();
        core_.shutdown();
        initialised_ = false;
        logTimelineView("teardown");
    }
}

QSize TimelineRenderView::currentClientPixelSize() const
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

void* TimelineRenderView::currentParentHwnd() const
{
#ifdef Q_OS_WIN
    if (window_ == nullptr) {
        return nullptr;
    }
    // Phase 3c-fix8 — TIMELINE always uses in-place mode (DComp visual
    // parented to the QQuickWindow's HWND directly). The chart preview
    // sticks with top-level popup mode because its tracked item maps
    // 1:1 to a screen rectangle Qt's QQuickWindow::mapToGlobal can
    // resolve. The timeline lives inside QuickShellNativeSurfaceHost's
    // multi-surface architecture (the QML scene is composed alongside
    // QWidget bridge surfaces), and the QQuickWindow→screen mapping
    // for an embedded item produced popup positions that didn't match
    // where TimelineQuickItem's QSG output actually rendered (user's
    // verification screenshot: popup at the wrong place ~250 px above
    // the visible timeline pane).
    //
    // In-place mode bypasses screen-coord mapping entirely: DComp's
    // visual is positioned via setVisualTransform within the
    // QQuickWindow's own client area, sharing the EXACT same coord
    // system QSG uses for TimelineQuickItem. If QSG renders at scene
    // (X, Y), DComp also renders at (X, Y); they're guaranteed to
    // overlap pixel-perfect.
    //
    // Trade-off: top-level popup mode gives DComp its own DWM
    // composition plane (no inter-swap-chain serialisation with QSG).
    // In-place mode shares the QQuickWindow's swap chain target, so
    // there's some DWM serialisation cost. For the timeline this is
    // fine — it doesn't have the chart preview's per-frame redraw
    // pressure (timeline state changes only on edit / scroll / playhead
    // tick, not every vsync).
    return reinterpret_cast<void*>(window_->winId());
#else
    return nullptr;
#endif
}

void TimelineRenderView::ensureCompositorInitialized()
{
    if (compositorInitialized_) {
        return;
    }
    compositorInitialized_ = true;
    using namespace miacode::sources::timeline;
    compositor_.registerSource(std::make_unique<TimelineGridSource>());
    compositor_.registerSource(std::make_unique<TimelineWaveformSource>());
    compositor_.registerSource(std::make_unique<TimelineNotesSource>(&spriteAssetCache_));
    compositor_.registerSource(std::make_unique<TimelineHeaderSource>(&labelCache_));
    compositor_.registerSource(std::make_unique<TimelineOverlaySource>());
    logTimelineView("compositor_initialized",
                    QStringLiteral("source_count=%1")
                        .arg(compositor_.sourceCount()));
}

}  // namespace miacode::preview::dcomp
