#include "runtime/Session.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"
#include "runtime/preview/StageMediaHost.h"
#include "runtime/playback/PlaybackHost.h"
#include "runtime/playback/Playback.Internal.h"
#include "runtime/validation/ValidationHost.h"

#include "common/DebugLog.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "UiText.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QtWidgets>

using namespace miacode::runtime::shared;
using namespace miacode::runtime::playback_detail;

void Session::setBackendActive(bool active)
{
    if (backendActive_ == active) {
        return;
    }
    backendActive_ = active;
    uiFocusBridgeMode_ = active;
    timelineReady_ = false;
    pendingQuickTimelineCursorSync_ = false;
    pendingQuickTimelineCursorSecond_ = 0.0;
    pendingQuickTimelineCursorCenterView_ = false;
    syncQuickShellBottomTabsProxyRoute();
    if (shell_ != nullptr) {
        shell_->updateBottomTabsDeviceHeight();
    }
}

miacode::chart_transform::ChartNormalizationOptions Session::chartNormalizeOptions() const
{
    return miacode::chart_transform::ChartNormalizationOptions{
        true,
        chartNormalizeReduceTo384Grid_,
        chartNormalizeSplitEveryFourMeasures_,
        chartNormalizeSyntax_,
        chartNormalizeSectionMeasureCount_};
}

void Session::setChartNormalizeOptions(
    const miacode::chart_transform::ChartNormalizationOptions& options)
{
    chartNormalizeStartAtNewMeasure_ = true;
    chartNormalizeReduceTo384Grid_ = options.reduceTo384Grid;
    chartNormalizeSplitEveryFourMeasures_ = options.splitEveryFourMeasures;
    chartNormalizeSectionMeasureCount_ = options.sectionMeasureCount;
    chartNormalizeSyntax_ = options.syntax;
    savePortableState();
}

miacode::v2::UiRequestService* Session::uiRequestService() const
{
    return uiRequests_;
}

miacode::v2::JobProgressService* Session::jobProgressService() const
{
    return jobProgress_;
}

namespace {

constexpr double kScrubMisalignWarnSeconds = 0.05;

}  // namespace

bool miacode::runtime::PlaybackHost::playing() const
{
    return state_.playing_ || exportIntroLeadInPlaying();
}

miacode::v2::PlaybackTransportState miacode::runtime::PlaybackHost::playbackTransportState() const
{
    return state_.previewTransportState_;
}

double miacode::runtime::PlaybackHost::positionSeconds() const
{
    if (state_.exportIntroRegionActive_) {
        return state_.exportIntroPlayheadSeconds_;
    }
    return qMax(0.0, state_.pauseSecond_);
}

double miacode::runtime::PlaybackHost::durationSeconds() const
{
    return previewDurationSeconds();
}

double miacode::runtime::PlaybackHost::lowerBoundSeconds() const
{
    return exportIntroLowerBoundSeconds();
}

void miacode::runtime::PlaybackHost::togglePlayback()
{
    onTogglePreviewPause();
}

void miacode::runtime::PlaybackHost::stop()
{
    onStopPreview();
}

void miacode::runtime::PlaybackHost::seek(double second)
{
    seekPreviewToSecond(second, true);
    if (!state_.playing_ && !exportIntroLeadInPlaying()) {
        state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Paused;
    }
}

void miacode::runtime::PlaybackHost::beginScrub()
{
    appendQuickShellBackendLog(QStringLiteral("preview_scrub_begin"));
    QToolTip::hideText();
    stopPreviewHeldSeek();
    state_.previewScrubDragging_ = true;
    state_.previewScrubRenderElapsed_.invalidate();
    if (state_.previewFullscreenActive_) {
        showPreviewFullscreenControls(false);
    }
    if (state_.playing_) {
        pauseQtPreviewPlaybackExact();
    }
    state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Scrubbing;
}

