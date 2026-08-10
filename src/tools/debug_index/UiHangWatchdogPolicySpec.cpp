#include <QString>
#include <QTextStream>

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
    namespace policy = miacode::hang_watchdog::policy;
    using policy::Trigger;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    policy::SuppressionEpisode suppressionEpisode;
    ok &= require(
        !suppressionEpisode.observe(false, Trigger::IdleHeartbeat).has_value(),
        QStringLiteral("first suppressed report opens an episode"), err);
    ok &= require(
        !suppressionEpisode.observe(false, Trigger::ActivePhase).has_value(),
        QStringLiteral("later suppressed report increments the same episode"), err);
    const auto suppressionSummary = suppressionEpisode.observe(true, Trigger::ActivePhase);
    ok &= require(
        suppressionSummary.has_value()
            && suppressionSummary->episodeId == 1
            && suppressionSummary->suppressedCount == 2
            && suppressionSummary->trigger == Trigger::ActivePhase,
        QStringLiteral("next report flushes its suppression count id and trigger"), err);
    ok &= require(
        !suppressionEpisode.endEpisode().has_value(),
        QStringLiteral("flushed suppression episode closes cleanly"), err);
    suppressionEpisode.observe(false, Trigger::IdleHeartbeat);
    const auto shutdownSummary = suppressionEpisode.endEpisode();
    ok &= require(
        shutdownSummary.has_value()
            && shutdownSummary->episodeId == 2
            && shutdownSummary->suppressedCount == 1
            && shutdownSummary->trigger == Trigger::IdleHeartbeat,
        QStringLiteral("episode end flushes its pending suppression summary"), err);

    ok &= require(policy::classify(false, 0, true, 4999, 2000, 5000) == Trigger::None,
                  QStringLiteral("inactive heartbeat below threshold"), err);
    ok &= require(policy::classify(false, 0, true, 5000, 2000, 5000) == Trigger::IdleHeartbeat,
                  QStringLiteral("inactive stale heartbeat triggers"), err);
    ok &= require(policy::classify(false, 0, false, 60000, 2000, 5000) == Trigger::None,
                  QStringLiteral("idle watchdog stays disarmed before first event-loop heartbeat"), err);
    ok &= require(policy::classify(true, 1999, false, 60000, 2000, 5000) == Trigger::None,
                  QStringLiteral("unarmed heartbeat does not hide a short active phase"), err);
    ok &= require(policy::classify(true, 1999, true, 100, 2000, 5000) == Trigger::None,
                  QStringLiteral("active phase below threshold"), err);
    ok &= require(policy::classify(true, 2000, false, 60000, 2000, 5000) == Trigger::ActivePhase,
                  QStringLiteral("active phase remains available before first heartbeat"), err);
    ok &= require(policy::classify(true, 2000, true, 100, 2000, 5000) == Trigger::ActivePhase,
                  QStringLiteral("active phase threshold triggers"), err);
    ok &= require(policy::classify(true, 5000, true, 5000, 2000, 5000) == Trigger::IdleHeartbeat,
                  QStringLiteral("stale heartbeat has priority over active phase"), err);

    ok &= require(!policy::monitorPauseRequiresRearm(999, 500),
                  QStringLiteral("sub-two-poll scheduling delay keeps current baseline"), err);
    ok &= require(policy::monitorPauseRequiresRearm(1000, 500),
                  QStringLiteral("two missed watchdog polls require rearm"), err);
    ok &= require(policy::monitorPauseRequiresRearm(4900, 500),
                  QStringLiteral("sub-idle-timeout process pause still requires rearm"), err);
    ok &= require(policy::monitorPauseRequiresRearm(120000, 500),
                  QStringLiteral("system sleep interval requires rearm"), err);

    ok &= require(!policy::shouldReport(
                      Trigger::IdleHeartbeat, 3, 10000,
                      Trigger::IdleHeartbeat, 3, 9000, 5000),
                  QStringLiteral("same idle trigger is rate limited"), err);
    ok &= require(policy::shouldReport(
                      Trigger::IdleHeartbeat, 3, 14000,
                      Trigger::IdleHeartbeat, 3, 9000, 5000),
                  QStringLiteral("same idle trigger repeats at interval"), err);
    ok &= require(policy::shouldReport(
                      Trigger::ActivePhase, 4, 10000,
                      Trigger::ActivePhase, 3, 9000, 5000),
                  QStringLiteral("new active phase generation reports immediately"), err);
    ok &= require(policy::shouldReport(
                      Trigger::ActivePhase, 3, 10000,
                      Trigger::IdleHeartbeat, 3, 9000, 5000),
                  QStringLiteral("trigger change reports immediately"), err);
    ok &= require(!policy::shouldReport(
                      Trigger::None, 3, 10000,
                      Trigger::None, 3, 0, 5000),
                  QStringLiteral("no trigger never reports"), err);

    // ---- sub-hang stall episodes -------------------------------------------------
    // The gap this closes: with a 2 s active-phase and a 5 s idle threshold, an unmarked
    // GUI stall between those two values is invisible to classify(). Both stalls in the
    // capture that motivated this (2075 ms and 4683 ms) land in that band, so pin the band
    // explicitly -- a future threshold edit that reopens the hole must fail here.
    using policy::StallTransition;
    ok &= require(policy::classify(false, 0, true, 2075, 2000, 5000) == Trigger::None,
                  QStringLiteral("2075 ms unmarked stall is not a hang"), err);
    ok &= require(policy::classify(false, 0, true, 4683, 2000, 5000) == Trigger::None,
                  QStringLiteral("4683 ms unmarked stall is not a hang"), err);
    ok &= require(
        policy::classifyHeartbeatStall(true, 2075, 1000, false) == StallTransition::Began,
        QStringLiteral("2075 ms unmarked stall opens a stall episode"), err);
    ok &= require(
        policy::classifyHeartbeatStall(true, 4683, 1000, false) == StallTransition::Began,
        QStringLiteral("4683 ms unmarked stall opens a stall episode"), err);

    ok &= require(policy::classifyHeartbeatStall(true, 999, 1000, false) == StallTransition::None,
                  QStringLiteral("below threshold with no episode open reports nothing"), err);
    ok &= require(policy::classifyHeartbeatStall(true, 1000, 1000, false) == StallTransition::Began,
                  QStringLiteral("threshold is inclusive"), err);
    ok &= require(policy::classifyHeartbeatStall(true, 30000, 1000, true) == StallTransition::None,
                  QStringLiteral("an open episode does not re-report while still stalled"), err);
    ok &= require(policy::classifyHeartbeatStall(true, 0, 1000, true) == StallTransition::Ended,
                  QStringLiteral("heartbeat resuming closes the episode"), err);
    ok &= require(policy::classifyHeartbeatStall(true, 999, 1000, true) == StallTransition::Ended,
                  QStringLiteral("dropping below threshold closes the episode"), err);
    // An unarmed heartbeat has no age worth attributing: it must never open an episode,
    // and it closes one left open across a rearm so the flag cannot wedge on.
    ok &= require(
        policy::classifyHeartbeatStall(false, 600000, 1000, false) == StallTransition::None,
        QStringLiteral("unarmed heartbeat never opens an episode"), err);
    ok &= require(
        policy::classifyHeartbeatStall(false, 600000, 1000, true) == StallTransition::Ended,
        QStringLiteral("unarmed heartbeat closes an open episode"), err);

    // Stall transition tokens are a greppable log contract.
    ok &= require(QLatin1String(policy::stallTransitionName(StallTransition::Began))
                      == QLatin1String("began"),
                  QStringLiteral("stall began token"), err);
    ok &= require(QLatin1String(policy::stallTransitionName(StallTransition::Ended))
                      == QLatin1String("ended"),
                  QStringLiteral("stall ended token"), err);
    ok &= require(QLatin1String(policy::stallTransitionName(StallTransition::None))
                      == QLatin1String("none"),
                  QStringLiteral("stall none token"), err);

    if (ok) {
        out << "UI hang watchdog policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
