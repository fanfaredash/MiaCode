#pragma once

#include "shell/RootLifecycle.h"
#include "chrome/WindowChrome.h"

#include <memory>

#include <QIcon>
#include <QObject>
#include <QPointer>

class QQmlApplicationEngine;
class QQuickWindow;

class Session;
namespace miacode {
class ApplicationServices;
}
namespace miacode::ui {
class ApplicationContext;
class CoverExportWindow;
class ChartDropBridge;
}

// The single UI entry. Builds the non-Widget application services first, then
// drives the runtime Session backend while the whole visible shell is QML. The
// document domain, the UI-request boundary and the job-progress surface are no
// longer among what it owns — they belong to ApplicationServices, which is
// constructed before the window and destroyed after it. The Session backend
// still contains non-visual migration state, but the product source set no
// longer depends on Qt Widgets; QML owns file dialogs, unsaved-change choices,
// and page navigation confirmations.
namespace miacode::ui {

class Bootstrap final : public QObject
{
    Q_OBJECT

public:
    explicit Bootstrap(const QIcon& appIcon, QObject* parent = nullptr);
    ~Bootstrap() override;

    bool start(const QString& startupOpenTarget = QString());

private:
    void openCoverExportWindow(int difficultyId);
    void beginAcceptedRootWindowShutdown(const QString& source);
    void destroyAcceptedRootWindowResourcesAndQuit(const QString& source);
    void releaseRootWindowResources();

    QIcon appIcon_;
    // Declared before backend_ so it is destroyed after it: the window's
    // teardown still talks to these services.
    std::unique_ptr<miacode::ApplicationServices> applicationServices_;
    std::unique_ptr<Session> backend_;
    std::unique_ptr<ApplicationContext> applicationContext_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    // Owns the native-event filter; must outlive the root window.
    std::unique_ptr<WindowChrome> windowChrome_;
    std::unique_ptr<miacode::ui::ChartDropBridge> chartDropBridge_;
    QPointer<QQuickWindow> rootWindow_;
    QPointer<CoverExportWindow> coverWindow_;
    miacode::ui::RootLifecycle rootLifecycle_;
    bool acceptedRootWindowShutdownStarted_ = false;
    bool acceptedRootWindowDestroyStarted_ = false;
};
} // namespace miacode::ui
