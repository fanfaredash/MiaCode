#pragma once

#include "chrome/NativeWindowThemePolicy.h"

namespace NativeWindowThemeMac {

// `nativeViewHandle` is the NSView returned by QWindow::winId().
// The declaration stays Objective-C-free so ordinary C++ callers can use it.
void applyToNativeView(
    void* nativeViewHandle,
    NativeWindowThemePolicy::Appearance appearance);

}  // namespace NativeWindowThemeMac
