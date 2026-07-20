#include "QuickShellNativeSurfaceHost.h"

#include "QuickShellMacSurfaceSupport.h"
#include "common/AdoptedWidgetCoordinates.h"
#include "app/ui/AppBackgroundPainter.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/UiHangWatchdog.h"

#include <QBoxLayout>
#include <QDockWidget>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>
#include <QWindow>

namespace {

constexpr qint64 kSurfaceStepSlowMs = 50;
constexpr qint64 kSurfaceTotalSlowMs = 80;

QString widgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("(null)");
    }
    return QStringLiteral("class=%1 name=%2 size=%3x%4 visible=%5")
        .arg(QString::fromUtf8(widget->metaObject()->className()))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(empty)") : widget->objectName())
        .arg(widget->width())
        .arg(widget->height())
        .arg(widget->isVisible() ? 1 : 0);
}

void appendSurfaceLayoutDiag(
    const QString& action,
    const char* role,
    QWidget* widget,
    qint64 elapsedMs,
    const QString& detail = QString(),
    miacode::debug_log::Level level = miacode::debug_log::Level::Info)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    QString payload = QStringLiteral("action=%1 role=%2 elapsed_ms=%3 widget=\"%4\"")
        .arg(action)
        .arg(QString::fromUtf8(role != nullptr ? role : "unknown"))
        .arg(elapsedMs)
        .arg(widgetSummary(widget));
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" %1").arg(detail.trimmed());
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/layout"),
        payload,
        /*force=*/false,
        level);
}

void activateLayout(QWidget* widget, const char* role)
{
    if (widget == nullptr) {
        return;
    }
    if (QLayout* layout = widget->layout(); layout != nullptr) {
        QElapsedTimer stepTimer;
        stepTimer.start();
        MIACODE_HANG_PHASE(
            "QuickShellNativeSurfaceHost::activateLayout",
            QStringLiteral("role=%1 %2")
                .arg(QString::fromUtf8(role != nullptr ? role : "unknown"), widgetSummary(widget)));
        layout->activate();
        const qint64 elapsedMs = stepTimer.elapsed();
        if (elapsedMs >= kSurfaceStepSlowMs) {
            appendSurfaceLayoutDiag(
                QStringLiteral("surface_layout_activate_slow"),
                role,
                widget,
                elapsedMs,
                QString(),
                miacode::debug_log::Level::Warn);
        }
    }
}

void resizeSurface(QWidget* surface, int width, int height, const char* role)
{
    if (surface == nullptr) {
        return;
    }
    MC_OP("QuickShellNativeSurfaceHost::resizeSurface");
    QElapsedTimer totalTimer;
    totalTimer.start();
    const QSize nextSize(qMax(1, width), qMax(1, height));
    if (surface->size() != nextSize) {
        QElapsedTimer stepTimer;
        stepTimer.start();
        MIACODE_HANG_PHASE(
            "QuickShellNativeSurfaceHost::resizeSurface.resize",
            QStringLiteral("role=%1 from=%2x%3 to=%4x%5 %6")
                .arg(QString::fromUtf8(role != nullptr ? role : "unknown"))
                .arg(surface->width())
                .arg(surface->height())
                .arg(nextSize.width())
                .arg(nextSize.height())
                .arg(widgetSummary(surface)));
        surface->resize(nextSize);
        surface->update();
        const qint64 elapsedMs = stepTimer.elapsed();
        if (elapsedMs >= kSurfaceStepSlowMs) {
            appendSurfaceLayoutDiag(
                QStringLiteral("surface_resize_slow"),
                role,
                surface,
                elapsedMs,
                QStringLiteral("requested=%1x%2").arg(width).arg(height),
                miacode::debug_log::Level::Warn);
        }
    }
    activateLayout(surface, role);
    {
        QElapsedTimer stepTimer;
        stepTimer.start();
        MIACODE_HANG_PHASE(
            "QuickShellNativeSurfaceHost::resizeSurface.updateGeometry",
            QStringLiteral("role=%1 %2")
                .arg(QString::fromUtf8(role != nullptr ? role : "unknown"), widgetSummary(surface)));
        surface->updateGeometry();
        const qint64 elapsedMs = stepTimer.elapsed();
        if (elapsedMs >= kSurfaceStepSlowMs) {
            appendSurfaceLayoutDiag(
                QStringLiteral("surface_update_geometry_slow"),
                role,
                surface,
                elapsedMs,
                QString(),
                miacode::debug_log::Level::Warn);
        }
    }
    const qint64 totalMs = totalTimer.elapsed();
    if (totalMs >= kSurfaceTotalSlowMs) {
        appendSurfaceLayoutDiag(
            QStringLiteral("surface_resize_total_slow"),
            role,
            surface,
            totalMs,
            QStringLiteral("requested=%1x%2").arg(width).arg(height),
            miacode::debug_log::Level::Warn);
    }
}

bool shouldUseBottomTabsNativeSurface(QuickShellStateSource* stateSource)
{
    if (stateSource == nullptr) {
        return true;
    }
    if (!stateSource->shellBottomTabsVisible()) {
        return false;
    }
    return stateSource->shellBottomTabsCurrentTabId().trimmed().compare(QStringLiteral("timeline"), Qt::CaseInsensitive) != 0;
}

