#include "chrome/WindowChrome.h"

#include <QWindow>

#import <AppKit/AppKit.h>


namespace miacode::ui {
namespace {
void configureNativeTitleBar(NSWindow* window)
{
    if (window == nil) {
        return;
    }
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;

    static NSString* const toolbarIdentifier = @"MiaCode.WindowTitleBar";
    if (window.toolbar == nil || ![window.toolbar.identifier isEqualToString:toolbarIdentifier]) {
        NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:toolbarIdentifier];
        toolbar.displayMode = NSToolbarDisplayModeIconOnly;
        toolbar.sizeMode = NSToolbarSizeModeSmall;
        toolbar.allowsUserCustomization = NO;
        toolbar.autosavesConfiguration = NO;
        toolbar.showsBaselineSeparator = NO;
        window.toolbar = toolbar;
#if !__has_feature(objc_arc)
        [toolbar release];
#endif
    }
    window.toolbar.visible = YES;

    if (@available(macOS 11.0, *)) {
        window.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
        window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    }
}
}

void WindowChrome::applyMacOs(QWindow* window)
{
    if (window == nullptr) {
        return;
    }

    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* nativeWindow = (view != nil) ? view.window : nil;
    if (nativeWindow == nil) {
        return;
    }

    configureNativeTitleBar(nativeWindow);

    NSView* contentView = nativeWindow.contentView;
    if (contentView == nil) {
        return;
    }

    [contentView.superview layoutSubtreeIfNeeded];

    const qreal systemTitleBarHeight = static_cast<qreal>(
        NSHeight(contentView.bounds) - NSHeight(nativeWindow.contentLayoutRect));
    if (systemTitleBarHeight > 0) {
        windowedTitleBarHeight_ = systemTitleBarHeight;
        setTitleBarHeight(windowedTitleBarHeight_);
    }

    if (window->windowState() == Qt::WindowFullScreen) {
        setTitleBarLeadingInset(0);
        return;
    }

    NSButton* buttons[] = {
        [nativeWindow standardWindowButton:NSWindowCloseButton],
        [nativeWindow standardWindowButton:NSWindowMiniaturizeButton],
        [nativeWindow standardWindowButton:NSWindowZoomButton]
    };

    NSRect group = NSZeroRect;
    NSRect previous = NSZeroRect;
    bool hasButton = false;
    qreal buttonGap = 0;
    for (NSButton* button : buttons) {
        if (button == nil || button.superview == nil) {
            continue;
        }
        const NSRect frame = [contentView convertRect:button.frame fromView:button.superview];
        group = hasButton ? NSUnionRect(group, frame) : frame;
        if (hasButton) {
            const qreal gap = NSMinX(frame) - NSMaxX(previous);
            if (gap > 0 && (buttonGap == 0 || gap < buttonGap)) {
                buttonGap = gap;
            }
        }
        previous = frame;
        hasButton = true;
    }

    if (!hasButton) {
        setTitleBarLeadingInset(0);
        return;
    }

    windowedTitleBarLeadingInset_ = static_cast<qreal>(NSMaxX(group)) + buttonGap;
    setTitleBarLeadingInset(windowedTitleBarLeadingInset_);
}

void WindowChrome::observeMacOsFullScreen(QWindow* window)
{
    stopObservingMacOsFullScreen();

    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(window->winId());
    NSWindow* nativeWindow = (view != nil) ? view.window : nil;
    if (nativeWindow == nil) {
        return;
    }

    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    id willEnterObserver = [center
        addObserverForName:NSWindowWillEnterFullScreenNotification
                    object:nativeWindow
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(__unused NSNotification* notification) {
                    nativeWindow.toolbar.visible = NO;
                    setTitleBarLeadingInset(0);
                    setTitleBarHeight(windowedTitleBarHeight_);
                }];
    id didEnterObserver = [center
        addObserverForName:NSWindowDidEnterFullScreenNotification
                    object:nativeWindow
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(__unused NSNotification* notification) {
                    nativeWindow.toolbar.visible = NO;
                    setTitleBarLeadingInset(0);
                    setTitleBarHeight(windowedTitleBarHeight_);
                }];
    id willExitObserver = [center
        addObserverForName:NSWindowWillExitFullScreenNotification
                    object:nativeWindow
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(__unused NSNotification* notification) {
                    configureNativeTitleBar(nativeWindow);
                    setTitleBarLeadingInset(windowedTitleBarLeadingInset_);
                    setTitleBarHeight(windowedTitleBarHeight_);
                }];
    id didExitObserver = [center
        addObserverForName:NSWindowDidExitFullScreenNotification
                    object:nativeWindow
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(__unused NSNotification* notification) {
                    if (!window_.isNull()) {
                        applyMacOs(window_.data());
                    }
                }];

    macWillEnterFullScreenObserver_ = (__bridge void*)willEnterObserver;
    macDidEnterFullScreenObserver_ = (__bridge void*)didEnterObserver;
    macWillExitFullScreenObserver_ = (__bridge void*)willExitObserver;
    macDidExitFullScreenObserver_ = (__bridge void*)didExitObserver;
}

void WindowChrome::stopObservingMacOsFullScreen()
{
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    if (macWillEnterFullScreenObserver_ != nullptr) {
        id observer = (__bridge id)macWillEnterFullScreenObserver_;
        [center removeObserver:observer];
        macWillEnterFullScreenObserver_ = nullptr;
    }
    if (macDidEnterFullScreenObserver_ != nullptr) {
        id observer = (__bridge id)macDidEnterFullScreenObserver_;
        [center removeObserver:observer];
        macDidEnterFullScreenObserver_ = nullptr;
    }
    if (macWillExitFullScreenObserver_ != nullptr) {
        id observer = (__bridge id)macWillExitFullScreenObserver_;
        [center removeObserver:observer];
        macWillExitFullScreenObserver_ = nullptr;
    }
    if (macDidExitFullScreenObserver_ != nullptr) {
        id observer = (__bridge id)macDidExitFullScreenObserver_;
        [center removeObserver:observer];
        macDidExitFullScreenObserver_ = nullptr;
    }
}

} // namespace miacode::ui
