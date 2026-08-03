#pragma once

#include <memory>

#include <QIcon>
#include <QObject>
#include <QPointer>

class QQmlApplicationEngine;
class QQuickWindow;

class MainWindow;
class QmlApplicationContext;
class QuickShellController;

// Pure-QML (v2) workbench entry. Shares the same hidden MainWindow backend as
// QuickShell, but never creates NativeSurfaceHost / StyleBridge. Default GUI
// remains v1 QuickShellBootstrap; this path is selected by --ui=v2.
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

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QuickShellController> controller_;
    std::unique_ptr<QmlApplicationContext> applicationContext_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QPointer<QQuickWindow> rootWindow_;
    bool acceptedRootWindowShutdownStarted_ = false;
    bool acceptedRootWindowDestroyStarted_ = false;
    bool showWelcomeDialogOnStartup_ = false;
};