// Whether the bottom-tabs bridge QWidget itself is hidden/re-shown as tabs
// switch. On macOS this must stay false: after the QML WindowContainer adopts
// the bridge's content NSView, QWidget::show() on the top-level re-attaches
// that NSView as its own NSPanel's contentView, ripping the embedded
// validation/Muri page out of the main window (the "content flies out as a
// standalone window" bug). There, per-tab visibility is driven solely by the
// QML WindowContainer toggling the foreign QWindow (BottomTabsQuickHost.qml),
// and the bridge widget stays permanently shown like the other four surfaces.
constexpr bool kBridgeSurfaceVisibilityFollowsTabs =
#ifdef Q_OS_MACOS
    false;
#else
    true;
#endif

constexpr int kBottomTabsSpeedToastMinWidth = 180;
constexpr int kBottomTabsSpeedToastMinHeight = 96;
constexpr int kBottomTabsSpeedToastHorizontalMargin = 20;

void setSurfaceVisible(QWidget* surface, bool visible)
{
    if (surface == nullptr) {
        return;
    }
    if (visible) {
        if (!surface->isVisible()) {
            surface->show();
        }
        return;
    }
    if (surface->isVisible()) {
        surface->hide();
    }
}

void applyBridgeSurfaceBaseStyle(QWidget* surface)
{
    if (surface == nullptr) {
        return;
    }
    const UiTheme::Colors& colors = UiTheme::colors();
    const QColor background = colors.windowBg;
    QPalette palette = surface->palette();
    if (palette.color(QPalette::Window) != background) {
        palette.setColor(QPalette::Window, background);
        surface->setPalette(palette);
    }
    if (!surface->autoFillBackground()) {
        surface->setAutoFillBackground(true);
    }
}

}  // namespace

QuickShellNativeSurfaceHost::QuickShellNativeSurfaceHost(
    QuickShellNativeContentProvider* contentProvider,
    QuickShellStateSource* stateSource,
    QObject* parent)
    : QObject(parent)
    , contentProvider_(contentProvider)
    , stateSource_(stateSource)
    , topChromeSurfaceWidget_(createBridgeSurface(QStringLiteral("QuickShellTopChromeSurface")))
    , sidebarSurfaceWidget_(createBridgeSurface(QStringLiteral("QuickShellSidebarSurface")))
    , workspaceSurfaceWidget_(createBridgeSurface(QStringLiteral("QuickShellWorkspaceSurface")))
    , bottomTabsSurfaceWidget_(createBridgeSurface(QStringLiteral("QuickShellBottomTabsSurface")))
    , statusSurfaceWidget_(createBridgeSurface(QStringLiteral("QuickShellStatusSurface")))
    , bottomTabsSpeedToastWindow_(new QWidget(
          nullptr,
          Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint
              | Qt::WindowDoesNotAcceptFocus
      ))
    , bottomTabsSpeedToastPanel_(new QWidget(bottomTabsSpeedToastWindow_))
    , bottomTabsSpeedToastLabel_(new QLabel(bottomTabsSpeedToastPanel_))
    , bottomTabsSpeedToastTimer_(new QTimer(this))
    , bottomTabsSpeedToastOpacityAnimation_(new QPropertyAnimation(bottomTabsSpeedToastWindow_, "windowOpacity", this))
{
    surfaceBundle_.topChrome = createForeignWindowForSurface(topChromeSurfaceWidget_);
    surfaceBundle_.sidebar = createForeignWindowForSurface(sidebarSurfaceWidget_);
    surfaceBundle_.workspace = createForeignWindowForSurface(workspaceSurfaceWidget_);
    surfaceBundle_.bottomTabs = createForeignWindowForSurface(bottomTabsSurfaceWidget_);
    surfaceBundle_.status = createForeignWindowForSurface(statusSurfaceWidget_);

#ifdef Q_OS_MACOS
    miacode::ui::bindAdoptedSurfaceWindow(workspaceSurfaceWidget_, surfaceBundle_.workspace);
#endif

    ensureSurfaceLayouts();
    attachNativeWidgets();
#ifdef Q_OS_MACOS
    showAllSurfaces();
#endif
    refreshBottomTabsSurfaceVisibility();

    // macOS: grab the orphan Qt::Tool panels now, while each bridge's content view
    // still lives inside its own panel. They are neutralized later (after QML
    // adoption) from noteQuickShellUiReady(). No-op on other platforms.
    captureOrphanShellWindows();

    bottomTabsSpeedToastWindow_->setObjectName(QStringLiteral("QuickShellBottomTabsSpeedToast"));
    bottomTabsSpeedToastWindow_->setAttribute(Qt::WA_TranslucentBackground, true);
    bottomTabsSpeedToastWindow_->setAttribute(Qt::WA_ShowWithoutActivating, true);
    bottomTabsSpeedToastWindow_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    bottomTabsSpeedToastWindow_->setWindowFlag(Qt::WindowTransparentForInput, true);
    bottomTabsSpeedToastWindow_->setFocusPolicy(Qt::NoFocus);
    bottomTabsSpeedToastPanel_->setObjectName(QStringLiteral("QuickShellBottomTabsSpeedToastPanel"));
    bottomTabsSpeedToastPanel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    bottomTabsSpeedToastPanel_->setFocusPolicy(Qt::NoFocus);
    bottomTabsSpeedToastWindow_->setStyleSheet(
        QStringLiteral(
            "QWidget#QuickShellBottomTabsSpeedToast {"
            " background: transparent;"
            " border: none;"
            "}"
            "QWidget#QuickShellBottomTabsSpeedToastPanel {"
            " background: rgba(0, 0, 0, 204);"
            " border: none;"
            " border-radius: 18px;"
            "}"
            "QLabel {"
            " background: transparent;"
            " color: #F8FAFC;"
            " border: none;"
            "}"
        )
    );
    auto* windowLayout = new QVBoxLayout(bottomTabsSpeedToastWindow_);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(bottomTabsSpeedToastPanel_);
    auto* toastLayout = new QVBoxLayout(bottomTabsSpeedToastPanel_);
    toastLayout->setContentsMargins(24, 18, 24, 18);
    toastLayout->setSpacing(0);
    bottomTabsSpeedToastLabel_->setAlignment(Qt::AlignCenter);
    bottomTabsSpeedToastLabel_->setFocusPolicy(Qt::NoFocus);
    bottomTabsSpeedToastLabel_->setTextFormat(Qt::RichText);
    bottomTabsSpeedToastLabel_->setTextInteractionFlags(Qt::NoTextInteraction);
    bottomTabsSpeedToastLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    toastLayout->addWidget(bottomTabsSpeedToastLabel_);
    bottomTabsSpeedToastWindow_->setWindowOpacity(1.0);
    bottomTabsSpeedToastOpacityAnimation_->setDuration(240);
    bottomTabsSpeedToastOpacityAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(bottomTabsSpeedToastOpacityAnimation_, &QPropertyAnimation::finished, this, [this]() {
        if (bottomTabsSpeedToastWindow_ != nullptr && bottomTabsSpeedToastWindow_->windowOpacity() <= 0.0) {
            hideBottomTabsSpeedToast();
        }
    });
    bottomTabsSpeedToastTimer_->setSingleShot(true);
    bottomTabsSpeedToastTimer_->setInterval(900);
    connect(bottomTabsSpeedToastTimer_, &QTimer::timeout, this, [this]() {
        if (bottomTabsSpeedToastOpacityAnimation_ == nullptr || bottomTabsSpeedToastWindow_ == nullptr) {
            return;
        }
        bottomTabsSpeedToastOpacityAnimation_->stop();
        bottomTabsSpeedToastWindow_->setWindowOpacity(1.0);
        bottomTabsSpeedToastOpacityAnimation_->setStartValue(1.0);
        bottomTabsSpeedToastOpacityAnimation_->setEndValue(0.0);
        bottomTabsSpeedToastOpacityAnimation_->start();
    });
    bottomTabsSpeedToastWindow_->hide();
}

