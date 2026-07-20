#include "UiNativeWindowThemeMac.h"

#import <AppKit/AppKit.h>

namespace UiNativeWindowThemeMac {

void applyToNativeView(
    void* nativeViewHandle,
    UiNativeWindowThemePolicy::Appearance appearance)
{
    NSView* view = (__bridge NSView*)nativeViewHandle;
    NSWindow* window = (view != nil) ? view.window : nil;
    if (window == nil) {
        return;
    }

    switch (appearance) {
    case UiNativeWindowThemePolicy::Appearance::Light:
        window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        break;
    case UiNativeWindowThemePolicy::Appearance::Dark:
        window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        break;
    case UiNativeWindowThemePolicy::Appearance::System:
    default:
        window.appearance = nil;
        break;
    }
}

}  // namespace UiNativeWindowThemeMac