void miacode::runtime::PlaybackHost::updateScrub(double second, bool centerView)
{
    if (handleExportIntroSliderSeek(second)) {
        return;
    }
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    QToolTip::hideText();
    appendQuickShellBackendLog(
        QStringLiteral("preview_scrub_update"),
        QString("second=%1 center=%2")
            .arg(clampedSecond, 0, 'f', 6)
            .arg(centerView ? 1 : 0));
    writePreviewPauseSecond(
        state_.pauseSecond_, clampedSecond, state_.playing_, "update_preview_scrub");
    if (state_.previewFullscreenActive_) {
        showPreviewFullscreenControls(false);
    }
    const bool shouldRenderNow =
        !state_.previewScrubRenderElapsed_.isValid()
        || state_.previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
    if (shouldRenderNow) {
        requestPausedPreviewSeek(clampedSecond, centerView, false, false);
        state_.previewScrubRenderElapsed_.restart();
    } else {
        schedulePreviewSeek(clampedSecond, centerView);
    }
    const double appliedSecond = state_.pausedSeekAppliedVisualSecond_;
    const double misalignDelta = clampedSecond - appliedSecond;
    if (qAbs(misalignDelta) > kScrubMisalignWarnSeconds) {
        appendQuickShellBackendLog(
            QStringLiteral("preview_scrub_misalign"),
            QString("phase=update handle=%1 requested=%2 applied=%3 delta=%4")
                .arg(second, 0, 'f', 6)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(appliedSecond, 0, 'f', 6)
                .arg(misalignDelta, 0, 'f', 6));
    }
}

void miacode::runtime::PlaybackHost::endScrub(double second, bool centerView)
{
    if (handleExportIntroSliderSeek(second)) {
        stopPreviewHeldSeek();
        state_.previewScrubDragging_ = false;
        state_.previewScrubRenderElapsed_.invalidate();
        return;
    }
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    QToolTip::hideText();
    appendQuickShellBackendLog(
        QStringLiteral("preview_scrub_end"),
        QString("second=%1 center=%2")
            .arg(clampedSecond, 0, 'f', 6)
            .arg(centerView ? 1 : 0));
    stopPreviewHeldSeek();
    state_.previewScrubDragging_ = false;
    state_.previewScrubRenderElapsed_.invalidate();
    writePreviewPauseSecond(
        state_.pauseSecond_, clampedSecond, state_.playing_, "end_preview_scrub");
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    seekPreviewToSecond(clampedSecond, centerView);
    state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Paused;
    const double appliedSecond = state_.pauseSecond_;
    const double misalignDelta = clampedSecond - appliedSecond;
    if (qAbs(misalignDelta) > kScrubMisalignWarnSeconds) {
        appendQuickShellBackendLog(
            QStringLiteral("preview_scrub_misalign"),
            QString("phase=end handle=%1 requested=%2 applied=%3 delta=%4")
                .arg(second, 0, 'f', 6)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(appliedSecond, 0, 'f', 6)
                .arg(misalignDelta, 0, 'f', 6));
    }
}

void miacode::runtime::PlaybackHost::setPlaybackRate(double rate)
{
    applyPreviewPlaybackRate(rate);
}

void miacode::runtime::PlaybackHost::nudgePlaybackRate(int direction)
{
    applyPreviewPlaybackRate(steppedPreviewPlaybackRate(state_.previewPlaybackRate_, direction));
}

double miacode::runtime::PlaybackHost::playbackRate() const
{
    return state_.previewPlaybackRate_;
}

QString miacode::runtime::PlaybackHost::playbackRateLabel() const
{
    QString rateText = QString::number(state_.previewPlaybackRate_, 'f', 2);
    while (rateText.endsWith('0')) {
        rateText.chop(1);
    }
    if (rateText.endsWith('.')) {
        rateText.chop(1);
    }
    return QStringLiteral("%1x").arg(rateText);
}

QObject* miacode::runtime::PlaybackHost::previewRuntimeObject() const
{
    return state_.scene_;
}

