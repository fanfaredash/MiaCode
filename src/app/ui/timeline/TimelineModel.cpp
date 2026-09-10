#include "timeline/TimelineModel.h"

#include "ui/preferences/LocaleService.h"
#include <QCoreApplication>

namespace miacode::ui {

TimelineModel::TimelineModel(miacode::ShellNotifications& notifications,
                                   miacode::TimelineSurface*& surfaceSlot,
                                   QObject* parent)
    : QObject(parent)
    , notifications_(&notifications)
    , surfaceSlot_(&surfaceSlot)
{
    connect(notifications_, &miacode::ShellNotifications::presentationChanged,
            this, &TimelineModel::tabsChanged);
    connect(&miacode::LocaleService::instance(), &miacode::LocaleService::languageChanged,
            this, [this](const QString&) { emit localeLabelsChanged(); });
}

QObject* TimelineModel::stateBridge() const
{
    return surface() != nullptr ? surface()->timelineStateBridge() : nullptr;
}

QString TimelineModel::currentTabId() const
{
    return surface() != nullptr ? surface()->bottomTabsCurrentTabId() : QString();
}

bool TimelineModel::panelVisible() const
{
    return surface() != nullptr && surface()->bottomTabsVisible();
}

bool TimelineModel::timelineTabVisible() const
{
    return surface() != nullptr && surface()->timelineTabVisible();
}

bool TimelineModel::validationTabVisible() const
{
    return surface() != nullptr && surface()->validationTabVisible();
}

bool TimelineModel::muriTabVisible() const
{
    return surface() != nullptr && surface()->muriTabVisible();
}

QString TimelineModel::timelineTabLabel() const
{
    return qtTrId("window.timeline");
}

QString TimelineModel::validationTabLabel() const
{
    return qtTrId("window.syntax");
}

QString TimelineModel::muriTabLabel() const
{
    return qtTrId("window.muri");
}

QString TimelineModel::followCodeLabel() const
{
    return qtTrId("shell.follow_code");
}

void TimelineModel::setCurrentTabId(const QString& tabId)
{
    miacode::TimelineSurface* current = surface();
    if (current == nullptr || tabId.trimmed().isEmpty() || tabId == currentTabId()) {
        return;
    }
    current->setBottomTabsCurrentTabId(current->issueCommandStamp(), tabId);
}

void TimelineModel::headerNavigate(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->navigateToSecond(current->issueCommandStamp(), second);
    }
}

void TimelineModel::wheelNavigate(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->wheelNavigateToSecond(current->issueCommandStamp(), second);
    }
}

void TimelineModel::centerNavigate(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->centerOnSecond(current->issueCommandStamp(), second);
    }
}

void TimelineModel::dragStarted()
{
    if (auto* current = surface(); current != nullptr) {
        current->timelineDragStarted(current->issueCommandStamp());
    }
}

void TimelineModel::dragFinished(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->timelineDragFinished(current->issueCommandStamp(), second);
    }
}

void TimelineModel::userInteractionStarted()
{
    if (auto* current = surface(); current != nullptr) {
        current->timelineUserInteractionStarted(current->issueCommandStamp());
    }
}

void TimelineModel::surfaceReady()
{
    if (auto* current = surface(); current != nullptr) {
        current->noteTimelineSurfaceReady(current->issueCommandStamp());
    }
}

void TimelineModel::followPreviewToggled(bool enabled)
{
    if (auto* current = surface(); current != nullptr) {
        current->setFollowPreviewEnabled(current->issueCommandStamp(), enabled);
    }
}

}  // namespace miacode::ui
