#include "QuickShellController.h"

#include "QuickShellNativeSurfaceHost.h"
#include "UiText.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewStageMediaHost.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace {

constexpr int kQuickShellActiveRefreshIntervalMs = 16;
constexpr int kQuickShellIdleRefreshIntervalMs = 250;

template <typename T>
bool assignIfChanged(T& target, const T& value)
{
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

void appendQuickShellControllerLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/controller"),
        text
    );
}

void appendQuickShellLifecycleLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/lifecycle"),
        text
    );
}

}  // namespace

QuickShellController::QuickShellController(
    QuickShellCommandSink* commandSink,
    QuickShellStateSource* stateSource,
    QuickShellNativeSurfaceHost* surfaceHost,
    QObject* parent)
    : QObject(parent)
    , commandSink_(commandSink)
    , stateSource_(stateSource)
    , surfaceHost_(surfaceHost)
    , refreshTimer_(new QTimer(this))
{
    if (stateSource_ != nullptr) {
        if (auto* mediaHost =
                qobject_cast<PreviewStageMediaHost*>(stateSource_->shellPreviewStageMediaHostObject());
            mediaHost != nullptr) {
            connect(mediaHost, &PreviewStageMediaHost::mediaStateChanged, this, [this]() {
                refreshFromStateSource();
            });
        }
    }
    refreshTimer_->setInterval(kQuickShellIdleRefreshIntervalMs);
    connect(refreshTimer_, &QTimer::timeout, this, &QuickShellController::refreshFromStateSource);
    refreshTimer_->start();
    refreshFromStateSource();
}

QString QuickShellController::windowTitle() const
{
    return windowTitle_;
}

bool QuickShellController::workspacePanelsSwapped() const
{
    return workspacePanelsSwapped_;
}

QString QuickShellController::previewSpeedLabel() const
{
    return previewSpeedLabel_;
}

bool QuickShellController::previewPlaying() const
{
    return previewPlaying_;
}

double QuickShellController::previewPositionSeconds() const
{
    return previewPositionSeconds_;
}

double QuickShellController::previewDurationSeconds() const
{
    return previewDurationSeconds_;
}

QStringList QuickShellController::previewStatsTexts() const
{
    return previewStatsTexts_;
}

double QuickShellController::previewCanvasAspectRatio() const
{
    return previewCanvasAspectRatio_;
}

qulonglong QuickShellController::previewPaneRestoreGeneration() const
{
    return previewPaneRestoreGeneration_;
}

double QuickShellController::previewSeekSingleStepSeconds() const
{
    return miacode::preview_interaction::kSeekSingleStepSeconds;
}

bool QuickShellController::previewFullscreen() const
{
    return previewFullscreen_;
}

QObject* QuickShellController::previewRuntime() const
{
    return stateSource_ != nullptr ? stateSource_->shellPreviewRuntimeObject() : nullptr;
}

QObject* QuickShellController::previewStageMediaHost() const
{
    return stateSource_ != nullptr ? stateSource_->shellPreviewStageMediaHostObject() : nullptr;
}

QWindow* QuickShellController::previewCompositeWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().previewCompositeWindow : nullptr;
}

bool QuickShellController::previewUsesSeparateSurface() const
{
    return previewUsesSeparateSurface_;
}

bool QuickShellController::previewDCompExclusive() const
{
    return miacode::debug_options::previewDCompExclusiveEnabled();
}

QObject* QuickShellController::timelineStateBridge() const
{
    return stateSource_ != nullptr ? stateSource_->shellTimelineStateBridgeObject() : nullptr;
}

bool QuickShellController::timelineSurfaceReady() const
{
    return timelineSurfaceReady_;
}

QString QuickShellController::bottomTabsCurrentTabId() const
{
    return bottomTabsCurrentTabId_;
}

int QuickShellController::bottomTabsHostHeight() const
{
    return bottomTabsHostHeight_;
}