QObject* miacode::runtime::PlaybackHost::stageMediaHostObject() const
{
    return state_.previewStageMediaHost_;
}

double miacode::runtime::PlaybackHost::canvasAspectRatio() const
{
    return normalizedPreviewCanvasAspectRatio(state_.previewCanvasAspectRatio_);
}

QStringList miacode::runtime::PlaybackHost::statsTexts() const
{
    const miacode::preview::scene::PreviewObjectStatsSnapshot stats =
        state_.previewProgressStatsCache_ != nullptr
            ? state_.previewProgressStatsCache_->snapshotAt(qMax(0.0, state_.pauseSecond_))
            : miacode::preview::scene::PreviewObjectStatsSnapshot();
    const auto fmt = [](const QString& name, int played, int total) {
        return QString("%1  %2/%3")
            .arg(name.leftJustified(5, QChar(' '), true))
            .arg(played)
            .arg(total);
    };
    return QStringList{
        fmt(QStringLiteral("Tap"), stats.tapPlayed, stats.tapTotal),
        fmt(QStringLiteral("Hold"), stats.holdPlayed, stats.holdTotal),
        fmt(QStringLiteral("Slide"), stats.slidePlayed, stats.slideTotal),
        fmt(QStringLiteral("Touch"), stats.touchPlayed, stats.touchTotal),
        fmt(QStringLiteral("Break"), stats.breakPlayed, stats.breakTotal),
        fmt(QStringLiteral("Total"), stats.totalPlayed, stats.totalCount),
    };
}

RenderMode miacode::runtime::PlaybackHost::muriRenderMode() const
{
    return state_.muriRenderOptions_.renderMode;
}

void miacode::runtime::PlaybackHost::setMuriRenderMode(RenderMode mode)
{
    session_.validation_->setMuriRenderMode(mode, true);
}

void miacode::runtime::PlaybackHost::toggleMuriRenderMode()
{
    RenderMode nextMode = RenderMode::MaimuriDxStyle;
    switch (state_.muriRenderOptions_.renderMode) {
    case RenderMode::Native:
        nextMode = RenderMode::EraseByArea;
        break;
    case RenderMode::EraseByArea:
        nextMode = RenderMode::MaimuriDxStyle;
        break;
    case RenderMode::MaimuriDxStyle:
        nextMode = RenderMode::Native;
        break;
    }
    session_.validation_->setMuriRenderMode(nextMode, true);
}

QStringList miacode::runtime::PlaybackHost::availableSkinDirectoryNames() const
{
    return session_.stageMedia_->availablePreviewSkinDirectoryNames();
}

QString miacode::runtime::PlaybackHost::skinDisplayName(const QString& directoryName) const
{
    return session_.stageMedia_->previewSkinDisplayName(directoryName);
}

QString miacode::runtime::PlaybackHost::resolveSkinDir() const
{
    return session_.stageMedia_->resolvePreviewSkinDir();
}

QString miacode::runtime::PlaybackHost::resolveSkinRootDir() const
{
    return session_.stageMedia_->resolvePreviewSkinRootDir();
}

QString miacode::runtime::PlaybackHost::resolveCustomOutlineDir() const
{
    return session_.stageMedia_->resolvePreviewCustomOutlineDir();
}

void miacode::runtime::PlaybackHost::applyOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                                                  bool persistState)
{
    session_.stageMedia_->applyPreviewOutlineVariant(variant, useAutoSelection, persistState);
}

QVariantMap miacode::runtime::PlaybackHost::renderSettings() const
{
    return session_.previewRenderSettings();
}

void miacode::runtime::PlaybackHost::setRenderSetting(const QString& key, const QVariant& value)
{
    session_.setPreviewRenderSetting(key, value);
}

void miacode::runtime::PlaybackHost::refreshSurfaces()
{
    session_.refreshPreviewSurfaces();
}

void miacode::runtime::PlaybackHost::applySfxLevels()
{
    session_.applyPreviewSfxLevels();
}

