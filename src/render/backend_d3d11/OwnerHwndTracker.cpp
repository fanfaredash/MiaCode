#include "render/backend_d3d11/OwnerHwndTracker.h"

#include "common/DebugLog.h"

#include <QHash>
#include <QMutex>

namespace {

void appendTrackerLog(const QString& tag, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/owner_hwnd_tracker"),
        QStringLiteral("tag=%1 %2").arg(tag, payload)
    );
}

#ifdef Q_OS_WIN
// SetWinEventHook callbacks are global C functions; we maintain a registry
// keyed by HWINEVENTHOOK so the worker session can have multiple trackers
// in principle (in practice there's only one, but keeping the registry
// pattern matches PreviewPopupHwndTracker's design and prevents leaks if
// the worker ever spawns nested previews).
QMutex& trackerRegistryMutex()
{
    static QMutex m;
    return m;
}

QHash<HWINEVENTHOOK, miacode::preview::worker::OwnerHwndTracker*>& trackerRegistry()
{
    static QHash<HWINEVENTHOOK, miacode::preview::worker::OwnerHwndTracker*> map;
    return map;
}

void CALLBACK winEventHookProc(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD /*idEventThread*/,
    DWORD /*dwmsEventTime*/)
{
    if (event != EVENT_OBJECT_LOCATIONCHANGE) {
        return;
    }
    miacode::preview::worker::OwnerHwndTracker* tracker = nullptr;
    {
        QMutexLocker lock(&trackerRegistryMutex());
        tracker = trackerRegistry().value(hook, nullptr);
    }
    if (tracker == nullptr) {
        return;
    }
    tracker->onWinEvent(hwnd, idObject, idChild);
}
#endif

}  // namespace