QuickShellNativeSurfaceHost::~QuickShellNativeSurfaceHost()
{
    // Per-delete bracket logging so a crash inside any single delete is pinpointed by the last
    // "*_enter" line in the log without a subsequent "*_exit". Matches the pattern added in
    // QuickShellBootstrap::acceptedCloseDestroyAndQuit() one level up.
    const auto logStep = [](const QString& action) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/surface_host"),
            QStringLiteral("action=%1").arg(action),
            true
        );
    };

    logStep(QStringLiteral("destructor_enter"));

    // Release rehosted MainWindow widgets before deleting surface widgets so they are not
    // transitively destroyed via child-widget cascade. attachNativeWidgets() reparented the
    // menu bar, tool bar, status bar, outline dock, workspace widget, and bottom-tabs widget
    // INTO these surface widgets; deleting the surface widget would then destroy those
    // rehosted children. The crash seen as "widget_sidebar_enter" with no "widget_sidebar_exit"
    // tracks to outline-dock (a QDockWidget) destruction inside sidebar deletion — Qt's QDockWidget
    // teardown does not tolerate being deleted as a grandchild of a widget it was never docked into.
    if (contentProvider_ != nullptr) {
        logStep(QStringLiteral("detach_rehosted_enter"));
        QMainWindow* mainWindow = qobject_cast<QMainWindow*>(contentProvider_->shellWindowWidget());
        const auto releaseBack = [mainWindow](QWidget* widget) {
            if (widget == nullptr || mainWindow == nullptr) {
                return;
            }
            if (widget->parentWidget() == mainWindow) {
                return;
            }
            widget->hide();
            widget->setParent(mainWindow);
        };
        if (mainWindow != nullptr) {
            releaseBack(mainWindow->menuBar());
            if (QToolBar* toolBar = mainWindow->findChild<QToolBar*>()) {
                releaseBack(toolBar);
            }
            releaseBack(mainWindow->statusBar());
        }
        releaseBack(contentProvider_->shellOutlineDockWidget());
        releaseBack(contentProvider_->shellWorkspaceWidget());
        releaseBack(contentProvider_->shellBottomTabsWidget());
        logStep(QStringLiteral("detach_rehosted_exit"));
    }

    if (surfaceBundle_.topChrome != nullptr) {
        logStep(QStringLiteral("bundle_topChrome_enter"));
        delete surfaceBundle_.topChrome;
        surfaceBundle_.topChrome = nullptr;
        logStep(QStringLiteral("bundle_topChrome_exit"));
    }
    if (surfaceBundle_.sidebar != nullptr) {
        logStep(QStringLiteral("bundle_sidebar_enter"));
        delete surfaceBundle_.sidebar;
        surfaceBundle_.sidebar = nullptr;
        logStep(QStringLiteral("bundle_sidebar_exit"));
    }
    if (surfaceBundle_.workspace != nullptr) {
        logStep(QStringLiteral("bundle_workspace_enter"));
        delete surfaceBundle_.workspace;
        surfaceBundle_.workspace = nullptr;
        logStep(QStringLiteral("bundle_workspace_exit"));
    }
    if (surfaceBundle_.bottomTabs != nullptr) {
        logStep(QStringLiteral("bundle_bottomTabs_enter"));
        delete surfaceBundle_.bottomTabs;
        surfaceBundle_.bottomTabs = nullptr;
        logStep(QStringLiteral("bundle_bottomTabs_exit"));
    }
    if (surfaceBundle_.status != nullptr) {
        logStep(QStringLiteral("bundle_status_enter"));
        delete surfaceBundle_.status;
        surfaceBundle_.status = nullptr;
        logStep(QStringLiteral("bundle_status_exit"));
    }
    surfaceBundle_.previewCompositeWindow = nullptr;

    if (topChromeSurfaceWidget_ != nullptr) {
        logStep(QStringLiteral("widget_topChrome_enter"));
        delete topChromeSurfaceWidget_;
        topChromeSurfaceWidget_ = nullptr;
        logStep(QStringLiteral("widget_topChrome_exit"));
    }
    if (sidebarSurfaceWidget_ != nullptr) {
        logStep(QStringLiteral("widget_sidebar_enter"));
        delete sidebarSurfaceWidget_;
        sidebarSurfaceWidget_ = nullptr;
        logStep(QStringLiteral("widget_sidebar_exit"));
    }
    if (workspaceSurfaceWidget_ != nullptr) {
        logStep(QStringLiteral("widget_workspace_enter"));
        delete workspaceSurfaceWidget_;
        workspaceSurfaceWidget_ = nullptr;
        logStep(QStringLiteral("widget_workspace_exit"));
    }
    if (bottomTabsSurfaceWidget_ != nullptr) {
        logStep(QStringLiteral("widget_bottomTabs_enter"));
        delete bottomTabsSurfaceWidget_;
        bottomTabsSurfaceWidget_ = nullptr;
        logStep(QStringLiteral("widget_bottomTabs_exit"));
    }
    if (statusSurfaceWidget_ != nullptr) {
        logStep(QStringLiteral("widget_status_enter"));
        delete statusSurfaceWidget_;
        statusSurfaceWidget_ = nullptr;
        logStep(QStringLiteral("widget_status_exit"));
    }
    if (bottomTabsSpeedToastWindow_ != nullptr) {
        logStep(QStringLiteral("widget_speedToastWindow_enter"));
        delete bottomTabsSpeedToastWindow_;
        bottomTabsSpeedToastWindow_ = nullptr;
        logStep(QStringLiteral("widget_speedToastWindow_exit"));
    }
    bottomTabsSpeedToastPanel_ = nullptr;
    bottomTabsSpeedToastLabel_ = nullptr;
    bottomTabsSpeedToastTimer_ = nullptr;
    bottomTabsSpeedToastOpacityAnimation_ = nullptr;

    logStep(QStringLiteral("destructor_exit"));
}

