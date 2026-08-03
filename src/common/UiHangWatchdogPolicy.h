#pragma once

#include <QtGlobal>

namespace miacode::hang_watchdog::policy {

enum class Trigger {
    None,
    ActivePhase,
    IdleHeartbeat,
};

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