namespace miacode::preview::worker {

OwnerHwndTracker::OwnerHwndTracker(QObject* parent)
    : QObject(parent)
{
    // Watchdog cadence — RDP detach silently drops the hook on some Windows
    // builds. 1 Hz lets us catch and re-register within a second of session
    // reconnect without burning CPU on a tighter cadence.
    watchdog_.setInterval(1000);
    watchdog_.setSingleShot(false);
    QObject::connect(&watchdog_, &QTimer::timeout,
                     this, &OwnerHwndTracker::onWatchdogTimerFired);
}

OwnerHwndTracker::~OwnerHwndTracker()
{
    unregister();
}

bool OwnerHwndTracker::registerOwner(quintptr editorHwnd, OwnerHwndLocationCallback callback)
{
    unregister();

    callback_ = std::move(callback);

#ifdef Q_OS_WIN
    editorHwnd_ = reinterpret_cast<HWND>(editorHwnd);
    if (editorHwnd_ == nullptr || !::IsWindow(editorHwnd_)) {
        appendTrackerLog(QStringLiteral("invalid_hwnd"),
                         QStringLiteral("hwnd=0x%1").arg(editorHwnd, 0, 16));
        return false;
    }

    // Filter the hook to the editor's PID/TID so we don't waste cycles
    // dispatching every other window's location change. EVENT_OBJECT_LOCATIONCHANGE
    // fires on EVERY HWND in the entire desktop session by default;
    // narrowing scope here is essential.
    editorTid_ = ::GetWindowThreadProcessId(editorHwnd_, &editorPid_);

    // OUTOFCONTEXT means the hook callback runs in our own process's thread
    // (rather than being injected into the editor process). This is the
    // safer mode for cross-process tracking and matches the plan.
    hook_ = ::SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_OBJECT_LOCATIONCHANGE,
        nullptr,
        winEventHookProc,
        editorPid_,
        editorTid_,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (hook_ == nullptr) {
        appendTrackerLog(QStringLiteral("hook_failed"),
                         QStringLiteral("editor_pid=%1 editor_tid=%2 err=%3")
                             .arg(editorPid_)
                             .arg(editorTid_)
                             .arg(::GetLastError()));
        return false;
    }

    {
        QMutexLocker lock(&trackerRegistryMutex());
        trackerRegistry().insert(hook_, this);
    }

    appendTrackerLog(QStringLiteral("registered"),
                     QStringLiteral("editor_hwnd=0x%1 editor_pid=%2 editor_tid=%3")
                         .arg(reinterpret_cast<quintptr>(editorHwnd_), 0, 16)
                         .arg(editorPid_)
                         .arg(editorTid_));

    watchdog_.start();
    // Initial sync so the popup snaps to the current editor rect even if
    // no location-change event has fired yet.
    resync();
    return true;
#else
    Q_UNUSED(editorHwnd);
    return false;
#endif
}

void OwnerHwndTracker::unregister()
{
    watchdog_.stop();
#ifdef Q_OS_WIN
    if (hook_ != nullptr) {
        {
            QMutexLocker lock(&trackerRegistryMutex());
            trackerRegistry().remove(hook_);
        }
        ::UnhookWinEvent(hook_);
        hook_ = nullptr;
    }
    editorHwnd_ = nullptr;
    editorPid_ = 0;
    editorTid_ = 0;
#endif
    callback_ = OwnerHwndLocationCallback();
}

void OwnerHwndTracker::resync()
{
#ifdef Q_OS_WIN
    if (editorHwnd_ == nullptr || !::IsWindow(editorHwnd_) || !callback_) {
        return;
    }
    RECT rect{};
    if (!::GetWindowRect(editorHwnd_, &rect)) {
        return;
    }
    callback_(rect.left, rect.top, rect.right, rect.bottom);
#endif
}

bool OwnerHwndTracker::isRegistered() const
{
#ifdef Q_OS_WIN
    return hook_ != nullptr;
#else
    return false;
#endif
}

#ifdef Q_OS_WIN
void OwnerHwndTracker::onWinEvent(HWND eventHwnd, LONG idObject, LONG /*idChild*/)
{
    if (eventHwnd != editorHwnd_) {
        // The hook was filtered to the editor's PID/TID, but other HWNDs in
        // that thread can still trigger LOCATIONCHANGE — caret, child
        // controls, tooltip etc. Filter to the editor HWND only.
        return;
    }
    if (idObject != OBJID_WINDOW) {
        // OBJID_WINDOW means the entire window's location/extent moved.
        // Smaller objects (caret, scrollbar) trigger their own idObject
        // values and are uninteresting here.
        return;
    }
    if (!callback_) {
        return;
    }
    RECT rect{};
    if (!::GetWindowRect(editorHwnd_, &rect)) {
        return;
    }
    callback_(rect.left, rect.top, rect.right, rect.bottom);
}
#endif

void OwnerHwndTracker::onWatchdogTimerFired()
{
#ifdef Q_OS_WIN
    if (editorHwnd_ == nullptr) {
        return;
    }
    // Editor process gone? Stop watching to avoid logging spam every second.
    if (!::IsWindow(editorHwnd_)) {
        appendTrackerLog(QStringLiteral("editor_hwnd_gone"),
                         QStringLiteral("hwnd=0x%1")
                             .arg(reinterpret_cast<quintptr>(editorHwnd_), 0, 16));
        unregister();
        return;
    }
    // Hook silently dropped on RDP detach — re-register if missing.
    if (hook_ == nullptr) {
        appendTrackerLog(QStringLiteral("hook_missing_recover"), QString());
        const HWND owner = editorHwnd_;
        OwnerHwndLocationCallback cb = callback_;
        unregister();
        registerOwner(reinterpret_cast<quintptr>(owner), std::move(cb));
        return;
    }
    // Periodic resync as defence in depth — even when the hook is healthy,
    // a missed event from a high-frequency drag can leave the popup at a
    // stale position for one frame; the watchdog absorbs this.
    resync();
#endif
}

}  // namespace miacode::preview::worker