QuickShellNativeSurfaceBundle QuickShellNativeSurfaceHost::surfaceBundle() const
{
    QuickShellNativeSurfaceBundle bundle = surfaceBundle_;
    bundle.previewCompositeWindow =
        stateSource_ != nullptr ? stateSource_->shellPreviewCompositeWindow() : nullptr;
    return bundle;
}

QWidget* QuickShellNativeSurfaceHost::topChromeSurfaceWidget() const
{
    return topChromeSurfaceWidget_;
}

QWidget* QuickShellNativeSurfaceHost::sidebarSurfaceWidget() const
{
    return sidebarSurfaceWidget_;
}

QWidget* QuickShellNativeSurfaceHost::workspaceSurfaceWidget() const
{
    return workspaceSurfaceWidget_;
}

QWidget* QuickShellNativeSurfaceHost::bottomTabsSurfaceWidget() const
{
    return bottomTabsSurfaceWidget_;
}

QWidget* QuickShellNativeSurfaceHost::statusSurfaceWidget() const
{
    return statusSurfaceWidget_;
}

void QuickShellNativeSurfaceHost::refreshSurfaceStyles()
{
    const QList<QWidget*> bridgeSurfaces{
        topChromeSurfaceWidget_,
        sidebarSurfaceWidget_,
        workspaceSurfaceWidget_,
        bottomTabsSurfaceWidget_,
        statusSurfaceWidget_,
    };
    for (QWidget* surface : bridgeSurfaces) {
        applyBridgeSurfaceBaseStyle(surface);
    }

    if (contentProvider_ == nullptr) {
        return;
    }
    QWidget* shellWindow = contentProvider_->shellWindowWidget();
    miacode::ui::AppBackgroundPainter* backgroundPainter =
        miacode::ui::appBackgroundPainterForWidget(shellWindow);
    if (backgroundPainter == nullptr) {
        return;
    }

    QRect canvasGeometry = shellWindow != nullptr
        ? shellWindow->property("miacode.quick_root_window_frame_geometry").toRect()
        : QRect();
    if (!canvasGeometry.isValid() && shellWindow != nullptr) {
        canvasGeometry = QRect(shellWindow->mapToGlobal(QPoint(0, 0)), shellWindow->size());
    }
    backgroundPainter->setCanvasGeometryGlobal(canvasGeometry);

    for (QWidget* surface : bridgeSurfaces) {
        if (surface == nullptr) {
            continue;
        }
        miacode::ui::installAppBackgroundPainter(surface, backgroundPainter);
        surface->update();
        const QList<QWidget*> children = surface->findChildren<QWidget*>();
        for (QWidget* child : children) {
            if (child != nullptr) {
                child->update();
            }
        }
    }
}