void miacode::runtime::PlaybackHost::prepareForShutdown()
{
    session_.preparePreviewForShutdown();
}

PreviewAudioSettings miacode::runtime::PlaybackHost::audioSettings() const
{
    return session_.currentPreviewAudioSettings();
}

void miacode::runtime::PlaybackHost::applyAudioSettings(const PreviewAudioSettings& settings)
{
    session_.applyPreviewAudioSettingsFromUi(settings);
}

void miacode::runtime::PlaybackHost::saveAudioSettingsAsSoftwareDefault()
{
    session_.savePreviewAudioSettingsAsSoftwareDefault();
}

void miacode::runtime::PlaybackHost::restoreAudioSettingsFromSoftwareDefault()
{
    session_.restorePreviewAudioSettingsFromSoftwareDefault();
}

QObject* miacode::runtime::PlaybackHost::timelineStateBridge() const
{
    return static_cast<QObject*>(state_.timelineQuickStateBridge_);
}

void miacode::runtime::PlaybackHost::navigateToSecond(double second)
{
    onTimelineHeaderNavigateRequested(second);
}

void miacode::runtime::PlaybackHost::centerOnSecond(double second)
{
    onTimelineCenterNavigateRequested(second);
}

void miacode::runtime::PlaybackHost::wheelNavigateToSecond(double second)
{
    onTimelineWheelNavigateRequested(second);
}

void miacode::runtime::PlaybackHost::timelineDragStarted()
{
    onTimelineDragStarted();
}

void miacode::runtime::PlaybackHost::timelineDragFinished(double second)
{
    onTimelineDragFinished(second);
}

void miacode::runtime::PlaybackHost::timelineUserInteractionStarted()
{
    onTimelineUserInteractionStarted();
}

void miacode::runtime::PlaybackHost::setFollowPreviewEnabled(bool enabled)
{
    onTimelineFollowPreviewToggled(enabled);
}

QString miacode::runtime::PlaybackHost::bottomTabsCurrentTabId() const
{
    return session_.currentBottomTabsTabIdString();
}

void miacode::runtime::PlaybackHost::setBottomTabsCurrentTabId(const QString& tabId)
{
    session_.setCurrentBottomTabsTabId(tabId);
}

bool miacode::runtime::PlaybackHost::bottomTabsVisible() const
{
    return session_.bottomTabsTabVisible(Session::BottomTabsTabId::Timeline)
        || session_.bottomTabsTabVisible(Session::BottomTabsTabId::Validation)
        || session_.bottomTabsTabVisible(Session::BottomTabsTabId::Muri);
}

bool miacode::runtime::PlaybackHost::timelineTabVisible() const
{
    return session_.bottomTabsTabVisible(Session::BottomTabsTabId::Timeline);
}

bool miacode::runtime::PlaybackHost::muriTabVisible() const
{
    return session_.bottomTabsTabVisible(Session::BottomTabsTabId::Muri);
}

bool miacode::runtime::PlaybackHost::validationTabVisible() const
{
    return session_.bottomTabsTabVisible(Session::BottomTabsTabId::Validation);
}

bool miacode::runtime::PlaybackHost::ignoreMuriIssuePrompts() const
{
    return state_.ignoreMuriIssuePrompts_;
}

void miacode::runtime::PlaybackHost::noteTimelineSurfaceReady()
{
    if (state_.timelineReady_) {
        return;
    }
    state_.timelineReady_ = true;
    if (session_.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("quick_shell/backend"),
            QStringLiteral("action=quick_timeline_ready pending_cursor_sync=%1 second=%2 center=%3")
                .arg(state_.pendingQuickTimelineCursorSync_ ? 1 : 0)
                .arg(state_.pendingQuickTimelineCursorSecond_, 0, 'f', 6)
                .arg(state_.pendingQuickTimelineCursorCenterView_ ? 1 : 0)
        );
    }
    flushDeferredTimelineBridgeState();
}

