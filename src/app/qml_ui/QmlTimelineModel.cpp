#include "QmlTimelineModel.h"

#include "mainwindow/MainWindow.h"
#include "ui/UiText.h"

namespace miacode::qml_ui {

QmlTimelineModel::QmlTimelineModel(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
{
    // Pushed, not polled: MainWindow says when the panel's presentation
    // changed, and nothing wakes in between.
    connect(backend_, &MainWindow::shellPresentationChanged,
            this, &QmlTimelineModel::tabsChanged);
}

QObject* QmlTimelineModel::stateBridge() const
{
    return backend_ != nullptr ? backend_->shellTimelineStateBridgeObject() : nullptr;
}

QString QmlTimelineModel::currentTabId() const
{
    return backend_ != nullptr ? backend_->shellBottomTabsCurrentTabId() : QString();
}

bool QmlTimelineModel::panelVisible() const
{
    return backend_ != nullptr && backend_->shellBottomTabsVisible();
}

bool QmlTimelineModel::timelineTabVisible() const
{
    return backend_ != nullptr && backend_->shellTimelineTabVisible();
}

bool QmlTimelineModel::validationTabVisible() const
{
    return backend_ != nullptr && backend_->shellValidationTabVisible();
}

bool QmlTimelineModel::muriTabVisible() const
{
    return backend_ != nullptr && backend_->shellMuriTabVisible();
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
    if (backend_ == nullptr || tabId.trimmed().isEmpty() || tabId == currentTabId()) {
        return;
    }
    backend_->setShellBottomTabsCurrentTab(tabId);
}

void QmlTimelineModel::headerNavigate(double second)
{
    if (backend_ != nullptr) backend_->navigateShellTimelineToSecond(second);
}

void QmlTimelineModel::wheelNavigate(double second)
{
    if (backend_ != nullptr) backend_->wheelShellTimelineNavigate(second);
}

void QmlTimelineModel::centerNavigate(double second)
{
    if (backend_ != nullptr) backend_->centerShellTimelineNavigate(second);
}

void QmlTimelineModel::dragStarted()
{
    if (backend_ != nullptr) backend_->shellTimelineDragStarted();
}

void QmlTimelineModel::dragFinished(double second)
{
    if (backend_ != nullptr) backend_->shellTimelineDragFinished(second);
}

void QmlTimelineModel::userInteractionStarted()
{
    if (backend_ != nullptr) backend_->shellTimelineUserInteractionStarted();
}

void QmlTimelineModel::surfaceReady()
{
    if (backend_ != nullptr) backend_->shellTimelineSurfaceReady();
}

void QmlTimelineModel::followPreviewToggled(bool enabled)
{
    if (backend_ != nullptr) backend_->shellTimelineFollowPreviewToggled(enabled);
}

}  // namespace miacode::qml_ui
