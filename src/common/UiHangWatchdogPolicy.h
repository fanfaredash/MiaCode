#pragma once

#include <QtGlobal>

#include <optional>

namespace miacode::hang_watchdog::policy {

enum class Trigger {
    None,
    ActivePhase,
    IdleHeartbeat,
};

struct SuppressedReportSummary {
    quint64 episodeId = 0;
    int suppressedCount = 0;
    Trigger trigger = Trigger::None;
};

// Holds only the state needed to explain repeated will_report=0 polls. The
// emitter decides where to write a returned summary; this policy never logs.
class SuppressionEpisode
{
public:
    std::optional<SuppressedReportSummary> observe(bool willReport, Trigger trigger)
    {
        if (!willReport) {
            if (!episode_.has_value()) {
                episode_ = SuppressedReportSummary{nextEpisodeId_++, 0, trigger};
            }
            ++episode_->suppressedCount;
            episode_->trigger = trigger;
            return std::nullopt;
        }
        return endEpisode();
    }

    std::optional<SuppressedReportSummary> endEpisode()
    {
        if (!episode_.has_value()) {
            return std::nullopt;
        }
        const auto summary = episode_;
        episode_.reset();
        return summary;
    }

private:
    quint64 nextEpisodeId_ = 1;
    std::optional<SuppressedReportSummary> episode_;
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