double QuickShellController::bottomTabsHeaderScale() const
{
    return bottomTabsHeaderScale_;
}

bool QuickShellController::bottomTabsVisible() const
{
    return bottomTabsVisible_;
}

bool QuickShellController::timelineTabVisible() const
{
    return timelineTabVisible_;
}

bool QuickShellController::validationTabVisible() const
{
    return validationTabVisible_;
}

bool QuickShellController::muriTabVisible() const
{
    return muriTabVisible_;
}

QString QuickShellController::timelineTabLabel() const
{
    // Mirrors MainWindow::bottomTabsFallbackLabel for the legacy QSG
    // path — keeps the QuickShell bottom-tab labels identical to the
    // ones the legacy QTabBar code uses, with the same Chinese /
    // English split.
    return UiText::isChineseUi() ? QStringLiteral("时间轴") : QStringLiteral("Timeline");
}

QString QuickShellController::validationTabLabel() const
{
    return UiText::isChineseUi() ? QStringLiteral("语法检查") : QStringLiteral("Syntax Check");
}

QString QuickShellController::muriTabLabel() const
{
    return UiText::isChineseUi() ? QStringLiteral("无理检查") : QStringLiteral("Muri Check");
}

QWindow* QuickShellController::topChromeWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().topChrome : nullptr;
}

QWindow* QuickShellController::sidebarWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().sidebar : nullptr;
}

QWindow* QuickShellController::workspaceWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().workspace : nullptr;
}

QWindow* QuickShellController::bottomTabsWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().bottomTabs : nullptr;
}

QWindow* QuickShellController::statusWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().status : nullptr;
}

void QuickShellController::setPreviewFullscreen(bool fullscreen)
{
    if (commandSink_ == nullptr || stateSource_ == nullptr) {
        return;
    }
    if (stateSource_->shellPreviewFullscreen() == fullscreen) {
        return;
    }
    commandSink_->setShellPreviewFullscreen(fullscreen);
    refreshFromStateSource();
}

void QuickShellController::refresh()
{
    refreshFromStateSource();
}

void QuickShellController::markNextCloseConfirmedExternally()
{
    closeConfirmedExternally_ = true;
}

void QuickShellController::clearPendingExternalCloseConfirmation()
{
    closeConfirmedExternally_ = false;
}

bool QuickShellController::confirmClose()
{
    QElapsedTimer timer;
    timer.start();
    appendQuickShellLifecycleLog(
        QStringLiteral("controller_confirm_close_enter"),
        QStringLiteral("bypass_pending=%1 has_command_sink=%2 preview_playing=%3 preview_fullscreen=%4")
            .arg(closeConfirmedExternally_ ? 1 : 0)
            .arg(commandSink_ != nullptr ? 1 : 0)
            .arg(previewPlaying_ ? 1 : 0)
            .arg(previewFullscreen_ ? 1 : 0)
    );
    if (closeConfirmedExternally_) {
        appendQuickShellControllerLog(QStringLiteral("confirm_close_bypass"));
        closeConfirmedExternally_ = false;
        appendQuickShellLifecycleLog(
            QStringLiteral("controller_confirm_close_exit"),
            QStringLiteral("result=bypass elapsed_ms=%1").arg(timer.elapsed())
        );
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("controller_confirm_close"),
            timer.elapsed(),
            QStringLiteral("result=bypass has_command_sink=%1").arg(commandSink_ != nullptr ? 1 : 0)
        );
        return true;
    }
    const bool confirmed = commandSink_ != nullptr ? commandSink_->confirmShellClose() : true;
    appendQuickShellLifecycleLog(
        QStringLiteral("controller_confirm_close_exit"),
        QStringLiteral("result=%1 elapsed_ms=%2 has_command_sink=%3 preview_playing=%4 preview_fullscreen=%5")
            .arg(confirmed ? QStringLiteral("confirmed") : QStringLiteral("rejected"))
            .arg(timer.elapsed())
            .arg(commandSink_ != nullptr ? 1 : 0)
            .arg(previewPlaying_ ? 1 : 0)
            .arg(previewFullscreen_ ? 1 : 0)
    );
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("controller_confirm_close"),
        timer.elapsed(),
        QStringLiteral("result=%1 has_command_sink=%2")
            .arg(confirmed ? QStringLiteral("confirmed") : QStringLiteral("rejected"))
            .arg(commandSink_ != nullptr ? 1 : 0)
    );
    return confirmed;
}

