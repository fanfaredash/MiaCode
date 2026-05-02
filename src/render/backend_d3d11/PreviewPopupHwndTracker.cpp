#include "render/backend_d3d11/PreviewPopupHwndTracker.h"

#include "common/DebugLog.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <QStringLiteral>

#include <cstdio>
#include <ctime>
#include <utility>
#include <vector>

namespace miacode::preview::dcomp {

#ifdef Q_OS_WIN
namespace {

struct Entry
{
    void* token;
    PreviewPopupHwndTracker::DeferCallback callback;
};

// GUI-thread-only registry — see header comment for thread-safety
// argument. Function-local static avoids static-initialisation-order
// surprises (the tracker is touched during early QML load before any
// global ctor would run).
std::vector<Entry>& registry()
{
    static std::vector<Entry> entries;
    return entries;
}

void logTracker(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    // force=true so this bypasses runtimeDebugOutputEnabled(). The
    // tracker's diagnostics are intentionally always-on for now —
    // they're rate-limited (notify is ≤1/sec) and rare otherwise
    // (register/unregister fire only on attach/detach), so the cost
    // is negligible and they're invaluable for confirming the hook
    // is wired up at all.
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("render/backend_d3d11/popup_tracker"),
        payload,
        /*force=*/true);
    // Also emit to qDebug() as a parallel signal; goes through whatever
    // QtMessageHandler is installed (default: stderr / OutputDebugString
    // on Windows). Removed once the WM_ hook is verified working.
    qDebug().noquote() << QStringLiteral("[popup_tracker]") << payload;
    // Last-resort diagnostic: write to .miacode/logs/popup_tracker_alive.log
    // so the app definitely has write access. Bypasses Qt's async
    // logger entirely (direct fopen).
    {
        const QString markerPath = miacode::debug_log::logDirectory()
            + QStringLiteral("/popup_tracker_alive.log");
        FILE* f = nullptr;
        if (::_wfopen_s(&f, reinterpret_cast<const wchar_t*>(markerPath.utf16()),
                        L"ab") == 0 && f != nullptr) {
            const auto t = std::time(nullptr);
            std::fprintf(f, "[%lld] %s\n",
                         static_cast<long long>(t),
                         payload.toUtf8().constData());
            std::fclose(f);
        }
        // Also OutputDebugStringW — visible in DebugView++ regardless
        // of file I/O. Distinct prefix so it's easy to filter.
        QString line = QStringLiteral("MIACODE_POPUP_TRACKER ") + payload + QStringLiteral("\n");
        ::OutputDebugStringW(reinterpret_cast<LPCWSTR>(line.utf16()));
    }
}

}  // namespace

void PreviewPopupHwndTracker::registerSurface(void* token, DeferCallback callback)
{
    if (token == nullptr || !callback) {
        return;
    }
    auto& reg = registry();
    for (auto& e : reg) {
        if (e.token == token) {
            // Idempotent re-register — replace the callback so the
            // closure captures the surface's current state (a fresh
            // popup HWND after detach + re-attach, for example).
            e.callback = std::move(callback);
            logTracker("re_register",
                       QStringLiteral("token=0x%1 entries=%2")
                           .arg(reinterpret_cast<quintptr>(token), 0, 16)
                           .arg(reg.size()));
            return;
        }
    }
    reg.push_back({ token, std::move(callback) });
    logTracker("register",
               QStringLiteral("token=0x%1 entries=%2")
                   .arg(reinterpret_cast<quintptr>(token), 0, 16)
                   .arg(reg.size()));
}

void PreviewPopupHwndTracker::unregisterSurface(void* token)
{
    if (token == nullptr) {
        return;
    }
    auto& reg = registry();
    for (auto it = reg.begin(); it != reg.end(); ++it) {
        if (it->token == token) {
            reg.erase(it);
            logTracker("unregister",
                       QStringLiteral("token=0x%1 entries=%2")
                           .arg(reinterpret_cast<quintptr>(token), 0, 16)
                           .arg(reg.size()));
            return;
        }
    }
}

void PreviewPopupHwndTracker::notifyOwnerWindowPosChanged()
{
    auto& reg = registry();
    if (reg.empty()) {
        return;
    }
    HDWP hdwp = ::BeginDeferWindowPos(static_cast<int>(reg.size()));
    if (hdwp == nullptr) {
        // BeginDeferWindowPos failure is rare (kernel object exhaustion).
        // The callbacks all require an HDWP (they only call DeferWindowPos,
        // not SetWindowPos), so without one we have to bail this tick;
        // the next WM_WINDOWPOSCHANGED retries. Logging unconditionally
        // because this should never happen in steady state.
        logTracker("begin_defer_failed",
                   QStringLiteral("entries=%1").arg(reg.size()));
        return;
    }
    // Bool-based result so the count actually reflects work done.
    // (Pointer comparison was unreliable — DeferWindowPos returns the
    // same handle pointer on success on modern Windows, so the old
    // logic always reported 0 extends even on a fully-functioning
    // batch.)
    int extends = 0;
    int noops = 0;
    int errors = 0;
    for (auto& e : reg) {
        const bool extended = e.callback(hdwp);
        if (hdwp == nullptr) {
            // Callback signalled error by clearing hdwp. We can't
            // continue the batch; abort cleanly.
            ++errors;
            break;
        }
        if (extended) {
            ++extends;
        } else {
            ++noops;
        }
    }
    if (hdwp != nullptr && !::EndDeferWindowPos(hdwp)) {
        logTracker("end_defer_failed",
                   QStringLiteral("extends=%1 noops=%2 errors=%3 entries=%4")
                       .arg(extends).arg(noops).arg(errors).arg(reg.size()));
        return;
    }
    // Throttled diagnostic — log at most once per second when there's
    // actual movement to report. Keeps the log clean during a 60 Hz
    // drag (otherwise 60 lines/sec) while still proving the hook is
    // wired up. The "noop_only" branch fires when the editor moves
    // but no popup geometry changed — useful for confirming the hook
    // ran during stable states (focus changes, z-order tweaks).
    static QElapsedTimer s_lastLogTimer;
    static bool s_timerStarted = false;
    if (!s_timerStarted) {
        s_lastLogTimer.start();
        s_timerStarted = true;
    }
    // Throttled diagnostic — 1/sec. extends > 0 during a drag means
    // the deferred path is committing batched popup repositioning in
    // the same DWM tick as the editor (the goal). extends == 0 with
    // noops == entries means every callback dedup'd (popup already at
    // intended position from a previous tick — fine, just nothing to
    // re-do). errors > 0 means a callback aborted the batch.
    if (s_lastLogTimer.elapsed() >= 1000) {
        logTracker("notify_called",
                   QStringLiteral("extends=%1 noops=%2 errors=%3 entries=%4")
                       .arg(extends).arg(noops).arg(errors).arg(reg.size()));
        s_lastLogTimer.restart();
    }
}
#endif

}  // namespace miacode::preview::dcomp
