#include "chrome/WindowChrome.h"

#include <QtGlobal>
#include <QCoreApplication>
#include <QPointer>
#include <QWindow>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#endif

namespace miacode::ui {

WindowChrome::WindowChrome(QObject* parent)
    : QObject(parent)
{
}

WindowChrome::~WindowChrome()
{
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

void WindowChrome::setTitleBarLeadingInset(qreal inset)
{
    if (qFuzzyCompare(titleBarLeadingInset_, inset)) {
        return;
    }
    titleBarLeadingInset_ = inset;
    emit titleBarLeadingInsetChanged();
}

void WindowChrome::attach(QWindow* window)
{
    if (window == nullptr) {
        return;
    }

    window_ = window;

#ifdef Q_OS_WIN
    nativeHandle_ = window->winId();
    const auto handle = reinterpret_cast<HWND>(nativeHandle_);
    QCoreApplication::instance()->installNativeEventFilter(this);
    extendDwmFrame();

    SetWindowPos(
        handle,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    setTitleBarLeadingInset(0);
#elif defined(Q_OS_MACOS)
    applyMacOs(window);
    QObject::connect(
        window,
        &QWindow::widthChanged,
        this,
        &WindowChrome::refreshTitleBarMetrics,
        static_cast<Qt::ConnectionType>(Qt::UniqueConnection));
    QObject::connect(
        window,
        &QWindow::visibleChanged,
        this,
        &WindowChrome::handleWindowVisibleChanged,
        static_cast<Qt::ConnectionType>(Qt::UniqueConnection));
#else
    Q_UNUSED(window);
    setTitleBarLeadingInset(0);
#endif
}

void WindowChrome::refreshTitleBarMetrics()
{
#ifdef Q_OS_MACOS
    if (window_.isNull()) {
        return;
    }
    applyMacOs(window_.data());
#else
    setTitleBarLeadingInset(0);
#endif
}

void WindowChrome::handleWindowVisibleChanged(bool visible)
{
    if (visible) {
        refreshTitleBarMetrics();
    }
}

bool WindowChrome::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" || nativeHandle_ == 0) {
        return false;
    }

    const auto* nativeMessage = static_cast<MSG*>(message);
    const auto handle = reinterpret_cast<HWND>(nativeHandle_);
    if (nativeMessage->hwnd != handle) {
        return false;
    }

    if (nativeMessage->message == WM_NCCALCSIZE && nativeMessage->wParam == TRUE) {
        if (IsZoomed(handle)) {
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            const HMONITOR monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(monitor, &monitorInfo)) {
                auto* parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(nativeMessage->lParam);
                parameters->rgrc[0] = monitorInfo.rcWork;
            }
        }

        *result = 0;
        return true;
    }

    if (nativeMessage->message == WM_NCHITTEST) {
        if (IsZoomed(handle)) {
            *result = HTCLIENT;
            return true;
        }

        RECT windowRect{};
        GetWindowRect(handle, &windowRect);

        const UINT dpi = GetDpiForWindow(handle);
        const int horizontalBorder = GetSystemMetricsForDpi(SM_CXFRAME, dpi)
            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const int verticalBorder = GetSystemMetricsForDpi(SM_CYFRAME, dpi)
            + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const POINT cursorPosition{
            GET_X_LPARAM(nativeMessage->lParam),
            GET_Y_LPARAM(nativeMessage->lParam)};

        const bool left = cursorPosition.x < windowRect.left + horizontalBorder;
        const bool right = cursorPosition.x >= windowRect.right - horizontalBorder;
        const bool top = cursorPosition.y < windowRect.top + verticalBorder;
        const bool bottom = cursorPosition.y >= windowRect.bottom - verticalBorder;

        if (top && left)
            *result = HTTOPLEFT;
        else if (top && right)
            *result = HTTOPRIGHT;
        else if (bottom && left)
            *result = HTBOTTOMLEFT;
        else if (bottom && right)
            *result = HTBOTTOMRIGHT;
        else if (left)
            *result = HTLEFT;
        else if (right)
            *result = HTRIGHT;
        else if (top)
            *result = HTTOP;
        else if (bottom)
            *result = HTBOTTOM;
        else
            *result = HTCLIENT;

        return true;
    }

    if (nativeMessage->message == WM_ACTIVATE
        || nativeMessage->message == WM_DWMCOMPOSITIONCHANGED) {
        extendDwmFrame();
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif

    return false;
}

void WindowChrome::extendDwmFrame() const
{
#ifdef Q_OS_WIN
    if (nativeHandle_ == 0) {
        return;
    }

    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(reinterpret_cast<HWND>(nativeHandle_), &margins);
#endif
}

#ifndef Q_OS_MACOS
void WindowChrome::applyMacOs(QWindow* window)
{
    Q_UNUSED(window);
}
#endif

} // namespace miacode::ui