void QuickShellController::logShellLifecycle(const QString& action, const QString& payload)
{
    appendQuickShellLifecycleLog(action.trimmed().isEmpty() ? QStringLiteral("qml_lifecycle") : action, payload);
}

void QuickShellController::notifyRootCloseAccepted(const QString& source)
{
    const QString normalizedSource = source.trimmed().isEmpty() ? QStringLiteral("qml_root_close") : source.trimmed();
    appendQuickShellLifecycleLog(
        QStringLiteral("root_close_accepted_notify"),
        QStringLiteral("source=%1").arg(normalizedSource)
    );
    emit rootCloseAccepted(normalizedSource);
}

void QuickShellController::togglePreviewPlayback()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->toggleShellPreviewPlayback();
    refreshFromStateSource();
}

void QuickShellController::stopPreview()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->stopShellPreview();
    refreshFromStateSource();
}

void QuickShellController::seekPreview(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->seekShellPreview(second);
    refreshFromStateSource();
}

void QuickShellController::beginPreviewScrub()
{
    if (commandSink_ == nullptr) {
        return;
    }
    appendQuickShellControllerLog(QStringLiteral("preview_scrub_begin"));
    commandSink_->beginShellPreviewScrub();
    refreshFromStateSource();
}

void QuickShellController::updatePreviewScrub(double second, bool centerView)
{
    if (commandSink_ == nullptr) {
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_scrub_update"),
        QString("second=%1 center=%2")
            .arg(second, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    commandSink_->updateShellPreviewScrub(second, centerView);
    refreshFromStateSource();
}

void QuickShellController::endPreviewScrub(double second, bool centerView)
{
    if (commandSink_ == nullptr) {
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_scrub_end"),
        QString("second=%1 center=%2")
            .arg(second, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    commandSink_->endShellPreviewScrub(second, centerView);
    refreshFromStateSource();
}

void QuickShellController::setPreviewRate(double rate)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->setShellPreviewRate(rate);
    refreshFromStateSource();
}

void QuickShellController::setBottomTabsCurrentTabId(const QString& tabId)
{
    if (commandSink_ == nullptr || stateSource_ == nullptr) {
        return;
    }
    if (tabId.trimmed().isEmpty() || stateSource_->shellBottomTabsCurrentTabId() == tabId) {
        return;
    }
    commandSink_->setShellBottomTabsCurrentTab(tabId);
    refreshFromStateSource();
}

void QuickShellController::setBottomTabsHostHeight(int height)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->setShellBottomTabsHeight(height);
    refreshFromStateSource();
}

void QuickShellController::timelineHeaderNavigate(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->navigateShellTimelineToSecond(second);
    refreshFromStateSource();
}

void QuickShellController::timelineWheelNavigate(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->wheelShellTimelineNavigate(second);
    refreshFromStateSource();
}

void QuickShellController::timelineCenterNavigate(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->centerShellTimelineNavigate(second);
    refreshFromStateSource();
}

void QuickShellController::timelineDragStarted()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineDragStarted();
    refreshFromStateSource();
}

void QuickShellController::timelineDragFinished(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineDragFinished(second);
    refreshFromStateSource();
}

void QuickShellController::timelineUserInteractionStarted()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineUserInteractionStarted();
    refreshFromStateSource();
}

