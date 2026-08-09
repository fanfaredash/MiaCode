#pragma once

#include "common/LogEmissionPolicy.h"

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

#ifdef Q_OS_WIN
namespace miacode::app::windows_idle_diagnostics {
class WindowsIdleEventMonitor;
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
    void logFocusEvent(const QString& action, QObject* watched = nullptr, QEvent* event = nullptr, const QString& detail = QString()) const;

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QuickShellNativeSurfaceHost> surfaceHost_;
    std::unique_ptr<QuickShellController> controller_;
    std::unique_ptr<QuickShellStyleBridge> styleBridge_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
#ifdef Q_OS_WIN
    std::unique_ptr<miacode::app::windows_idle_diagnostics::WindowsIdleEventMonitor> windowsIdleEventMonitor_;
    std::unique_ptr<QAbstractNativeEventFilter> nativeCloseEventFilter_;
    quintptr rootWindowNativeHwnd_ = 0;
#endif
    QPointer<QQuickWindow> rootWindow_;
    // Collapses runs of `action=event_filter` focus lines that carry no state
    // change. In a 39-minute field capture `quick_shell/focus` was 27% of the
    // whole runtime channel and 1616 of its 1748 lines were event_filter, most
    // of them repeating the previous line's focus state verbatim. Mutable
    // because logFocusEvent() is const.
    mutable QString lastFocusFilterSignature_;
    // Dedup drops are counted, not discarded: the next emitted focus line carries
    // ` deduped=N`, mirroring how appendExtraSelectionsPerfLog preserves its
    // throttled volume as `suppressed=…`. If focus thrash is itself a symptom, the
    // rate has to survive the filtering. GUI-thread only, so a plain member is
    // enough; mutable for the same reason as the signature above.
    mutable int suppressedFocusFilterCount_ = 0;
    miacode::diagnostics::MousePressLogGate mousePressLogGate_;
    bool previewSeekArmed_ = false;
    bool rootWindowCloseRelayScheduled_ = false;
    bool rootWindowCloseRelayActive_ = false;
    bool acceptedRootWindowShutdownStarted_ = false;
    bool acceptedRootWindowDestroyScheduled_ = false;
    bool acceptedRootWindowDestroyStarted_ = false;
    bool showWelcomeDialogOnStartup_ = false;
};
