#pragma once

class QWidget;
class QWindow;

// Native (non-client) window theming. On Windows the OS paints top-level
// title bars per the *system* theme, so a dark in-app UiTheme gets a white
// caption unless each window is explicitly themed through DWM. These helpers
// own that DWM logic (immersive dark mode + caption/text/border colors +
// system backdrop) in one place; every function is a no-op off Windows.
namespace UiNativeWindowTheme {

// True for top-level widgets that own a native caption worth theming.
// Excludes popups / tooltips / splash screens and frameless windows (their
// non-client area is either absent or custom-drawn).
bool isEligibleWidget(const QWidget* widget);

// Theme the native frame of `widget`'s top-level window with the current
// UiTheme. Forces native-window creation; safe to call repeatedly.
void applyToWidget(QWidget* widget, bool backdropEnabled = true);

// QWindow variant for QML / QQuickWindow top-levels. Unlike the widget
// variant this does not force a frame recalc (preserves the historical
// quick-shell behavior of plain attribute writes).
void applyToWindow(QWindow* window, bool backdropEnabled = true);

// Re-theme every visible, non-minimized eligible top-level widget. Used on
// live theme switches so already-open dialogs follow immediately.
void applyToAllTopLevelWidgets();

// Install the application-wide auto-theming event filter on qApp
// (idempotent). Eligible top-level widgets are themed automatically on
// Show / ActivationChange / ApplicationPaletteChange / ThemeChange, so
// dialogs opened anywhere — including tools-layer code that cannot reach
// MainWindow — no longer need an explicit call. The filter only observes;
// it never consumes events.
void installAutoApplyFilter();

}  // namespace UiNativeWindowTheme
