#pragma once

class QWindow;

// Native (non-client) window theming. Windows uses DWM attributes for its
// caption/backdrop; macOS sets NSWindow.appearance to Aqua, Dark Aqua, or nil
// for the app's explicit light, explicit dark, or system preference. Other
// platforms keep these helpers as no-ops.
namespace UiNativeWindowTheme {

// QWindow variant for QML / QQuickWindow top-levels. Unlike the widget
// variant this does not force a frame recalc (preserves the historical
// quick-shell behavior of plain attribute writes).
void applyToWindow(QWindow* window, bool backdropEnabled = true);

}  // namespace UiNativeWindowTheme
