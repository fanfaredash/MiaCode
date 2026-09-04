#include "UiNativeWindowTheme.h"

#include "UiText.h"
#include "UiTheme.h"

#ifdef Q_OS_MACOS
#include "UiNativeWindowThemeMac.h"
#endif

#include <QColor>
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

#endif  // Q_OS_WIN

}  // namespace

void applyToWindow(QWindow* window, bool backdropEnabled)
{
#ifdef Q_OS_WIN
    if (window == nullptr) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    applyToNativeHandle(hwnd, window->isActive(), backdropEnabled, false);
#elif defined(Q_OS_MACOS)
    Q_UNUSED(backdropEnabled);
    if (window == nullptr) {
        return;
    }
    UiNativeWindowThemeMac::applyToNativeView(
        reinterpret_cast<void*>(window->winId()),
        UiNativeWindowThemePolicy::appearanceFor(UiText::preferredTheme()));
#else
    Q_UNUSED(window);
    Q_UNUSED(backdropEnabled);
#endif
}

}  // namespace UiNativeWindowTheme
