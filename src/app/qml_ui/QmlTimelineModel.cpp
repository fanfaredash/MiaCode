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
    if (surface() == nullptr || tabId.trimmed().isEmpty() || tabId == currentTabId()) {
        return;
    }
    surface()->setBottomTabsCurrentTabId(tabId);
}

void QmlTimelineModel::headerNavigate(double second)
{
    if (surface() != nullptr) surface()->navigateToSecond(second);
}

void QmlTimelineModel::wheelNavigate(double second)
{
    if (surface() != nullptr) surface()->wheelNavigateToSecond(second);
}

void QmlTimelineModel::centerNavigate(double second)
{
    if (surface() != nullptr) surface()->centerOnSecond(second);
}

void QmlTimelineModel::dragStarted()
{
    if (surface() != nullptr) surface()->timelineDragStarted();
}

void QmlTimelineModel::dragFinished(double second)
{
    if (surface() != nullptr) surface()->timelineDragFinished(second);
}

void QmlTimelineModel::userInteractionStarted()
{
    if (surface() != nullptr) surface()->timelineUserInteractionStarted();
}

void QmlTimelineModel::surfaceReady()
{
    if (surface() != nullptr) surface()->noteTimelineSurfaceReady();
}

void QmlTimelineModel::followPreviewToggled(bool enabled)
{
    if (surface() != nullptr) surface()->setFollowPreviewEnabled(enabled);
}

}  // namespace miacode::qml_ui
