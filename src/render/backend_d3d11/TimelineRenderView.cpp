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

// Phase 3c-fix11 — force-log key geometry diagnostics regardless of
// runtimeDebugOutputEnabled. Lets the user capture these without
// having to remember `--debug` on every test session. The log overhead
// is bounded by the dedup check inside applyTrackedItemGeometry — it
// only emits on geometry change, not every publish.
void logTimelineViewForced(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("render/backend_d3d11/timeline_view"),
        payload,
        /*force=*/true);
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
    // Device-removed recovery — see PreviewDCompSurface for the full
    // rationale. Render thread emits deviceLost on D3D11 device removal;
    // we tear down + re-initialise Core to recover.
    connect(&renderer_, &PreviewDCompRenderer::deviceLost, this,
            &TimelineRenderView::onRendererDeviceLost,
            Qt::QueuedConnection);

    // Phase 3f-1 — startup visibility gate. Popup stays SW_HIDE from
    // creation until geometry has been quiescent for the debounce
    // window. The debounce timer is single-shot, restarted on every
    // applyTrackedItemGeometry call where the (xPx, yPx, pixelSize)
    // tuple actually changed. When the timer fires (= no geometry
    // change for the debounce duration), we ShowWindow.
    //
    // 80 ms covers the typical layout-settle cascade (the user's logs
    // showed all geometry transitions within ~230 ms; the LAST
    // transition was followed by a quiet period until snapshot
    // publishing began ~400 ms later). A shorter debounce risks
    // flashing the popup mid-cascade; a longer one delays first paint.
    popupVisibilityDebounce_.setSingleShot(true);
    popupVisibilityDebounce_.setInterval(80);
    connect(&popupVisibilityDebounce_, &QTimer::timeout, this, [this]() {
        // Geometry has been quiet for 80 ms — half of the show
        // criterion. The other half is `contentPublishedFrames_ >= 2`,
        // checked inside maybeShowPopupHwnd. Whichever arrives last
        // triggers the actual ShowWindow.
        geometrySettled_ = true;
        maybeShowPopupHwnd();
    });
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
    logTimelineViewForced(
        "attachToWindow_enter",
        QStringLiteral("incoming=0x%1 current=0x%2 same=%3")
            .arg(reinterpret_cast<quintptr>(window), 0, 16)
            .arg(reinterpret_cast<quintptr>(window_.data()), 0, 16)
            .arg(window_ == window ? 1 : 0));
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
    // Idle-detach fix — re-anchor + republish on focus return. See
    // PreviewDCompSurface::onWindowActiveChanged for the full rationale.
    connect(window_, &QQuickWindow::activeChanged, this,
            &TimelineRenderView::onWindowActiveChanged);
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
    logTimelineViewForced(
        "detach_enter",
        QStringLiteral("initialised=%1 childHwnd=0x%2 window=0x%3 trackedItem=0x%4")
            .arg(initialised_ ? 1 : 0)
            .arg(reinterpret_cast<quintptr>(childHwnd_), 0, 16)
            .arg(reinterpret_cast<quintptr>(window_.data()), 0, 16)
            .arg(reinterpret_cast<quintptr>(trackedItem_.data()), 0, 16));
    if (window_) {
        disconnect(window_, nullptr, this, nullptr);
    }
    // rendererPresentedConnection_ stays — see destructor / chart-surface
    // notes for why detach() doesn't disconnect it.
    setTrackedItem(nullptr);
    teardownCore();
