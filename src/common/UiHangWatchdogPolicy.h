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

// ---- sub-hang GUI stall episodes ------------------------------------------------
//
// `classify()` only fires at `idleHeartbeatHangMs` (5 s), or at `activePhaseHangMs` (2 s)
// while an explicitly marked phase is open. Ordinary playback marks no phase, so a GUI
// thread that stops servicing its 250 ms heartbeat for two, three, four seconds produces
// ZERO watchdog rows. That is not hypothetical: a user capture taken to investigate exactly
// this symptom contained a 2.075 s and a 4.683 s GUI stall -- both plainly visible as a
// hole in every other channel, and as a collapse to 10 fps in the render thread's own
// stats -- with not one `ui/hang_watchdog` line between them. Every stall in that capture
// sat in the dead band between 2 s and 5 s.
//
// A stall episode is therefore reported separately from a hang:
//   * it opens as soon as the heartbeat is `stallThresholdMs` stale, so the fact survives
//     a process kill that happens before the GUI thread ever comes back,
//   * it closes when the heartbeat moves again, carrying the measured duration,
//   * it is Info, not Fatal, and never carries a stack.
// The hang path keeps its Fatal rows, its 5 s repeat cadence and its stack budget
// untouched -- this adds an observation, it does not re-tune a threshold.
enum class StallTransition {
    None,   // nothing to report on this poll
    Began,  // the heartbeat just went stale past the threshold
    Ended,  // the GUI thread came back; the episode's duration is now known
};

// Total and edge-triggered: `stallOpen` is the caller's memory of the previous poll, so
// each episode yields exactly one Began and at most one Ended. An unarmed heartbeat counts
// as not stalled -- it carries no age worth attributing -- which also closes an episode
// left open across a rearm. The caller drops the baseline in that case rather than
// reporting a duration measured against a timestamp that no longer means anything.
inline StallTransition classifyHeartbeatStall(
    bool heartbeatArmed,
    qint64 heartbeatAgeMs,
    qint64 stallThresholdMs,
    bool stallOpen)
{
    const bool stalled = heartbeatArmed && heartbeatAgeMs >= stallThresholdMs;
    if (stalled && !stallOpen) {
        return StallTransition::Began;
    }
    if (!stalled && stallOpen) {
        return StallTransition::Ended;
    }
    return StallTransition::None;
}

inline const char* stallTransitionName(StallTransition transition)
{
    switch (transition) {
    case StallTransition::Began:
        return "began";
    case StallTransition::Ended:
        return "ended";
    case StallTransition::None:
    default:
        return "none";
    }
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

// Why a hang report did or did not bring a stack with it. Most stale rows in a long
// freeze legitimately arrive bare — the budget below is deliberately much stricter than
// the 5 s report cadence — but a reader cannot tell "budget spent" from "stack capture
// broke" unless the row says which. Every report therefore carries one of these.
enum class StackCaptureDecision {
    Capture,          // eligible; a stack follows this report
    SkipDisabled,     // a previous capture timed out and switched the session off for good
    SkipBudget,       // maxCaptures already reached for this process
    SkipInterval,     // inside minIntervalMs of the previous capture
    SkipNoTrigger,    // no hang trigger at all (this path emits no report either)
};

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
//
// The veto order is fixed and reported verbatim, so it is part of the contract: a
// permanently disabled session outranks the budget, which outranks the interval. When
// two would apply the earlier one wins, which is what an operator wants — "we stopped
// capturing at all" is the more important fact than "and also the interval had not
// elapsed".
inline StackCaptureDecision classifyStackCapture(
    bool sessionEnabled,
    Trigger trigger,
    qint64 nowMs,
    qint64 lastCaptureAtMs,
    int capturesSoFar,
    qint64 minIntervalMs,
    int maxCaptures)
{
    if (!sessionEnabled) {
        return StackCaptureDecision::SkipDisabled;
    }
    if (trigger == Trigger::None) {
        return StackCaptureDecision::SkipNoTrigger;
    }
    if (maxCaptures > 0 && capturesSoFar >= maxCaptures) {
        return StackCaptureDecision::SkipBudget;
    }
    if (lastCaptureAtMs <= 0) {
        return StackCaptureDecision::Capture;
    }
    return nowMs - lastCaptureAtMs >= minIntervalMs
        ? StackCaptureDecision::Capture
        : StackCaptureDecision::SkipInterval;
}

// The boolean gate, kept as the single decision point by deferring to the classifier —
// the two can only ever disagree if this line is edited to stop calling it.
inline bool shouldCaptureStack(
    bool sessionEnabled,
    Trigger trigger,
    qint64 nowMs,
    qint64 lastCaptureAtMs,
    int capturesSoFar,
    qint64 minIntervalMs,
    int maxCaptures)
{
    return classifyStackCapture(
               sessionEnabled,
               trigger,
               nowMs,
               lastCaptureAtMs,
               capturesSoFar,
               minIntervalMs,
               maxCaptures)
        == StackCaptureDecision::Capture;
}

inline const char* stackCaptureDecisionName(StackCaptureDecision decision)
{
    switch (decision) {
    case StackCaptureDecision::Capture:
        return "capture";
    case StackCaptureDecision::SkipDisabled:
        return "skipped_disabled";
    case StackCaptureDecision::SkipBudget:
        return "skipped_budget";
    case StackCaptureDecision::SkipInterval:
        return "skipped_interval";
    case StackCaptureDecision::SkipNoTrigger:
    default:
        return "skipped_no_trigger";
    }
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
