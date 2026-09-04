#pragma once

#include "UiNativeWindowThemePolicy.h"

namespace UiNativeWindowThemeMac {

// `nativeViewHandle` is the NSView returned by QWindow::winId().
// The declaration stays Objective-C-free so ordinary C++ callers can use it.
void applyToNativeView(
    void* nativeViewHandle,
    UiNativeWindowThemePolicy::Appearance appearance);

}  // namespace UiNativeWindowThemeMac