void QuickShellNativeSurfaceHost::syncTopChromeSurfaceSize(int width, int height)
{
    resizeSurface(topChromeSurfaceWidget_, width, height, "top_chrome");
    setSurfaceVisible(topChromeSurfaceWidget_, canShowBridgeSurfaces());
}

void QuickShellNativeSurfaceHost::syncSidebarSurfaceSize(int width, int height)
{
    resizeSurface(sidebarSurfaceWidget_, width, height, "sidebar");
    setSurfaceVisible(sidebarSurfaceWidget_, canShowBridgeSurfaces());
    if (QDockWidget* outlineDock = contentProvider_ != nullptr ? contentProvider_->shellOutlineDockWidget() : nullptr;
        outlineDock != nullptr) {
        if (QWidget* widget = outlineDock->widget(); widget != nullptr) {
            widget->updateGeometry();
        }
        outlineDock->updateGeometry();
        if (!outlineDock->isVisible()) {
            outlineDock->show();
        }
    }
}

void QuickShellNativeSurfaceHost::syncWorkspaceSurfaceSize(int width, int height)
{
    resizeSurface(workspaceSurfaceWidget_, width, height, "workspace");
    setSurfaceVisible(workspaceSurfaceWidget_, canShowBridgeSurfaces());
    if (QWidget* workspaceWidget = contentProvider_ != nullptr ? contentProvider_->shellWorkspaceWidget() : nullptr;
        workspaceWidget != nullptr) {
        workspaceWidget->updateGeometry();
        if (!workspaceWidget->isVisible()) {
            workspaceWidget->show();
        }
    }
}

void QuickShellNativeSurfaceHost::syncBottomTabsSurfaceSize(int width, int height)
{
    if (!shouldUseBottomTabsNativeSurface(stateSource_)) {
        if (kBridgeSurfaceVisibilityFollowsTabs) {
            setSurfaceVisible(bottomTabsSurfaceWidget_, false);
        }
        return;
    }
    resizeSurface(bottomTabsSurfaceWidget_, width, height, "bottom_tabs");
    if (kBridgeSurfaceVisibilityFollowsTabs) {
        setSurfaceVisible(bottomTabsSurfaceWidget_, canShowBridgeSurfaces());
    }
    // macOS: opportunistic single-shot pass in case the UI-ready retry window
    // elapsed before the WindowContainer finished adopting this surface.
    runOrphanShellNeutralizePass(1);
    if (QWidget* bottomTabsWidget =
            contentProvider_ != nullptr ? contentProvider_->shellBottomTabsWidget() : nullptr;
        bottomTabsWidget != nullptr) {
        bottomTabsWidget->updateGeometry();
        if (!bottomTabsWidget->isVisible()) {
            bottomTabsWidget->show();
        }
    }
}

void QuickShellNativeSurfaceHost::syncBottomTabsToastAnchor(int x, int y, int width, int height, bool visible)
{
    bottomTabsToastAnchorRect_ = QRect(x, y, qMax(1, width), qMax(1, height));
    bottomTabsToastAnchorVisible_ = visible && width > 0 && height > 0;
    if (!bottomTabsToastAnchorVisible_) {
        hideBottomTabsSpeedToast();
        return;
    }
    updateBottomTabsSpeedToastGeometry();
}

void QuickShellNativeSurfaceHost::syncStatusSurfaceSize(int width, int height)
{
    resizeSurface(statusSurfaceWidget_, width, height, "status");
    setSurfaceVisible(statusSurfaceWidget_, canShowBridgeSurfaces());
    if (QMainWindow* mainWindow =
            qobject_cast<QMainWindow*>(contentProvider_ != nullptr ? contentProvider_->shellWindowWidget() : nullptr);
        mainWindow != nullptr) {
        if (QStatusBar* statusBar = mainWindow->statusBar(); statusBar != nullptr) {
            statusBar->updateGeometry();
            if (!statusBar->isVisible()) {
                statusBar->show();
            }
        }
    }
}

void QuickShellNativeSurfaceHost::refreshBottomTabsSurfaceVisibility()
{
    if (kBridgeSurfaceVisibilityFollowsTabs) {
        setSurfaceVisible(
            bottomTabsSurfaceWidget_,
            canShowBridgeSurfaces() && shouldUseBottomTabsNativeSurface(stateSource_));
    }
    syncBottomTabsForeignWindowVisibility();
    if (stateSource_ != nullptr && !stateSource_->shellBottomTabsVisible()) {
        hideBottomTabsSpeedToast();
    }
}

void QuickShellNativeSurfaceHost::updateRootWindowFrameGeometry(const QRect& geometry)
{
    if (contentProvider_ != nullptr) {
        contentProvider_->shellSetRootWindowFrameGeometry(geometry);
    }
    refreshSurfaceStyles();
}

void QuickShellNativeSurfaceHost::noteQuickShellUiReady()
{
    quickShellUiReady_ = true;
    if (contentProvider_ != nullptr) {
        contentProvider_->shellNoteQuickUiReady();
    }
    // By now the QML WindowContainers have adopted the bridge surfaces, so the
    // orphan panels can be hidden. The pass retries because the native reparent
    // may lag a frame or two behind this callback. No-op on non-macOS.
    runOrphanShellNeutralizePass(40);
}

