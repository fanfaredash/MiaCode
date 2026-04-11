#include "QuickShellNativeSurfaceHost.h"

#include <QBoxLayout>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
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
    }
    activateLayout(surface);
    surface->updateGeometry();
    surface->update();
    surface->show();
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
    , previewControlsSurfaceWidget_(createBridgeSurface(QStringLiteral("QuickShellPreviewControlsSurface")))
    , statusSurfaceWidget_(createBridgeSurface(QStringLiteral("QuickShellStatusSurface")))
{
    surfaceBundle_.topChrome = createForeignWindowForSurface(topChromeSurfaceWidget_);
    surfaceBundle_.sidebar = createForeignWindowForSurface(sidebarSurfaceWidget_);
    surfaceBundle_.workspace = createForeignWindowForSurface(workspaceSurfaceWidget_);
    surfaceBundle_.previewControls = createForeignWindowForSurface(previewControlsSurfaceWidget_);
    surfaceBundle_.status = createForeignWindowForSurface(statusSurfaceWidget_);

    ensureSurfaceLayouts();
    attachNativeWidgets();
    showAllSurfaces();
}

QuickShellNativeSurfaceHost::~QuickShellNativeSurfaceHost()
{
    delete surfaceBundle_.topChrome;
    surfaceBundle_.topChrome = nullptr;
    delete surfaceBundle_.sidebar;
    surfaceBundle_.sidebar = nullptr;
    delete surfaceBundle_.workspace;
    surfaceBundle_.workspace = nullptr;
    delete surfaceBundle_.previewControls;
    surfaceBundle_.previewControls = nullptr;
    delete surfaceBundle_.status;
    surfaceBundle_.status = nullptr;
    surfaceBundle_.previewCompositeWindow = nullptr;
    delete topChromeSurfaceWidget_;
    topChromeSurfaceWidget_ = nullptr;
    delete sidebarSurfaceWidget_;
    sidebarSurfaceWidget_ = nullptr;
    delete workspaceSurfaceWidget_;
    workspaceSurfaceWidget_ = nullptr;
    delete previewControlsSurfaceWidget_;
    previewControlsSurfaceWidget_ = nullptr;
    delete statusSurfaceWidget_;
    statusSurfaceWidget_ = nullptr;
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

QWidget* QuickShellNativeSurfaceHost::previewControlsSurfaceWidget() const
{
    return previewControlsSurfaceWidget_;
}

QWidget* QuickShellNativeSurfaceHost::statusSurfaceWidget() const
{
    return statusSurfaceWidget_;
}

void QuickShellNativeSurfaceHost::refreshSurfaceStyles()
{
    if (previewControlsSurfaceWidget_ == nullptr || contentProvider_ == nullptr) {
        return;
    }
    previewControlsSurfaceWidget_->setStyleSheet(contentProvider_->shellPreviewPanelStyleSheet());
}

int QuickShellNativeSurfaceHost::recommendedPreviewControlsHeight(int previewPaneWidth, int fallbackHeight) const
{
    if (contentProvider_ == nullptr) {
        return qMax(180, fallbackHeight);
    }
    int preferredHeight = qMax(180, fallbackHeight);
    if (QWidget* controlCard = contentProvider_->shellPreviewControlCardWidget(); controlCard != nullptr) {
        const int controlCardHeight = qMax(
            controlCard->minimumSizeHint().height(),
            controlCard->sizeHint().height()
        );
        preferredHeight = qMax(
            preferredHeight,
            controlCardHeight + contentProvider_->shellPreviewStatsMinimumPanelHeight(previewPaneWidth) + 34
        );
    }
    return preferredHeight;
}

void QuickShellNativeSurfaceHost::syncTopChromeSurfaceSize(int width, int height)
{
    resizeSurface(topChromeSurfaceWidget_, width, height);
}

void QuickShellNativeSurfaceHost::syncSidebarSurfaceSize(int width, int height)
{
    resizeSurface(sidebarSurfaceWidget_, width, height);
    if (QDockWidget* outlineDock = contentProvider_ != nullptr ? contentProvider_->shellOutlineDockWidget() : nullptr;
        outlineDock != nullptr) {
        if (QWidget* widget = outlineDock->widget(); widget != nullptr) {
            widget->updateGeometry();
        }
        outlineDock->updateGeometry();
        outlineDock->show();
    }
}

void QuickShellNativeSurfaceHost::syncWorkspaceSurfaceSize(int width, int height)
{
    resizeSurface(workspaceSurfaceWidget_, width, height);
    if (QWidget* workspaceWidget = contentProvider_ != nullptr ? contentProvider_->shellWorkspaceWidget() : nullptr;
        workspaceWidget != nullptr) {
        workspaceWidget->updateGeometry();
        workspaceWidget->show();
    }
}

void QuickShellNativeSurfaceHost::syncPreviewControlsSurfaceSize(int width, int height)
{
    resizeSurface(previewControlsSurfaceWidget_, width, height);
    if (QWidget* controlCard =
            contentProvider_ != nullptr ? contentProvider_->shellPreviewControlCardWidget() : nullptr;
        controlCard != nullptr) {
        controlCard->updateGeometry();
        controlCard->show();
    }
    if (QWidget* statsCard = contentProvider_ != nullptr ? contentProvider_->shellPreviewStatsCardWidget() : nullptr;
        statsCard != nullptr) {
        statsCard->updateGeometry();
        statsCard->show();
    }
    if (contentProvider_ != nullptr) {
        contentProvider_->shellUpdatePreviewStatsLayout();
    }
}

void QuickShellNativeSurfaceHost::syncStatusSurfaceSize(int width, int height)
{
    resizeSurface(statusSurfaceWidget_, width, height);
    if (QMainWindow* mainWindow =
            qobject_cast<QMainWindow*>(contentProvider_ != nullptr ? contentProvider_->shellWindowWidget() : nullptr);
        mainWindow != nullptr) {
        if (QStatusBar* statusBar = mainWindow->statusBar(); statusBar != nullptr) {
            statusBar->updateGeometry();
            statusBar->show();
        }
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
    bridgeRoot->setContentsMargins(0, 0, 0, 0);
    bridgeRoot->setMinimumSize(QSize(64, 64));
    bridgeRoot->resize(960, 720);
    bridgeRoot->winId();
    bridgeRoot->hide();
    return bridgeRoot;
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
    auto* previewControlsLayout = qobject_cast<QBoxLayout*>(previewControlsSurfaceWidget_->layout());
    auto* statusLayout = qobject_cast<QBoxLayout*>(statusSurfaceWidget_->layout());
    if (topChromeLayout == nullptr
        || sidebarLayout == nullptr
        || workspaceLayout == nullptr
        || previewControlsLayout == nullptr
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

    if (QWidget* controlCard = contentProvider_->shellPreviewControlCardWidget(); controlCard != nullptr) {
        if (controlCard->parentWidget() != previewControlsSurfaceWidget_) {
            controlCard->setParent(previewControlsSurfaceWidget_);
        }
        if (previewControlsLayout->indexOf(controlCard) < 0) {
            previewControlsLayout->addWidget(controlCard);
        }
        controlCard->show();
    }

    if (QWidget* statsCard = contentProvider_->shellPreviewStatsCardWidget(); statsCard != nullptr) {
        if (statsCard->parentWidget() != previewControlsSurfaceWidget_) {
            statsCard->setParent(previewControlsSurfaceWidget_);
        }
        if (previewControlsLayout->indexOf(statsCard) < 0) {
            previewControlsLayout->addWidget(statsCard);
        }
        statsCard->show();
    }

    if (QWidget* previewPanel = contentProvider_->shellPreviewPanelWidget(); previewPanel != nullptr) {
        previewPanel->hide();
    }

    activateLayout(topChromeSurfaceWidget_);
    activateLayout(sidebarSurfaceWidget_);
    activateLayout(workspaceSurfaceWidget_);
    activateLayout(previewControlsSurfaceWidget_);
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
    if (previewControlsSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(previewControlsSurfaceWidget_);
        layout->setContentsMargins(8, 12, 8, 12);
        layout->setSpacing(10);
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
            outlineDock->show();
        }
    }
    topChromeSurfaceWidget_->show();
    sidebarSurfaceWidget_->show();
    workspaceSurfaceWidget_->show();
    previewControlsSurfaceWidget_->show();
    statusSurfaceWidget_->show();
}
