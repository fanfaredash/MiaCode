#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace miacode::app::windows_idle_diagnostics {

bool isRelevantMessage(quint32 message);
QString powerBroadcastReasonName(quintptr reason);
QString consoleDisplayStateName(quint32 state);
QString consoleDisplayStatePayload(quint32 state);
QString sessionChangeReasonName(quintptr reason);
QString deviceChangeReasonName(quintptr reason);
QString eventPayload(quint32 message, quintptr wParam, qintptr lParam);

class WindowsIdleEventMonitor {
public:
    WindowsIdleEventMonitor();
    ~WindowsIdleEventMonitor();

    WindowsIdleEventMonitor(const WindowsIdleEventMonitor&) = delete;
    WindowsIdleEventMonitor& operator=(const WindowsIdleEventMonitor&) = delete;

    void registerWindow(quintptr nativeWindow);
    void unregisterWindow();
    void observeNativeMessage(const QByteArray& eventType, void* message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace miacode::app::windows_idle_diagnostics