#ifdef Q_OS_MACOS
namespace {

// The foreign QWindows (QWindow::fromWinId) whose winId is the stable content
// NSView handle adopted by the QML WindowContainers. Order matches
// orphanShellWindows_/orphanShellNeutralized_: {topChrome, sidebar, workspace,
// bottomTabs, status}.
void collectBridgeForeignWindows(const QuickShellNativeSurfaceBundle& bundle, QWindow* out[5])
{
    out[0] = bundle.topChrome;
    out[1] = bundle.sidebar;
    out[2] = bundle.workspace;
    out[3] = bundle.bottomTabs;
    out[4] = bundle.status;
}

void* nativeViewHandleOf(QWindow* window)
{
    return window != nullptr ? reinterpret_cast<void*>(window->winId()) : nullptr;
}

}  // namespace
#endif

void QuickShellNativeSurfaceHost::captureOrphanShellWindows()
{
#ifdef Q_OS_MACOS
    QWindow* windows[kBridgeSurfaceCount] = {};
    collectBridgeForeignWindows(surfaceBundle_, windows);
    for (int i = 0; i < kBridgeSurfaceCount; ++i) {
        orphanShellWindows_[i] =
            miacode::quick_shell::mac::captureOrphanShellWindow(nativeViewHandleOf(windows[i]));
    }
#endif
}

void QuickShellNativeSurfaceHost::runOrphanShellNeutralizePass(int attemptsLeft)
{
#ifdef Q_OS_MACOS
    QWindow* windows[kBridgeSurfaceCount] = {};
    collectBridgeForeignWindows(surfaceBundle_, windows);
    bool allDone = true;
    for (int i = 0; i < kBridgeSurfaceCount; ++i) {
        if (orphanShellNeutralized_[i]) {
            continue;
        }
        if (miacode::quick_shell::mac::neutralizeOrphanShellWindow(
                nativeViewHandleOf(windows[i]), orphanShellWindows_[i])) {
            orphanShellNeutralized_[i] = true;
        } else {
            allDone = false;
        }
    }
    // Piggyback on the startup retry window: re-assert the bottom-tabs foreign
    // window's visibility each pass, covering the WindowContainer-vs-adoption
    // race that could leave the validation page painted over the timeline.
    syncBottomTabsForeignWindowVisibility();
    // Keep ticking through the whole startup window even once every panel is
    // neutralized (allDone): the visibility sync above must keep re-asserting
    // the bottom-tabs NSView state, because the AppKit-level clobber can land
    // after adoption completes. Each extra tick is an idempotent no-op check.
    Q_UNUSED(allDone);
    if (attemptsLeft > 1) {
        QTimer::singleShot(100, this, [this, attemptsLeft]() {
            runOrphanShellNeutralizePass(attemptsLeft - 1);
        });
    }
#else
    Q_UNUSED(attemptsLeft);
#endif
}

void QuickShellNativeSurfaceHost::syncBottomTabsForeignWindowVisibility()
{
#ifdef Q_OS_MACOS
    QWindow* foreignWindow = surfaceBundle_.bottomTabs;
    if (foreignWindow == nullptr || foreignWindow->parent() == nullptr) {
        // Not adopted by the QML WindowContainer yet — showing/hiding the
        // standalone foreign window here would surface it as its own window.
        return;
    }
    const bool shouldShow = shouldUseBottomTabsNativeSurface(stateSource_);
    if (foreignWindow->isVisible() != shouldShow) {
        foreignWindow->setVisible(shouldShow);
    }
    // Qt's cached visibility can already agree while the NSView itself is
    // still showing: the adoption-time reparent clobbers the container's
    // initial setVisible(false) at the AppKit level. Enforce the state on the
    // NSView directly; idempotent, and later Qt setVisible calls stay in sync.
    miacode::quick_shell::mac::setContentViewHidden(
        nativeViewHandleOf(foreignWindow), !shouldShow);
#endif
}

QWidget* QuickShellNativeSurfaceHost::createBridgeSurface(const QString& objectName)
{
    auto* bridgeRoot = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    bridgeRoot->setObjectName(objectName);
    bridgeRoot->setAttribute(Qt::WA_NativeWindow);
    bridgeRoot->setAttribute(Qt::WA_StyledBackground, true);
    bridgeRoot->setFocusPolicy(Qt::StrongFocus);
    bridgeRoot->setContentsMargins(0, 0, 0, 0);
    bridgeRoot->setMinimumSize(QSize(64, 64));
    bridgeRoot->resize(960, 720);
    applyBridgeSurfaceBaseStyle(bridgeRoot);
    // Phase 3f-3 — hide() BEFORE winId(). Order matters: winId() forces
    // native HWND creation, and Qt creates the HWND in the visible
    // state if hide() hasn't been called first. Calling hide() AFTER
    // winId() means the HWND briefly flashes on screen at the default
    // (0,0) position with size (960,720) before Qt issues
    // ShowWindow(SW_HIDE). That's the "black square dancing during
    // startup" the user reported.
    //
    // Setting WA_DontShowOnScreen would suppress the native window
    // entirely, but we NEED the HWND for QWindow::fromWinId so the
    // QML WindowContainer can adopt it. So we just hide it cleanly
    // before forcing creation.
    bridgeRoot->hide();
    bridgeRoot->winId();
    return bridgeRoot;
}