bool Session::quickShellBottomTabsProxyActive() const
{
    return backendActive_ && quickShellBottomTabsProxy_ != nullptr;
}

QString Session::bottomTabsFallbackLabel(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return UiText::text(QStringLiteral("window.timeline"));
    case BottomTabsTabId::Validation:
        return UiText::text(QStringLiteral("window.syntax"));
    case BottomTabsTabId::Muri:
        return UiText::text(QStringLiteral("window.muri"));
    case BottomTabsTabId::Unknown:
        break;
    }
    return QString();
}

QWidget* Session::bottomTabsPageForTab(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return nullptr;
    case BottomTabsTabId::Validation:
        return errorList_;
    case BottomTabsTabId::Muri:
        return muriList_;
    case BottomTabsTabId::Unknown:
        break;
    }
    return nullptr;
}

QTabWidget* Session::bottomTabsContainerForTab(BottomTabsTabId tabId) const
{
    if (tabId == BottomTabsTabId::Unknown) {
        return nullptr;
    }
    if (tabId == BottomTabsTabId::Timeline) {
        return nullptr;
    }
    if (quickShellBottomTabsProxyActive() && tabId != BottomTabsTabId::Timeline) {
        return quickShellBottomTabsProxy_;
    }
    return bottomTabs_;
}

int Session::bottomTabsTabIndex(BottomTabsTabId tabId) const
{
    const QTabWidget* container = bottomTabsContainerForTab(tabId);
    const QWidget* targetWidget = bottomTabsPageForTab(tabId);
    if (container == nullptr || targetWidget == nullptr) {
        return -1;
    }
    return container->indexOf(const_cast<QWidget*>(targetWidget));
}

Session::BottomTabsTabId Session::bottomTabsTabIdFromString(const QString& tabId) const
{
    const QString normalized = tabId.trimmed().toLower();
    if (normalized == QStringLiteral("timeline")) {
        return BottomTabsTabId::Timeline;
    }
    if (normalized == QStringLiteral("validation")) {
        return BottomTabsTabId::Validation;
    }
    if (normalized == QStringLiteral("muri")) {
        return BottomTabsTabId::Muri;
    }
    return BottomTabsTabId::Unknown;
}

QString Session::bottomTabsTabIdString(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return QStringLiteral("timeline");
    case BottomTabsTabId::Validation:
        return QStringLiteral("validation");
    case BottomTabsTabId::Muri:
        return QStringLiteral("muri");
    case BottomTabsTabId::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

Session::BottomTabsTabId Session::currentBottomTabsTabId() const
{
    return currentBottomTabsTabId_;
}

QString Session::currentBottomTabsTabIdString() const
{
    return bottomTabsTabIdString(currentBottomTabsTabId());
}

bool Session::bottomTabsTabVisible(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return timelineTabVisible_;
    case BottomTabsTabId::Validation:
        return validationTabVisible_;
    case BottomTabsTabId::Muri:
        return muriTabVisible_;
    case BottomTabsTabId::Unknown:
        break;
    }
    return false;
}

void Session::syncBottomTabsCurrentTabToContainers()
{
    if (bottomTabs_ == nullptr) {
        return;
    }

    if (!quickShellBottomTabsProxyActive()) {
        const QWidget* targetPage = bottomTabsPageForTab(currentBottomTabsTabId_);
        const int index = targetPage != nullptr ? bottomTabs_->indexOf(const_cast<QWidget*>(targetPage)) : -1;
        if (index >= 0 && bottomTabs_->isTabVisible(index) && bottomTabs_->currentIndex() != index) {
            bottomTabs_->setCurrentIndex(index);
        }
        return;
    }

    if (quickShellBottomTabsProxy_ != nullptr) {
        const QWidget* targetPage = bottomTabsPageForTab(currentBottomTabsTabId_);
        const int proxyIndex = targetPage != nullptr
            ? quickShellBottomTabsProxy_->indexOf(const_cast<QWidget*>(targetPage))
            : -1;
        if (proxyIndex >= 0
            && quickShellBottomTabsProxy_->isTabVisible(proxyIndex)
            && quickShellBottomTabsProxy_->currentIndex() != proxyIndex) {
            quickShellBottomTabsProxy_->setCurrentIndex(proxyIndex);
        }
    }
}

