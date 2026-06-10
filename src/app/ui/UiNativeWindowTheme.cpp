#include "UiNativeWindowTheme.h"

#include "UiText.h"
#include "UiTheme.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QList>
#include <QPointer>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace UiNativeWindowTheme {
namespace {

#ifdef Q_OS_WIN
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr DWORD kDwmwaTextColor = 36;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmwaMicaEffect = 1029;
constexpr int kDwmsbtNone = 1;
constexpr int kDwmsbtMainWindow = 2;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

bool setDwmWindowAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size)
{
    if (hwnd == nullptr || value == nullptr || size == 0) {
        return false;
    }
    static HMODULE dwmapiModule = ::LoadLibraryW(L"dwmapi.dll");
    if (dwmapiModule == nullptr) {
        return false;
    }
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        ::GetProcAddress(dwmapiModule, "DwmSetWindowAttribute")
    );
    if (setWindowAttribute == nullptr) {
        return false;
    }
    return SUCCEEDED(setWindowAttribute(hwnd, attribute, value, size));
}

COLORREF colorRefForDwm(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}

void applyToNativeHandle(HWND hwnd, bool active, bool backdropEnabled, bool forceFrameRefresh)
{
    if (hwnd == nullptr) {
        return;
    }

    const BOOL darkMode = UiTheme::isDarkTheme() ? TRUE : FALSE;
    setDwmWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode, sizeof(darkMode));

    if (UiText::preferredTheme() == UiText::ThemePreference::System) {
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
    } else {
        const UiTheme::Colors& themeColors = UiTheme::colors();
        const COLORREF borderColor = colorRefForDwm(active ? themeColors.borderStrong : themeColors.borderSoft);
        const COLORREF captionColor = colorRefForDwm(active ? themeColors.toolbarBg : themeColors.windowAltBg);
        const COLORREF textColor = colorRefForDwm(active ? themeColors.textPrimary : themeColors.textSecondary);
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &borderColor, sizeof(borderColor));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &captionColor, sizeof(captionColor));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &textColor, sizeof(textColor));
    }

    const int backdropType = backdropEnabled ? kDwmsbtMainWindow : kDwmsbtNone;
    if (!setDwmWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType))) {
        const BOOL micaEnabled = backdropEnabled ? TRUE : FALSE;
        setDwmWindowAttribute(hwnd, kDwmwaMicaEffect, &micaEnabled, sizeof(micaEnabled));
    }

    if (!forceFrameRefresh) {
        return;
    }
    ::SetWindowPos(
        hwnd,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );
    ::RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME
    );
}

class AutoApplyFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event != nullptr ? event->type() : QEvent::None) {
        case QEvent::Show:
        case QEvent::ActivationChange:
        case QEvent::ApplicationPaletteChange:
        case QEvent::ThemeChange: {
            QWidget* widget = qobject_cast<QWidget*>(watched);
            if (isEligibleWidget(widget)) {
                applyToWidget(widget);
            }
            break;
        }
        default:
            break;
        }
        return false;  // observe only — never consume
    }
};
#endif  // Q_OS_WIN

}  // namespace

bool isEligibleWidget(const QWidget* widget)
{
    if (widget == nullptr || !widget->isWindow()) {
        return false;
    }
    const Qt::WindowFlags flags = widget->windowFlags();
    const Qt::WindowType type = static_cast<Qt::WindowType>(int(flags & Qt::WindowType_Mask));
    if (type != Qt::Window && type != Qt::Dialog && type != Qt::Tool) {
        return false;
    }
    if (flags.testFlag(Qt::FramelessWindowHint)) {
        return false;
    }
    return true;
}

void applyToWidget(QWidget* widget, bool backdropEnabled)
{
#ifdef Q_OS_WIN
    if (widget == nullptr) {
        return;
    }
    QWidget* topLevel = widget->window();
    if (topLevel == nullptr) {
        topLevel = widget;
    }
    const HWND hwnd = reinterpret_cast<HWND>(topLevel->winId());
    applyToNativeHandle(hwnd, topLevel->isActiveWindow(), backdropEnabled, true);
#else
    Q_UNUSED(widget);
    Q_UNUSED(backdropEnabled);
#endif
}

void applyToWindow(QWindow* window, bool backdropEnabled)
{
#ifdef Q_OS_WIN
    if (window == nullptr) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    applyToNativeHandle(hwnd, window->isActive(), backdropEnabled, false);
#else
    Q_UNUSED(window);
    Q_UNUSED(backdropEnabled);
#endif
}

void applyToAllTopLevelWidgets()
{
#ifdef Q_OS_WIN
    const QList<QWidget*> topLevels = QApplication::topLevelWidgets();
    for (QWidget* topLevel : topLevels) {
        if (!isEligibleWidget(topLevel)
            || !topLevel->isVisible()
            || topLevel->windowState().testFlag(Qt::WindowMinimized)) {
            continue;
        }
        applyToWidget(topLevel);
    }
#endif
}

void installAutoApplyFilter()
{
#ifdef Q_OS_WIN
    if (qApp == nullptr) {
        return;
    }
    static QPointer<AutoApplyFilter> installedFilter;
    if (!installedFilter.isNull()) {
        return;
    }
    installedFilter = new AutoApplyFilter(qApp);
    qApp->installEventFilter(installedFilter.data());
#endif
}

}  // namespace UiNativeWindowTheme
