#include "QmlTimelineModel.h"

#include "ui/UiText.h"

namespace miacode::qml_ui {

QmlTimelineModel::QmlTimelineModel(miacode::v2::ShellNotifications& notifications,
                                   miacode::v2::TimelineSurface*& surfaceSlot,
                                   QObject* parent)
    : QObject(parent)
    , notifications_(&notifications)
    , surfaceSlot_(&surfaceSlot)
{
    // Pushed, not polled: MainWindow says when the panel's presentation
    // changed, and nothing wakes in between.
    connect(notifications_, &miacode::v2::ShellNotifications::presentationChanged,
            this, &QmlTimelineModel::tabsChanged);
}

QObject* QmlTimelineModel::stateBridge() const
{
    return surface() != nullptr ? surface()->timelineStateBridge() : nullptr;
}

QString QmlTimelineModel::currentTabId() const
{
    return surface() != nullptr ? surface()->bottomTabsCurrentTabId() : QString();
}

bool QmlTimelineModel::panelVisible() const
{
    return surface() != nullptr && surface()->bottomTabsVisible();
}

bool QmlTimelineModel::timelineTabVisible() const
{
    return surface() != nullptr && surface()->timelineTabVisible();
}

bool QmlTimelineModel::validationTabVisible() const
{
    return surface() != nullptr && surface()->validationTabVisible();
}

bool QmlTimelineModel::muriTabVisible() const
{
    return surface() != nullptr && surface()->muriTabVisible();
}

QString QmlTimelineModel::timelineTabLabel() const
{
    return UiText::text(QStringLiteral("window.timeline"));
}

QString QmlTimelineModel::validationTabLabel() const
{
    return UiText::text(QStringLiteral("window.syntax"));
}

QString QmlTimelineModel::muriTabLabel() const
{
    return UiText::text(QStringLiteral("window.muri"));
}

QString QmlTimelineModel::followCodeLabel() const
{
    return UiText::text(QStringLiteral("shell.follow_code"));
}

void QmlTimelineModel::setCurrentTabId(const QString& tabId)
{
    miacode::v2::TimelineSurface* current = surface();
    if (current == nullptr || tabId.trimmed().isEmpty() || tabId == currentTabId()) {
        return;
    }
    current->setBottomTabsCurrentTabId(current->issueCommandStamp(), tabId);
}

void QmlTimelineModel::headerNavigate(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->navigateToSecond(current->issueCommandStamp(), second);
    }
}

void QmlTimelineModel::wheelNavigate(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->wheelNavigateToSecond(current->issueCommandStamp(), second);
    }
}

void QmlTimelineModel::centerNavigate(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->centerOnSecond(current->issueCommandStamp(), second);
    }
}

void QmlTimelineModel::dragStarted()
{
    if (auto* current = surface(); current != nullptr) {
        current->timelineDragStarted(current->issueCommandStamp());
    }
}

void QmlTimelineModel::dragFinished(double second)
{
    if (auto* current = surface(); current != nullptr) {
        current->timelineDragFinished(current->issueCommandStamp(), second);
    }
}

void QmlTimelineModel::userInteractionStarted()
{
    if (auto* current = surface(); current != nullptr) {
        current->timelineUserInteractionStarted(current->issueCommandStamp());
    }
}

void QmlTimelineModel::surfaceReady()
{
    if (auto* current = surface(); current != nullptr) {
        current->noteTimelineSurfaceReady(current->issueCommandStamp());
    }
}

void QmlTimelineModel::followPreviewToggled(bool enabled)
{
    if (auto* current = surface(); current != nullptr) {
        current->setFollowPreviewEnabled(current->issueCommandStamp(), enabled);
    }
}

}  // namespace miacode::qml_ui
