#pragma once

#include "QuickShellContracts.h"

#include <QObject>

class QLabel;
class QPropertyAnimation;
class QTimer;
class QWidget;

class QuickShellNativeSurfaceHost : public QObject
{
    Q_OBJECT

public:
    QuickShellNativeSurfaceHost(
        QuickShellNativeContentProvider* contentProvider,
        QuickShellStateSource* stateSource,
        QObject* parent = nullptr
    );
    ~QuickShellNativeSurfaceHost() override;

    QuickShellNativeSurfaceBundle surfaceBundle() const;

    QWidget* topChromeSurfaceWidget() const;
    QWidget* sidebarSurfaceWidget() const;
    QWidget* workspaceSurfaceWidget() const;
    QWidget* bottomTabsSurfaceWidget() const;
    QWidget* statusSurfaceWidget() const;

    void refreshSurfaceStyles();
    void syncTopChromeSurfaceSize(int width, int height);
    void syncSidebarSurfaceSize(int width, int height);
    void syncWorkspaceSurfaceSize(int width, int height);
    void syncBottomTabsSurfaceSize(int width, int height);
    void syncBottomTabsToastAnchor(int x, int y, int width, int height, bool visible);
    void syncStatusSurfaceSize(int width, int height);
    void refreshBottomTabsSurfaceVisibility();
    void updateRootWindowFrameGeometry(const QRect& geometry);
    void noteQuickShellUiReady();
    void showBottomTabsSpeedToast(const QString& speedLabel);
    void hideBottomTabsSpeedToast();

private:
    static QWidget* createBridgeSurface(const QString& objectName);
    static QString formatBottomTabsSpeedToastText(const QString& speedLabel);
    QWindow* createForeignWindowForSurface(QWidget* surface) const;
    void attachNativeWidgets();
    void ensureSurfaceLayouts();
    void updateBottomTabsSpeedToastGeometry();
    bool canShowBridgeSurfaces() const;

    QuickShellNativeContentProvider* contentProvider_ = nullptr;
    QuickShellStateSource* stateSource_ = nullptr;
    QWidget* topChromeSurfaceWidget_ = nullptr;
    QWidget* sidebarSurfaceWidget_ = nullptr;
    QWidget* workspaceSurfaceWidget_ = nullptr;
    QWidget* bottomTabsSurfaceWidget_ = nullptr;
    QWidget* statusSurfaceWidget_ = nullptr;
    QWidget* bottomTabsSpeedToastWindow_ = nullptr;
    QWidget* bottomTabsSpeedToastPanel_ = nullptr;
    QLabel* bottomTabsSpeedToastLabel_ = nullptr;
    QTimer* bottomTabsSpeedToastTimer_ = nullptr;
    QPropertyAnimation* bottomTabsSpeedToastOpacityAnimation_ = nullptr;
    QRect bottomTabsToastAnchorRect_;
    bool bottomTabsToastAnchorVisible_ = false;
    bool quickShellUiReady_ = false;
    QuickShellNativeSurfaceBundle surfaceBundle_;
};
