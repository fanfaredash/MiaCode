#include "WindowsIdleEventDiagnostics.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <cstring>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <initguid.h>
#include <windows.h>
#include <wtsapi32.h>
#endif

namespace miacode::app::windows_idle_diagnostics {
namespace {

constexpr quint32 kWmDisplayChange = 0x007E;
constexpr quint32 kWmPowerBroadcast = 0x0218;
constexpr quint32 kWmDeviceChange = 0x0219;
constexpr quint32 kWmWtsSessionChange = 0x02B1;
constexpr quintptr kPbtPowerSettingChange = 0x8013;

QString rawCode(quintptr value)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16);
}

#if defined(Q_OS_WIN)
static_assert(kWmDisplayChange == WM_DISPLAYCHANGE);
static_assert(kWmPowerBroadcast == WM_POWERBROADCAST);
static_assert(kWmDeviceChange == WM_DEVICECHANGE);
static_assert(kWmWtsSessionChange == WM_WTSSESSION_CHANGE);
static_assert(kPbtPowerSettingChange == PBT_POWERSETTINGCHANGE);
static_assert(0x0004 == PBT_APMSUSPEND);
static_assert(0x0012 == PBT_APMRESUMEAUTOMATIC);
static_assert(7 == WTS_SESSION_LOCK);
static_assert(8 == WTS_SESSION_UNLOCK);

void appendDurableEnvironmentEvent(const QString& payload)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    // Level::Fatal IS the durability contract here, and it is self-contained: for a
    // Fatal-level line DebugLog::appendText() drains the async writer and then does its
    // own synchronous open/write/flush/close, never enqueuing (src/common/DebugLog.cpp,
    // appendText()). By the time appendLine() returns, the record is on disk and the
    // queue is empty.
    //
    // Do NOT add a follow-up flushAsyncLogWriter() here. It could only ever find an empty
    // queue and report success, and this runs on the GUI thread inside the Windows native
    // event filter while dispatching WM_POWERBROADCAST and friends -- a redundant blocking
    // flush there is a self-inflicted GUI stall in exactly the scenario this monitor
    // exists to diagnose.
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("windows/environment_event"),
        payload,
        /*force=*/true,
        miacode::debug_log::Level::Fatal);
}
#endif

}  // namespace

bool isRelevantMessage(quint32 message)
{
    return message == kWmDisplayChange
        || message == kWmPowerBroadcast
        || message == kWmDeviceChange
        || message == kWmWtsSessionChange;
}

QString powerBroadcastReasonName(quintptr reason)
{
    switch (reason) {
    case 0x0004: return QStringLiteral("suspend");
    case 0x0006: return QStringLiteral("resume_critical");
    case 0x0007: return QStringLiteral("resume_suspend");
    case 0x000A: return QStringLiteral("power_status_change");
    case 0x0012: return QStringLiteral("resume_automatic");
    case kPbtPowerSettingChange: return QStringLiteral("power_setting_change");
    default: return QStringLiteral("unknown");
    }
}

QString consoleDisplayStateName(quint32 state)
{
    switch (state) {
    case 0: return QStringLiteral("off");
    case 1: return QStringLiteral("on");
    case 2: return QStringLiteral("dimmed");
    default: return QStringLiteral("unknown");
    }
}

QString consoleDisplayStatePayload(quint32 state)
{
    return QStringLiteral("action=console_display_state console_display_state=%1 raw=%2")
        .arg(consoleDisplayStateName(state))
        .arg(state);
}

QString sessionChangeReasonName(quintptr reason)
{
    switch (reason) {
    case 1: return QStringLiteral("console_connect");
    case 2: return QStringLiteral("console_disconnect");
    case 3: return QStringLiteral("remote_connect");
    case 4: return QStringLiteral("remote_disconnect");
    case 5: return QStringLiteral("session_logon");
    case 6: return QStringLiteral("session_logoff");
    case 7: return QStringLiteral("session_lock");
    case 8: return QStringLiteral("session_unlock");
    case 9: return QStringLiteral("session_remote_control");
    case 10: return QStringLiteral("session_create");
    case 11: return QStringLiteral("session_terminate");
    default: return QStringLiteral("unknown");
    }
}

QString deviceChangeReasonName(quintptr reason)
{
    switch (reason) {
    case 0x0007: return QStringLiteral("device_nodes_changed");
    case 0x8000: return QStringLiteral("device_arrival");
    case 0x8004: return QStringLiteral("device_remove_complete");
    default: return QStringLiteral("unknown");
    }
}

QString eventPayload(quint32 message, quintptr wParam, qintptr lParam)
{
    switch (message) {
    case kWmPowerBroadcast:
        return QStringLiteral("action=power_broadcast reason=%1 raw=%2")
            .arg(powerBroadcastReasonName(wParam), rawCode(wParam));
    case kWmDisplayChange: {
        const quint32 packed = static_cast<quint32>(lParam);
        const quint32 width = packed & 0xffffu;
        const quint32 height = (packed >> 16) & 0xffffu;
        return QStringLiteral("action=display_change bits_per_pixel=%1 width=%2 height=%3")
            .arg(static_cast<qulonglong>(wParam))
            .arg(width)
            .arg(height);
    }
    case kWmWtsSessionChange:
        return QStringLiteral("action=session_change reason=%1 raw=%2 session_id=%3")
            .arg(sessionChangeReasonName(wParam), rawCode(wParam))
            .arg(static_cast<qlonglong>(lParam));
    case kWmDeviceChange:
        return QStringLiteral("action=device_change reason=%1 raw=%2")
            .arg(deviceChangeReasonName(wParam), rawCode(wParam));
    default:
        return QString();
    }
}

