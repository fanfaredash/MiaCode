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
namespace miacode::qml_ui {
class QmlChartDropBridge;
}

// The single UI entry. Drives a hidden MainWindow as its backend: the
// widgets layer keeps owning documents, actions and dialogs while the
// whole visible shell is QML.
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

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QmlApplicationContext> applicationContext_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    // Owns the native-event filter; must outlive the root window.
    std::unique_ptr<QmlUiWindowChrome> windowChrome_;
    std::unique_ptr<miacode::qml_ui::QmlChartDropBridge> chartDropBridge_;
    QPointer<QQuickWindow> rootWindow_;
    miacode::qml_ui::RootLifecycle rootLifecycle_;
    bool acceptedRootWindowShutdownStarted_ = false;
    bool acceptedRootWindowDestroyStarted_ = false;
    bool showWelcomeDialogOnStartup_ = false;
};
