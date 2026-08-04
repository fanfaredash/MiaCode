#pragma once

#include <memory>

#include <QObject>
#include <QIcon>
#include <QElapsedTimer>
#include <QPointer>

class QByteArray;
class QQmlApplicationEngine;
class QQuickWindow;
class QAbstractNativeEventFilter;
class QTimer;

class MainWindow;
class QuickShellController;
class QuickShellNativeSurfaceHost;
class QuickShellStyleBridge;
class ChartDropOverlay;

#ifdef Q_OS_WIN
namespace miacode::preview::dcomp {
class PreviewDCompSurface;
}
#endif

class QuickShellBootstrap : public QObject
{
    Q_OBJECT

public:
    explicit QuickShellBootstrap(const QIcon& appIcon, QObject* parent = nullptr);
    ~QuickShellBootstrap() override;

    bool start(const QString& startupOpenTarget = QString());
    // When set before start(), the first-run welcome / initial-config
    // dialog is shown once the QuickShell UI is ready (see start()'s
    // post-show hook). Driven by main()'s first-run detection / --welcome.
    void setShowWelcomeDialogOnStartup(bool show) { showWelcomeDialogOnStartup_ = show; }
    QuickShellController* controller() const;
    QuickShellStyleBridge* styleBridge() const;
#ifdef Q_OS_WIN
    bool handleNativeCloseEvent(const QByteArray& eventType, void* message, qintptr* result);
#endif

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool previewSeekHotRectContainsGlobalPoint(const QPoint& globalPoint) const;
    bool shouldTraceFocusObject(QObject* watched) const;
    QString describeFocusObject(QObject* object) const;
    QString focusReasonName(Qt::FocusReason reason) const;
    void scheduleRootWindowCloseRelay(const QString& source);
    void processRootWindowCloseRelay();
    void beginAcceptedRootWindowShutdown(const QString& source);
    void scheduleAcceptedRootWindowDestroyAndQuit(const QString& source);
    void destroyAcceptedRootWindowResourcesAndQuit(const QString& source);
    void syncChartDropOverlay();
    bool dragInputStillActive() const;
    bool cursorIsOverQuickShellRoot();
    void logFocusEvent(const QString& action, QObject* watched = nullptr, QEvent* event = nullptr, const QString& detail = QString()) const;
    // Construct previewDCompSurface_ + wire it to the runtime, the
    // stage-media-host signal, and the present sync interval. Idempotent
    // — returns immediately if a surface already exists. `reason` is
    // logged via `dcomp_surface_attached reason=<reason>` for diagnostics.
    // Safe to call after the window has been around for a while; the
    // in-process surface attaches to whatever HWND the window currently
    // owns.
    bool createInProcessPreviewSurface(QQuickWindow* window, const QString& reason);

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QuickShellNativeSurfaceHost> surfaceHost_;
    std::unique_ptr<QuickShellController> controller_;
    std::unique_ptr<QuickShellStyleBridge> styleBridge_;
    std::unique_ptr<ChartDropOverlay> chartDropOverlay_;
    QTimer* chartDropOverlayMonitorTimer_ = nullptr;
    std::unique_ptr<QQmlApplicationEngine> engine_;
#ifdef Q_OS_WIN
    std::unique_ptr<miacode::preview::dcomp::PreviewDCompSurface> previewDCompSurface_;
    std::unique_ptr<QAbstractNativeEventFilter> nativeCloseEventFilter_;
    quintptr rootWindowNativeHwnd_ = 0;
#endif
    QPointer<QQuickWindow> rootWindow_;
    bool previewSeekArmed_ = false;
    bool rootWindowCloseRelayScheduled_ = false;
    bool rootWindowCloseRelayActive_ = false;
    bool acceptedRootWindowShutdownStarted_ = false;
    bool acceptedRootWindowDestroyScheduled_ = false;
    bool acceptedRootWindowDestroyStarted_ = false;
    bool showWelcomeDialogOnStartup_ = false;
};
