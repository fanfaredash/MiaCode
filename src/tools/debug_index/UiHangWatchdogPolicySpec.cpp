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

    if (ok) {
        out << "UI hang watchdog policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