void Session::syncQuickShellBottomTabsProxyRoute()
{
    if (bottomTabs_ == nullptr
        || quickShellBottomTabsProxy_ == nullptr
        || errorList_ == nullptr
        || muriList_ == nullptr) {
        return;
    }

    const int validationIndexInBottomTabs = bottomTabs_->indexOf(errorList_);
    const int muriIndexInBottomTabs = bottomTabs_->indexOf(muriList_);
    const bool validationVisible = validationIndexInBottomTabs >= 0
        ? bottomTabs_->isTabVisible(validationIndexInBottomTabs)
        : (quickShellBottomTabsProxy_ != nullptr
               && quickShellBottomTabsProxy_->indexOf(errorList_) >= 0
               && quickShellBottomTabsProxy_->isTabVisible(quickShellBottomTabsProxy_->indexOf(errorList_)));
    const bool muriVisible = muriIndexInBottomTabs >= 0
        ? bottomTabs_->isTabVisible(muriIndexInBottomTabs)
        : (quickShellBottomTabsProxy_ != nullptr
               && quickShellBottomTabsProxy_->indexOf(muriList_) >= 0
               && quickShellBottomTabsProxy_->isTabVisible(quickShellBottomTabsProxy_->indexOf(muriList_)));
    const QString validationLabel = validationIndexInBottomTabs >= 0
        ? bottomTabs_->tabText(validationIndexInBottomTabs)
        : bottomTabsFallbackLabel(BottomTabsTabId::Validation);
    const QString muriLabel = muriIndexInBottomTabs >= 0
        ? bottomTabs_->tabText(muriIndexInBottomTabs)
        : bottomTabsFallbackLabel(BottomTabsTabId::Muri);

    if (quickShellBottomTabsProxyActive()) {
        if (validationIndexInBottomTabs >= 0) {
            bottomTabs_->removeTab(validationIndexInBottomTabs);
        }
        const int muriIndexAfterValidationRemoval = bottomTabs_->indexOf(muriList_);
        if (muriIndexAfterValidationRemoval >= 0) {
            bottomTabs_->removeTab(muriIndexAfterValidationRemoval);
        }
        if (quickShellBottomTabsProxy_->indexOf(errorList_) < 0) {
            quickShellBottomTabsProxy_->addTab(errorList_, validationLabel);
        }
        if (quickShellBottomTabsProxy_->indexOf(muriList_) < 0) {
            quickShellBottomTabsProxy_->addTab(muriList_, muriLabel);
        }
        const int validationIndexInProxy = quickShellBottomTabsProxy_->indexOf(errorList_);
        if (validationIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->setTabVisible(validationIndexInProxy, validationVisible);
        }
        const int muriIndexInProxy = quickShellBottomTabsProxy_->indexOf(muriList_);
        if (muriIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->setTabVisible(muriIndexInProxy, muriVisible);
        }
    } else {
        const int validationIndexInProxy = quickShellBottomTabsProxy_->indexOf(errorList_);
        if (validationIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->removeTab(validationIndexInProxy);
        }
        const int muriIndexInProxy = quickShellBottomTabsProxy_->indexOf(muriList_);
        if (muriIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->removeTab(muriIndexInProxy);
        }
        if (bottomTabs_->indexOf(errorList_) < 0) {
            bottomTabs_->addTab(errorList_, validationLabel);
        }
        if (bottomTabs_->indexOf(muriList_) < 0) {
            bottomTabs_->addTab(muriList_, muriLabel);
        }
        const int restoredValidationIndex = bottomTabs_->indexOf(errorList_);
        if (restoredValidationIndex >= 0) {
            bottomTabs_->setTabVisible(restoredValidationIndex, validationVisible);
        }
        const int restoredMuriIndex = bottomTabs_->indexOf(muriList_);
        if (restoredMuriIndex >= 0) {
            bottomTabs_->setTabVisible(restoredMuriIndex, muriVisible);
        }
    }

    syncBottomTabsCurrentTabToContainers();
}