#ifdef Q_OS_WIN
    if (childHwnd_ != nullptr) {
        // Phase 4e — unregister BEFORE the HWND vanishes so an
        // in-flight WM_WINDOWPOSCHANGED can't fire DeferWindowPos on
        // a freed handle. Same rationale as PreviewDCompSurface.
        unregisterFromPopupTracker();
        ::DestroyWindow(reinterpret_cast<HWND>(childHwnd_));
        childHwnd_ = nullptr;
        logTimelineViewForced("child_hwnd_destroyed");
    }
    // Phase 3f-1 — reset visibility-gate state so a re-attach starts
    // fresh (popup re-created hidden, debounce timer re-armed on next
    // applyTrackedItemGeometry).
    popupVisibilityDebounce_.stop();
    popupShown_ = false;
    lastAppliedXPx_ = INT_MIN;
    lastAppliedYPx_ = INT_MIN;
    lastAppliedPixelSize_ = QSize();
    // Phase 3f-4 — reset the content-frame gate so a re-attach
    // re-arms it (mirror of PreviewDCompSurface::detach handling).
    geometrySettled_ = false;
    contentPublishedFrames_ = 0;
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
    // Phase 4d-fix7-final2 — setVT before requestResize + displaySize
    // bundled with the resize request to avoid render-thread race.
    core_.setVisualTransform(0, 0, clientPx);
    if (clientPx != core_.swapChainPixelSize()) {
        renderer_.requestResize(clientPx, clientPx);
    }
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

