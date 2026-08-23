#pragma once

#include "QmlUiRootLifecycle.h"
#include "QmlUiWindowChrome.h"

#include <memory>

#include <QIcon>
#include <QObject>
#include <QPointer>

class QQmlApplicationEngine;
class QQuickWindow;

class MainWindow;
class QmlApplicationContext;
class QuickShellController;
class ChartDropOverlay;
class QTimer;

// Default UI entry (v2). Shares the hidden MainWindow backend with
// QuickShell; no NativeSurfaceHost / StyleBridge. QuickShell: --ui=v1.
class QmlUiBootstrap final : public QObject
{
    Q_OBJECT

public:
    explicit QmlUiBootstrap(const QIcon& appIcon, QObject* parent = nullptr);
    ~QmlUiBootstrap() override;

    bool start(const QString& startupOpenTarget = QString());
    void setShowWelcomeDialogOnStartup(bool show) { showWelcomeDialogOnStartup_ = show; }

private:
    void beginAcceptedRootWindowShutdown(const QString& source);
    void destroyAcceptedRootWindowResourcesAndQuit(const QString& source);
    void releaseRootWindowResources();
    void syncChartDropOverlay();

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QuickShellController> controller_;
    std::unique_ptr<QmlApplicationContext> applicationContext_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    // Owns the native-event filter; must outlive the root window.
    std::unique_ptr<QmlUiWindowChrome> windowChrome_;
    std::unique_ptr<ChartDropOverlay> chartDropOverlay_;
    std::unique_ptr<QTimer> chartDropOverlayMonitorTimer_;
    QPointer<QQuickWindow> rootWindow_;
    miacode::qml_ui::RootLifecycle rootLifecycle_;
    bool acceptedRootWindowShutdownStarted_ = false;
    bool acceptedRootWindowDestroyStarted_ = false;
    bool showWelcomeDialogOnStartup_ = false;
};
