#include "TimelineHost.h"

namespace miacode::runtime {

TimelineHost::TimelineHost(miacode::v2::TimelineSurface& legacySurface,
                           quint64 sessionGeneration)
    : legacySurface_(&legacySurface)
    , commandGate_(sessionGeneration)
{
}

void TimelineHost::setDocumentRevision(quint64 revision)
{
    commandGate_.setDocumentRevision(revision);
}

void TimelineHost::invalidateSession()
{
    commandGate_.invalidateSession();
    legacySurface_ = nullptr;
}

TimelineCommandStamp TimelineHost::issueCommandStamp()
{
    return commandGate_.issue();
}

bool TimelineHost::acceptsCommand(const TimelineCommandStamp& stamp) const
{
    return legacySurface_ != nullptr && commandGate_.accepts(stamp);
}

bool TimelineHost::acceptCommand(const TimelineCommandStamp& stamp)
{
    return legacySurface_ != nullptr && commandGate_.accept(stamp);
}

QObject* TimelineHost::timelineStateBridge() const
{
    return legacySurface_ != nullptr ? legacySurface_->timelineStateBridge() : nullptr;
}

void TimelineHost::noteTimelineSurfaceReady()
{
    noteTimelineSurfaceReady(issueCommandStamp());
}

void TimelineHost::noteTimelineSurfaceReady(const TimelineCommandStamp& stamp)
{
    if (acceptCommand(stamp)) {
        legacySurface_->noteTimelineSurfaceReady();
    }
}

void TimelineHost::navigateToSecond(double second)
{
    navigateToSecond(issueCommandStamp(), second);
}

void TimelineHost::navigateToSecond(const TimelineCommandStamp& stamp, double second)
{
    if (acceptCommand(stamp)) {
        legacySurface_->navigateToSecond(second);
    }
}

void TimelineHost::centerOnSecond(double second)
{
    centerOnSecond(issueCommandStamp(), second);
}

void TimelineHost::centerOnSecond(const TimelineCommandStamp& stamp, double second)
{
    if (acceptCommand(stamp)) {
        legacySurface_->centerOnSecond(second);
    }
}

void TimelineHost::wheelNavigateToSecond(double second)
{
    wheelNavigateToSecond(issueCommandStamp(), second);
}

void TimelineHost::wheelNavigateToSecond(const TimelineCommandStamp& stamp, double second)
{
    if (acceptCommand(stamp)) {
        legacySurface_->wheelNavigateToSecond(second);
    }
}

void TimelineHost::timelineDragStarted()
{
    timelineDragStarted(issueCommandStamp());
}

void TimelineHost::timelineDragStarted(const TimelineCommandStamp& stamp)
{
    if (acceptCommand(stamp)) {
        legacySurface_->timelineDragStarted();
    }
}

void TimelineHost::timelineDragFinished(double second)
{
    timelineDragFinished(issueCommandStamp(), second);
}

void TimelineHost::timelineDragFinished(const TimelineCommandStamp& stamp, double second)
{
    if (acceptCommand(stamp)) {
        legacySurface_->timelineDragFinished(second);
    }
}

void TimelineHost::timelineUserInteractionStarted()
{
    timelineUserInteractionStarted(issueCommandStamp());
}

void TimelineHost::timelineUserInteractionStarted(const TimelineCommandStamp& stamp)
{
    if (acceptCommand(stamp)) {
        legacySurface_->timelineUserInteractionStarted();
    }
}

void TimelineHost::setFollowPreviewEnabled(bool enabled)
{
    setFollowPreviewEnabled(issueCommandStamp(), enabled);
}

void TimelineHost::setFollowPreviewEnabled(const TimelineCommandStamp& stamp, bool enabled)
{
    if (acceptCommand(stamp)) {
        legacySurface_->setFollowPreviewEnabled(enabled);
    }
}

QString TimelineHost::bottomTabsCurrentTabId() const
{
    return legacySurface_ != nullptr ? legacySurface_->bottomTabsCurrentTabId() : QString();
}

void TimelineHost::setBottomTabsCurrentTabId(const QString& tabId)
{
    setBottomTabsCurrentTabId(issueCommandStamp(), tabId);
}

void TimelineHost::setBottomTabsCurrentTabId(const TimelineCommandStamp& stamp,
                                             const QString& tabId)
{
    if (acceptCommand(stamp)) {
        legacySurface_->setBottomTabsCurrentTabId(tabId);
    }
}

bool TimelineHost::bottomTabsVisible() const
{
    return legacySurface_ != nullptr && legacySurface_->bottomTabsVisible();
}

bool TimelineHost::timelineTabVisible() const
{
    return legacySurface_ != nullptr && legacySurface_->timelineTabVisible();
}

bool TimelineHost::muriTabVisible() const
{
    return legacySurface_ != nullptr && legacySurface_->muriTabVisible();
}

bool TimelineHost::validationTabVisible() const
{
    return legacySurface_ != nullptr && legacySurface_->validationTabVisible();
}

bool TimelineHost::ignoreMuriIssuePrompts() const
{
    return legacySurface_ != nullptr && legacySurface_->ignoreMuriIssuePrompts();
}

}  // namespace miacode::runtime
