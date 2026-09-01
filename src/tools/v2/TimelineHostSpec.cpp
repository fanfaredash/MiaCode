// Contract regression for the Timeline half of the Preview/Timeline split.
//
// TimelineHost is intentionally a forwarding seam in this stage: the old
// composite PlaybackCoordinator remains the implementation, while every Timeline
// command receives a generation/revision/sequence identity before forwarding.

#include "app/runtime/timeline/TimelineCommandGate.h"
#include "app/runtime/timeline/TimelineHost.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

class FakeTimelineSurface final : public miacode::v2::TimelineSurface
{
public:
    QObject* timelineStateBridge() const override { return bridge; }
    void noteTimelineSurfaceReady() override { ++readyCount; }
    void navigateToSecond(double second) override
    {
        lastSecond = second;
        ++navigateCount;
    }
    void centerOnSecond(double second) override
    {
        lastSecond = second;
        ++centerCount;
    }
    void wheelNavigateToSecond(double second) override
    {
        lastSecond = second;
        ++wheelCount;
    }
    void timelineDragStarted() override { ++dragStartedCount; }
    void timelineDragFinished(double second) override
    {
        lastSecond = second;
        ++dragFinishedCount;
    }
    void timelineUserInteractionStarted() override { ++interactionCount; }
    void setFollowPreviewEnabled(bool enabled) override
    {
        followEnabled = enabled;
        ++followCount;
    }
    QString bottomTabsCurrentTabId() const override { return currentTab; }
    void setBottomTabsCurrentTabId(const QString& tabId) override
    {
        currentTab = tabId;
        ++tabSetCount;
    }
    bool bottomTabsVisible() const override { return panelVisible; }
    bool timelineTabVisible() const override { return timelineVisible; }
    bool muriTabVisible() const override { return muriVisible; }
    bool validationTabVisible() const override { return validationVisible; }
    bool ignoreMuriIssuePrompts() const override { return ignorePrompts; }

    QObject* bridge = nullptr;
    double lastSecond = 0.0;
    QString currentTab = QStringLiteral("timeline");
    bool followEnabled = false;
    bool panelVisible = true;
    bool timelineVisible = true;
    bool muriVisible = false;
    bool validationVisible = true;
    bool ignorePrompts = false;
    int readyCount = 0;
    int navigateCount = 0;
    int centerCount = 0;
    int wheelCount = 0;
    int dragStartedCount = 0;
    int dragFinishedCount = 0;
    int interactionCount = 0;
    int followCount = 0;
    int tabSetCount = 0;
};

bool verifyGateRejectsStaleOrder(QTextStream& err)
{
    miacode::runtime::TimelineCommandGate gate(3);
    const auto first = gate.issue();
    const auto second = gate.issue();
    bool ok = require(gate.accept(second) && !gate.accept(first),
                      QStringLiteral("a newer command sequence supersedes an older command"), err);

    gate.setDocumentRevision(8);
    ok &= require(!gate.accept(second),
                  QStringLiteral("a document revision change rejects the previous revision"), err);
    const auto newRevision = gate.issue();
    ok &= require(newRevision.sessionGeneration == 3
                      && newRevision.documentRevision == 8
                      && gate.accept(newRevision),
                  QStringLiteral("a new revision receives the current identity"), err);

    gate.invalidateSession();
    ok &= require(!gate.accept(newRevision),
                  QStringLiteral("session invalidation rejects the previous generation"), err);
    const auto newSession = gate.issue();
    ok &= require(newSession.sessionGeneration == 4 && gate.accept(newSession),
                  QStringLiteral("the post-invalidation command starts a fresh generation"), err);
    return ok;
}

bool verifyTimelineHostForwardsAndWithdraws(QTextStream& err)
{
    FakeTimelineSurface legacy;
    miacode::runtime::TimelineHost host(legacy, 11);
    host.setDocumentRevision(21);

    host.noteTimelineSurfaceReady();
    host.navigateToSecond(1.25);
    host.centerOnSecond(2.5);
    host.wheelNavigateToSecond(3.75);
    host.timelineDragStarted();
    host.timelineDragFinished(4.0);
    host.timelineUserInteractionStarted();
    host.setFollowPreviewEnabled(true);
    host.setBottomTabsCurrentTabId(QStringLiteral("muri"));

    bool ok = require(legacy.readyCount == 1 && legacy.navigateCount == 1
                          && legacy.centerCount == 1 && legacy.wheelCount == 1
                          && legacy.dragStartedCount == 1 && legacy.dragFinishedCount == 1
                          && legacy.interactionCount == 1 && legacy.followCount == 1
                          && legacy.tabSetCount == 1 && qFuzzyCompare(legacy.lastSecond, 4.0),
                      QStringLiteral("timeline projection commands forward through the host seam"), err);
    ok &= require(host.timelineStateBridge() == nullptr
                      && host.bottomTabsCurrentTabId() == QStringLiteral("muri")
                      && host.bottomTabsVisible() && host.timelineTabVisible()
                      && !host.muriTabVisible() && host.validationTabVisible(),
                  QStringLiteral("timeline read projections remain owned by the wrapped surface"), err);

    const auto first = host.issueCommandStamp();
    const auto second = host.issueCommandStamp();
    const int navigateCountBeforeOutOfOrder = legacy.navigateCount;
    host.navigateToSecond(second, 6.0);
    host.navigateToSecond(first, 7.0);
    ok &= require(legacy.navigateCount == navigateCountBeforeOutOfOrder + 1
                      && qFuzzyCompare(legacy.lastSecond, 6.0),
                  QStringLiteral("an older out-of-order Timeline command is dropped"), err);

    const auto staleRevision = host.issueCommandStamp();
    host.setDocumentRevision(22);
    const int navigateCountBeforeRevisionDrop = legacy.navigateCount;
    host.navigateToSecond(staleRevision, 8.0);
    const auto current = host.issueCommandStamp();
    ok &= require(host.acceptsCommand(current),
                  QStringLiteral("the host exposes an accepted current command stamp"), err);
    host.navigateToSecond(current, 9.0);
    ok &= require(legacy.navigateCount == navigateCountBeforeRevisionDrop + 1
                      && qFuzzyCompare(legacy.lastSecond, 9.0),
                  QStringLiteral("a revision change drops a command captured by the old revision"), err);
    host.invalidateSession();
    const int readyCountBeforeWithdrawnCommand = legacy.readyCount;
    host.noteTimelineSurfaceReady();
    host.navigateToSecond(current, 10.0);
    ok &= require(legacy.readyCount == readyCountBeforeWithdrawnCommand
                      && qFuzzyCompare(legacy.lastSecond, 9.0)
                      && !host.acceptsCommand(current),
                  QStringLiteral("an invalidated host drops commands and stale stamps"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;
    ok &= verifyGateRejectsStaleOrder(err);
    ok &= verifyTimelineHostForwardsAndWithdraws(err);
    if (ok) {
        QTextStream(stdout) << "timeline_host_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