void Session::setCurrentBottomTabsTabId(BottomTabsTabId tabId)
{
    if (tabId == BottomTabsTabId::Unknown) {
        return;
    }
    // Issue #2 fix — in QuickShell mode the legacy QTabWidget is hidden
    // (the QML BottomTabBar renders the tab bar instead) and the
    // legacy widget's `isTabVisible(index)` may report false even when
    // the QML controller reports the tab as user-visible. The early
    // return on `!bottomTabsTabVisible(tabId)` then drops every click
    // from the QML tab bar — which is exactly the symptom the user
    // reported ("clicking validation/muri detect has no effect").
    // Skip the legacy visibility gate when the QuickShell backend is
    // active; visibility there is already gated at the QML level by
    // the validationTabVisible / muriTabVisible properties.
    if (!backendActive_ && !bottomTabsTabVisible(tabId)) {
        return;
    }
    const BottomTabsTabId previousTabId = currentBottomTabsTabId_;
    currentBottomTabsTabId_ = tabId;
    syncBottomTabsCurrentTabToContainers();
    if (tabId == BottomTabsTabId::Muri && validation_ != nullptr) {
        validation_->flushPendingMuriDiagnosticsPanelRefresh();
    }
    if (tabId == BottomTabsTabId::Timeline && playback_ != nullptr) {
        playback_->flushDeferredTimelineBridgeState();
    }
    if (tabId != BottomTabsTabId::Timeline) {
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    }
    if (previousTabId != tabId) {
        emit presentationChanged();
    }
}

void Session::setCurrentBottomTabsTabId(const QString& tabId)
{
    setCurrentBottomTabsTabId(bottomTabsTabIdFromString(tabId));
}

void Session::setBottomTabsTabVisible(BottomTabsTabId tabId, bool visible)
{
    bool* flag = nullptr;
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        flag = &timelineTabVisible_;
        break;
    case BottomTabsTabId::Validation:
        flag = &validationTabVisible_;
        break;
    case BottomTabsTabId::Muri:
        flag = &muriTabVisible_;
        break;
    case BottomTabsTabId::Unknown:
        break;
    }
    if (flag == nullptr || *flag == visible) {
        return;
    }
    *flag = visible;
    if (!visible && currentBottomTabsTabId() == tabId) {
        restoreBottomTabsCurrentTabAfterRefresh(
            tabId == BottomTabsTabId::Timeline
                ? BottomTabsTabId::Validation
                : BottomTabsTabId::Timeline);
    }
    emit presentationChanged();
}

void Session::restoreBottomTabsCurrentTabAfterRefresh(BottomTabsTabId preferredTabId)
{
    const auto pickVisibleTab = [this](BottomTabsTabId tabId) -> bool {
        return bottomTabsTabVisible(tabId);
    };

    BottomTabsTabId targetTabId = BottomTabsTabId::Unknown;
    if (pickVisibleTab(preferredTabId)) {
        targetTabId = preferredTabId;
    }
    if (targetTabId == BottomTabsTabId::Unknown && pickVisibleTab(currentBottomTabsTabId())) {
        targetTabId = currentBottomTabsTabId();
    }
    if (targetTabId == BottomTabsTabId::Unknown) {
        for (const BottomTabsTabId fallbackTab : {
                 BottomTabsTabId::Timeline,
                 BottomTabsTabId::Validation,
                 BottomTabsTabId::Muri}) {
            if (pickVisibleTab(fallbackTab)) {
                targetTabId = fallbackTab;
                break;
            }
        }
    }

    if (targetTabId != BottomTabsTabId::Unknown) {
        setCurrentBottomTabsTabId(targetTabId);
    }
}
