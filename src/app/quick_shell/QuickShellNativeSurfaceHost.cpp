#include "QuickShellNativeSurfaceHost.h"

#include "common/DebugLog.h"

#include <QBoxLayout>
#include <QDockWidget>
#include <QEasingCurve>
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

void activateLayout(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    if (QLayout* layout = widget->layout(); layout != nullptr) {
        layout->activate();
    }
}

void resizeSurface(QWidget* surface, int width, int height)
{
    if (surface == nullptr) {
        return;
    }
    const QSize nextSize(qMax(1, width), qMax(1, height));
    if (surface->size() != nextSize) {
        surface->resize(nextSize);
        surface->update();
    }
    activateLayout(surface);
    surface->updateGeometry();
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

    ensureSurfaceLayouts();
    attachNativeWidgets();
    showAllSurfaces();
    refreshBottomTabsSurfaceVisibility();

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
    Q_UNUSED(contentProvider_);
}

void QuickShellNativeSurfaceHost::syncTopChromeSurfaceSize(int width, int height)
{
    resizeSurface(topChromeSurfaceWidget_, width, height);
}

void QuickShellNativeSurfaceHost::syncSidebarSurfaceSize(int width, int height)
{
    resizeSurface(sidebarSurfaceWidget_, width, height);
    setSurfaceVisible(sidebarSurfaceWidget_, true);
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
    resizeSurface(workspaceSurfaceWidget_, width, height);
    setSurfaceVisible(workspaceSurfaceWidget_, true);
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
        setSurfaceVisible(bottomTabsSurfaceWidget_, false);
        return;
    }
    resizeSurface(bottomTabsSurfaceWidget_, width, height);
    setSurfaceVisible(bottomTabsSurfaceWidget_, true);
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
    resizeSurface(statusSurfaceWidget_, width, height);
    setSurfaceVisible(statusSurfaceWidget_, true);
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
    setSurfaceVisible(bottomTabsSurfaceWidget_, shouldUseBottomTabsNativeSurface(stateSource_));
    if (stateSource_ != nullptr && !stateSource_->shellBottomTabsVisible()) {
        hideBottomTabsSpeedToast();
    }
}

void QuickShellNativeSurfaceHost::updateRootWindowFrameGeometry(const QRect& geometry)
{
    if (contentProvider_ != nullptr) {
        contentProvider_->shellSetRootWindowFrameGeometry(geometry);
    }
}

void QuickShellNativeSurfaceHost::noteQuickShellUiReady()
{
    if (contentProvider_ != nullptr) {
        contentProvider_->shellNoteQuickUiReady();
    }
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

    activateLayout(topChromeSurfaceWidget_);
    activateLayout(sidebarSurfaceWidget_);
    activateLayout(workspaceSurfaceWidget_);
    activateLayout(bottomTabsSurfaceWidget_);
    activateLayout(statusSurfaceWidget_);
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
    setSurfaceVisible(bottomTabsSurfaceWidget_, shouldUseBottomTabsNativeSurface(stateSource_));
    setSurfaceVisible(statusSurfaceWidget_, true);
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
