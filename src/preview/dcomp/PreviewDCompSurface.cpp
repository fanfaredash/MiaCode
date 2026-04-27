#include "preview/dcomp/PreviewDCompSurface.h"

#include "common/DebugLog.h"

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

void PreviewDCompSurface::detach()
{
    if (window_) {
        disconnect(window_, nullptr, this, nullptr);
    }
    teardownCore();
    window_ = nullptr;
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
    // For Phase 1: keep the swap chain at the test-rect size, but update
    // the visual transform so the rectangle stays at the new placement.
    const QSize rectSize = testRectSize(clientPx);
    if (rectSize != core_.swapChainPixelSize()) {
        core_.resize(rectSize);
    }
    core_.setVisualTransform(kTestRectInsetPx, kTestRectInsetPx, rectSize);
    scheduleTestRender();
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
    scheduleTestRender();
    return true;
}

void PreviewDCompSurface::teardownCore()
{
    if (initialised_) {
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

void PreviewDCompSurface::scheduleTestRender()
{
    if (!initialised_) {
        return;
    }
    // Phase 1 renders synchronously on the GUI thread when called.
    // Phase 2 will replace this with the dedicated render thread + frame
    // latency waitable.
    core_.renderTestFrame();
}

}  // namespace miacode::preview::dcomp