QString QuickShellNativeSurfaceHost::formatBottomTabsSpeedToastText(const QString& speedLabel)
{
    QString numericText = speedLabel;
    numericText.remove(QLatin1Char('x'), Qt::CaseInsensitive);
    bool ok = false;
    const double numericRate = numericText.trimmed().toDouble(&ok);
    const int percent = ok ? qRound(numericRate * 100.0) : 100;
    return QStringLiteral(
               "<div style='text-align:center;'>"
               "<div style='font-size:14px;font-weight:600;line-height:1.2;'>当前倍速</div>"
               "<div style='margin-top:6px;font-size:28px;font-weight:700;line-height:1.1;'>%1%</div>"
               "</div>"
           )
        .arg(percent);
}

QWindow* QuickShellNativeSurfaceHost::createForeignWindowForSurface(QWidget* surface) const
{
    if (surface == nullptr) {
        return nullptr;
    }
    const WId wid = surface->winId();
    if (wid == 0) {
        return nullptr;
    }
    QWindow* window = QWindow::fromWinId(wid);
    if (window != nullptr) {
        window->QObject::setParent(const_cast<QuickShellNativeSurfaceHost*>(this));
    }
    return window;
}

void QuickShellNativeSurfaceHost::attachNativeWidgets()
{
    if (contentProvider_ == nullptr) {
        return;
    }

    refreshSurfaceStyles();

    auto* topChromeLayout = qobject_cast<QBoxLayout*>(topChromeSurfaceWidget_->layout());
    auto* sidebarLayout = qobject_cast<QBoxLayout*>(sidebarSurfaceWidget_->layout());
    auto* workspaceLayout = qobject_cast<QBoxLayout*>(workspaceSurfaceWidget_->layout());
    auto* bottomTabsLayout = qobject_cast<QBoxLayout*>(bottomTabsSurfaceWidget_->layout());
    auto* statusLayout = qobject_cast<QBoxLayout*>(statusSurfaceWidget_->layout());
    if (topChromeLayout == nullptr
        || sidebarLayout == nullptr
        || workspaceLayout == nullptr
        || bottomTabsLayout == nullptr
        || statusLayout == nullptr) {
        return;
    }

    if (QMainWindow* mainWindow = qobject_cast<QMainWindow*>(contentProvider_->shellWindowWidget());
        mainWindow != nullptr) {
        if (QMenuBar* windowMenuBar = mainWindow->menuBar(); windowMenuBar != nullptr) {
            windowMenuBar->setNativeMenuBar(false);
            if (windowMenuBar->parentWidget() != topChromeSurfaceWidget_) {
                windowMenuBar->setParent(topChromeSurfaceWidget_);
            }
            if (topChromeLayout->indexOf(windowMenuBar) < 0) {
                topChromeLayout->addWidget(windowMenuBar);
            }
#ifdef Q_OS_MACOS
            miacode::quick_shell::mac::installTopLevelMenuPopupPositioning(
                windowMenuBar, surfaceBundle_.topChrome);
#endif
            windowMenuBar->show();
        }

        if (QToolBar* toolBar = mainWindow->findChild<QToolBar*>(); toolBar != nullptr) {
            mainWindow->removeToolBar(toolBar);
            if (toolBar->parentWidget() != topChromeSurfaceWidget_) {
                toolBar->setParent(topChromeSurfaceWidget_);
            }
            if (topChromeLayout->indexOf(toolBar) < 0) {
                topChromeLayout->addWidget(toolBar);
            }
            toolBar->show();
        }

        if (QStatusBar* windowStatusBar = mainWindow->statusBar(); windowStatusBar != nullptr) {
            if (windowStatusBar->parentWidget() != statusSurfaceWidget_) {
                windowStatusBar->setParent(statusSurfaceWidget_);
            }
            if (statusLayout->indexOf(windowStatusBar) < 0) {
                statusLayout->addWidget(windowStatusBar);
            }
            windowStatusBar->show();
        }
    }

    if (QDockWidget* outlineDock = contentProvider_->shellOutlineDockWidget(); outlineDock != nullptr) {
        if (QMainWindow* mainWindow = qobject_cast<QMainWindow*>(contentProvider_->shellWindowWidget());
            mainWindow != nullptr) {
            mainWindow->removeDockWidget(outlineDock);
        }
        if (outlineDock->parentWidget() != sidebarSurfaceWidget_) {
            outlineDock->setParent(sidebarSurfaceWidget_);
        }
        if (sidebarLayout->indexOf(outlineDock) < 0) {
            sidebarLayout->addWidget(outlineDock);
        }
        outlineDock->show();
    }

    if (QWidget* workspaceWidget = contentProvider_->shellWorkspaceWidget(); workspaceWidget != nullptr) {
        if (workspaceWidget->parentWidget() != workspaceSurfaceWidget_) {
            workspaceWidget->setParent(workspaceSurfaceWidget_);
        }
        if (workspaceLayout->indexOf(workspaceWidget) < 0) {
            workspaceLayout->addWidget(workspaceWidget, 1);
        }
        workspaceWidget->show();
    }

    if (QWidget* bottomTabsWidget = contentProvider_->shellBottomTabsWidget(); bottomTabsWidget != nullptr) {
        if (auto* tabWidget = qobject_cast<QTabWidget*>(bottomTabsWidget); tabWidget != nullptr) {
            if (QTabBar* tabBar = tabWidget->tabBar(); tabBar != nullptr) {
                tabBar->hide();
            }
        }
        if (bottomTabsWidget->parentWidget() != bottomTabsSurfaceWidget_) {
            bottomTabsWidget->setParent(bottomTabsSurfaceWidget_);
        }
        if (bottomTabsLayout->indexOf(bottomTabsWidget) < 0) {
            bottomTabsLayout->addWidget(bottomTabsWidget, 1);
        }
        bottomTabsWidget->show();
    }

    if (QWidget* previewPanel = contentProvider_->shellPreviewPanelWidget(); previewPanel != nullptr) {
        previewPanel->hide();
    }

    activateLayout(topChromeSurfaceWidget_, "top_chrome");
    activateLayout(sidebarSurfaceWidget_, "sidebar");
    activateLayout(workspaceSurfaceWidget_, "workspace");
    activateLayout(bottomTabsSurfaceWidget_, "bottom_tabs");
    activateLayout(statusSurfaceWidget_, "status");
}

