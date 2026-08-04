#pragma once

#include <QtGlobal>

namespace miacode::hang_watchdog::policy {

enum class Trigger {
    None,
    ActivePhase,
    IdleHeartbeat,
};

inline bool monitorPauseRequiresRearm(qint64 monitorLoopGapMs, qint64 expectedMonitorLoopMs)
{
    return expectedMonitorLoopMs > 0
        && monitorLoopGapMs >= expectedMonitorLoopMs * 2;
}

inline Trigger classify(
    bool phaseActive,
    qint64 activeMs,
    bool heartbeatArmed,
    qint64 heartbeatAgeMs,
    qint64 activePhaseHangMs,
    qint64 idleHeartbeatHangMs)
{
    if (heartbeatArmed && heartbeatAgeMs >= idleHeartbeatHangMs) {
        return Trigger::IdleHeartbeat;
    }
    if (phaseActive && activeMs >= activePhaseHangMs) {
        return Trigger::ActivePhase;
    }
    return Trigger::None;
}

inline bool shouldReport(
    Trigger trigger,
    quint64 phaseGeneration,
    qint64 nowMs,
    Trigger lastTrigger,
    quint64 lastPhaseGeneration,
    qint64 lastReportedAtMs,
    qint64 repeatedReportMs)
{
    if (trigger == Trigger::None) {
        return false;
    }
    if (trigger != lastTrigger || lastReportedAtMs <= 0) {
        return true;
    }
    if (trigger == Trigger::ActivePhase && phaseGeneration != lastPhaseGeneration) {
        return true;
    }
    return nowMs - lastReportedAtMs >= repeatedReportMs;
}

// Gate for the GUI-thread stack capture that accompanies a hang report. The report
// itself repeats every `repeatedReportMs` (5 s) for as long as the stall lasts; a stack
// capture is far more expensive — it suspends the GUI thread and emits up to 64
// fatal-grade lines each time — so it gets its own, stricter budget:
//   * `sessionEnabled` must still be true (a capture that times out switches it off
//     permanently — see stackCaptureSessionEnabledAfter),
//   * at most one capture per `minIntervalMs`, and
//   * at most `maxCaptures` captures per process lifetime.
// A long freeze therefore yields a handful of stacks (enough to tell "wedged forever"
// from "moving slowly") instead of thousands of lines that bury the rest of the log.
// `lastCaptureAtMs <= 0` means "never captured", which always passes the interval test.
inline bool shouldCaptureStack(
    bool sessionEnabled,
    Trigger trigger,
    qint64 nowMs,
    qint64 lastCaptureAtMs,
    int capturesSoFar,
    qint64 minIntervalMs,
    int maxCaptures)
{
    if (!sessionEnabled) {
        return false;
    }
    if (trigger == Trigger::None) {
        return false;
    }
    if (maxCaptures > 0 && capturesSoFar >= maxCaptures) {
        return false;
    }
    if (lastCaptureAtMs <= 0) {
        return true;
    }
    return nowMs - lastCaptureAtMs >= minIntervalMs;
}

enum class StackCaptureOutcome {
    Captured,   // frames were walked and the target resumed normally
    Failed,     // declined or errored, but the target resumed normally
    TimedOut,   // the worker wedged; the watchdog had to force-resume the target
};

inline const char* stackCaptureOutcomeName(StackCaptureOutcome outcome)
{
    switch (outcome) {
    case StackCaptureOutcome::Captured:
        return "captured";
    case StackCaptureOutcome::TimedOut:
        return "timeout";
    case StackCaptureOutcome::Failed:
    default:
        return "failed";
    }
}

// Whether stack capture stays available for the rest of the session after `outcome`.
//
// A timeout means the stack-walk worker blocked inside the suspension window and the
// watchdog had to force-resume the GUI thread to keep from freezing it permanently. That
// reflects this machine's dbghelp/loader state rather than a transient hiccup, so it WILL
// recur, and each recurrence risks another forced resume of a thread we no longer have a
// coherent picture of. Switch captures off for good instead of rolling the dice again.
//
// Ordinary failures (not registered, no frames, a bad thread context) leave the budget
// untouched: those paths resumed the target normally and may well succeed on a later hang.
inline bool stackCaptureSessionEnabledAfter(bool enabledBefore, StackCaptureOutcome outcome)
{
    if (!enabledBefore) {
        return false;
    }
    return outcome != StackCaptureOutcome::TimedOut;
}

inline const char* triggerName(Trigger trigger)
{
    switch (trigger) {
    case Trigger::ActivePhase:
        return "active_phase";
    case Trigger::IdleHeartbeat:
        return "idle_heartbeat";
    case Trigger::None:
    default:
        return "none";
    }
}

}  // namespace miacode::hang_watchdog::policy