void TimelineRenderView::onWindowActiveChanged()
{
    if (window_ == nullptr || !initialised_) {
        return;
    }
    const bool active = window_->isActive();
    if (!active) {
        // Beta20-fix3 — pause render thread on focus loss to prevent
        // DEVICE_REMOVED triggered by GPU power transitions during
        // prolonged out-of-focus playback. See
        // PreviewDCompSurface::onWindowActiveChanged for the full
        // rationale (Intel iGPU swap-chain invalidation, ~2s recovery
        // visible to user as a "preview restart" hiccup).
        renderer_.setPaused(true);
        return;
    }
    // Regained focus — resume the render thread, re-anchor the popup,
    // and force a republish so the render thread has fresh state.
    renderer_.setPaused(false);
    if (trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
        QTimer::singleShot(50, this, [this]() {
            if (trackedItem_ != nullptr) {
                applyTrackedItemGeometry();
            }
        });
    }
    if (sceneStateValid_) {
        buildAndPublishSnapshot();
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

void TimelineRenderView::onRendererDeviceLost()
{
    if (!initialised_) {
        return;
    }
    // Tear down + re-init on the same popup HWND. See
    // PreviewDCompSurface::onRendererDeviceLost for the full rationale.
    teardownCore();
    if (!initialiseIfReady()) {
        return;
    }
    if (trackedItem_ != nullptr) {
        applyTrackedItemGeometry();
    }
    if (sceneStateValid_) {
        buildAndPublishSnapshot();
    }
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
    // Issue #2 follow-up — hide the popup HWND when the tracked QQuickItem
    // is no longer effectively visible (e.g. user switched the bottom-tab
    // from Timeline to Validation/Muri, which sets the QML
    // TimelineTabSurface `visible: false`; or navigated to Chart Info
    // Settings, which destroys the tab area entirely). QQuickItem::isVisible
    // in Qt 6 returns the *effective* visibility (parent chain considered),
    // so checking it here detects both cases. Without this hide path the
    // popup HWND keeps painting at its last screen position — the user
    // sees a "ghost timeline" overlaid on whatever else is on screen.
    //
    // We preserve `geometrySettled_` and `contentPublishedFrames_` across
    // the hide/show cycle: those gates were designed for the FIRST show
    // (so the popup never appears mid-startup with stale clear-colour
    // content). After the first successful show, they've already done
    // their job; resetting them on every hide would force the user to
    // wait through the gate every time they re-open the Timeline tab.
#ifdef Q_OS_WIN
    if (childHwnd_ != nullptr) {
        const bool itemEffectivelyVisible = trackedItem_->isVisible();
        if (!itemEffectivelyVisible) {
            if (popupShown_) {
                ::ShowWindow(reinterpret_cast<HWND>(childHwnd_), SW_HIDE);
                popupShown_ = false;
                logTimelineView(
                    "popup_hidden_track_invisible",
                    QStringLiteral("popup=0x%1 obj=%2")
                        .arg(reinterpret_cast<quintptr>(childHwnd_), 0, 16)
                        .arg(trackedItem_->objectName()));
            }
            return;
        }
        // Becoming visible again. If we were shown previously (the
        // popup has real swap-chain content already), re-show
        // immediately rather than waiting for the debounce + content-
        // frame gate — those guard against showing an empty swap
        // chain at startup, which doesn't apply to a hide/show cycle.
        if (!popupShown_ && geometrySettled_ && contentPublishedFrames_ >= 2) {
            ::ShowWindow(reinterpret_cast<HWND>(childHwnd_), SW_SHOWNOACTIVATE);
            popupShown_ = true;
            logTimelineView(
                "popup_reshown_track_visible",
                QStringLiteral("popup=0x%1 obj=%2")
                    .arg(reinterpret_cast<quintptr>(childHwnd_), 0, 16)
                    .arg(trackedItem_->objectName()));
        }
    }
#endif
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
    // Phase 4d-fix7-final2 — call setVisualTransform BEFORE
    // requestResize, and bundle displaySize with the resize request,
    // so the render thread's post-resize VT re-apply uses a value
    // paired atomically with this specific resize. Without that
    // bundling, rapid GUI-thread updates can leave swap≠vt_display
    // post-settle, scaling content non-uniformly.
    const QSize prevSwapSize = core_.swapChainPixelSize();
#ifdef Q_OS_WIN
    if (childHwnd_ != nullptr) {
        core_.setVisualTransform(0, 0, pixelSize);
    } else {
        core_.setVisualTransform(xPx, yPx, pixelSize);
    }
#else
    core_.setVisualTransform(xPx, yPx, pixelSize);
#endif
    if (pixelSize != prevSwapSize) {
        renderer_.requestResize(pixelSize, pixelSize);
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
            logTimelineViewForced(
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
        // Phase 3f-1 — only call MoveWindow + restart the visibility
        // debounce when the geometry actually changed. Without this
        // gate, the per-publish geometry resync (fix9, 60 Hz) would
        // restart the debounce every frame and the popup would never
        // become visible.
        const bool geometryChanged =
            (lastAppliedXPx_ != globalXPx)
            || (lastAppliedYPx_ != globalYPx)
            || (lastAppliedPixelSize_ != pixelSize);
        if (geometryChanged) {
            ::MoveWindow(reinterpret_cast<HWND>(childHwnd_),
                         globalXPx, globalYPx,
                         pixelSize.width(), pixelSize.height(),
                         TRUE);
            lastAppliedXPx_ = globalXPx;
            lastAppliedYPx_ = globalYPx;
            lastAppliedPixelSize_ = pixelSize;
            // Restart the visibility debounce. If the popup hasn't
            // shown yet, the timer's timeout will ShowWindow once
            // 80 ms have passed without another geometry change.
            // If the popup is already visible (post-startup resize),
            // restarting is harmless — the timer's slot short-circuits
            // on `popupShown_=true`.
            if (!popupShown_) {
                popupVisibilityDebounce_.start();
            }
        }
    }
#endif
    // Phase 4d-fix7-final2 — setVisualTransform was called earlier in
    // this function (above the resize request) so the render thread's
    // post-resize VT re-apply uses a displaySize that's paired with
    // the resize target.
}

void TimelineRenderView::buildAndPublishSnapshot()
{
    if (!sceneStateValid_) {
        return;
    }
    // Issue #4 fix — fullscreen exit recovery. Same rationale as
    // PreviewDCompSurface::buildAndPublishSnapshot. The timeline
    // QQuickItem in the editor window may be destroyed and recreated
    // around state transitions (fullscreen / panel reorganisations),
    // and no window-level signal triggers tryDiscoverTrackedItem on
    // the re-create path. Re-discover lazily here when needed.
    if (trackedItem_.isNull() && window_ != nullptr) {
        tryDiscoverTrackedItem();
        if (!trackedItem_.isNull()) {
            applyTrackedItemGeometry();
        }
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
    // Phase 8 — propagate the horizontal scroll value into the snapshot
    // so the GPU pipeline can apply it as an X offset at vertex emission
    // time. The QSG path used a per-layer translate(-scroll, 0) on the
    // QSGTransformNode; in DComp mode the equivalent is to subtract
    // this value from every timeline-batch vertex's X. Without it, the
    // renderer drew at content-X = screen-X regardless of scroll, so
    // playback / drag / wheel had no visible effect on the body.
    snapshot.timelineHorizontalScrollPx = sceneState_.horizontalScrollValue;
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

    // Phase 3f-4 — count publishes that actually pushed content. Two
    // is the magic number: the first publish puts content in the
    // renderer's queue; the next presented signal is the one that
    // confirms the renderer drew with that content. ShowWindow on the
    // boundary means the popup becomes visible already showing real
    // pixels, not the swap chain's clear-color initial state. Cap at
    // 2 so the counter doesn't grow forever — once we've shown, the
    // gate is irrelevant.
    if (contentPublishedFrames_ < 2) {
        ++contentPublishedFrames_;
        if (contentPublishedFrames_ == 2) {
            maybeShowPopupHwnd();
        }
    }
}

void TimelineRenderView::maybeShowPopupHwnd()
{
#ifdef Q_OS_WIN
    if (childHwnd_ == nullptr || popupShown_) {
        return;
    }
    if (!geometrySettled_ || contentPublishedFrames_ < 2) {
        return;
    }
    ::ShowWindow(reinterpret_cast<HWND>(childHwnd_), SW_SHOWNOACTIVATE);
    popupShown_ = true;
    logTimelineViewForced(
        "popup_shown",
        QStringLiteral("popup=0x%1 px_w=%2 px_h=%3 publishes=%4")
            .arg(reinterpret_cast<quintptr>(childHwnd_), 0, 16)
            .arg(lastAppliedPixelSize_.width())
            .arg(lastAppliedPixelSize_.height())
            .arg(contentPublishedFrames_));
#endif
}

bool TimelineRenderView::initialiseIfReady()
{
    logTimelineViewForced(
        "initialiseIfReady_enter",
        QStringLiteral("initialised=%1 initialising=%2 childHwnd=0x%3 window=0x%4")
            .arg(initialised_ ? 1 : 0)
            .arg(initialising_ ? 1 : 0)
            .arg(reinterpret_cast<quintptr>(childHwnd_), 0, 16)
            .arg(reinterpret_cast<quintptr>(window_.data()), 0, 16));
    if (initialised_) {
        return true;
    }
    // Phase 3e-fix — Win32 reentry guard. CreateWindowExW + ShowWindow
    // + SetLayeredWindowAttributes inside currentParentHwnd can pump
    // messages on Windows, and Qt dispatches queued events during that
    // pump. If a queued event re-enters initialiseIfReady (observed in
    // user logs at 15:01:05.825 → 05.859, 34ms apart, both seeing
    // initialised_=false), the second call would create a duplicate
    // popup HWND, leading to two stacked top-level popups in DWM —
    // exactly the "empty timeline" symptom from the verification
    // screenshot.
    if (initialising_) {
        return false;
    }
    initialising_ = true;
    if (window_ == nullptr) {
        initialising_ = false;
        return false;
    }
    void* parentHwnd = currentParentHwnd();
    if (parentHwnd == nullptr) {
        logTimelineViewForced("init_deferred", QStringLiteral("reason=null_hwnd"));
        initialising_ = false;
        return false;
    }
    const QSize clientPx = currentClientPixelSize();
    if (clientPx.width() <= 0 || clientPx.height() <= 0) {
        logTimelineViewForced("init_deferred", QStringLiteral("reason=zero_size"));
        initialising_ = false;
        return false;
    }
#ifdef Q_OS_WIN
    if (!core_.initialise(reinterpret_cast<HWND>(parentHwnd), clientPx)) {
        logTimelineViewForced("init_failed");
        initialising_ = false;
        return false;
    }
#else
    Q_UNUSED(parentHwnd);
    initialising_ = false;
    return false;
#endif
    core_.setVisualTransform(0, 0, clientPx);
    initialised_ = true;
    initialising_ = false;
    logTimelineViewForced("initialised",
                          QStringLiteral("client_w=%1 client_h=%2")
                              .arg(clientPx.width()).arg(clientPx.height()));
    if (!renderer_.start(&core_)) {
        logTimelineViewForced("renderer_start_failed");
    }
    return true;
}

void TimelineRenderView::teardownCore()
{
    if (initialised_) {
        renderer_.stop();
        core_.shutdown();
        initialised_ = false;
        logTimelineViewForced("teardown");
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
    // Phase 3e — same top-level popup HWND pattern as PreviewDCompSurface.
    // The earlier fix8 attempt (in-place mode) was a workaround for
    // what looked like wrong popup positioning, but the actual cause
    // was the simultaneous QSG rendering of TimelineQuickItem
    // producing the "two timelines" symptom. With Phase 3e's QSG-paint
    // suppression in place (TimelineQuickItem::updatePaintNode gates on
    // previewTimelineUseDCompEnabled and returns nullptr), the popup's
    // top-level positioning matches what the chart preview already
    // does successfully — geometry log confirms mapToGlobal returns
    // accurate global coords (item_global == win_global).
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
            logTimelineViewForced("toplevel_hwnd_create_failed",
                                   QStringLiteral("err=%1").arg(err));
            return reinterpret_cast<void*>(owner);
        }
        ::SetLayeredWindowAttributes(popup, 0, 255, LWA_ALPHA);
        // Phase 3f-1 — DON'T ShowWindow here. The popup stays SW_HIDE
        // until applyTrackedItemGeometry's debounce timer fires, which
        // happens 80 ms after the last MoveWindow. This eliminates the
        // visible "popup dancing around screen during startup" flicker
        // — the popup is positioned silently while QML's layout
        // cascade resolves, and only flips to visible once geometry
        // has settled.
        //
        // Note: SW_HIDE is the default state of a freshly CreateWindowExW'd
        // window in modern Windows when no SW_SHOW* flag is in WS_*. We
        // don't need an explicit SW_HIDE call.
        const_cast<TimelineRenderView*>(this)->childHwnd_ = popup;
        // Phase 4e — register with the owner-followed popup tracker.
        // See PreviewDCompSurface::registerWithPopupTracker for the
        // architecture; the timeline popup uses the same hook so both
        // popups commit in the same DWM tick on every editor
        // WM_WINDOWPOSCHANGED.
        const_cast<TimelineRenderView*>(this)->registerWithPopupTracker();
        logTimelineViewForced("toplevel_hwnd_created",
                               QStringLiteral("owner=0x%1 popup=0x%2 (hidden until geometry settles)")
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

#ifdef Q_OS_WIN
bool TimelineRenderView::applyPopupHwndDeferred(HDWP& hdwp)
{
    if (hdwp == nullptr) {
        return false;
    }
    if (childHwnd_ == nullptr || !initialised_) {
        return false;
    }
    if (trackedItem_ == nullptr || window_.isNull()) {
        return false;
    }
    if (trackedItem_->window() != window_.data()) {
        return false;
    }
    const qreal itemW = trackedItem_->width();
    const qreal itemH = trackedItem_->height();
    if (itemW <= 0.0 || itemH <= 0.0) {
        return false;
    }
    const qreal dpr = window_->effectiveDevicePixelRatio() > 0.0
        ? window_->effectiveDevicePixelRatio() : 1.0;
    // Phase 4e-fix1 — bypass Qt's mapToGlobal (cached, stale during
    // WM_WINDOWPOSCHANGED). Use Win32 ClientToScreen on the owner HWND
    // for the fresh client-area origin, then add the tracked item's
    // SCENE position. trackedItem_->mapToScene gives us the offset
    // within the QQuickWindow's client area.
    const HWND ownerHwnd = reinterpret_cast<HWND>(window_->winId());
    POINT clientOriginScreen{ 0, 0 };
    ::ClientToScreen(ownerHwnd, &clientOriginScreen);
    // Issue #1 fix — see PreviewDCompSurface for rationale. Bail when
    // the owner's client size has changed since the last deferred apply
    // so the (still-stale) Qt-computed scene coords don't paint the
    // popup at a wrong Y.
    {
        RECT clientRect{ 0, 0, 0, 0 };
        ::GetClientRect(ownerHwnd, &clientRect);
        const int clientW = clientRect.right - clientRect.left;
        const int clientH = clientRect.bottom - clientRect.top;
        if (lastClientWPx_ != INT_MIN && lastClientHPx_ != INT_MIN
            && (lastClientWPx_ != clientW || lastClientHPx_ != clientH)) {
            lastClientWPx_ = clientW;
            lastClientHPx_ = clientH;
            return false;
        }
        lastClientWPx_ = clientW;
        lastClientHPx_ = clientH;
    }
    const QPointF topLeftScene = trackedItem_->mapToScene(QPointF(0.0, 0.0));
    const int globalXPx = clientOriginScreen.x + qRound(topLeftScene.x() * dpr);
    const int globalYPx = clientOriginScreen.y + qRound(topLeftScene.y() * dpr);
    const QSize pixelSize(qMax(1, qRound(itemW * dpr)),
                           qMax(1, qRound(itemH * dpr)));
    // Phase 4e-fix2 — see PreviewDCompSurface for rationale. Dedup
    // against lastDeferred* (independent from applyTrackedItemGeometry's
    // lastApplied*) so this path always wins on each WM_WINDOWPOSCHANGED.
    const bool geometryChanged =
        (lastDeferredAppliedXPx_ != globalXPx)
        || (lastDeferredAppliedYPx_ != globalYPx)
        || (lastDeferredAppliedPixelSize_ != pixelSize);
    if (!geometryChanged) {
        return false;
    }
    HDWP next = ::DeferWindowPos(hdwp,
                                  reinterpret_cast<HWND>(childHwnd_),
                                  nullptr,
                                  globalXPx, globalYPx,
                                  pixelSize.width(), pixelSize.height(),
                                  SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
    if (next == nullptr) {
        hdwp = nullptr;
        return false;
    }
    hdwp = next;
    lastDeferredAppliedXPx_ = globalXPx;
    lastDeferredAppliedYPx_ = globalYPx;
    lastDeferredAppliedPixelSize_ = pixelSize;
    lastAppliedXPx_ = globalXPx;
    lastAppliedYPx_ = globalYPx;
    lastAppliedPixelSize_ = pixelSize;
    if (!popupShown_) {
        popupVisibilityDebounce_.start();
    }
    return true;
}

void TimelineRenderView::registerWithPopupTracker()
{
    if (registeredWithPopupTracker_) {
        return;
    }
    PreviewPopupHwndTracker::registerSurface(
        this,
        [this](HDWP& hdwp) -> bool {
            return applyPopupHwndDeferred(hdwp);
        });
    registeredWithPopupTracker_ = true;
}

void TimelineRenderView::unregisterFromPopupTracker()
{
    if (!registeredWithPopupTracker_) {
        return;
    }
    PreviewPopupHwndTracker::unregisterSurface(this);
    registeredWithPopupTracker_ = false;
}
#endif

}  // namespace miacode::preview::dcomp
