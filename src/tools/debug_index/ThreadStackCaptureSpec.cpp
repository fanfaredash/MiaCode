// Spec for the hang watchdog's GUI-thread self stack walk.
//
// The Win32 suspend/StackWalk64 path cannot run on the CI/dev machine (macOS), so this
// spec pins the parts that ARE portable and that a Windows regression would break first:
//   * the frame line format an operator greps and an offline PDB resolver parses,
//   * the non-Windows contract (report unsupported, never pretend to have captured),
//   * the capture budget policy that keeps a long freeze from flooding the log.
// The Windows-only behaviour is guarded by #if defined(Q_OS_WIN) in ThreadStackCapture.cpp
// and is verified by compilation plus the assertions below that hold on both platforms.

#include <QString>
#include <QTextStream>

#include "common/ThreadStackCapture.h"
#include "common/UiHangWatchdogPolicy.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

}  // namespace

int main()
{
    namespace diag = miacode::diag;
    namespace policy = miacode::hang_watchdog::policy;
    using policy::Trigger;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    // ---- frame line format ----------------------------------------------------
    const QString resolved = diag::formatStackFrameLine(
        3, 0x7ff6abcd1234ull, QStringLiteral("MiaCode.exe"), 0x1234ull,
        QStringLiteral("QCoreApplication::exec"), 0x2aull);
    ok &= require(resolved.contains(QStringLiteral("frame=03")),
                  QStringLiteral("frame index is zero-padded for stable sorting"), err);
    ok &= require(resolved.contains(QStringLiteral("addr=0x7ff6abcd1234")),
                  QStringLiteral("frame reports the raw return address in hex"), err);
    ok &= require(resolved.contains(QStringLiteral("module=MiaCode.exe"))
                      && resolved.contains(QStringLiteral("module_offset=0x1234")),
                  QStringLiteral("frame reports module + offset for offline PDB resolution"), err);
    ok &= require(resolved.contains(QStringLiteral("symbol=QCoreApplication::exec"))
                      && resolved.contains(QStringLiteral("symbol_offset=0x2a")),
                  QStringLiteral("frame reports symbol + displacement when available"), err);

    // Users run without PDBs: an unresolved frame must still be emitted, and must still
    // carry module+offset. Empty names degrade to explicit tokens rather than blanks,
    // which would otherwise produce `module= module_offset=` and break key=value parsing.
    const QString unresolved =
        diag::formatStackFrameLine(0, 0x140001000ull, QString(), 0x1000ull, QString(), 0);
    ok &= require(unresolved.contains(QStringLiteral("symbol=(nosym)")),
                  QStringLiteral("missing symbol renders as (nosym)"), err);
    ok &= require(unresolved.contains(QStringLiteral("module=(unknown)")),
                  QStringLiteral("missing module renders as (unknown)"), err);
    ok &= require(!unresolved.contains(QStringLiteral("module= ")),
                  QStringLiteral("no empty key=value pair is emitted"), err);

    ok &= require(diag::kMaxCapturedStackFrames > 0 && diag::kMaxCapturedStackFrames <= 256,
                  QStringLiteral("frame count is bounded"), err);

    // ---- platform contract ----------------------------------------------------
    const diag::StackCaptureResult result = diag::captureRegisteredThreadStack();
#if defined(Q_OS_WIN)
    // Nothing registered on this thread in the spec process, so the capture must decline
    // rather than walk an arbitrary thread.
    ok &= require(result.supported, QStringLiteral("windows build reports stack walk supported"), err);
    ok &= require(!result.captured && result.skipReason == QStringLiteral("not_registered"),
                  QStringLiteral("unregistered capture declines with a reason"), err);
#else
    ok &= require(!result.supported && !result.captured,
                  QStringLiteral("non-windows capture reports unsupported"), err);
    ok &= require(result.skipReason == QStringLiteral("unsupported_platform"),
                  QStringLiteral("non-windows capture states why"), err);
    ok &= require(!diag::registerStackWalkTargetThread(nullptr),
                  QStringLiteral("non-windows registration fails cleanly"), err);
    ok &= require(!diag::hasStackWalkTargetThread(),
                  QStringLiteral("non-windows registration holds no handle"), err);
    diag::releaseStackWalkTargetThread();  // must be safe with nothing registered
#endif

    ok &= require(!result.timedOut,
                  QStringLiteral("a declined capture is not reported as a timeout"), err);

    // ---- symbol handler status -------------------------------------------------
    // symbol=(nosym) on a frame has two very different causes — the machine has no PDB
    // (normal, documented) or SymInitialize never came up (a real fault) — and the frame
    // itself cannot tell them apart. This status is the only thing that can, so its
    // contract has to hold: never claim ready without having attempted, and never report
    // an error code alongside success.
    const diag::SymbolHandlerStatus symbols = diag::prepareStackWalkSymbols();
    ok &= require(!symbols.ready || symbols.attempted,
                  QStringLiteral("symbol handler is never ready without an attempt"), err);
    ok &= require(!symbols.ready || symbols.lastErrorCode == 0,
                  QStringLiteral("a ready symbol handler reports no error code"), err);
    const diag::SymbolHandlerStatus queried = diag::stackWalkSymbolStatus();
    ok &= require(queried.attempted == symbols.attempted && queried.ready == symbols.ready
                      && queried.lastErrorCode == symbols.lastErrorCode,
                  QStringLiteral("querying the status matches what preparing returned"), err);
#if defined(Q_OS_WIN)
    ok &= require(symbols.attempted,
                  QStringLiteral("windows build attempts to initialise the symbol handler"), err);
#else
    // Off Windows there is no dbghelp to initialise, which must read as "never tried"
    // rather than as a failed attempt.
    ok &= require(!symbols.attempted && !symbols.ready && symbols.lastErrorCode == 0,
                  QStringLiteral("non-windows symbol preparation reports no attempt"), err);
#endif

    // ---- capture budget --------------------------------------------------------
    ok &= require(!policy::shouldCaptureStack(true, Trigger::None, 100000, 0, 0, 30000, 16),
                  QStringLiteral("no trigger never captures"), err);
    ok &= require(policy::shouldCaptureStack(true, Trigger::IdleHeartbeat, 100000, 0, 0, 30000, 16),
                  QStringLiteral("first hang captures immediately"), err);
    ok &= require(
        !policy::shouldCaptureStack(true, Trigger::IdleHeartbeat, 105000, 100000, 1, 30000, 16),
        QStringLiteral("repeat report inside the interval does not re-capture"), err);
    ok &= require(
        policy::shouldCaptureStack(true, Trigger::IdleHeartbeat, 130000, 100000, 1, 30000, 16),
        QStringLiteral("capture repeats once the interval elapses"), err);
    ok &= require(
        !policy::shouldCaptureStack(true, Trigger::IdleHeartbeat, 900000, 100000, 16, 30000, 16),
        QStringLiteral("session capture budget is enforced"), err);
    ok &= require(
        policy::shouldCaptureStack(true, Trigger::ActivePhase, 900000, 100000, 16, 30000, 0),
        QStringLiteral("maxCaptures=0 means unlimited"), err);
    // A disabled session vetoes everything, including an otherwise-eligible first capture.
    ok &= require(
        !policy::shouldCaptureStack(false, Trigger::IdleHeartbeat, 100000, 0, 0, 30000, 16),
        QStringLiteral("a disabled session never captures"), err);

    // ---- timeout / session disable ---------------------------------------------
    // The safety property behind this: a timed-out capture means the watchdog had to
    // force-resume the GUI thread out of the suspension window because the stack-walk
    // worker wedged. That reflects a standing dbghelp/loader condition on the machine,
    // not a blip, so no further capture may ever be attempted this session — another one
    // would risk leaving the GUI thread suspended for good.
    using policy::StackCaptureOutcome;
    ok &= require(policy::stackCaptureSessionEnabledAfter(true, StackCaptureOutcome::Captured),
                  QStringLiteral("a successful capture keeps the session enabled"), err);
    ok &= require(policy::stackCaptureSessionEnabledAfter(true, StackCaptureOutcome::Failed),
                  QStringLiteral("an ordinary failure keeps the session enabled"), err);
    ok &= require(!policy::stackCaptureSessionEnabledAfter(true, StackCaptureOutcome::TimedOut),
                  QStringLiteral("a timeout disables capture for the session"), err);
    // Disabling is permanent: nothing may silently re-enable it.
    ok &= require(!policy::stackCaptureSessionEnabledAfter(false, StackCaptureOutcome::Captured),
                  QStringLiteral("a disabled session is never re-enabled"), err);
    ok &= require(!policy::stackCaptureSessionEnabledAfter(false, StackCaptureOutcome::Failed),
                  QStringLiteral("a disabled session stays disabled after a failure"), err);

    // Outcome tokens are what an operator greps (`capture=timeout`), so pin them.
    ok &= require(QLatin1String(policy::stackCaptureOutcomeName(StackCaptureOutcome::Captured))
                      == QLatin1String("captured"),
                  QStringLiteral("captured outcome token"), err);
    ok &= require(QLatin1String(policy::stackCaptureOutcomeName(StackCaptureOutcome::TimedOut))
                      == QLatin1String("timeout"),
                  QStringLiteral("timeout outcome token"), err);
    ok &= require(QLatin1String(policy::stackCaptureOutcomeName(StackCaptureOutcome::Failed))
                      == QLatin1String("failed"),
                  QStringLiteral("failed outcome token"), err);

    ok &= require(diag::kStackWalkTimeoutMs > 0 && diag::kStackWalkTimeoutMs <= 5000,
                  QStringLiteral("suspension window is bounded by a sane timeout"), err);

    if (ok) {
        out << "Thread stack capture spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