void QuickShellController::noteTimelineSurfaceReady()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineSurfaceReady();
    refreshFromStateSource();
}

void QuickShellController::timelineFollowPreviewToggled(bool enabled)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineFollowPreviewToggled(enabled);
    refreshFromStateSource();
}

void QuickShellController::timelineFollowProgressToggled(bool enabled)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineFollowProgressToggled(enabled);
    refreshFromStateSource();
}

bool QuickShellController::stepPreviewBySeconds(double deltaSeconds, bool centerView)
{
    if (commandSink_ == nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("preview_step_ignored"),
            QString("reason=no_command_sink delta=%1 center=%2")
                .arg(deltaSeconds, 0, 'f', 6)
                .arg(centerView ? 1 : 0)
        );
        return false;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_step_request"),
        QString("delta=%1 center=%2")
            .arg(deltaSeconds, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    const bool moved = commandSink_->stepShellPreviewBySeconds(deltaSeconds, centerView);
    appendQuickShellControllerLog(
        QStringLiteral("preview_step_result"),
        QString("delta=%1 center=%2 moved=%3 pos=%4 duration=%5")
            .arg(deltaSeconds, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
            .arg(moved ? 1 : 0)
            .arg(previewPositionSeconds_, 0, 'f', 6)
            .arg(previewDurationSeconds_, 0, 'f', 6)
    );
    if (moved) {
        refreshFromStateSource();
    }
    return moved;
}

void QuickShellController::beginPreviewHeldSeek(int direction, int key)
{
    if (commandSink_ == nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("preview_hold_begin_ignored"),
            QString("reason=no_command_sink direction=%1 key=%2").arg(direction).arg(key)
        );
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_hold_begin"),
        QString("direction=%1 key=%2").arg(direction).arg(key)
    );
    commandSink_->beginShellPreviewHeldSeek(direction, key);
}

void QuickShellController::stopPreviewHeldSeek(int key)
{
    if (commandSink_ == nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("preview_hold_stop_ignored"),
            QString("reason=no_command_sink key=%1").arg(key)
        );
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_hold_stop"),
        QString("key=%1").arg(key)
    );
    commandSink_->stopShellPreviewHeldSeek(key);
    refreshFromStateSource();
}

QString QuickShellController::formatPreviewTimestamp(double second) const
{
    const int totalCentiseconds = qMax(0, qRound(second * 100.0));
    const int minutes = totalCentiseconds / 6000;
    const int secondsPart = (totalCentiseconds / 100) % 60;
    const int centiseconds = totalCentiseconds % 100;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secondsPart, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

void QuickShellController::logPreviewInteraction(const QString& action, const QString& payload)
{
    appendQuickShellControllerLog(action.trimmed().isEmpty() ? QStringLiteral("preview_interaction") : action, payload);
}

void QuickShellController::syncTopChromeSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncTopChromeSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->topChromeSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_top_chrome"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncSidebarSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncSidebarSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->sidebarSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_sidebar"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncWorkspaceSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncWorkspaceSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->workspaceSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_workspace"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncBottomTabsSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncBottomTabsSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->bottomTabsSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_bottom_tabs"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncBottomTabsToastAnchor(int x, int y, int width, int height, bool visible)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncBottomTabsToastAnchor(x, y, width, height, visible);
}

void QuickShellController::syncStatusSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncStatusSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->statusSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_status"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

bool QuickShellController::hasShortcut(const QKeySequence& sequence) const
{
    return commandSink_ != nullptr ? commandSink_->shellHasShortcut(sequence) : false;
}

bool QuickShellController::triggerShortcut(const QKeySequence& sequence)
{
    if (commandSink_ == nullptr) {
        return false;
    }
    const bool triggered = commandSink_->shellTriggerShortcut(sequence);
    if (triggered) {
        refreshFromStateSource();
    }
    return triggered;
}

