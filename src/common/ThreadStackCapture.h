#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// Self-inflicted, in-process stack walk of ONE registered target thread (in practice the
// GUI thread), taken from a different thread while the target is frozen.
//
// Why this exists: the Windows freeze reports come from users who cannot realistically
// transfer a full `.dmp`. The runtime log has to replace the dump, which means the hang
// watchdog must answer "where is the GUI thread stuck?" on its own. See
// docs/ops/WINDOWS_IDLE_FREEZE_REPRO_ZH.md.
//
// CORRECTNESS HAZARD 1 — read before editing captureRegisteredThreadStack():
// while a thread is suspended it may hold the loader lock or a CRT heap lock. Calling
// ANY allocating or module-loading function from the capturing thread during that window
// (SymInitialize, SymFromAddr, LoadLibrary, malloc-backed containers, QString building)
// can deadlock the watchdog against the very thread it is trying to diagnose. The
// implementation therefore does exactly three things under suspension —
// GetThreadContext, StackWalk64, and writing raw return addresses into a fixed-size
// stack array — and symbolizes only AFTER ResumeThread. Keep it that way.
//
// CORRECTNESS HAZARD 2 — why the suspension window runs on a worker thread:
// even that minimal work is not provably non-blocking. StackWalk64 calls
// SymFunctionTableAccess64 / SymGetModuleBase64, which take the dbghelp lock and can
// touch the loader. If the SUSPENDED thread holds that lock, StackWalk64 blocks forever,
// the ResumeThread that follows it is never reached, and the GUI thread stays suspended
// for the life of the process. The diagnostic would then have converted a possibly
// recoverable hang into a guaranteed hard freeze — and it would do so precisely when the
// GUI thread hung inside a loader/symbol path, which is not far-fetched for the bug being
// chased. A diagnostic must never leave the app worse off than the fault it observes.
//
// So the suspend/walk/resume sequence runs on a dedicated short-lived worker thread and
// the caller waits on it with a bounded timeout (kStackWalkTimeoutMs). On timeout the
// caller force-resumes the target itself, abandons the stuck worker, and permanently
// disables further captures for the session (a timeout means dbghelp is unsafe on this
// machine and the condition will recur). One leaked thread and one leaked handle in an
// already-failing process is an acceptable price; a permanently suspended GUI thread is
// not. The two resume paths are mutually exclusive via an atomic claim flag.
//
// Symbols: user machines ship without PDBs, so SymFromAddr usually fails. That is fine
// and still useful: every frame also reports `module` + `offset`, which resolves offline
// against the matching PDB for that build.
namespace miacode::diag {

// Maximum number of return addresses collected per capture. Bounded so the suspension
// window stays short and a runaway/corrupt stack cannot spin StackWalk64 forever.
inline constexpr int kMaxCapturedStackFrames = 64;

// How long the caller waits for the worker before force-resuming the target. Generous
// relative to a healthy capture (sub-millisecond) so a merely slow machine is not
// mistaken for a deadlock, but short enough that the GUI thread is never held for long.
inline constexpr int kStackWalkTimeoutMs = 2000;

struct StackCaptureResult {
    bool supported = false;      // false on non-Windows builds
    bool captured = false;       // true when at least one frame was walked
    bool timedOut = false;       // worker exceeded kStackWalkTimeoutMs; target force-resumed
    int frameCount = 0;
    qint64 suspendedUs = 0;      // how long the target thread was actually suspended
    quint32 lastErrorCode = 0;   // GetLastError() from the failing Win32 call, else 0
    QString skipReason;          // populated whenever captured == false
    QStringList frames;          // one formatted line per frame (see formatStackFrameLine)
};

// Register the CALLING thread as the capture target. Duplicates a real thread handle
// (GetCurrentThread() is a pseudo-handle that always means "the calling thread", so it
// cannot be handed to another thread). Call once, from the thread to be diagnosed.
// Returns false on non-Windows or when DuplicateHandle fails.
bool registerStackWalkTargetThread(QString* outReason = nullptr);

// Release the duplicated handle. Safe to call when nothing was registered.
void releaseStackWalkTargetThread();

// Outcome of the one-time SymInitialize. Reported so `symbol=(nosym)` on a frame stays
// attributable: with ready=true it means the documented, expected "user machine has no
// PDB"; with ready=false it means the symbol handler never came up at all and NO frame in
// this process could ever have been named. Those are different bug reports.
struct SymbolHandlerStatus {
    bool attempted = false;      // false off Windows, where there is no dbghelp to init
    bool ready = false;          // SymInitialize succeeded (by either route below)
    quint32 lastErrorCode = 0;   // GetLastError() from the attempt that decided `ready`
    // Which route brought the handler up. `SymInitialize(fInvadeProcess=TRUE)` enumerates
    // every loaded module up front and fails as a unit; when it does, the handler is
    // retried with FALSE and the module list populated separately. Both facts are reported
    // because they mean different things to a reader: invaded=0 with ready=1 is a working
    // handler that took the fallback, whereas ready=0 is no symbols at all.
    bool invadedProcess = false;
    // GetLastError() from the failed fInvadeProcess=TRUE attempt, 0 if it succeeded. Kept
    // separate from `lastErrorCode` so a successful fallback still records what the
    // preferred route hit — that code is the only evidence of why the fallback was needed.
    quint32 invadeErrorCode = 0;
};

// Initialise dbghelp's symbol handler up front, and report whether it came up. Idempotent
// (later calls return the first attempt's outcome); off Windows it is a no-op that reports
// attempted=false, ready=false.
//
// Call this ONCE from the capturing (watchdog) thread, while nothing is hung, and not
// before the event loop is up. SymInitialize enumerates loaded modules and takes the
// loader lock, which pulls in two directions: called first during a hang, while the GUI
// thread holds that lock, the watchdog would block before it ever reached SuspendThread
// and the hang would go unreported; called at the very start of the watchdog thread it
// contends with the GUI thread still loading Qt plugins, and blocks through the startup
// window it exists to watch. The caller therefore waits for the first GUI heartbeat --
// proof the event loop runs and plugin loading is done -- and calls it then, without
// suspending its own monitoring in the meantime. captureRegisteredThreadStack still
// calls it lazily as a fallback, always before opening the suspension window.
SymbolHandlerStatus prepareStackWalkSymbols();

// The current symbol-handler outcome without forcing an attempt: reports
// attempted=false when prepareStackWalkSymbols() has not run yet. Query this rather than
// caching a copy — a caller that snapshots at startup would keep reporting the startup
// value even though the lazy fallback inside captureRegisteredThreadStack can be the call
// that actually performs the attempt.
SymbolHandlerStatus stackWalkSymbolStatus();

// Suspend the registered thread, walk it, resume it, then symbolize. MUST NOT be called
// from the target thread itself (self-suspension would deadlock) — that case is detected
// and reported as skipReason=same_thread instead.
//
// The target is ALWAYS resumed: either by the worker on its normal path, or by this
// function after kStackWalkTimeoutMs. A timed-out call returns timedOut=true and
// skipReason="timeout"; callers must then stop capturing for the rest of the session (see
// hang_watchdog::policy::stackCaptureSessionEnabledAfter).
StackCaptureResult captureRegisteredThreadStack(int maxFrames = kMaxCapturedStackFrames);

// Render one frame as a stable, greppable `key=value` payload. Platform-independent and
// pure so the format is covered by a spec on every platform.
// `moduleName` / `symbolName` may be empty; they degrade to `(unknown)` / `(nosym)`.
QString formatStackFrameLine(
    int index,
    quint64 address,
    const QString& moduleName,
    quint64 moduleOffset,
    const QString& symbolName,
    quint64 symbolOffset);

}  // namespace miacode::diag
