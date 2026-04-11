#pragma once

#include "QuickShellContracts.h"

#include <QObject>

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
    QWidget* previewControlsSurfaceWidget() const;
    QWidget* statusSurfaceWidget() const;

    void refreshSurfaceStyles();
    int recommendedPreviewControlsHeight(int previewPaneWidth, int fallbackHeight) const;
    void syncTopChromeSurfaceSize(int width, int height);
    void syncSidebarSurfaceSize(int width, int height);
    void syncWorkspaceSurfaceSize(int width, int height);
    void syncPreviewControlsSurfaceSize(int width, int height);
    void syncStatusSurfaceSize(int width, int height);
    void updateRootWindowFrameGeometry(const QRect& geometry);
    void noteQuickShellUiReady();

private:
    static QWidget* createBridgeSurface(const QString& objectName);
    QWindow* createForeignWindowForSurface(QWidget* surface) const;
    void attachNativeWidgets();
    void ensureSurfaceLayouts();
    void showAllSurfaces();

    QuickShellNativeContentProvider* contentProvider_ = nullptr;
    QuickShellStateSource* stateSource_ = nullptr;
    QWidget* topChromeSurfaceWidget_ = nullptr;
    QWidget* sidebarSurfaceWidget_ = nullptr;
    QWidget* workspaceSurfaceWidget_ = nullptr;
    QWidget* previewControlsSurfaceWidget_ = nullptr;
    QWidget* statusSurfaceWidget_ = nullptr;
    QuickShellNativeSurfaceBundle surfaceBundle_;
};