struct WindowsIdleEventMonitor::Impl {
#if defined(Q_OS_WIN)
    HWND window = nullptr;
    HPOWERNOTIFY consoleDisplayNotification = nullptr;
    bool sessionRegistered = false;
#endif
};

WindowsIdleEventMonitor::WindowsIdleEventMonitor()
    : impl_(std::make_unique<Impl>())
{
}

WindowsIdleEventMonitor::~WindowsIdleEventMonitor()
{
    unregisterWindow();
}

void WindowsIdleEventMonitor::registerWindow(quintptr nativeWindow)
{
#if defined(Q_OS_WIN)
    unregisterWindow();
    if (!miacode::debug_options::runtimeDebugOutputEnabled() || nativeWindow == 0) {
        return;
    }

    impl_->window = reinterpret_cast<HWND>(nativeWindow);
    impl_->sessionRegistered =
        ::WTSRegisterSessionNotification(impl_->window, NOTIFY_FOR_THIS_SESSION) != FALSE;
    const DWORD sessionError = impl_->sessionRegistered ? ERROR_SUCCESS : ::GetLastError();

    impl_->consoleDisplayNotification = ::RegisterPowerSettingNotification(
        impl_->window,
        &GUID_CONSOLE_DISPLAY_STATE,
        DEVICE_NOTIFY_WINDOW_HANDLE);
    const DWORD displayError = impl_->consoleDisplayNotification != nullptr
        ? ERROR_SUCCESS
        : ::GetLastError();

    appendDurableEnvironmentEvent(
        QStringLiteral("action=registration hwnd=0x%1 session_registered=%2 session_error=%3 "
                       "console_display_registered=%4 console_display_error=%5")
            .arg(nativeWindow, 0, 16)
            .arg(impl_->sessionRegistered ? 1 : 0)
            .arg(sessionError)
            .arg(impl_->consoleDisplayNotification != nullptr ? 1 : 0)
            .arg(displayError));
#else
    Q_UNUSED(nativeWindow);
#endif
}

void WindowsIdleEventMonitor::unregisterWindow()
{
#if defined(Q_OS_WIN)
    if (impl_ == nullptr || impl_->window == nullptr) {
        return;
    }
    const quintptr nativeWindow = reinterpret_cast<quintptr>(impl_->window);
    const bool sessionRegistered = impl_->sessionRegistered;
    const bool consoleDisplayRegistered = impl_->consoleDisplayNotification != nullptr;
    if (impl_->sessionRegistered) {
        ::WTSUnRegisterSessionNotification(impl_->window);
    }
    if (impl_->consoleDisplayNotification != nullptr) {
        ::UnregisterPowerSettingNotification(impl_->consoleDisplayNotification);
    }
    impl_->window = nullptr;
    impl_->consoleDisplayNotification = nullptr;
    impl_->sessionRegistered = false;

    // Symmetric with `action=registration`. Every windows/environment_event stops after
    // this returns, and both callers (the destructor and
    // QuickShellBootstrap::beginAcceptedRootWindowShutdown) can run long before the
    // process does — without this line a log that simply goes quiet is indistinguishable
    // from one where the monitor was torn down early. The early return above keeps a
    // never-registered monitor silent.
    appendDurableEnvironmentEvent(
        QStringLiteral("action=unregistration hwnd=0x%1 session_registered=%2 "
                       "console_display_registered=%3")
            .arg(nativeWindow, 0, 16)
            .arg(sessionRegistered ? 1 : 0)
            .arg(consoleDisplayRegistered ? 1 : 0));
#endif
}

void WindowsIdleEventMonitor::observeNativeMessage(const QByteArray& eventType, void* message)
{
#if defined(Q_OS_WIN)
    if (!miacode::debug_options::runtimeDebugOutputEnabled()
        || message == nullptr
        || (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")) {
        return;
    }

    const auto* msg = static_cast<const MSG*>(message);
    if (impl_ == nullptr || impl_->window == nullptr || msg->hwnd != impl_->window) {
        return;
    }
    // nativeEventFilter sees every message the process dispatches, so reject the
    // overwhelming majority here rather than letting eventPayload()'s empty return do the
    // filtering further down. Safe ahead of the PBT_POWERSETTINGCHANGE special case below:
    // that case only ever fires for WM_POWERBROADCAST, which isRelevantMessage() accepts.
    if (!isRelevantMessage(static_cast<quint32>(msg->message))) {
        return;
    }
    QString payload;
    if (msg->message == WM_POWERBROADCAST
        && msg->wParam == PBT_POWERSETTINGCHANGE
        && msg->lParam != 0) {
        const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(msg->lParam);
        if (::IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE)
            && setting->DataLength >= sizeof(DWORD)) {
            DWORD state = 0;
            std::memcpy(&state, setting->Data, sizeof(state));
            payload = consoleDisplayStatePayload(state)
                + QStringLiteral(" source=pbt_power_setting_change");
        }
    }
    if (payload.isEmpty()) {
        payload = eventPayload(
            static_cast<quint32>(msg->message),
            static_cast<quintptr>(msg->wParam),
            static_cast<qintptr>(msg->lParam));
    }
    if (!payload.isEmpty()) {
        appendDurableEnvironmentEvent(payload);
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
}

}  // namespace miacode::app::windows_idle_diagnostics
