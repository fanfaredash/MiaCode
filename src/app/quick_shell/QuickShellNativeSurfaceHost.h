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
    void showAllSurfaces();
    void updateBottomTabsSpeedToastGeometry();

    // macOS only (no-op elsewhere). Capture the Qt::Tool orphan NSPanel behind
    // each bridge surface at construction; then, once the QML WindowContainer has
    // adopted each surface's content view, hide the leftover empty panel so it
    // stops floating over the UI. See QuickShellMacSurfaceSupport.
    void captureOrphanShellWindows();
    void runOrphanShellNeutralizePass(int attemptsLeft);

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
    QuickShellNativeSurfaceBundle surfaceBundle_;

    // Opaque (NSWindow*) handles to the orphan Qt::Tool panels behind the five
    // bridge surfaces, and per-surface "already hidden" flags. Ordered as
    // {topChrome, sidebar, workspace, bottomTabs, status}. Unused on non-macOS.
    static constexpr int kBridgeSurfaceCount = 5;
    // All five surfaces (bottom-tabs included) are neutralized. On macOS the
    // bottom-tabs bridge widget is never hidden/re-shown per tab — a top-level
    // QWidget::show() after adoption re-attaches the content NSView as the
    // panel's contentView, ripping the 语法/无理 page out of the main window.
    // Per-tab visibility is handled by the QML WindowContainer toggling the
    // foreign QWindow (see kBridgeSurfaceVisibilityFollowsTabs in the .cpp).
    void* orphanShellWindows_[kBridgeSurfaceCount] = {};
    bool orphanShellNeutralized_[kBridgeSurfaceCount] = {};
};
