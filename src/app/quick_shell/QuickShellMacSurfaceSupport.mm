#include "QuickShellMacSurfaceSupport.h"

#import <AppKit/AppKit.h>

namespace miacode::quick_shell::mac {

void* captureOrphanShellWindow(void* nativeViewHandle)
{
    // (__bridge NSView*) is a non-owning cast valid under both ARC and MRC.
    NSView* view = (__bridge NSView*)nativeViewHandle;
    if (view == nil) {
        return nullptr;
    }
    NSWindow* window = [view window];
    if (window == nil) {
        return nullptr;
    }
    // Non-owning: the panel is owned by Qt's QCocoaWindow, which lives as long as
    // the bridge surface (the whole app session). We only read/mutate it later.
    return (__bridge void*)window;
}

bool neutralizeOrphanShellWindow(void* nativeViewHandle, void* capturedPanel)
{
    if (capturedPanel == nullptr) {
        return true;  // nothing captured (non-macOS path shouldn't reach here) — done
    }

    NSWindow* panel = (__bridge NSWindow*)capturedPanel;
    NSView* view = (__bridge NSView*)nativeViewHandle;
    NSWindow* currentWindow = (view != nil) ? [view window] : nil;

    // Only safe to hide the panel once the content NSView has left it. Until the
    // QML WindowContainer adopts the view, it is still the panel's contentView and
    // hiding the panel would hide the embedded menu/sidebar/editor. Tell the caller
    // to retry.
    if (currentWindow == panel) {
        return false;
    }

    // Defensive: the main QQuickWindow is a QNSWindow, never an NSPanel, so this
    // guarantees we never touch the real window even if the pointer were stale.
    if (![panel isKindOfClass:[NSPanel class]]) {
        return true;
    }

    // The content view has been reparented out; `panel` is now an empty orphan.
    // Disable hidesOnDeactivate BEFORE orderOut: NSPanel has this flag YES by default,
    // which causes macOS to call orderFront on every app reactivation, undoing the
    // orderOut and re-showing the empty panel as a white ghost window over the UI.
    [panel setHidesOnDeactivate:NO];
    [panel setAlphaValue:0.0];
    [panel setIgnoresMouseEvents:YES];
    [panel setOpaque:NO];
    [panel setBackgroundColor:[NSColor clearColor]];
    [panel orderOut:nil];
    return true;
}

void setContentViewHidden(void* nativeViewHandle, bool hidden)
{
    NSView* view = (__bridge NSView*)nativeViewHandle;
    if (view == nil) {
        return;
    }
    if (view.hidden != hidden) {
        view.hidden = hidden;
    }
}

}  // namespace miacode::quick_shell::mac