void QuickShellController::refreshFromStateSource()
{
    if (stateSource_ == nullptr) {
        updateRefreshTimerInterval();
        return;
    }

    const QString previousBottomTabsCurrentTabId = bottomTabsCurrentTabId_;
    const QString previousPreviewSpeedLabel = previewSpeedLabel_;
    bool stateChanged = false;
    stateChanged |= assignIfChanged(windowTitle_, stateSource_->shellWindowTitle());
    stateChanged |= assignIfChanged(workspacePanelsSwapped_, stateSource_->shellWorkspacePanelsSwapped());
    stateChanged |= assignIfChanged(previewSpeedLabel_, stateSource_->shellPreviewSpeedLabel());
    stateChanged |= assignIfChanged(previewPlaying_, stateSource_->shellPreviewPlaying());
    stateChanged |= assignIfChanged(previewPositionSeconds_, stateSource_->shellPreviewPositionSeconds());
    stateChanged |= assignIfChanged(previewDurationSeconds_, stateSource_->shellPreviewDurationSeconds());
    stateChanged |= assignIfChanged(previewStatsTexts_, stateSource_->shellPreviewStatsTexts());
    stateChanged |= assignIfChanged(previewCanvasAspectRatio_, stateSource_->shellPreviewCanvasAspectRatio());
    stateChanged |= assignIfChanged(previewPaneRestoreGeneration_, stateSource_->shellPreviewPaneRestoreGeneration());
    stateChanged |= assignIfChanged(previewUsesSeparateSurface_, stateSource_->shellPreviewUsesSeparateSurface());
    stateChanged |= assignIfChanged(timelineSurfaceReady_, stateSource_->shellTimelineSurfaceReady());
    stateChanged |= assignIfChanged(bottomTabsCurrentTabId_, stateSource_->shellBottomTabsCurrentTabId());
    stateChanged |= assignIfChanged(bottomTabsHostHeight_, stateSource_->shellBottomTabsHeight());
    stateChanged |= assignIfChanged(bottomTabsHeaderScale_, stateSource_->shellBottomTabsHeaderScale());
    stateChanged |= assignIfChanged(bottomTabsVisible_, stateSource_->shellBottomTabsVisible());
    stateChanged |= assignIfChanged(timelineTabVisible_, stateSource_->shellTimelineTabVisible());
    stateChanged |= assignIfChanged(validationTabVisible_, stateSource_->shellValidationTabVisible());
    stateChanged |= assignIfChanged(muriTabVisible_, stateSource_->shellMuriTabVisible());

    const bool nextPreviewFullscreen = stateSource_->shellPreviewFullscreen();
    if (assignIfChanged(previewFullscreen_, nextPreviewFullscreen)) {
        stateChanged = true;
        emit previewFullscreenChanged();
    }

    if (surfaceHost_ != nullptr && previousBottomTabsCurrentTabId != bottomTabsCurrentTabId_) {
        surfaceHost_->refreshBottomTabsSurfaceVisibility();
    }
    if (surfaceHost_ != nullptr) {
        if (!previewSpeedToastInitialized_) {
            previewSpeedToastInitialized_ = true;
        } else if (previousPreviewSpeedLabel != previewSpeedLabel_) {
            surfaceHost_->showBottomTabsSpeedToast(previewSpeedLabel_);
        }
        if (!bottomTabsVisible_) {
            surfaceHost_->hideBottomTabsSpeedToast();
        }
    }

    if (stateChanged) {
        emit shellStateChanged();
    }
    updateRefreshTimerInterval();
}

void QuickShellController::updateRefreshTimerInterval()
{
    if (refreshTimer_ == nullptr) {
        return;
    }
    const int nextIntervalMs = previewPlaying_
        ? kQuickShellActiveRefreshIntervalMs
        : kQuickShellIdleRefreshIntervalMs;
    if (refreshTimer_->interval() != nextIntervalMs) {
        refreshTimer_->setInterval(nextIntervalMs);
    }
}
