#pragma once

// Worker-side helper that keeps the preview popup HWND tracking the editor
// HWND across cross-process OS events. The in-process editor uses
// PreviewPopupHwndTracker (subclasses the editor HWND's window proc) — but
// the worker is in a different process, so subclassing is not available.
// Instead, we use SetWinEventHook with WINEVENT_OUTOFCONTEXT scope to receive
// EVENT_OBJECT_LOCATIONCHANGE notifications from the editor's HWND on the
// worker's main thread message queue.
//
// Lifecycle:
//   1. registerOwner(editorHwnd, popupHwnd, callback):
//        - records the HWNDs
//        - calls SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, ...) with
//          process+thread filter set to the editor's PID/TID, OUTOFCONTEXT
//          scope (so the hook fires even though the editor is a different
//          process), SKIPOWNPROCESS not set.
//        - kicks off a 1-Hz watchdog timer for RDP / session reconnect
//          recovery (the hook is silently dropped on RDP detach; we
//          re-register after WTS_SESSION_RECONNECT).
//   2. on each EVENT_OBJECT_LOCATIONCHANGE:
//        - filters by hwnd == editorHwnd (the hook callback may fire for
//          any window in the editor's process)
//        - reads the editor's window rect via GetWindowRect
//        - invokes the callback so the worker session can reposition
//          popupHwnd via SetWindowPos.
//   3. unregister() releases the hook.
//
// Phase 2 of the validation plan covers single-monitor move, multi-monitor
// drag, DPI changes, RDP attach/detach. See
// docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md.

#include <QObject>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <functional>

namespace miacode::preview::worker {

// Callback signature: receives the editor HWND's screen rect (left, top,
// right, bottom in screen pixels). Worker decides where to put the popup.
using OwnerHwndLocationCallback = std::function<void(int left, int top, int right, int bottom)>;

class OwnerHwndTracker : public QObject
{
    Q_OBJECT
public:
    explicit OwnerHwndTracker(QObject* parent = nullptr);
    ~OwnerHwndTracker() override;

    // Register a tracker for editorHwnd. The callback fires on the worker's
    // main thread on every editor location change. Returns false on
    // SetWinEventHook failure; the caller can fall back to a polling loop.
    // Idempotent: calling again replaces the registration.
    bool registerOwner(quintptr editorHwnd, OwnerHwndLocationCallback callback);

    // Stop tracking. Idempotent; safe in the destructor.
    void unregister();

    // Force a manual sync — read the editor's current rect and invoke the
    // callback. Used for the periodic watchdog, RDP reconnect, etc.
    void resync();

    bool isRegistered() const;

#ifdef Q_OS_WIN
    // Internal: invoked by the global SetWinEventHook trampoline.
    void onWinEvent(HWND eventHwnd, LONG idObject, LONG idChild);
#endif

private slots:
    void onWatchdogTimerFired();

private:
#ifdef Q_OS_WIN
    HWND editorHwnd_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    DWORD editorPid_ = 0;
    DWORD editorTid_ = 0;
#endif
    OwnerHwndLocationCallback callback_;
    QTimer watchdog_;
};

}  // namespace miacode::preview::worker
