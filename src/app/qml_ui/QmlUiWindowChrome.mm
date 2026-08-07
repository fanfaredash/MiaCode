#include "QmlUiWindowChrome.h"

#include <QWindow>

#import <AppKit/AppKit.h>

void QmlUiWindowChrome::applyMacOs(QWindow* window)
{
    if (window == nullptr) {
        return;
    }

    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* nativeWindow = (view != nil) ? view.window : nil;
    if (nativeWindow == nil) {
        return;
    }

    nativeWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nativeWindow.titlebarAppearsTransparent = YES;
    nativeWindow.titleVisibility = NSWindowTitleHidden;

    NSView* contentView = nativeWindow.contentView;
    NSButton* zoomButton = [nativeWindow standardWindowButton:NSWindowZoomButton];
    qreal leading = 0;
    if (contentView != nil && zoomButton != nil && zoomButton.superview != nil) {
        const NSRect zoomInContent =
            [zoomButton.superview convertRect:zoomButton.frame toView:contentView];
        leading = static_cast<qreal>(NSMaxX(zoomInContent) + 12.0);
    }
    setTitleBarLeadingInset(leading);
}
