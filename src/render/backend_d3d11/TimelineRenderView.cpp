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
    sceneState_ = state;
    sceneStateValid_ = true;
    if (initialised_) {
        buildAndPublishSnapshot();
    }
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
    initialiseIfReady();
    tryDiscoverTrackedItem();
}

void TimelineRenderView::onWindowGeometryChanged()
{
    if (!initialised_) {
        initialiseIfReady();
        tryDiscoverTrackedItem();
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
    if (window_->isVisible() && !initialised_) {
        initialiseIfReady();
    }
    tryDiscoverTrackedItem();
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
    if (childHwnd_ != nullptr) {
        // Match PreviewDCompSurface's geometry maths exactly: mapToGlobal
        // converts the scene-space top-left to LOGICAL screen coords;
        // multiply by DPR to get screen PIXELS for MoveWindow. Using
        // logical numbers here (the previous version of this branch)
        // resulted in a popup that was 1/DPR the correct size on HiDPI
        // displays (DPR=1.5 → popup at 67% size, content corner shifted
        // up-left into the editor area — produced the gray-block overlap
        // in the user verification screenshot).
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
}

void TimelineRenderView::buildAndPublishSnapshot()
{
    if (!sceneStateValid_) {
        return;
    }
    ensureCompositorInitialized();

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
    if (childHwnd_ != nullptr) {
        return childHwnd_;
    }
    if (miacode::debug_options::previewDCompTopLevelHwndEnabled()) {
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
            logTimelineView("toplevel_hwnd_create_failed",
                            QStringLiteral("err=%1").arg(err));
            return reinterpret_cast<void*>(owner);
        }
        ::SetLayeredWindowAttributes(popup, 0, 255, LWA_ALPHA);
        ::ShowWindow(popup, SW_SHOWNOACTIVATE);
        const_cast<TimelineRenderView*>(this)->childHwnd_ = popup;
        logTimelineView("toplevel_hwnd_created",
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