void QuickShellNativeSurfaceHost::ensureSurfaceLayouts()
{
    if (topChromeSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(topChromeSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    if (sidebarSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(sidebarSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    if (workspaceSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QHBoxLayout(workspaceSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    if (bottomTabsSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(bottomTabsSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    if (statusSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(statusSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
}

void QuickShellNativeSurfaceHost::showAllSurfaces()
{
    if (contentProvider_ != nullptr) {
        if (QDockWidget* outlineDock = contentProvider_->shellOutlineDockWidget(); outlineDock != nullptr) {
            if (!outlineDock->isVisible()) {
                outlineDock->show();
            }
        }
    }
    setSurfaceVisible(topChromeSurfaceWidget_, true);
    setSurfaceVisible(sidebarSurfaceWidget_, true);
    setSurfaceVisible(workspaceSurfaceWidget_, true);
    setSurfaceVisible(
        bottomTabsSurfaceWidget_,
        !kBridgeSurfaceVisibilityFollowsTabs || shouldUseBottomTabsNativeSurface(stateSource_)
    );
    setSurfaceVisible(statusSurfaceWidget_, true);
}

bool QuickShellNativeSurfaceHost::canShowBridgeSurfaces() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return quickShellUiReady_;
#endif
}

void QuickShellNativeSurfaceHost::updateBottomTabsSpeedToastGeometry()
{
    if (bottomTabsSpeedToastWindow_ == nullptr
        || !bottomTabsToastAnchorVisible_
        || !bottomTabsToastAnchorRect_.isValid()) {
        return;
    }
    const QSize preferredSize = bottomTabsSpeedToastWindow_->sizeHint();
    const int availableWidth = qMax(1, bottomTabsToastAnchorRect_.width() - kBottomTabsSpeedToastHorizontalMargin * 2);
    int toastWidth = qMax(kBottomTabsSpeedToastMinWidth, preferredSize.width());
    toastWidth = qMin(toastWidth, availableWidth);
    const int toastHeight = qMax(kBottomTabsSpeedToastMinHeight, preferredSize.height());
    const int toastX = bottomTabsToastAnchorRect_.x()
        + qMax(0, (bottomTabsToastAnchorRect_.width() - toastWidth) / 2);
    const int toastY = bottomTabsToastAnchorRect_.y()
        + qMax(0, (bottomTabsToastAnchorRect_.height() - toastHeight) / 2);
    bottomTabsSpeedToastWindow_->setGeometry(toastX, toastY, toastWidth, toastHeight);
}

void QuickShellNativeSurfaceHost::showBottomTabsSpeedToast(const QString& speedLabel)
{
    if (bottomTabsSpeedToastWindow_ == nullptr
        || bottomTabsSpeedToastLabel_ == nullptr
        || stateSource_ == nullptr
        || !stateSource_->shellBottomTabsVisible()
        || !bottomTabsToastAnchorVisible_) {
        return;
    }
    if (bottomTabsSpeedToastTimer_ != nullptr) {
        bottomTabsSpeedToastTimer_->stop();
    }
    if (bottomTabsSpeedToastOpacityAnimation_ != nullptr) {
        bottomTabsSpeedToastOpacityAnimation_->stop();
    }
    bottomTabsSpeedToastLabel_->setText(formatBottomTabsSpeedToastText(speedLabel));
    updateBottomTabsSpeedToastGeometry();
    bottomTabsSpeedToastWindow_->setWindowOpacity(1.0);
    bottomTabsSpeedToastWindow_->show();
    bottomTabsSpeedToastWindow_->raise();
    if (bottomTabsSpeedToastTimer_ != nullptr) {
        bottomTabsSpeedToastTimer_->start();
    }
}

void QuickShellNativeSurfaceHost::hideBottomTabsSpeedToast()
{
    if (bottomTabsSpeedToastTimer_ != nullptr) {
        bottomTabsSpeedToastTimer_->stop();
    }
    if (bottomTabsSpeedToastOpacityAnimation_ != nullptr) {
        bottomTabsSpeedToastOpacityAnimation_->stop();
    }
    if (bottomTabsSpeedToastWindow_ != nullptr) {
        bottomTabsSpeedToastWindow_->setWindowOpacity(1.0);
        bottomTabsSpeedToastWindow_->hide();
    }
}
