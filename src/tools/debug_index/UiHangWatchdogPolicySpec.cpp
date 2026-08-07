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

    // Tokens are greppable contract, same as the stack-capture decisions above.
    ok &= require(QLatin1String(policy::stallTransitionName(StallTransition::Began))
                      == QLatin1String("began"),
                  QStringLiteral("stall began token"), err);
    ok &= require(QLatin1String(policy::stallTransitionName(StallTransition::Ended))
                      == QLatin1String("ended"),
                  QStringLiteral("stall ended token"), err);
    ok &= require(QLatin1String(policy::stallTransitionName(StallTransition::None))
                      == QLatin1String("none"),
                  QStringLiteral("stall none token"), err);

    // ---- stack capture decision -------------------------------------------------
    // A hang report that arrives without a stack must say why. These five decisions are
    // the complete set of answers, and each maps to a token an operator greps for in the
    // `stack=` field of `action=gui_thread_stale`.
    using policy::StackCaptureDecision;
    ok &= require(policy::classifyStackCapture(true, Trigger::IdleHeartbeat, 100000, 0, 0, 30000, 16)
                      == StackCaptureDecision::Capture,
                  QStringLiteral("first hang is classified as a capture"), err);
    ok &= require(policy::classifyStackCapture(false, Trigger::IdleHeartbeat, 100000, 0, 0, 30000, 16)
                      == StackCaptureDecision::SkipDisabled,
                  QStringLiteral("a session disabled by a timeout is classified as such"), err);
    ok &= require(policy::classifyStackCapture(true, Trigger::None, 100000, 0, 0, 30000, 16)
                      == StackCaptureDecision::SkipNoTrigger,
                  QStringLiteral("no trigger is classified as no trigger"), err);
    ok &= require(
        policy::classifyStackCapture(true, Trigger::IdleHeartbeat, 900000, 100000, 16, 30000, 16)
            == StackCaptureDecision::SkipBudget,
        QStringLiteral("an exhausted session budget is classified as budget"), err);
    ok &= require(
        policy::classifyStackCapture(true, Trigger::IdleHeartbeat, 105000, 100000, 1, 30000, 16)
            == StackCaptureDecision::SkipInterval,
        QStringLiteral("a repeat inside the interval is classified as interval"), err);
    ok &= require(
        policy::classifyStackCapture(true, Trigger::IdleHeartbeat, 130000, 100000, 1, 30000, 16)
            == StackCaptureDecision::Capture,
        QStringLiteral("an elapsed interval is classified as a capture"), err);

    // Veto order is reported verbatim, so it is contract: disabled outranks budget
    // outranks interval. "We stopped capturing entirely" is the fact worth surfacing.
    ok &= require(
        policy::classifyStackCapture(false, Trigger::IdleHeartbeat, 105000, 100000, 16, 30000, 16)
            == StackCaptureDecision::SkipDisabled,
        QStringLiteral("disabled outranks budget and interval"), err);
    ok &= require(
        policy::classifyStackCapture(true, Trigger::IdleHeartbeat, 105000, 100000, 16, 30000, 16)
            == StackCaptureDecision::SkipBudget,
        QStringLiteral("budget outranks interval"), err);

    // The classifier is the only decision point; the boolean gate must never disagree.
    struct StackCaptureCase {
        bool sessionEnabled;
        Trigger trigger;
        qint64 nowMs;
        qint64 lastCaptureAtMs;
        int capturesSoFar;
        qint64 minIntervalMs;
        int maxCaptures;
    };
    const StackCaptureCase cases[] = {
        {true, Trigger::IdleHeartbeat, 100000, 0, 0, 30000, 16},
        {false, Trigger::IdleHeartbeat, 100000, 0, 0, 30000, 16},
        {true, Trigger::None, 100000, 0, 0, 30000, 16},
        {true, Trigger::IdleHeartbeat, 900000, 100000, 16, 30000, 16},
        {true, Trigger::IdleHeartbeat, 105000, 100000, 1, 30000, 16},
        {true, Trigger::IdleHeartbeat, 130000, 100000, 1, 30000, 16},
        {true, Trigger::ActivePhase, 900000, 100000, 16, 30000, 0},
        {false, Trigger::None, 0, 0, 0, 30000, 16},
    };
    for (const StackCaptureCase& item : cases) {
        const bool gate = policy::shouldCaptureStack(
            item.sessionEnabled, item.trigger, item.nowMs, item.lastCaptureAtMs,
            item.capturesSoFar, item.minIntervalMs, item.maxCaptures);
        const bool classified = policy::classifyStackCapture(
                                    item.sessionEnabled, item.trigger, item.nowMs,
                                    item.lastCaptureAtMs, item.capturesSoFar,
                                    item.minIntervalMs, item.maxCaptures)
            == StackCaptureDecision::Capture;
        ok &= require(gate == classified,
                      QStringLiteral("shouldCaptureStack agrees with the classifier"), err);
    }

    // Tokens are what an operator greps (`stack=skipped_budget`), so pin them.
    ok &= require(QLatin1String(policy::stackCaptureDecisionName(StackCaptureDecision::Capture))
                      == QLatin1String("capture"),
                  QStringLiteral("capture decision token"), err);
    ok &= require(QLatin1String(policy::stackCaptureDecisionName(StackCaptureDecision::SkipDisabled))
                      == QLatin1String("skipped_disabled"),
                  QStringLiteral("disabled decision token"), err);
    ok &= require(QLatin1String(policy::stackCaptureDecisionName(StackCaptureDecision::SkipBudget))
                      == QLatin1String("skipped_budget"),
                  QStringLiteral("budget decision token"), err);
    ok &= require(QLatin1String(policy::stackCaptureDecisionName(StackCaptureDecision::SkipInterval))
                      == QLatin1String("skipped_interval"),
                  QStringLiteral("interval decision token"), err);
    ok &= require(
        QLatin1String(policy::stackCaptureDecisionName(StackCaptureDecision::SkipNoTrigger))
            == QLatin1String("skipped_no_trigger"),
        QStringLiteral("no-trigger decision token"), err);

    if (ok) {
        out << "UI hang watchdog policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
