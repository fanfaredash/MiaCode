#include "QuickShellMacSurfaceSupport.h"

#include "QuickShellPopupPosition.h"
#include "common/DebugLog.h"

#import <AppKit/AppKit.h>

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWindow>

namespace {

QPoint qtPointInView(NSView* view, const QPoint& point)
{
    if (view == nil || view.isFlipped) {
        return point;
    }
    return QPoint(point.x(), qRound(NSHeight(view.bounds)) - point.y());
}

QPoint viewPointInQt(NSView* view, NSPoint point)
{
    if (view == nil || view.isFlipped) {
        return QPoint(qRound(point.x), qRound(point.y));
    }
    return QPoint(qRound(point.x), qRound(NSHeight(view.bounds) - point.y));
}

QWindow* qtTopLevelForNativeWindow(NSWindow* nativeWindow)
{
    if (nativeWindow == nil) {
        return nullptr;
    }
    const QList<QWindow*> topLevels = QGuiApplication::topLevelWindows();
    for (QWindow* candidate : topLevels) {
        if (candidate == nullptr || candidate->winId() == 0) {
            continue;
        }
        NSView* candidateView = (__bridge NSView*)reinterpret_cast<void*>(candidate->winId());
        if (candidateView != nil && candidateView.window == nativeWindow) {
            return candidate;
        }
    }
    return nullptr;
}

QPoint menuBarPointToGlobal(QMenuBar* menuBar, const QPoint& localPoint)
{
    if (menuBar == nullptr || menuBar->winId() == 0) {
        return {};
    }
    NSView* menuBarView = (__bridge NSView*)reinterpret_cast<void*>(menuBar->winId());
    NSWindow* nativeWindow = (menuBarView != nil) ? menuBarView.window : nil;
    QWindow* qtWindow = qtTopLevelForNativeWindow(nativeWindow);
    if (menuBarView == nil || nativeWindow == nil || qtWindow == nullptr || qtWindow->winId() == 0) {
        return menuBar->mapToGlobal(localPoint);
    }

    NSView* rootView = (__bridge NSView*)reinterpret_cast<void*>(qtWindow->winId());
    const QPoint cocoaLocal = qtPointInView(menuBarView, localPoint);
    const NSPoint rootPoint = [menuBarView
        convertPoint:NSMakePoint(cocoaLocal.x(), cocoaLocal.y())
             toView:rootView];
    return qtWindow->mapToGlobal(viewPointInQt(rootView, rootPoint));
}

class TopLevelMenuPopupPositionFilter final : public QObject
{
public:
    TopLevelMenuPopupPositionFilter(QMenuBar* menuBar, QWindow* adoptedSurfaceWindow)
        : QObject(menuBar)
        , menuBar_(menuBar)
        , adoptedSurfaceWindow_(adoptedSurfaceWindow)
    {
        qApp->installEventFilter(this);
    }

    ~TopLevelMenuPopupPositionFilter() override
    {
        if (qApp != nullptr) {
            qApp->removeEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event == nullptr || event->type() != QEvent::Show || menuBar_.isNull()) {
            return false;
        }
        auto* menu = qobject_cast<QMenu*>(watched);
        if (menu == nullptr) {
            return false;
        }
        QAction* menuAction = menu->menuAction();
        if (menuAction == nullptr || !menuBar_->actions().contains(menuAction)) {
            return false;  // Preserve Qt placement for nested/context menus.
        }

        const QRect actionRect = menuBar_->actionGeometry(menuAction);
        if (!actionRect.isValid()) {
            return false;
        }
        QWidget* bridgeSurface = menuBar_->window();
        const QPoint actionSurfaceTopLeft = bridgeSurface != nullptr
            ? menuBar_->mapTo(bridgeSurface, actionRect.topLeft())
            : actionRect.topLeft();

        QPoint surfaceGlobalOrigin;
        QString mappingRoute;
        if (!adoptedSurfaceWindow_.isNull()) {
            surfaceGlobalOrigin = adoptedSurfaceWindow_->mapToGlobal(QPoint(0, 0));
            mappingRoute = QStringLiteral("adopted_qwindow");
        } else {
            surfaceGlobalOrigin = menuBarPointToGlobal(menuBar_, QPoint(0, 0));
            mappingRoute = QStringLiteral("appkit_fallback");
        }
        const QRect actionScreenRect = miacode::quick_shell::actionScreenRectFromSurface(
            surfaceGlobalOrigin,
            QRect(actionSurfaceTopLeft, actionRect.size()));
        QScreen* screen = QGuiApplication::screenAt(actionScreenRect.center());
        if (screen == nullptr) {
            screen = menuBar_->screen();
        }
        const QRect available = screen != nullptr ? screen->availableGeometry() : QRect();
        const QSize popupSize = menu->size().expandedTo(menu->sizeHint());
        const QPoint target = miacode::quick_shell::popupTopLeftForAction(
            actionScreenRect.normalized(), popupSize, available);
        const QPoint defaultPosition = menu->pos();
        menu->move(target);
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("quick_shell/mac_menu_popup"),
            QStringLiteral(
                "phase=show route=%1 action=%2,%3,%4,%5 surface_origin=%6,%7 "
                "default=%8,%9 target=%10,%11")
                .arg(mappingRoute)
                .arg(actionScreenRect.x())
                .arg(actionScreenRect.y())
                .arg(actionScreenRect.width())
                .arg(actionScreenRect.height())
                .arg(surfaceGlobalOrigin.x())
                .arg(surfaceGlobalOrigin.y())
                .arg(defaultPosition.x())
                .arg(defaultPosition.y())
                .arg(target.x())
                .arg(target.y()));

        // QMenuPrivate may perform one final platform placement after QEvent::Show.
        // Re-assert the same absolute target on the next event-loop turn so Qt's
        // stale orphan-panel coordinate cannot overwrite it.
        QPointer<QMenu> menuGuard(menu);
        QTimer::singleShot(0, menu, [menuGuard, target]() {
            if (menuGuard.isNull()) {
                return;
            }
            const QPoint beforeQueuedMove = menuGuard->pos();
            if (beforeQueuedMove != target) {
                menuGuard->move(target);
            }
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("quick_shell/mac_menu_popup"),
                QStringLiteral("phase=queued before=%1,%2 final=%3,%4")
                    .arg(beforeQueuedMove.x())
                    .arg(beforeQueuedMove.y())
                    .arg(menuGuard->pos().x())
                    .arg(menuGuard->pos().y()));
        });
        return false;
    }

private:
    QPointer<QMenuBar> menuBar_;
    QPointer<QWindow> adoptedSurfaceWindow_;
};

}  // namespace

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

void installTopLevelMenuPopupPositioning(QMenuBar* menuBar, QWindow* adoptedSurfaceWindow)
{
    if (menuBar == nullptr
        || menuBar->property("miacodeMacPopupPositionFilterInstalled").toBool()) {
        return;
    }
    menuBar->setProperty("miacodeMacPopupPositionFilterInstalled", true);
    new TopLevelMenuPopupPositionFilter(menuBar, adoptedSurfaceWindow);
}

}  // namespace miacode::quick_shell::mac
