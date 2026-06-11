#include "MainWindow.TimelineSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/IntroConfig.h"
#include "common/OperationLog.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <cstdio>  // G2 Diag: std::snprintf for sync rate-change beacon lines

using namespace miacode::mainwindow::shared;

namespace {

constexpr double kTimelineZeroSecondTolerance = 1e-6;
constexpr auto kQuickShellTransportSeekProperty = "miacode.quick_shell_transport_seek";
constexpr int kPreviewPlaybackRateToastMinWidth = 196;
constexpr int kPreviewPlaybackRateToastMinHeight = 96;
constexpr int kPreviewPlaybackRateToastHorizontalMargin = 20;

void appendQuickShellBackendLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/backend"),
        text
    );
}

void appendPreviewPlaybackLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/playback"),
        text
    );
}

void appendPreviewInteractionLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/interaction"),
        text
    );
}

void appendPreviewFramePacingDiagLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/frame_pacing"),
        text,
        true
    );
}

double previewVisualLeadInStartSecond(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double requestedSecond,
    double tapFlowSpeed,
    double touchFlowSpeed)
{
    if (requestedSecond > kTimelineZeroSecondTolerance || noteMarkers.isEmpty()) {
        return requestedSecond;
    }

    const auto tapTiming = miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(tapFlowSpeed));
    const auto slideTrackTiming =
        miacode::preview::scene::previewSlideTrackTimingForFlowSpeed(static_cast<qreal>(tapFlowSpeed));
    const auto touchTiming =
        miacode::preview::scene::previewTouchTimingForFlowSpeed(static_cast<qreal>(touchFlowSpeed));

    double earliestSecond = requestedSecond;
    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString type = marker.type.toLower();
        if (type == QLatin1String("tap")) {
            if (marker.second + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - tapTiming.lifecycleDurationSeconds);
            }
            continue;
        }
        if (type == QLatin1String("hold")) {
            if (qMax(marker.second, marker.endSecond) + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - tapTiming.lifecycleDurationSeconds);
            }
            continue;
        }
        if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
            if (marker.second + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - tapTiming.lifecycleDurationSeconds);
            }
            if (qMax(marker.second, qMax(marker.endSecond, marker.slideTraceSecond)) + kTimelineZeroSecondTolerance
                >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - slideTrackTiming.appearLeadInSeconds);
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            if (marker.second + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - touchTiming.durationSeconds);
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            if (qMax(marker.second, marker.endSecond) + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - touchTiming.durationSeconds);
            }
            continue;
        }
    }

    return earliestSecond;
}

}  // namespace

bool MainWindow::hasActiveDifficulty() const
{
    return timelineSection_->hasActiveDifficulty();
}

int MainWindow::activeDifficultyId() const
{
    return timelineSection_->activeDifficultyId();
}

QString MainWindow::activeChartText() const
{
    return timelineSection_->activeChartText();
}

miacode::simai::SimaiTimingMetadata MainWindow::currentTimingMetadata() const
{
    return timelineSection_->currentTimingMetadata();
}

double MainWindow::parsedRawFirstSeconds(bool* ok) const
{
    return timelineSection_->parsedRawFirstSeconds(ok);
}

double MainWindow::parsedFirstSeconds(bool* ok) const
{
    return timelineSection_->parsedFirstSeconds(ok);
}

double MainWindow::parsedWholeBpm(bool* ok) const
{
    return timelineSection_->parsedWholeBpm(ok);
}

int MainWindow::parsedClockCount() const
{
    return timelineSection_->parsedClockCount();
}

QString MainWindow::parsedLatencyMeterId() const
{
    return timelineSection_->parsedLatencyMeterId();
}

void MainWindow::TimelineSection::scheduleDeferredPreviewUiTail(
    bool applyPreviewVisualSettings,
    bool applyDeferredAnalysis,
    bool dispatchTimelineAnalysis,
    bool writeProfilingSummary,
    bool updatePauseButton,
    bool updateObjectStats,
    double objectStatsSecond,
    bool refreshStageMediaDebugState,
    bool updatePausedPreviewFollowDecoration)
{
    const quint64 generation = ++state_.deferredPreviewUiTailGeneration_;
    QTimer::singleShot(
        0,
        &owner_,
        [this,
         generation,
         applyPreviewVisualSettings,
         applyDeferredAnalysis,
         dispatchTimelineAnalysis,
         writeProfilingSummary,
         updatePauseButton,
         updateObjectStats,
         objectStatsSecond,
         refreshStageMediaDebugState,
         updatePausedPreviewFollowDecoration]() {
            if (generation != state_.deferredPreviewUiTailGeneration_) {
                return;
            }
            if (applyPreviewVisualSettings) {
                owner_.applyEffectivePreviewOutlineVariantToCanvas();
                owner_.applyPreviewStageMediaRouteVisualSettings();
            }
            if (applyDeferredAnalysis) {
                owner_.applyDeferredAnalysisUiUpdates();
            }
            if (dispatchTimelineAnalysis && state_.pendingTimelineAnalysisRefresh_.revision != 0) {
                requestTimelineAnalysisDispatch(0);
            }
            if (writeProfilingSummary && state_.runtimeDebugOutputEnabled_ && state_.previewCanvas_ != nullptr) {
                state_.previewCanvas_->writeProfilingSummaryToFile();
            }
            if (updateObjectStats) {
                updatePreviewObjectStats(objectStatsSecond);
            }
            if (refreshStageMediaDebugState) {
                owner_.refreshPreviewStageMediaRouteDebugState(true);
            }
            if (updatePausedPreviewFollowDecoration && state_.previewFollowEnabled_ && !state_.qtPreviewPlaying_) {
                updatePreviewFollowDecorationForTimelineBlueLine(objectStatsSecond, true);
            }
            if (updatePauseButton) {
                owner_.updatePauseButtonAppearance();
            }
        });
}

void MainWindow::TimelineSection::schedulePreviewSeek(double second, bool centerView)
{
    requestPausedPreviewSeek(second, centerView, false, false);
}

quint64 MainWindow::TimelineSection::requestPausedPreviewVisualSeek(
    double second,
    bool centerView,
    int submitNowLogValue,
    bool logHotPath)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    const quint64 generation = ++state_.pausedSeekGeneration_;
    state_.pausedSeekTargetSecond_ = clampedSecond;
    state_.previewPendingSeekSecond_ = clampedSecond;
    state_.previewPendingSeekCenterView_ = centerView;
    if (logHotPath) {
        appendQuickShellBackendLog(
            QStringLiteral("paused_seek_request"),
            QString("generation=%1 second=%2 center=%3 submit_now=%4 media_pending=%5")
                .arg(generation)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(centerView ? 1 : 0)
                .arg(submitNowLogValue)
                .arg(state_.pausedSeekMediaPending_ ? 1 : 0)
        );
    }
    {
        const QScopedValueRollback<bool> suppressSecondaryUiUpdates(
            state_.suppressPausedPreviewSecondaryUiUpdates_,
            true);
        applyPausedPreviewVisualSecond(clampedSecond, centerView);
    }
    if (logHotPath) {
        appendQuickShellBackendLog(
            QStringLiteral("paused_seek_visual_apply"),
            QString("generation=%1 second=%2 center=%3")
                .arg(generation)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(centerView ? 1 : 0)
        );
    }
    scheduleDeferredPreviewUiTail(
        false,
        false,
        false,
        false,
        false,
        true,
        clampedSecond,
        true,
        state_.previewFollowEnabled_);
    return generation;
}

void MainWindow::TimelineSection::requestPausedPreviewSeek(
    double second,
    bool centerView,
    bool submitMediaImmediately,
    bool logHotPath
)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    const quint64 generation = requestPausedPreviewVisualSeek(
        clampedSecond,
        centerView,
        submitMediaImmediately ? 1 : 0,
        logHotPath);
    if (!owner_.previewStageMediaRouteHasVideo()) {
        state_.pausedSeekMediaPending_ = false;
        state_.pausedSeekMediaAckGeneration_ = generation;
        if (ui_.previewSeekDebounceTimer_ != nullptr) {
            ui_.previewSeekDebounceTimer_->stop();
        }
        return;
    }
    if (submitMediaImmediately && !state_.pausedSeekMediaPending_) {
        submitPausedMediaSeek(clampedSecond, generation);
        return;
    }
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->start();
    } else {
        maybeSubmitLatestPausedMediaSeek();
    }
}

void MainWindow::TimelineSection::applyPausedPreviewVisualSecond(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    state_.qtPreviewStartSecond_ = clampedSecond;
    state_.qtPreviewPauseSecond_ = clampedSecond;
    state_.qtPreviewTimelineStartSecond_ = clampedSecond;
    state_.qtPreviewTimelineElapsed_.restart();
    state_.qtPreviewPendingTimelineSecond_ = clampedSecond;
    state_.qtPreviewPendingTimelineCenterView_ = centerView;
    state_.qtPreviewTimelineDirty_ = true;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    applyQtPreviewPosition(clampedSecond, centerView);
    state_.pausedSeekAppliedVisualSecond_ = clampedSecond;
}

void MainWindow::TimelineSection::submitPausedMediaSeek(double second, quint64 generation)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    if (!owner_.previewStageMediaRouteHasVideo()) {
        state_.pausedSeekMediaPending_ = false;
        state_.pausedSeekMediaAckGeneration_ = generation;
        return;
    }
    state_.pausedSeekMediaPending_ = true;
    state_.pausedSeekMediaSubmittedGeneration_ = generation;
    appendQuickShellBackendLog(
        QStringLiteral("paused_seek_media_submit"),
        QString("generation=%1 second=%2")
            .arg(generation)
            .arg(clampedSecond, 0, 'f', 6)
    );
    owner_.submitPreviewStageMediaRoutePausedSeek(clampedSecond, generation);
}

void MainWindow::TimelineSection::maybeSubmitLatestPausedMediaSeek()
{
    if (state_.qtPreviewPlaying_ || state_.pausedSeekMediaPending_ || !owner_.previewStageMediaRouteHasVideo()) {
        return;
    }
    if (state_.pausedSeekMediaAckGeneration_ >= state_.pausedSeekGeneration_) {
        return;
    }
    submitPausedMediaSeek(state_.pausedSeekTargetSecond_, state_.pausedSeekGeneration_);
}

void MainWindow::TimelineSection::handlePausedPreviewMediaSeekCompleted(double second, quint64 generation)
{
    const bool staleSubmitted = generation != state_.pausedSeekMediaSubmittedGeneration_;
    appendQuickShellBackendLog(
        QStringLiteral("paused_seek_media_ack"),
        QString("generation=%1 submitted=%2 latest=%3 second=%4 stale_submitted=%5")
            .arg(generation)
            .arg(state_.pausedSeekMediaSubmittedGeneration_)
            .arg(state_.pausedSeekGeneration_)
            .arg(second, 0, 'f', 6)
            .arg(staleSubmitted ? 1 : 0)
    );
    if (staleSubmitted) {
        return;
    }
    state_.pausedSeekMediaPending_ = false;
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.pausedSeekMediaAckGeneration_ = generation;
    state_.qtPreviewStartSecond_ = second;
    state_.qtPreviewElapsed_.restart();
    owner_.refreshPreviewStageMediaRouteDebugState(false);
    if (generation < state_.pausedSeekGeneration_) {
        appendQuickShellBackendLog(
            QStringLiteral("paused_seek_media_drop"),
            QString("acked=%1 latest=%2").arg(generation).arg(state_.pausedSeekGeneration_)
        );
        maybeSubmitLatestPausedMediaSeek();
    }
}

bool MainWindow::TimelineSection::stepPreviewBySeconds(double deltaSeconds, bool centerView)
{
    if (!qIsFinite(deltaSeconds)) {
        return false;
    }
    if (qAbs(deltaSeconds) < 1e-9) {
        return false;
    }
    const double nextSecond = qBound(
        0.0,
        state_.qtPreviewPauseSecond_ + deltaSeconds,
        previewDurationSeconds()
    );
    const bool moved = qAbs(nextSecond - state_.qtPreviewPauseSecond_) >= 1e-9;
    if (moved) {
        seekPreviewToSecond(nextSecond, centerView);
    }
    return moved;
}

bool MainWindow::TimelineSection::handlePreviewSeekWheel(QWheelEvent* event)
{
    if (event == nullptr) {
        return false;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        delta = event->pixelDelta().x();
    }
    if (delta == 0) {
        return false;
    }
    const int steps = delta > 0 ? qMax(1, qRound(static_cast<double>(delta) / 120.0))
                                : qMin(-1, qRound(static_cast<double>(delta) / 120.0));
    const bool handled = stepPreviewBySeconds(
        static_cast<double>(steps) * miacode::preview_interaction::kSeekSingleStepSeconds,
        true
    );
    if (handled) {
        event->accept();
    }
    return handled;
}

void MainWindow::TimelineSection::beginPreviewHeldSeek(int direction, int key)
{
    if (direction == 0) {
        return;
    }
    appendQuickShellBackendLog(
        QStringLiteral("native_hold_begin"),
        QString("direction=%1 key=%2 has_focus=0")
            .arg(direction > 0 ? 1 : -1)
            .arg(key)
    );
    state_.previewHeldSeekDirection_ = direction > 0 ? 1 : -1;
    state_.previewSeekHeldArrowKey_ = key;
    state_.previewSeekHeldArrowLastElapsedMs_ = 0;
    state_.previewSeekHeldArrowElapsed_.restart();
    if (ui_.previewHeldSeekTimer_ != nullptr && !ui_.previewHeldSeekTimer_->isActive()) {
        ui_.previewHeldSeekTimer_->start();
    }
}

void MainWindow::TimelineSection::stopPreviewHeldSeek(int key)
{
    if (key != 0 && state_.previewSeekHeldArrowKey_ != key) {
        return;
    }
    appendQuickShellBackendLog(
        QStringLiteral("native_hold_stop"),
        QString("key=%1 active_key=%2 has_focus=0")
            .arg(key)
            .arg(state_.previewSeekHeldArrowKey_)
    );
    state_.previewHeldSeekDirection_ = 0;
    state_.previewSeekHeldArrowKey_ = 0;
    state_.previewSeekHeldArrowLastElapsedMs_ = 0;
    state_.previewSeekHeldArrowElapsed_.invalidate();
    owner_.setProperty(kQuickShellTransportSeekProperty, false);
    if (ui_.previewHeldSeekTimer_ != nullptr) {
        ui_.previewHeldSeekTimer_->stop();
    }
}

void MainWindow::TimelineSection::applyPreviewHeldSeekTick()
{
    if (state_.previewHeldSeekDirection_ == 0
        || state_.previewSeekHeldArrowKey_ == 0
        || !state_.previewSeekHeldArrowElapsed_.isValid()) {
        return;
    }
    const int elapsedMs = static_cast<int>(state_.previewSeekHeldArrowElapsed_.elapsed());
    const int deltaMs = state_.previewSeekHeldArrowLastElapsedMs_ > 0
        ? (elapsedMs - state_.previewSeekHeldArrowLastElapsedMs_)
        : miacode::preview_interaction::kSeekHoldTickIntervalMs;
    state_.previewSeekHeldArrowLastElapsedMs_ = elapsedMs;
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    const double deltaSeconds =
        static_cast<double>(state_.previewHeldSeekDirection_)
        * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds);
    appendQuickShellBackendLog(
        QStringLiteral("native_hold_tick"),
        QString("direction=%1 key=%2 delta_ms=%3 held=%4 delta=%5 has_focus=0 pos=%6")
            .arg(state_.previewHeldSeekDirection_)
            .arg(state_.previewSeekHeldArrowKey_)
            .arg(deltaMs)
            .arg(heldSeconds, 0, 'f', 6)
            .arg(deltaSeconds, 0, 'f', 6)
            .arg(owner_.shellPreviewPositionSeconds(), 0, 'f', 6)
    );
    QToolTip::hideText();
    const double nextSecond = qBound(
        0.0,
        state_.qtPreviewPauseSecond_ + deltaSeconds,
        previewDurationSeconds()
    );
    if (qAbs(nextSecond - state_.qtPreviewPauseSecond_) >= 1e-9) {
        seekPreviewToSecond(nextSecond, true);
    }
}

void MainWindow::TimelineSection::seekPreviewToSecond(double second, bool centerView)
{
    owner_.ensurePreviewStageMediaRouteInitialized();
    owner_.ensurePreviewSfxRuntimePrepared();
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (state_.qtPreviewPlaying_ && state_.previewSfxRuntime_ != nullptr) {
        // G1 Commit 8 followup: bass_clock_seek per §7.2 — playing-seek branch.
        // from_chart is the wall-clock chart-second right before the re-anchor,
        // to_chart is the requested target. Reason "playing_seek" disambiguates
        // from the discrete / paused / scrub paths.
        const double fromChartSecond = owner_.currentPreviewAuthoritativeAudioClockSecond();
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QString(),
            QString("bass_clock_seek from_chart=%1 to_chart=%2 rate=%3 reason=playing_seek")
                .arg(fromChartSecond, 0, 'f', 6)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(state_.previewPlaybackRate_, 0, 'f', 3));
        const double effectiveSecond =
            state_.previewSfxRuntime_->seekRetainedPreviewPlaybackTransaction(clampedSecond, true);
        state_.qtPreviewStartSecond_ = effectiveSecond;
        state_.qtPreviewPauseSecond_ = effectiveSecond;
        state_.qtPreviewElapsed_.restart();
        state_.qtPreviewTimelineElapsed_.restart();
        state_.qtPreviewPendingTimelineSecond_ = effectiveSecond;
        state_.qtPreviewPendingTimelineCenterView_ = centerView;
        state_.qtPreviewTimelineDirty_ = true;
        state_.qtPreviewLastTimelineSecond_ = -1.0;
        if (state_.previewCanvas_ != nullptr) {
            state_.previewCanvas_->setPlayheadSeconds(effectiveSecond, true);
        }
        owner_.syncPreviewStageMediaRoutePlayback(effectiveSecond);
        applyQtPreviewPosition(effectiveSecond, centerView);
        return;
    }
    if (state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        cancelPreviewStartupSync();
    }
    // G1 Commit 8 followup: bass_clock_seek per §7.2 — paused-anchor branch
    // (also covers the post-cancel startup-pending case).
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QString(),
        QString("bass_clock_seek from_chart=%1 to_chart=%2 rate=%3 reason=paused_anchor")
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(state_.previewPlaybackRate_, 0, 'f', 3));
    anchorQtPreviewPlaybackToSecond(clampedSecond, centerView);
}

void MainWindow::TimelineSection::seekPreviewDiscreteToSecond(double second, bool centerView)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    owner_.ensurePreviewStageMediaRouteInitialized();
    owner_.ensurePreviewSfxRuntimePrepared();
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    // G1 Commit 8 followup: bass_clock_seek per §7.2 — discrete-seek branch
    // (arrow keys, jump-to-note). from_chart is the current wall-clock chart
    // sec; the branch below may then reanchor a playing session into pause
    // before applying the seek.
    {
        const double fromChartSecond = owner_.currentPreviewAuthoritativeAudioClockSecond();
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QString(),
            QString("bass_clock_seek from_chart=%1 to_chart=%2 rate=%3 reason=discrete")
                .arg(fromChartSecond, 0, 'f', 6)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(state_.previewPlaybackRate_, 0, 'f', 3));
    }
    if (state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        cancelPreviewStartupSync();
    } else if (state_.qtPreviewPlaying_) {
        pauseQtPreviewPlaybackForReanchor();
    }

    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    state_.qtPreviewPauseSecond_ = clampedSecond;
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = clampedSecond;
    state_.qtPreviewPendingTimelineCenterView_ = centerView;
    state_.qtPreviewTimelineDirty_ = true;
    state_.qtPreviewPlaying_ = false;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(false);
    }
    invalidatePreviewFollowBindingCache();
    state_.activePreviewPlaybackTransactionId_ = 0;
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    state_.qtPreviewDisplayRefreshTickQueued_ = false;
    state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    state_.qtPreviewFixedAwaitingFrame_ = false;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    state_.qtPreviewFixedFrameTickQueued_ = false;
    state_.qtPreviewLastVisualTickNs_ = -1;
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }

    QElapsedTimer visualTimer;
    visualTimer.start();
    const quint64 generation = requestPausedPreviewVisualSeek(clampedSecond, centerView, -1, false);
    const double visualElapsedMs = visualTimer.nsecsElapsed() / 1000000.0;
    double retainedResetElapsedMs = 0.0;
    if (state_.previewSfxRuntime_ != nullptr) {
        QElapsedTimer retainedResetTimer;
        retainedResetTimer.start();
        state_.previewSfxRuntime_->resetRetainedPreviewPlaybackTransaction(clampedSecond);
        retainedResetElapsedMs = retainedResetTimer.nsecsElapsed() / 1000000.0;
    }
    scheduleDeferredPreviewUiTail(
        true,
        false,
        false,
        false,
        false,
        true,
        clampedSecond,
        true,
        state_.previewFollowEnabled_);

    if (!owner_.previewStageMediaRouteHasVideo()) {
        state_.pausedSeekMediaPending_ = false;
        state_.pausedSeekMediaAckGeneration_ = generation;
        appendPreviewInteractionLog(
            QStringLiteral("discrete_seek_complete"),
            QString("generation=%1 second=%2 elapsed_ms=%3 visual_ms=%4 retained_ms=%5 has_video=0")
                .arg(generation)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(totalTimer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
                .arg(visualElapsedMs, 0, 'f', 3)
                .arg(retainedResetElapsedMs, 0, 'f', 3));
        return;
    }

    QTimer::singleShot(0, &owner_, [this, generation, clampedSecond]() {
        if (generation != state_.pausedSeekGeneration_
            || state_.qtPreviewPlaying_
            || state_.pausedSeekMediaPending_) {
            return;
        }
        submitPausedMediaSeek(clampedSecond, generation);
    });
    appendPreviewInteractionLog(
        QStringLiteral("discrete_seek_complete"),
        QString("generation=%1 second=%2 elapsed_ms=%3 visual_ms=%4 retained_ms=%5 has_video=1")
            .arg(generation)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(totalTimer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
            .arg(visualElapsedMs, 0, 'f', 3)
            .arg(retainedResetElapsedMs, 0, 'f', 3));
}

void MainWindow::TimelineSection::applyPreviewPlaybackRate(double rate)
{
    owner_.ensurePreviewStageMediaRouteInitialized();
    const double clampedRate = qMax(0.25, rate);
    // G2 Diag: sync beacon at the rate-change UI entry. The user-reported 0.5x
    // crash leaves NO async DebugLog tail (the AsyncLogWriter queue drops on
    // fast-fail), so the existing bass_clock_set_rate row a few lines down
    // never reaches disk. Mirror every leg of the dispatch into the sync
    // beacon (pure-Win32 fsync per line) so the next crash leaves a usable
    // trail: ui_enter → qt_media_call → bass_runtime_call → ui_exit.
    {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "ui/rate/enter from=%.3f requested=%.3f clamped=%.3f playing=%d",
            state_.previewPlaybackRate_,
            rate,
            clampedRate,
            state_.qtPreviewPlaying_ ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (qFuzzyCompare(state_.previewPlaybackRate_ + 1.0, clampedRate + 1.0)) {
        miacode::oplog::appendStartupBeaconLine("ui/rate/exit reason=noop_same_rate");
        return;
    }
    // G2 Commit 2: capture the wall-clock chart-second using the OLD rate,
    // before any state is overwritten. This is the value we pass to the
    // backend's atomic pause-modify-resume sequence below (and the value we
    // re-anchor the wall-clock timer to a few lines down). Per
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §6.2 — sampling chart-second
    // before the rate flip is the only way to keep audio and visual in step
    // across the transition.
    const double chartNow = owner_.currentPreviewAuthoritativeAudioClockSecond();
    const bool wasPlaying = state_.qtPreviewPlaying_;
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QString(),
        QString("bass_clock_set_rate from=%1 to=%2 while_playing=%3 chart_now=%4")
            .arg(state_.previewPlaybackRate_, 0, 'f', 3)
            .arg(clampedRate, 0, 'f', 3)
            .arg(wasPlaying ? 1 : 0)
            .arg(chartNow, 0, 'f', 6));
    state_.previewPlaybackRate_ = clampedRate;
    // G2 Commit 2: re-anchor the wall-clock master to the captured chart-second
    // so the next tick reads chartNow + 0*newRate = chartNow (no jump), then
    // advances at the new rate.
    if (wasPlaying) {
        state_.qtPreviewStartSecond_ = chartNow;
        state_.qtPreviewElapsed_.restart();
        state_.qtPreviewTimelineStartSecond_ = chartNow;
        state_.qtPreviewTimelineElapsed_.restart();
    }
    if (ui_.previewSpeedButton_ != nullptr) {
        QString rateText = QString::number(state_.previewPlaybackRate_, 'f', 2);
        while (rateText.endsWith('0')) {
            rateText.chop(1);
        }
        if (rateText.endsWith('.')) {
            rateText.chop(1);
        }
        ui_.previewSpeedButton_->setText(QString("%1x").arg(rateText));
        if (QMenu* speedMenu = ui_.previewSpeedButton_->menu(); speedMenu != nullptr) {
            const int targetIndex = nearestPreviewPlaybackRateIndex(state_.previewPlaybackRate_);
            const QList<QAction*> actions = speedMenu->actions();
            for (int index = 0; index < actions.size(); ++index) {
                QAction* action = actions[index];
                const QVariant data = action != nullptr ? action->data() : QVariant();
                const bool checked = data.isValid()
                    ? qFuzzyCompare(data.toDouble() + 1.0, state_.previewPlaybackRate_ + 1.0)
                    : (index == targetIndex);
                if (action != nullptr) {
                    action->setChecked(checked);
                }
            }
        }
    }
    owner_.showPreviewPlaybackRateToast(state_.previewPlaybackRate_);
    miacode::oplog::appendStartupBeaconLine("ui/rate/qt_media_about_to_call");
    owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "ui_rate_change");
    miacode::oplog::appendStartupBeaconLine("ui/rate/qt_media_returned");
    if (state_.previewSfxRuntime_ != nullptr) {
        if (wasPlaying) {
            // G2 Commit 2: live rate change while playing. The backend's
            // applyPlaybackRateAtChartSecond runs the atomic pause-modify-resume
            // sequence (pause BGM flag → BASS_ATTRIB_TEMPO write → re-anchor BGM
            // cursor to chartNow → unpause flag), all while the master mixer
            // keeps running. Pre-G2 this site forced a full
            // stopQtPreviewPlayback + startQtPreviewPlayback cycle to push the
            // new TEMPO in — heavyweight and broke audio continuity. The new
            // call covers the same effect with ~50-100ms BGM gap (per §6.2)
            // instead of a full session restart.
            miacode::oplog::appendStartupBeaconLine("ui/rate/bass_live_about_to_call");
            state_.previewSfxRuntime_->applyPlaybackRateAtChartSecond(
                state_.previewPlaybackRate_, chartNow);
            miacode::oplog::appendStartupBeaconLine("ui/rate/bass_live_returned");
        } else {
            miacode::oplog::appendStartupBeaconLine("ui/rate/bass_paused_about_to_call");
            state_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state_.previewPlaybackRate_);
            miacode::oplog::appendStartupBeaconLine("ui/rate/bass_paused_returned");
            if (!state_.previewStartupSyncPending_
                && !state_.previewLateVideoStartPending_
                && !state_.latestTimelineNoteMarkers_.isEmpty()) {
                miacode::oplog::appendStartupBeaconLine("ui/rate/bass_apply_paused_about_to_call");
                state_.previewSfxRuntime_->applyPausedPreviewState(
                    state_.latestTimelineNoteMarkers_,
                    false,
                    state_.qtPreviewPauseSecond_,
                    state_.previewPlaybackRate_,
                    state_.previewTimingSettings_);
                miacode::oplog::appendStartupBeaconLine("ui/rate/bass_apply_paused_returned");
            }
        }
    }
    owner_.savePortableState();
    // G2 Commit 2: the playback-restart cycle below previously ran whenever
    // qtPreviewPlaying_ was true, because it was the only way to get the new
    // TEMPO onto the BGM tempo stream. With applyPlaybackRateAtChartSecond
    // handling that atomically, restart is no longer needed for live rate
    // change. It still kicks in for the startup-pending / late-video-pending
    // paths since those represent a partially-prepared session that must
    // be re-laid-out.
    if (state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        miacode::oplog::appendStartupBeaconLine("ui/rate/restart_cycle_about_to_call");
        stopQtPreviewPlayback(true);
        startQtPreviewPlayback(state_.qtPreviewPauseSecond_, true);
        miacode::oplog::appendStartupBeaconLine("ui/rate/restart_cycle_returned");
    }
    miacode::oplog::appendStartupBeaconLine("ui/rate/exit reason=ok");
}

double MainWindow::currentPreviewAuthoritativeAudioClockSecond() const
{
    // G1 Commit 4: wall-clock is now the master timeline. BASS handles
    // audio output, but chart-second comes from qtPreviewElapsed_ —
    // a monotonic QElapsedTimer that doesn't suffer from buffer
    // underrun, tempo-stream stalls, or the 0.5x ramp-up race.
    // See docs/PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §5.1, §6.1.
    if (previewStartupSyncPending_ || previewLateVideoStartPending_) {
        return previewStartupPreparedSecond_;
    }
    if (qtPreviewPlaying_) {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        return qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    }
    return qtPreviewPauseSecond_;
}

void MainWindow::TimelineSection::cancelPreviewStartupSync()
{
    if (!state_.previewStartupSyncPending_
        && !state_.previewLateVideoStartPending_
        && !state_.previewStartupStrongGroupCommitted_) {
        return;
    }
    const quint64 playbackTxn = state_.activePreviewPlaybackTransactionId_;
    appendPreviewPlaybackLog(
        QStringLiteral("cancel"),
        QString("txn=%1 pending=%2 late_video_pending=%3 committed=%4")
            .arg(playbackTxn)
            .arg(state_.previewStartupSyncPending_ ? 1 : 0)
            .arg(state_.previewLateVideoStartPending_ ? 1 : 0)
            .arg(state_.previewStartupStrongGroupCommitted_ ? 1 : 0));
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->cancelPreparedPreviewPlayback();
    }
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->cancelPreparedPlaybackStart(playbackTxn);
    }
    state_.previewStartupSyncPending_ = false;
    state_.previewLateVideoStartPending_ = false;
    state_.previewStartupAudioPrepared_ = false;
    state_.previewStartupCanvasPresented_ = false;
    state_.previewStartupStrongGroupCommitted_ = false;
    state_.previewStartupResumeFromPause_ = false;
    state_.previewStartupVideoPrepareStarted_ = false;
    state_.previewStartupVideoPrepared_ = false;
    state_.previewStartupVideoStarted_ = false;
    state_.previewStartupRequestedSecond_ = 0.0;
    state_.previewStartupPreparedSecond_ = 0.0;
}

void MainWindow::TimelineSection::handlePreviewStartupCanvasPresented()
{
    if (!state_.previewStartupSyncPending_ || state_.previewStartupStrongGroupCommitted_) {
        return;
    }
    if (state_.previewStartupCanvasPresented_) {
        return;
    }
    state_.previewStartupCanvasPresented_ = true;
    appendPreviewPlaybackLog(
        QStringLiteral("canvas_presented"),
        QString("txn=%1 second=%2")
            .arg(state_.activePreviewPlaybackTransactionId_)
            .arg(state_.previewStartupPreparedSecond_, 0, 'f', 6));
    tryCommitPreviewStartupSync();
}

void MainWindow::TimelineSection::handlePreviewStartupVideoPrepared(double second, quint64 transactionId)
{
    if (transactionId != state_.activePreviewPlaybackTransactionId_) {
        appendPreviewPlaybackLog(
            QStringLiteral("weak_video_prepare_drop"),
            QString("txn=%1 active_txn=%2 second=%3")
                .arg(transactionId)
                .arg(state_.activePreviewPlaybackTransactionId_)
                .arg(second, 0, 'f', 6));
        return;
    }
    if (!state_.previewStartupSyncPending_ && !state_.previewLateVideoStartPending_) {
        appendPreviewPlaybackLog(
            QStringLiteral("weak_video_prepare_drop"),
            QString("txn=%1 second=%2 reason=inactive")
                .arg(transactionId)
                .arg(second, 0, 'f', 6));
        return;
    }

    state_.previewStartupVideoPrepared_ = true;
    appendPreviewPlaybackLog(
        QStringLiteral("weak_video_prepared"),
        QString("txn=%1 second=%2 committed=%3")
            .arg(transactionId)
            .arg(second, 0, 'f', 6)
            .arg(state_.previewStartupStrongGroupCommitted_ ? 1 : 0));
    if (state_.previewStartupStrongGroupCommitted_
        && state_.previewLateVideoStartPending_
        && state_.previewStageMediaHost_ != nullptr) {
        const double currentSecond = owner_.currentPreviewAuthoritativeAudioClockSecond();
        state_.previewStageMediaHost_->commitPreparedPlaybackStart(currentSecond);
        state_.previewStartupVideoStarted_ = true;
        state_.previewLateVideoStartPending_ = false;
        appendPreviewPlaybackLog(
            QStringLiteral("late_video_start_after_commit"),
            QString("txn=%1 second=%2")
                .arg(transactionId)
                .arg(currentSecond, 0, 'f', 6));
    }
}

void MainWindow::TimelineSection::tryCommitPreviewStartupSync()
{
    if (!state_.previewStartupSyncPending_ || state_.previewStartupStrongGroupCommitted_) {
        return;
    }
    if (!state_.previewStartupAudioPrepared_) {
        return;
    }
    if (state_.previewCanvas_ != nullptr && !state_.previewStartupCanvasPresented_) {
        return;
    }

    state_.previewStartupStrongGroupCommitted_ = true;
    state_.previewStartupSyncPending_ = false;
    const double effectiveStartSecond = state_.previewStartupPreparedSecond_;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->commitPreparedPreviewPlayback();
    }
    if (state_.previewStageMediaHost_ != nullptr
        && state_.previewStartupVideoPrepareStarted_
        && state_.previewStartupVideoPrepared_) {
        state_.previewStageMediaHost_->commitPreparedPlaybackStart(effectiveStartSecond);
        state_.previewStartupVideoStarted_ = true;
        state_.previewLateVideoStartPending_ = false;
        appendPreviewPlaybackLog(
            QStringLiteral("weak_video_ready_before_commit"),
            QString("txn=%1 second=%2")
                .arg(state_.activePreviewPlaybackTransactionId_)
                .arg(effectiveStartSecond, 0, 'f', 6));
    } else {
        state_.previewLateVideoStartPending_ = state_.previewStartupVideoPrepareStarted_;
    }
    finalizeQtPreviewPlaybackStart(effectiveStartSecond);
    appendPreviewPlaybackLog(
        QStringLiteral("commit"),
        QString("txn=%1 effective=%2 late_video_pending=%3")
            .arg(state_.activePreviewPlaybackTransactionId_)
            .arg(effectiveStartSecond, 0, 'f', 6)
            .arg(state_.previewLateVideoStartPending_ ? 1 : 0));
}

void MainWindow::TimelineSection::stopQtPreviewTimers()
{
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    if (ui_.qtPreviewTimer_ != nullptr) {
        ui_.qtPreviewTimer_->stop();
    }
    if (ui_.qtPreviewTimelineTimer_ != nullptr) {
        ui_.qtPreviewTimelineTimer_->stop();
    }
    if (ui_.previewStatsUiTimer_ != nullptr) {
        ui_.previewStatsUiTimer_->stop();
    }
    owner_.setPreviewFixedTimerHighResolutionActive(false);
}

void MainWindow::TimelineSection::finalizeQtPreviewPlaybackStart(double effectiveStartSecond)
{
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewStartSecond_ = effectiveStartSecond;
    state_.qtPreviewPauseSecond_ = effectiveStartSecond;
    state_.qtPreviewElapsed_.restart();
    state_.qtPreviewTimelineElapsed_.restart();
    state_.qtPreviewPlaying_ = true;
    // beta7 leak gauge — anchor private bytes at play start so the pause handler can report the
    // playback-window delta (d_play), the largest previously-unbracketed slice of the cycle.
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::leak_gauge::notePlayStartPrivateBytes(
            miacode::debug_log::processPrivateBytes());
        miacode::debug_log::leak_gauge::markPlayStartTimelinePresents();
    }
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    state_.qtPreviewDisplayRefreshTickQueued_ = false;
    state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    state_.qtPreviewFixedAwaitingFrame_ = false;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    state_.qtPreviewFixedFrameTickQueued_ = false;
    state_.qtPreviewLastVisualTickNs_ = -1;
    state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ = -1;
    resetVisualClockSmoothing();
    resetQtPreviewFixedFramePacing();
    // Doc 4.4: activate active-playback profiling so realtime FPS stats aren't polluted by
    // paused/closed-window intervals.
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(true);
    }
    owner_.setPreviewFixedTimerHighResolutionActive(!previewCanvasUsesFrameSwappedPacing());
    if (state_.previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        requestNextFixedIntervalPreviewFrame();
    }
    if (ui_.qtPreviewTimelineTimer_ != nullptr && !ui_.qtPreviewTimelineTimer_->isActive()) {
        ui_.qtPreviewTimelineTimer_->start();
    }
    if (ui_.previewStatsUiTimer_ != nullptr && !ui_.previewStatsUiTimer_->isActive()) {
        ui_.previewStatsUiTimer_->start();
    }
    invalidatePreviewFollowBindingCache();
    syncEditorCursorToPreviewSecond(effectiveStartSecond, false);
    updatePreviewSliderPosition(effectiveStartSecond);
    scheduleDeferredPreviewUiTail(
        true,
        false,
        false,
        false,
        true,
        false,
        effectiveStartSecond,
        false,
        false);
    if (state_.previewPendingPlayInteractionId_ != 0) {
        appendPreviewInteractionLog(
            QStringLiteral("play_complete"),
            QString("op=%1 source=%2 effective_second=%3 txn=%4")
                .arg(state_.previewPendingPlayInteractionId_)
                .arg(state_.previewPendingPlayInteractionSource_)
                .arg(effectiveStartSecond, 0, 'f', 6)
                .arg(state_.activePreviewPlaybackTransactionId_));
        state_.previewPendingPlayInteractionId_ = 0;
        state_.previewPendingPlayInteractionSource_.clear();
    }
}

void MainWindow::TimelineSection::pauseQtPreviewPlaybackExact()
{
    if (state_.exportEffectPreviewPlaybackActive_) {
        stopExportEffectPreviewPlayback();
        return;
    }
    const bool wasPlaying = state_.qtPreviewPlaying_;
    const quint64 playbackTxn = state_.activePreviewPlaybackTransactionId_;
    if (!wasPlaying || state_.previewSfxRuntime_ == nullptr) {
        stopQtPreviewPlayback(true);
        return;
    }

    // G1 Commit 6: capture the canonical pause-second from the wall-clock master
    // BEFORE handing pause control to the runtime. With BASS_ChannelGetPosition no
    // longer consulted in BassPreviewAudioBackend::authoritativeSecond
    // (PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §5.3 / §6.1 step 6), the runtime's
    // returned pauseSecond would be stale; the wall-clock value is the truth, so we
    // overwrite the runtime's PausePreviewResult.pauseSecond with it below.
    const double wallClockPauseSecond = owner_.currentPreviewAuthoritativeAudioClockSecond();
    const QtPreviewSfxRuntime::PausePreviewResult pauseResult =
        state_.previewSfxRuntime_->pausePreviewPlaybackTransaction();
    state_.qtPreviewPauseSecond_ = wallClockPauseSecond;
    cancelPreviewStartupSync();
    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    state_.pausedPreviewMediaSeekPending_ = false;
    appendPreviewPlaybackLog(
        QStringLiteral("pause_exact"),
        QString("txn=%1 pause_second=%2 retained_mode=%3")
            .arg(playbackTxn)
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6)
            .arg(static_cast<int>(pauseResult.retainedMode)));
    state_.qtPreviewPendingTimelineSecond_ = state_.qtPreviewPauseSecond_;
    // Per docs/TIMELINE_COORDINATE_FOCUS_SPEC.md §5: 播放中 → 点击暂停 → 暂停-R,
    // Timeline 聚焦 R. The next paused-flush must call
    // setPlayheadSeconds(s, /*centerView=*/true) so centerOnSecond
    // recomputes horizontalScrollValue around the freeze point. Was
    // false here, which left scroll wherever it was at the moment of
    // pause (often 0 if a startup race kept the playback timer's
    // ticks blocked) — visible symptom: timeline body locked at
    // chart start with playhead off-screen to the right.
    state_.qtPreviewPendingTimelineCenterView_ = true;
    state_.qtPreviewTimelineDirty_ = true;
    state_.qtPreviewPlaying_ = false;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(false);
    }
    invalidatePreviewFollowBindingCache();
    state_.activePreviewPlaybackTransactionId_ = 0;
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    state_.qtPreviewDisplayRefreshTickQueued_ = false;
    state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    state_.qtPreviewFixedAwaitingFrame_ = false;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    state_.qtPreviewFixedFrameTickQueued_ = false;
    state_.qtPreviewLastVisualTickNs_ = -1;
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    resetVisualClockSmoothing();
    flushQtPreviewTimelinePosition();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->focusPlayhead(false);
        state_.timelineQuickStateBridge_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    updatePreviewSliderPosition(state_.qtPreviewPauseSecond_);
    // beta4 leak gauge (LOW-FREQUENCY — once per user pause, never per frame): sample
    // process resource counters so a monotonic climb across edit→play→pause cycles localises
    // the reported "改多了就掉帧" leak. Gated on runtime debug output so it only costs the
    // findChildren() walk in --debug / diagnostic builds. See
    // docs/PREVIEW_FRAMEDROP_DIAGNOSIS_AND_FIX_SPEC_ZH.md.
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        // beta7 leak gauge — d_play_kb = private-bytes grown over the playback window (the big
        // previously-unmeasured slice); inflight/inflight_peak = outstanding worker→GUI queued
        // lambdas (≈0 here exonerates async backlog). Then arm the render thread to emit one
        // timeline/leak_gauge line (nodes/tex/d_render) on its next present, correlated by txn.
        const qint64 pausePrivBytes = miacode::debug_log::processPrivateBytes();
        const qint64 playStartPrivBytes = miacode::debug_log::leak_gauge::playStartPrivateBytes();
        const qint64 dPlayKb = (pausePrivBytes >= 0 && playStartPrivBytes >= 0)
            ? (pausePrivBytes - playStartPrivBytes) / 1024
            : 0;
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview/resource_gauge"),
            QStringLiteral("reason=pause_exact txn=%1 qobject_descendants=%2 d_play_kb=%3 "
                           "inflight=%4 inflight_peak=%5 presents_in_play=%6 %7 %8")
                .arg(playbackTxn)
                .arg(static_cast<qint64>(owner_.findChildren<QObject*>().size()))
                .arg(dPlayKb)
                .arg(miacode::debug_log::leak_gauge::inflightDepth())
                .arg(miacode::debug_log::leak_gauge::inflightPeak())
                .arg(miacode::debug_log::leak_gauge::timelinePresentsSincePlayStart())
                .arg(miacode::debug_log::processResourceGaugePayload())
                .arg(state_.previewCanvas_ != nullptr
                         ? state_.previewCanvas_->resourceGaugePayload()
                         : QStringLiteral("scene_revision=-")));
        miacode::debug_log::leak_gauge::armRenderSample(pausePrivBytes, playbackTxn);
    }
    scheduleDeferredPreviewUiTail(
        true,
        true,
        true,
        false,
        false,
        true,
        state_.qtPreviewPauseSecond_,
        true,
        state_.previewFollowEnabled_);
}

void MainWindow::TimelineSection::pauseQtPreviewPlaybackForReanchor()
{
    const bool wasPlaying = state_.qtPreviewPlaying_;
    const quint64 playbackTxn = state_.activePreviewPlaybackTransactionId_;
    if (!wasPlaying || state_.previewSfxRuntime_ == nullptr) {
        stopQtPreviewPlayback(true);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    // G1 Commit 6: pause-second from wall-clock master; see pauseQtPreviewPlaybackExact
    // for rationale.
    const double wallClockPauseSecond = owner_.currentPreviewAuthoritativeAudioClockSecond();
    const QtPreviewSfxRuntime::PausePreviewResult pauseResult =
        state_.previewSfxRuntime_->pausePreviewPlaybackTransaction();
    state_.qtPreviewPauseSecond_ = wallClockPauseSecond;
    cancelPreviewStartupSync();
    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = state_.qtPreviewPauseSecond_;
    // Spec: pause-for-reanchor lands in 暂停-R, so request R-centring.
    state_.qtPreviewPendingTimelineCenterView_ = true;
    state_.qtPreviewTimelineDirty_ = true;
    state_.qtPreviewPlaying_ = false;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(false);
    }
    invalidatePreviewFollowBindingCache();
    state_.activePreviewPlaybackTransactionId_ = 0;
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    state_.qtPreviewDisplayRefreshTickQueued_ = false;
    state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    state_.qtPreviewFixedAwaitingFrame_ = false;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    state_.qtPreviewFixedFrameTickQueued_ = false;
    state_.qtPreviewLastVisualTickNs_ = -1;
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    appendPreviewPlaybackLog(
        QStringLiteral("pause_for_reanchor"),
        QString("txn=%1 pause_second=%2 retained_mode=%3 elapsed_ms=%4")
            .arg(playbackTxn)
            .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6)
            .arg(static_cast<int>(pauseResult.retainedMode))
            .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3));
}

void MainWindow::TimelineSection::softStopQtPreviewPlaybackToSecond(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        stopQtPreviewPlayback(true);
    } else if (state_.qtPreviewPlaying_) {
        pauseQtPreviewPlaybackExact();
    }
    anchorQtPreviewPlaybackToSecond(clampedSecond, centerView);
}

void MainWindow::TimelineSection::anchorQtPreviewPlaybackToSecond(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    owner_.ensurePreviewStageMediaRouteInitialized();
    cancelPreviewStartupSync();
    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    state_.qtPreviewPauseSecond_ = clampedSecond;
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = clampedSecond;
    state_.qtPreviewPendingTimelineCenterView_ = centerView;
    state_.qtPreviewTimelineDirty_ = true;
    state_.qtPreviewPlaying_ = false;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(false);
    }
    invalidatePreviewFollowBindingCache();
    state_.activePreviewPlaybackTransactionId_ = 0;
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    state_.qtPreviewDisplayRefreshTickQueued_ = false;
    state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    state_.qtPreviewFixedAwaitingFrame_ = false;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    state_.qtPreviewFixedFrameTickQueued_ = false;
    state_.qtPreviewLastVisualTickNs_ = -1;
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    requestPausedPreviewSeek(clampedSecond, centerView, true);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->resetRetainedPreviewPlaybackTransaction(clampedSecond);
    }
    scheduleDeferredPreviewUiTail(
        true,
        false,
        false,
        false,
        false,
        true,
        clampedSecond,
        true,
        state_.previewFollowEnabled_);
}

bool MainWindow::TimelineSection::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    if (!owner_.preparePreviewStartState()) {
        state_.pendingPreviewPlaybackStart_ = hasActiveDifficulty();
        state_.pendingPreviewPlaybackResumeFromPause_ = resumeFromPause;
        state_.pendingPreviewPlaybackRevision_ = state_.timelineRevision_;
        state_.pendingPreviewPlaybackDifficultyId_ = activeDifficultyId();
        state_.pendingPreviewPlaybackSecond_ = qBound(0.0, second, previewDurationSeconds());
        return false;
    }

    state_.pendingPreviewPlaybackStart_ = false;

    owner_.ensurePreviewStageMediaRouteInitialized();
    owner_.ensurePreviewSfxRuntimePrepared();
    // Re-assert the correct preview levels on EVERY play (fresh or resume).
    // applyPreviewAudioSettingsToRuntime() re-derives them from the current mode:
    // a normal difficulty gets the user's real mix, while the latency page's
    // audition keeps its independent SFX slider — so neither can bleed into the
    // other. resetCursor does not touch the retained transaction, so this is safe
    // on resume.
    owner_.applyPreviewAudioSettingsToRuntime();
    cancelPreviewStartupSync();
    applyLatestTimelinePreviewStateToPausedPreview();
    const double requestedSecond = qBound(0.0, second, previewDurationSeconds());
    if (state_.previewPendingPlayInteractionId_ != 0) {
        appendPreviewInteractionLog(
            QStringLiteral("play_dispatch"),
            QString("op=%1 source=%2 requested_second=%3 resume=%4")
                .arg(state_.previewPendingPlayInteractionId_)
                .arg(state_.previewPendingPlayInteractionSource_)
                .arg(requestedSecond, 0, 'f', 6)
                .arg(resumeFromPause ? 1 : 0));
    }
    const double startSecond = (!resumeFromPause && requestedSecond <= kTimelineZeroSecondTolerance)
        ? previewVisualLeadInStartSecond(
              state_.latestTimelineNoteMarkers_,
              requestedSecond,
              state_.previewTapFlowSpeed_,
              state_.previewTouchFlowSpeed_)
        : requestedSecond;
    const bool hasVideoMedia = owner_.previewStageMediaRouteHasVideo();
    const quint64 playbackTxn = ++state_.previewPlaybackTransactionCounter_;
    state_.activePreviewPlaybackTransactionId_ = playbackTxn;
    const auto applyPlaybackClockState = [this](double initialSecond) {
        state_.qtPreviewStartSecond_ = initialSecond;
        state_.qtPreviewPauseSecond_ = initialSecond;
        state_.qtPreviewLastTimelineSecond_ = initialSecond;
        state_.qtPreviewPendingTimelineSecond_ = initialSecond;
        state_.qtPreviewPendingTimelineCenterView_ = true;
        state_.qtPreviewTimelineDirty_ = false;
        state_.qtPreviewTimelineStartSecond_ = initialSecond;
        state_.qtPreviewFramePacingDiagLastTickNs_ = -1;
        state_.qtPreviewFramePacingDiagLastTickLogMs_ = -1;
        state_.qtPreviewFramePacingDiagLastRequestLogMs_ = -1;
        state_.qtPreviewFramePacingDiagLastPresentLogMs_ = -1;
        state_.qtPreviewFramePacingDiagLastTickSecond_ = -1.0;
        state_.qtPreviewFramePacingDiagLastTickFallbackSecond_ = -1.0;
        state_.qtPreviewFramePacingDiagLastTickAudioSecond_ = -1.0;
        state_.qtPreviewDisplayRefreshFrameRequestSeq_ = 0;
        state_.qtPreviewDisplayRefreshFramePresentSeq_ = 0;
        state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    };

    state_.qtPreviewPlaybackReturnSecond_ = requestedSecond;
    state_.qtPreviewPlaybackEndSecond_ = qMax(0.0, previewPlaybackEndSeconds());
    owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "playback_start_prepare");
    state_.pausedSeekMediaPending_ = false;
    state_.pausedSeekMediaSubmittedGeneration_ = 0;
    state_.pausedSeekMediaAckGeneration_ = 0;
    if (resumeFromPause && state_.previewSfxRuntime_ != nullptr) {
        const QtPreviewSfxRuntime::RetainedPlaybackMode retainedMode =
            state_.previewSfxRuntime_->retainedPlaybackMode();
        if (retainedMode == QtPreviewSfxRuntime::RetainedPlaybackMode::PausedExact
            || retainedMode == QtPreviewSfxRuntime::RetainedPlaybackMode::PausedAnchored) {
            double effectiveStartSecond = 0.0;
            if (retainedMode == QtPreviewSfxRuntime::RetainedPlaybackMode::PausedExact
                && qAbs(state_.previewSfxRuntime_->authoritativePlaybackSecond() - requestedSecond)
                    <= kTimelineZeroSecondTolerance) {
                effectiveStartSecond = state_.previewSfxRuntime_->resumeRetainedPreviewPlaybackTransaction();
            } else {
                effectiveStartSecond =
                    state_.previewSfxRuntime_->seekRetainedPreviewPlaybackTransaction(requestedSecond, true);
            }
            applyPlaybackClockState(effectiveStartSecond);
            state_.qtPreviewPendingTimelineSecond_ = effectiveStartSecond;
            state_.qtPreviewPendingTimelineCenterView_ = true;
            state_.qtPreviewTimelineDirty_ = true;
            state_.qtPreviewLastTimelineSecond_ = -1.0;
            if (state_.timelineQuickStateBridge_ != nullptr) {
                state_.timelineQuickStateBridge_->setPlaybackEntrySeconds(state_.qtPreviewPlaybackReturnSecond_);
                state_.timelineQuickStateBridge_->setPlayheadUpperLimitSeconds(state_.qtPreviewPlaybackEndSecond_);
                state_.timelineQuickStateBridge_->setPlayheadSeconds(effectiveStartSecond, false);
            }
            if (state_.previewCanvas_ != nullptr) {
                state_.previewCanvas_->setPlayheadSeconds(effectiveStartSecond, true);
            }
            if (hasVideoMedia) {
                owner_.startPreviewStageMediaRoutePlayback(effectiveStartSecond);
            }
            appendPreviewPlaybackLog(
                QStringLiteral("retained_start"),
                QString("txn=%1 effective=%2 retained_mode=%3")
                    .arg(playbackTxn)
                    .arg(effectiveStartSecond, 0, 'f', 6)
                    .arg(static_cast<int>(retainedMode)));
            finalizeQtPreviewPlaybackStart(effectiveStartSecond);
            return true;
        }
    }
    state_.previewStartupSyncPending_ = true;
    state_.previewLateVideoStartPending_ = false;
    state_.previewStartupAudioPrepared_ = false;
    state_.previewStartupCanvasPresented_ = state_.previewCanvas_ == nullptr;
    state_.previewStartupStrongGroupCommitted_ = false;
    state_.previewStartupResumeFromPause_ = resumeFromPause;
    state_.previewStartupVideoPrepareStarted_ = hasVideoMedia;
    state_.previewStartupVideoPrepared_ = false;
    state_.previewStartupVideoStarted_ = false;
    state_.previewStartupRequestedSecond_ = requestedSecond;
    appendPreviewPlaybackLog(
        QStringLiteral("start_request"),
        QString("txn=%1 requested=%2 resume=%3 rate=%4 has_video=%5 duration=%6")
            .arg(playbackTxn)
            .arg(startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(state_.previewPlaybackRate_, 0, 'f', 3)
            .arg(hasVideoMedia ? 1 : 0)
            .arg(previewDurationSeconds(), 0, 'f', 6));

    double effectiveStartSecond = startSecond;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setPlaybackTransactionId(playbackTxn);
        effectiveStartSecond = state_.previewSfxRuntime_->preparePreviewPlaybackTransaction(
            startSecond,
            resumeFromPause,
            state_.previewPlaybackRate_);
    }
    state_.previewStartupAudioPrepared_ = true;
    state_.previewStartupPreparedSecond_ = effectiveStartSecond;
    appendPreviewPlaybackLog(
        QStringLiteral("audio_prepared"),
        QString("txn=%1 effective=%2")
            .arg(playbackTxn)
            .arg(effectiveStartSecond, 0, 'f', 6));

    applyPlaybackClockState(effectiveStartSecond);
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = effectiveStartSecond;
    state_.qtPreviewPendingTimelineCenterView_ = true;
    state_.qtPreviewTimelineDirty_ = true;
    state_.qtPreviewLastTimelineSecond_ = -1.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setPlaybackEntrySeconds(state_.qtPreviewPlaybackReturnSecond_);
        state_.timelineQuickStateBridge_->setPlayheadUpperLimitSeconds(state_.qtPreviewPlaybackEndSecond_);
        state_.timelineQuickStateBridge_->setPlayheadSeconds(effectiveStartSecond, false);
    }
    if (state_.previewCanvas_ != nullptr) {
        if (!resumeFromPause) {
            state_.previewCanvas_->resetProfilingSession();
        }
        state_.previewCanvas_->setPlayheadSeconds(effectiveStartSecond, true);
    }
    if (hasVideoMedia) {
        if (state_.previewStageMediaHost_ != nullptr) {
            state_.previewStageMediaHost_->setPlaybackTransactionId(playbackTxn);
            state_.previewStageMediaHost_->preparePlaybackStart(effectiveStartSecond, playbackTxn);
        }
        appendPreviewPlaybackLog(
            QStringLiteral("weak_video_prepare_started"),
            QString("txn=%1 second=%2")
                .arg(playbackTxn)
                .arg(effectiveStartSecond, 0, 'f', 6));
    }
    tryCommitPreviewStartupSync();
    return true;
}

void MainWindow::TimelineSection::finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage)
{
    stopQtPreviewPlayback(true);
    if (owner_.statusBar() != nullptr && !statusMessage.isEmpty()) {
        owner_.statusBar()->showMessage(statusMessage);
    }
}

void MainWindow::TimelineSection::stopQtPreviewPlayback(bool keepPosition)
{
    if (state_.exportEffectPreviewPlaybackActive_) {
        stopExportEffectPreviewPlayback();
        return;
    }
    const bool wasPlaying = state_.qtPreviewPlaying_;
    const bool hadStartupSync = state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_;
    const quint64 playbackTxn = state_.activePreviewPlaybackTransactionId_;
    bool pauseSecondCaptured = false;
    if (hadStartupSync && !wasPlaying) {
        state_.qtPreviewPauseSecond_ = state_.previewStartupPreparedSecond_;
        pauseSecondCaptured = true;
    }
    if (!pauseSecondCaptured) {
        state_.qtPreviewPauseSecond_ = owner_.currentPreviewAuthoritativeAudioClockSecond();
        pauseSecondCaptured = true;
    }
    cancelPreviewStartupSync();
    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    if (!keepPosition) {
        state_.qtPreviewPauseSecond_ = 0.0;
    }
    state_.pausedPreviewMediaSeekPending_ = false;
    if (wasPlaying || hadStartupSync) {
        appendPreviewPlaybackLog(
            QStringLiteral("stop"),
            QString("txn=%1 keep_position=%2 pause_second=%3")
                .arg(playbackTxn)
                .arg(keepPosition ? 1 : 0)
                .arg(state_.qtPreviewPauseSecond_, 0, 'f', 6));
        state_.qtPreviewPendingTimelineSecond_ = state_.qtPreviewPauseSecond_;
        // Spec: 任意 → 点击停止 → 停止位 (= 暂停-R 且 R = L), Timeline 聚焦 R.
        // Stop must request R-centring so the timeline jumps back to
        // L=R = the playback entry point.
        state_.qtPreviewPendingTimelineCenterView_ = true;
        state_.qtPreviewTimelineDirty_ = true;
    }
    state_.qtPreviewPlaying_ = false;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(false);
    }
    invalidatePreviewFollowBindingCache();
    state_.activePreviewPlaybackTransactionId_ = 0;
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    state_.qtPreviewDisplayRefreshTickQueued_ = false;
    state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    state_.qtPreviewFixedAwaitingFrame_ = false;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    state_.qtPreviewFixedFrameTickQueued_ = false;
    state_.qtPreviewLastVisualTickNs_ = -1;
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    resetVisualClockSmoothing();
    requestPausedPreviewSeek(state_.qtPreviewPauseSecond_, false, true);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->resetRetainedPreviewPlaybackTransaction(state_.qtPreviewPauseSecond_);
    }
    updatePreviewSliderPosition(state_.qtPreviewPauseSecond_);
    scheduleDeferredPreviewUiTail(
        true,
        true,
        true,
        wasPlaying,
        true,
        false,
        state_.qtPreviewPauseSecond_,
        false,
        false);
}

void MainWindow::TimelineSection::applyQtPreviewPosition(double second, bool centerView)
{
    const bool quickTimelineBridgeReady =
        !state_.quickShellUiFocusBridgeMode_ || state_.quickTimelineSurfaceReady_;
    const double timelineCadenceSeconds =
        static_cast<double>(qMax<qint64>(1, timelineTargetFrameIntervalNs())) / 1000000000.0;
    state_.qtPreviewPauseSecond_ = second;
    const bool timelineShouldCenter = centerView && (!state_.qtPreviewPlaying_ || state_.previewProgressFollowEnabled_);
    if (!state_.qtPreviewPlaying_
        && state_.timelineQuickStateBridge_ != nullptr
        && (state_.qtPreviewLastTimelineSecond_ < 0.0
            || qAbs(second - state_.qtPreviewLastTimelineSecond_) >= timelineCadenceSeconds)) {
        state_.qtPreviewPendingTimelineSecond_ = second;
        state_.qtPreviewPendingTimelineCenterView_ = state_.qtPreviewPendingTimelineCenterView_ || timelineShouldCenter;
        state_.qtPreviewTimelineDirty_ = true;
        if (quickTimelineBridgeReady) {
            flushQtPreviewTimelinePosition();
        }
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setPlayheadSeconds(second, !state_.qtPreviewPlaying_);
    }
    owner_.setPreviewStageMediaRouteObservedPlayheadSecond(second);
    const bool suppressPausedSecondaryUi =
        !state_.qtPreviewPlaying_ && state_.suppressPausedPreviewSecondaryUiUpdates_;
    // During playback the media-host's syncPlayback already emits diagnosticsChanged on actual
    // state transitions, which runs refreshPreviewStageMediaRouteDebugState via its connected
    // lambda. Calling the refresh again here would duplicate the work on every tick.
    // For paused state, keep calling it so UI reflects changes while the user seeks.
    if (!suppressPausedSecondaryUi && !state_.qtPreviewPlaying_) {
        owner_.refreshPreviewStageMediaRouteDebugState(true);
    }
    updatePreviewSliderPosition(second);
    if (!state_.qtPreviewPlaying_ && !suppressPausedSecondaryUi) {
        updatePreviewObjectStats(second);
    }
    if (state_.previewFollowEnabled_) {
        if (state_.qtPreviewPlaying_) {
            // Editor cursor now self-updates from the playback clock during playback, so
            // syncing it here every tick would be redundant ~0.5-2ms of per-frame work that
            // directly eats into the 16.67ms vsync budget. The paused branch below is kept
            // because the editor only has a clock to sample from during active playback.
        } else if (!suppressPausedSecondaryUi) {
            updatePreviewFollowDecorationForTimelineBlueLine(second, true);
        }
    }
}

namespace {

double exportEffectSceneSecond(
    bool fullRange,
    double outputSecond,
    double leadInSeconds,
    double timelineOriginSecond,
    double segmentStartSecond)
{
    if (fullRange) {
        return timelineOriginSecond + outputSecond;
    }
    if (outputSecond < leadInSeconds) {
        return segmentStartSecond;
    }
    return segmentStartSecond + qMax(0.0, outputSecond - leadInSeconds);
}

QVariantMap introBannerTrackMap(const IntroBannerSpec& intro)
{
    QVariantMap track;
    track.insert(QStringLiteral("title"), intro.title);
    track.insert(QStringLiteral("artist"), intro.artist);
    track.insert(QStringLiteral("designer"), intro.designer);
    track.insert(QStringLiteral("level"), intro.level);
    track.insert(QStringLiteral("difficulty"), intro.difficulty);
    track.insert(QStringLiteral("bpm"), intro.bpm);
    track.insert(QStringLiteral("mode"), intro.mode);
    track.insert(QStringLiteral("lvRenderMode"), intro.lvRenderMode);
    return track;
}

QVariantMap introBannerTemplateMap()
{
    QVariantMap templateMap;
    QFile templateFile(QStringLiteral(":/intro/templates/maimai_banner.json"));
    if (templateFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(templateFile.readAll());
        if (doc.isObject()) {
            templateMap = doc.object().toVariantMap();
        }
    }
    return templateMap;
}

bool exportEffectBgmShouldBeAudible(
    const miacode::video_export::VideoExportAudioRenderPlan& plan,
    double outputSecond)
{
    return plan.backgroundTrack.enabled
        && outputSecond + kTimelineZeroSecondTolerance >= plan.backgroundTrack.mixStartSecond;
}

bool exportEffectMediaShouldPlay(bool fullRange, double outputSecond, double sceneSecond, double leadInSeconds)
{
    return fullRange
        ? sceneSecond >= -kTimelineZeroSecondTolerance
        : outputSecond + kTimelineZeroSecondTolerance >= leadInSeconds;
}

QString exportEffectSfxAssetKind(const miacode::video_export::ScheduledSfxPlaybackRenderPlan& playback)
{
    const QString assetKind = previewSfxNormalizedKind(playback.assetKind);
    if (!assetKind.isEmpty()) {
        return assetKind;
    }
    return previewSfxNormalizedKind(playback.kind);
}

}  // namespace

bool MainWindow::TimelineSection::startExportEffectPreviewPlayback(
    const VideoExportTask& task,
    double outputSecond,
    QString* errorMessage)
{
    if (!owner_.preparePreviewStartState()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Preview is not ready.");
        }
        return false;
    }

    miacode::video_export::VideoExportAudioRenderPlan plan;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, errorMessage)) {
        return false;
    }

    if (state_.qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
    }
    owner_.ensurePreviewStageMediaRouteInitialized();
    owner_.ensurePreviewSfxRuntimePrepared();
    owner_.applyPreviewAudioSettingsToRuntime();
    cancelPreviewStartupSync();
    applyLatestTimelinePreviewStateToPausedPreview();

    state_.exportEffectPreviewAudioPlan_ = plan;
    state_.exportEffectPreviewPlaybackActive_ = true;
    state_.exportEffectPreviewPlaybackFullRange_ = task.fullRangeExport;
    state_.exportEffectPreviewIntroEnabled_ = task.fullRangeExport && task.intro.enabled && plan.introLeadSeconds > 0.0;
    state_.exportEffectPreviewPlaybackTotalSeconds_ = qMax(0.0, plan.totalSeconds);
    state_.exportEffectPreviewPlaybackLeadInSeconds_ = qMax(0.0, plan.leadInSeconds);
    state_.exportEffectPreviewPlaybackTimelineOriginSecond_ = plan.timelineOriginSecond;
    state_.exportEffectPreviewPlaybackSegmentStartSecond_ = plan.segmentStartSecond;
    state_.exportEffectPreviewBgmStarted_ = false;
    state_.exportEffectPreviewIntroSoundPlayed_ = false;
    state_.exportEffectPreviewMediaPlaybackStarted_ = false;
    state_.exportEffectPreviewMediaFrozen_ = false;
    state_.exportEffectPreviewNextSfxIndex_ = 0;
    state_.exportEffectPreviewFps_ = qMax(1, task.fps);
    updatePreviewSliderRange();

    const double clampedOutput = qBound(0.0, outputSecond, state_.exportEffectPreviewPlaybackTotalSeconds_);
    state_.qtPreviewStartSecond_ = clampedOutput;
    state_.qtPreviewPauseSecond_ = clampedOutput;
    state_.qtPreviewPlaybackReturnSecond_ = clampedOutput;
    state_.qtPreviewPlaybackEndSecond_ = state_.exportEffectPreviewPlaybackTotalSeconds_;
    state_.qtPreviewLastTimelineSecond_ = -1.0;
    state_.qtPreviewPlaying_ = true;
    state_.qtPreviewElapsed_.restart();
    state_.qtPreviewTimelineElapsed_.restart();
    state_.activePreviewPlaybackTransactionId_ = ++state_.previewPlaybackTransactionCounter_;
    state_.exportEffectPreviewIntroSoundPlayed_ = clampedOutput > kTimelineZeroSecondTolerance;

    const double sceneSecond = exportEffectSceneSecond(
        state_.exportEffectPreviewPlaybackFullRange_,
        clampedOutput,
        state_.exportEffectPreviewPlaybackLeadInSeconds_,
        state_.exportEffectPreviewPlaybackTimelineOriginSecond_,
        state_.exportEffectPreviewPlaybackSegmentStartSecond_);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setPlaybackTransactionId(state_.activePreviewPlaybackTransactionId_);
        state_.previewSfxRuntime_->resetCursor(sceneSecond, true);
        if (exportEffectBgmShouldBeAudible(state_.exportEffectPreviewAudioPlan_, clampedOutput)) {
            state_.previewSfxRuntime_->startBackgroundTrack(sceneSecond);
            state_.exportEffectPreviewBgmStarted_ = true;
        } else {
            state_.previewSfxRuntime_->pauseBackgroundTrack();
        }
    }
    if (owner_.previewStageMediaRouteHasVideo()) {
        if (exportEffectMediaShouldPlay(
                state_.exportEffectPreviewPlaybackFullRange_,
                clampedOutput,
                sceneSecond,
                state_.exportEffectPreviewPlaybackLeadInSeconds_)) {
            owner_.startPreviewStageMediaRoutePlayback(sceneSecond);
            state_.exportEffectPreviewMediaPlaybackStarted_ = true;
        } else {
            owner_.seekPreviewStageMediaRouteWhilePaused(qMax(0.0, sceneSecond));
            state_.exportEffectPreviewMediaFrozen_ = true;
        }
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(true);
        if (state_.exportEffectPreviewIntroEnabled_) {
            const QUrl jacketUrl = task.intro.jacketPath.isEmpty()
                ? QUrl()
                : QUrl::fromLocalFile(task.intro.jacketPath);
            state_.previewCanvas_->setIntroOverlayData(
                introBannerTrackMap(task.intro),
                introBannerTemplateMap(),
                jacketUrl,
                QUrl(QString::fromLatin1(miacode::intro::kLogoFallbackUrl)));
        } else {
            state_.previewCanvas_->clearIntroOverlay(false);
        }
    }
    if (state_.exportEffectPreviewIntroEnabled_ && clampedOutput <= kTimelineZeroSecondTolerance) {
        playExportEffectPreviewIntroSound();
    }
    applyExportEffectPreviewPosition(clampedOutput);
    resetVisualClockSmoothing();
    owner_.setPreviewFixedTimerHighResolutionActive(!previewCanvasUsesFrameSwappedPacing());
    if (state_.previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        requestNextFixedIntervalPreviewFrame();
    }
    if (ui_.qtPreviewTimelineTimer_ != nullptr && !ui_.qtPreviewTimelineTimer_->isActive()) {
        ui_.qtPreviewTimelineTimer_->start();
    }
    if (ui_.previewStatsUiTimer_ != nullptr && !ui_.previewStatsUiTimer_->isActive()) {
        ui_.previewStatsUiTimer_->start();
    }
    return true;
}

void MainWindow::TimelineSection::seekExportEffectPreviewPlayback(double outputSecond)
{
    if (!state_.exportEffectPreviewPlaybackActive_) {
        return;
    }
    const double clampedOutput = qBound(0.0, outputSecond, state_.exportEffectPreviewPlaybackTotalSeconds_);
    state_.qtPreviewStartSecond_ = clampedOutput;
    state_.qtPreviewPauseSecond_ = clampedOutput;
    state_.qtPreviewElapsed_.restart();
    state_.exportEffectPreviewNextSfxIndex_ = 0;
    state_.exportEffectPreviewIntroSoundPlayed_ = clampedOutput > kTimelineZeroSecondTolerance;
    state_.exportEffectPreviewMediaPlaybackStarted_ = false;
    state_.exportEffectPreviewMediaFrozen_ = false;
    while (state_.exportEffectPreviewNextSfxIndex_ < state_.exportEffectPreviewAudioPlan_.scheduledSfxPlaybacks.size()
           && state_.exportEffectPreviewAudioPlan_.scheduledSfxPlaybacks.at(state_.exportEffectPreviewNextSfxIndex_).mixSecond
              < clampedOutput) {
        ++state_.exportEffectPreviewNextSfxIndex_;
    }
    const double sceneSecond = exportEffectSceneSecond(
        state_.exportEffectPreviewPlaybackFullRange_,
        clampedOutput,
        state_.exportEffectPreviewPlaybackLeadInSeconds_,
        state_.exportEffectPreviewPlaybackTimelineOriginSecond_,
        state_.exportEffectPreviewPlaybackSegmentStartSecond_);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->resetCursor(sceneSecond, true);
        if (exportEffectBgmShouldBeAudible(state_.exportEffectPreviewAudioPlan_, clampedOutput)) {
            state_.previewSfxRuntime_->startBackgroundTrack(sceneSecond);
            state_.exportEffectPreviewBgmStarted_ = true;
        } else {
            state_.previewSfxRuntime_->pauseBackgroundTrack();
            state_.exportEffectPreviewBgmStarted_ = false;
        }
    }
    if (state_.exportEffectPreviewIntroEnabled_
        && !state_.exportEffectPreviewIntroSoundPlayed_
        && clampedOutput <= kTimelineZeroSecondTolerance) {
        playExportEffectPreviewIntroSound();
    }
    if (owner_.previewStageMediaRouteHasVideo()) {
        if (exportEffectMediaShouldPlay(
                state_.exportEffectPreviewPlaybackFullRange_,
                clampedOutput,
                sceneSecond,
                state_.exportEffectPreviewPlaybackLeadInSeconds_)) {
            owner_.startPreviewStageMediaRoutePlayback(sceneSecond);
            state_.exportEffectPreviewMediaPlaybackStarted_ = true;
        } else {
            owner_.seekPreviewStageMediaRouteWhilePaused(qMax(0.0, sceneSecond));
            state_.exportEffectPreviewMediaFrozen_ = true;
        }
    }
    applyExportEffectPreviewPosition(clampedOutput);
}

void MainWindow::TimelineSection::stopExportEffectPreviewPlayback()
{
    if (!state_.exportEffectPreviewPlaybackActive_) {
        return;
    }
    state_.exportEffectPreviewPlaybackActive_ = false;
    state_.exportEffectPreviewBgmStarted_ = false;
    state_.exportEffectPreviewIntroEnabled_ = false;
    state_.exportEffectPreviewIntroSoundPlayed_ = false;
    state_.exportEffectPreviewMediaPlaybackStarted_ = false;
    state_.exportEffectPreviewMediaFrozen_ = false;
    state_.exportEffectPreviewNextSfxIndex_ = 0;
    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->pausePreviewPlaybackTransaction();
        state_.previewSfxRuntime_->resetRetainedPreviewPlaybackTransaction(state_.qtPreviewPauseSecond_);
    }
    state_.qtPreviewPlaying_ = false;
    state_.qtPreviewPauseSecond_ = qBound(0.0, state_.qtPreviewPauseSecond_, previewDurationSeconds());
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->clearHudPlayheadSecondsOverride(true);
        state_.previewCanvas_->clearIntroOverlay(false);
        state_.previewCanvas_->setActivePlaybackProfilingEnabled(false);
    }
    updatePreviewSliderRange();
    updatePreviewSliderPosition(state_.qtPreviewPauseSecond_);
    owner_.updatePauseButtonAppearance();
}

void MainWindow::TimelineSection::playExportEffectPreviewIntroSound()
{
    if (!state_.exportEffectPreviewIntroEnabled_ || state_.exportEffectPreviewIntroSoundPlayed_) {
        return;
    }
    const bool played = state_.previewSfxRuntime_ != nullptr
        && state_.previewSfxRuntime_->audition(QStringLiteral("intro_start"), 1.0);
    if (!played) {
        appendPreviewPlaybackLog(QStringLiteral("export_effect_intro_sfx_play_failed"));
    }
    state_.exportEffectPreviewIntroSoundPlayed_ = true;
}

bool MainWindow::TimelineSection::isExportEffectPreviewPlaybackActive() const
{
    return state_.exportEffectPreviewPlaybackActive_ && state_.qtPreviewPlaying_;
}

double MainWindow::TimelineSection::currentExportEffectPreviewOutputSecond() const
{
    if (!state_.exportEffectPreviewPlaybackActive_) {
        return 0.0;
    }
    return qBound(0.0, state_.qtPreviewPauseSecond_, state_.exportEffectPreviewPlaybackTotalSeconds_);
}

double MainWindow::TimelineSection::exportEffectPreviewOutputSecondFromDisplaySecond(double displaySecond) const
{
    if (!state_.exportEffectPreviewPlaybackActive_) {
        return qBound(0.0, displaySecond, previewDurationSeconds());
    }
    const double outputSecond = state_.exportEffectPreviewPlaybackFullRange_
        ? displaySecond - state_.exportEffectPreviewPlaybackTimelineOriginSecond_
        : displaySecond;
    return qBound(0.0, outputSecond, state_.exportEffectPreviewPlaybackTotalSeconds_);
}

void MainWindow::TimelineSection::applyExportEffectPreviewPosition(double outputSecond)
{
    const double clampedOutput = qBound(0.0, outputSecond, state_.exportEffectPreviewPlaybackTotalSeconds_);
    const double sceneSecond = exportEffectSceneSecond(
        state_.exportEffectPreviewPlaybackFullRange_,
        clampedOutput,
        state_.exportEffectPreviewPlaybackLeadInSeconds_,
        state_.exportEffectPreviewPlaybackTimelineOriginSecond_,
        state_.exportEffectPreviewPlaybackSegmentStartSecond_);
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setPlayheadSeconds(sceneSecond, false);
        state_.previewCanvas_->setHudPlayheadSecondsOverride(sceneSecond, false);
        if (state_.exportEffectPreviewIntroEnabled_
            && clampedOutput < state_.exportEffectPreviewAudioPlan_.introLeadSeconds) {
            const int outputFrame = qMax(0, qFloor(clampedOutput * state_.exportEffectPreviewFps_));
            const int authoringFrame = miacode::intro::authoringFrameForOutputFrame(
                outputFrame,
                state_.exportEffectPreviewFps_);
            state_.previewCanvas_->setIntroOverlayFrame(authoringFrame, true, false);
        } else if (state_.previewCanvas_->introOverlayActive()) {
            state_.previewCanvas_->clearIntroOverlay(false);
        }
        state_.previewCanvas_->update();
    }
    updatePreviewSliderPosition(clampedOutput);
    owner_.setPreviewStageMediaRouteObservedPlayheadSecond(sceneSecond);
}

void MainWindow::TimelineSection::syncPausedPreviewMediaTimestamps(double second)
{
    owner_.seekPreviewStageMediaRouteWhilePaused(second);
}

void MainWindow::TimelineSection::flushQtPreviewTimelinePosition()
{
    if (state_.qtPreviewPlaying_) {
        if (state_.timelineQuickStateBridge_ == nullptr
            || !quickTimelineBridgeReady()
            || !timelineTabIsForeground()) {
            return;
        }
        const double second = qMax(0.0, owner_.currentPreviewAuthoritativeAudioClockSecond());
        state_.timelineQuickStateBridge_->setPlayheadSeconds(second, state_.previewProgressFollowEnabled_);
        state_.timelineQuickStateBridge_->focusPlayhead(false);
        state_.qtPreviewLastTimelineSecond_ = second;
        // Follow-preview cursor sync runs on the timeline tick (throttled to timeline target
        // FPS, typically 30-60Hz) rather than on the per-frame preview tick. This keeps the
        // editor cursor tracking the playhead without eating into the preview render budget.
        if (state_.previewFollowEnabled_) {
            syncEditorCursorToPreviewSecond(second, state_.previewViewportLockEnabled_, false);
        }
        return;
    }
    if (state_.timelineQuickStateBridge_ == nullptr || !quickTimelineBridgeReady()) {
        return;
    }
    if (!state_.qtPreviewTimelineDirty_) {
        return;
    }
    state_.timelineQuickStateBridge_->setPlayheadSeconds(
        state_.qtPreviewPendingTimelineSecond_,
        state_.qtPreviewPendingTimelineCenterView_);
    state_.timelineQuickStateBridge_->focusPlayhead(state_.qtPreviewPendingTimelineCenterView_);
    state_.qtPreviewLastTimelineSecond_ = state_.qtPreviewPendingTimelineSecond_;
    state_.qtPreviewPendingTimelineCenterView_ = false;
    state_.qtPreviewTimelineDirty_ = false;
}

void MainWindow::TimelineSection::onQtPreviewTick()
{
    if (!state_.qtPreviewPlaying_) {
        return;
    }
    const double elapsedSeconds = static_cast<double>(state_.qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
    const double fallbackSecond = state_.qtPreviewStartSecond_ + (elapsedSeconds * state_.previewPlaybackRate_);
    // G1 Commit 5: the old syncPreviewPlaybackClockTransaction call is gone. Its three
    // side effects are now driven directly off wall-clock chart-second:
    //   * SFX drain — handled by drainEvents() inside onQtPreviewTickAtSecond.
    //   * Pending-BGM-start (BGM with positive offset) — handled by syncBackgroundTrack
    //     called below; it forwards to maybeStartPendingBackgroundTrack on the backend.
    //   * BASS_SYNC_POS re-arming — retired (see armNextGroupSync, which is now an
    //     early-return no-op pending Commit 7's full deletion). With wall-clock-driven
    //     drainEvents, the BASS SYNC chain can only produce duplicate triggers and
    //     buys nothing.
    if (state_.previewSfxRuntime_ != nullptr) {
        if (!state_.exportEffectPreviewPlaybackActive_) {
            state_.previewSfxRuntime_->syncBackgroundTrack(fallbackSecond);
        }
    }
    const double second = fallbackSecond;
    const bool hasAudioClock = false;
    onQtPreviewTickAtSecond(second, fallbackSecond, hasAudioClock);
}

double MainWindow::TimelineSection::applyVisualClockSmoothing(
    double audioSecond, double fallbackSecond, bool hasAudioClock)
{
    Q_UNUSED(fallbackSecond);
    Q_UNUSED(hasAudioClock);
    // G1 Commit 4: smoothing collapsed to pass-through.
    //
    // The pre-G1 implementation existed to absorb jitter in the BASS-master-mixer cursor
    // (~50-100ms stalls from DXGI back-pressure, tempo-stream stalls, buffer underrun).
    // With wall-clock now the master timeline (`qtPreviewElapsed_`), the input here is
    // monotonic and rate-correct by construction — there is nothing to smooth.
    //
    // What's preserved: the lookahead-vsync shift. That compensates for GPU pipeline
    // latency (GUI → render → composite → present takes 1-2 vsyncs after the tick that
    // samples chart-second) and is independent of the audio backend, so it survives the
    // clock flip. State variables are still maintained so debug overlays and the
    // smoothing-enabled toggle continue to work without dangling references.
    //
    // See docs/PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §3.6, §5.3, §6.1 step 4.
    const qint64 targetIntervalNs = qMax<qint64>(1, previewCanvasTargetFrameIntervalNs());
    const double playbackRate = qMax(0.0, state_.previewPlaybackRate_);
    const double targetStepSeconds =
        (static_cast<double>(targetIntervalNs) / 1000000000.0) * playbackRate;
    const double lookaheadVsyncs = miacode::debug_options::previewVisualLookaheadVsyncs();
    const double lookaheadSeconds = targetStepSeconds * lookaheadVsyncs;

    state_.qtPreviewVisualClockSecond_ = audioSecond;
    state_.qtPreviewVisualClockLastAudioSecond_ = audioSecond;
    state_.qtPreviewVisualClockInitialized_ = true;
    return audioSecond + lookaheadSeconds;
}

void MainWindow::TimelineSection::resetVisualClockSmoothing()
{
    // Called on playback start, resume, and seek so the next tick hard-syncs visual to audio.
    state_.qtPreviewVisualClockSecond_ = -1.0;
    state_.qtPreviewVisualClockLastAudioSecond_ = -1.0;
    state_.qtPreviewVisualClockInitialized_ = false;
    state_.qtPreviewVisualClockDiagLastLogMs_ = -1;
}

void MainWindow::TimelineSection::onQtPreviewTickAtSecond(double second, double fallbackSecond, bool hasAudioClock)
{
    if (!state_.qtPreviewPlaying_) {
        return;
    }
    if (state_.exportEffectPreviewPlaybackActive_) {
        Q_UNUSED(fallbackSecond);
        Q_UNUSED(hasAudioClock);
        const double outputSecond = qBound(0.0, second, state_.exportEffectPreviewPlaybackTotalSeconds_);
        if (state_.exportEffectPreviewPlaybackTotalSeconds_ > 0.0
            && outputSecond + kTimelineZeroSecondTolerance >= state_.exportEffectPreviewPlaybackTotalSeconds_) {
            state_.qtPreviewPauseSecond_ = state_.exportEffectPreviewPlaybackTotalSeconds_;
            applyExportEffectPreviewPosition(state_.qtPreviewPauseSecond_);
            stopExportEffectPreviewPlayback();
            return;
        }
        const double sceneSecond = exportEffectSceneSecond(
            state_.exportEffectPreviewPlaybackFullRange_,
            outputSecond,
            state_.exportEffectPreviewPlaybackLeadInSeconds_,
            state_.exportEffectPreviewPlaybackTimelineOriginSecond_,
            state_.exportEffectPreviewPlaybackSegmentStartSecond_);
        if (owner_.previewStageMediaRouteHasVideo()) {
            const bool mediaShouldPlay = exportEffectMediaShouldPlay(
                state_.exportEffectPreviewPlaybackFullRange_,
                outputSecond,
                sceneSecond,
                state_.exportEffectPreviewPlaybackLeadInSeconds_);
            if (mediaShouldPlay) {
                if (!state_.exportEffectPreviewMediaPlaybackStarted_) {
                    owner_.startPreviewStageMediaRoutePlayback(sceneSecond);
                    state_.exportEffectPreviewMediaPlaybackStarted_ = true;
                } else {
                    owner_.syncPreviewStageMediaRoutePlayback(sceneSecond);
                }
                state_.exportEffectPreviewMediaFrozen_ = false;
            } else if (!state_.exportEffectPreviewMediaFrozen_) {
                owner_.seekPreviewStageMediaRouteWhilePaused(qMax(0.0, sceneSecond));
                state_.exportEffectPreviewMediaFrozen_ = true;
                state_.exportEffectPreviewMediaPlaybackStarted_ = false;
            }
        }
        applyExportEffectPreviewPosition(outputSecond);
        if (state_.exportEffectPreviewIntroEnabled_
            && !state_.exportEffectPreviewIntroSoundPlayed_
            && outputSecond < state_.exportEffectPreviewAudioPlan_.introLeadSeconds) {
            playExportEffectPreviewIntroSound();
        }
        if (state_.previewSfxRuntime_ != nullptr) {
            if (!exportEffectBgmShouldBeAudible(state_.exportEffectPreviewAudioPlan_, outputSecond)) {
                if (state_.exportEffectPreviewBgmStarted_) {
                    state_.previewSfxRuntime_->pauseBackgroundTrack();
                    state_.exportEffectPreviewBgmStarted_ = false;
                }
            } else if (!state_.exportEffectPreviewBgmStarted_) {
                state_.previewSfxRuntime_->startBackgroundTrack(sceneSecond);
                state_.exportEffectPreviewBgmStarted_ = true;
            } else {
                state_.previewSfxRuntime_->syncBackgroundTrack(sceneSecond);
            }
            while (state_.exportEffectPreviewNextSfxIndex_ < state_.exportEffectPreviewAudioPlan_.scheduledSfxPlaybacks.size()) {
                const auto& playback =
                    state_.exportEffectPreviewAudioPlan_.scheduledSfxPlaybacks.at(state_.exportEffectPreviewNextSfxIndex_);
                if (playback.mixSecond > outputSecond + kTimelineZeroSecondTolerance) {
                    break;
                }
                const QString assetKind = exportEffectSfxAssetKind(playback);
                if (!assetKind.isEmpty() && playback.gain > 0.0) {
                    const double runtimeBaseVolume =
                        previewSfxVolumeForKind(state_.previewAudioSettings_, assetKind);
                    if (runtimeBaseVolume <= 0.0) {
                        ++state_.exportEffectPreviewNextSfxIndex_;
                        continue;
                    }
                    const double auditionGain = qMax(0.0, playback.gain / runtimeBaseVolume);
                    const bool played = state_.previewSfxRuntime_->audition(assetKind, auditionGain);
                    if (!played) {
                        appendPreviewPlaybackLog(
                            QStringLiteral("export_effect_sfx_play_failed"),
                            QStringLiteral("kind=%1 asset=%2 mix=%3 plan_gain=%4 audition_gain=%5")
                                .arg(playback.kind)
                                .arg(assetKind)
                                .arg(playback.mixSecond, 0, 'f', 6)
                                .arg(playback.gain, 0, 'f', 6)
                                .arg(auditionGain, 0, 'f', 6));
                    }
                }
                ++state_.exportEffectPreviewNextSfxIndex_;
            }
        }
        if (state_.previewCanvas_ != nullptr) {
            state_.previewCanvas_->noteTickForProfiling();
        }
        state_.qtPreviewPauseSecond_ = outputSecond;
        if (previewCanvasUsesFrameSwappedPacing()) {
            requestNextDisplayRefreshPreviewFrame();
        } else {
            requestNextFixedIntervalPreviewFrame();
        }
        return;
    }
    const bool diagEnabled = miacode::debug_options::previewFramePacingDiagnosticsEnabled();
    QElapsedTimer tickProfileTimer;
    qint64 syncMediaElapsedNs = 0;
    qint64 applyPositionElapsedNs = 0;
    qint64 drainEventsElapsedNs = 0;
    if (diagEnabled) {
        tickProfileTimer.start();
    }
    owner_.syncPreviewStageMediaRoutePlayback(second);
    if (diagEnabled) {
        syncMediaElapsedNs = tickProfileTimer.nsecsElapsed();
    }
    const double playbackEndSecond = previewPlaybackEndSeconds();
    if (playbackEndSecond > 0.0
        && second + kTimelineZeroSecondTolerance >= playbackEndSecond) {
        second = playbackEndSecond;
        applyQtPreviewPosition(second, true);
        if (state_.previewSfxRuntime_ != nullptr) {
            state_.previewSfxRuntime_->drainEvents(second);
        }
        finishQtPreviewPlaybackAndReturnToEntry("Qt preview reached the end of current timeline.");
        return;
    }

    const qint64 tickNowNs = state_.qtPreviewWatchdogElapsed_.nsecsElapsed();
    qint64 wallDeltaNs = 0;
    double playheadDeltaSeconds = 0.0;
    double fallbackDeltaSeconds = 0.0;
    double audioDeltaSeconds = 0.0;
    double expectedDeltaSeconds = 0.0;
    double speedRatio = 0.0;
    if (state_.qtPreviewFramePacingDiagLastTickNs_ >= 0
        && state_.qtPreviewFramePacingDiagLastTickSecond_ >= 0.0) {
        wallDeltaNs = qMax<qint64>(0, tickNowNs - state_.qtPreviewFramePacingDiagLastTickNs_);
        playheadDeltaSeconds = qMax(0.0, second - state_.qtPreviewFramePacingDiagLastTickSecond_);
        expectedDeltaSeconds =
            (static_cast<double>(wallDeltaNs) / 1000000000.0) * qMax(0.0, state_.previewPlaybackRate_);
        if (expectedDeltaSeconds > 1e-6) {
            speedRatio = playheadDeltaSeconds / expectedDeltaSeconds;
        }
        if (state_.qtPreviewFramePacingDiagLastTickFallbackSecond_ >= 0.0) {
            fallbackDeltaSeconds =
                qMax(0.0, fallbackSecond - state_.qtPreviewFramePacingDiagLastTickFallbackSecond_);
        }
        if (hasAudioClock && state_.qtPreviewFramePacingDiagLastTickAudioSecond_ >= 0.0) {
            audioDeltaSeconds =
                qMax(0.0, second - state_.qtPreviewFramePacingDiagLastTickAudioSecond_);
        }
    }
    state_.qtPreviewFramePacingDiagLastTickNs_ = tickNowNs;
    state_.qtPreviewFramePacingDiagLastTickSecond_ = second;
    state_.qtPreviewFramePacingDiagLastTickFallbackSecond_ = fallbackSecond;
    if (hasAudioClock) {
        state_.qtPreviewFramePacingDiagLastTickAudioSecond_ = second;
    } else {
        state_.qtPreviewFramePacingDiagLastTickAudioSecond_ = -1.0;
    }
    const double audioMinusFallbackSeconds = hasAudioClock ? (second - fallbackSecond) : 0.0;
    const bool largeStep = speedRatio > 1.5 && playheadDeltaSeconds > 0.0;
    const bool audioLargeStep =
        hasAudioClock
        && audioDeltaSeconds > 0.0
        && ((expectedDeltaSeconds > 1e-6 && audioDeltaSeconds > expectedDeltaSeconds * 1.5)
            || audioDeltaSeconds > 0.050);
    const qint64 beforeNotesNs = diagEnabled ? tickProfileTimer.nsecsElapsed() : 0;
    qint64 notesElapsedNs = 0;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->notePreviewPacingTick(wallDeltaNs, playheadDeltaSeconds, speedRatio);
        state_.previewCanvas_->notePreviewClockMetrics(
            audioDeltaSeconds,
            playheadDeltaSeconds,
            audioMinusFallbackSeconds,
            hasAudioClock,
            audioLargeStep,
            largeStep
        );
        state_.previewCanvas_->noteTickForProfiling();
    }
    if (diagEnabled) {
        notesElapsedNs = tickProfileTimer.nsecsElapsed() - beforeNotesNs;
    }
    const qint64 beforeApplyNs = diagEnabled ? tickProfileTimer.nsecsElapsed() : 0;
    applyQtPreviewPosition(second, true);
    if (diagEnabled) {
        applyPositionElapsedNs = tickProfileTimer.nsecsElapsed() - beforeApplyNs;
    }
    const qint64 beforeSmoothingNs = diagEnabled ? tickProfileTimer.nsecsElapsed() : 0;
    qint64 smoothingElapsedNs = 0;
    // Visual-time smoothing: override ONLY the canvas scene playhead with a bounded visual time.
    // applyQtPreviewPosition already wrote `second` (audio time) to setPlayheadSeconds; we
    // overwrite it with the smoothed value so that object motion looks steady frame-to-frame
    // even when render-variance makes audio-delta jump between ticks. Everything else (slider,
    // media-host observed time, SFX drain, timeline follow) keeps using audio time so that
    // audio/video/SFX remain tightly aligned with BGM.
    if (state_.previewCanvas_ != nullptr && state_.qtPreviewPlaying_) {
        const double visualSecond = applyVisualClockSmoothing(second, fallbackSecond, hasAudioClock);
        if (qAbs(visualSecond - second) > 1e-9) {
            state_.previewCanvas_->setPlayheadSeconds(visualSecond, false);
        }
    }
    if (diagEnabled) {
        smoothingElapsedNs = tickProfileTimer.nsecsElapsed() - beforeSmoothingNs;
    }
    const qint64 beforeDrainNs = diagEnabled ? tickProfileTimer.nsecsElapsed() : 0;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->drainEvents(second);
    }
    if (diagEnabled) {
        drainEventsElapsedNs = tickProfileTimer.nsecsElapsed() - beforeDrainNs;
    }
    if (diagEnabled && wallDeltaNs > 0) {
        const qint64 totalTickElapsedNs = tickProfileTimer.nsecsElapsed();
        const qint64 nowMs = state_.qtPreviewWatchdogElapsed_.elapsed();
        const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
        // Rate-limit ALL tick log writes to sampleMs cadence (default 1s). Even "anomaly" log
        // events — largeStep / audioLargeStep — used to fire per-tick when audio jitters, and
        // each appendLine call is a GUI-thread mutex+file-write that can stall 0-50ms on slow
        // disk I/O. The classification is kept (still labeled tick_large_step vs tick_sample)
        // so we can see how often anomalies happen, but we only emit one log per sampleMs.
        const bool isAnomaly = largeStep || audioLargeStep;
        if (state_.qtPreviewFramePacingDiagLastTickLogMs_ < 0
            || nowMs - state_.qtPreviewFramePacingDiagLastTickLogMs_ >= sampleMs) {
            state_.qtPreviewFramePacingDiagLastTickLogMs_ = nowMs;
            appendPreviewFramePacingDiagLog(
                isAnomaly ? QStringLiteral("tick_large_step") : QStringLiteral("tick_sample"),
                QStringLiteral(
                    "tick=%1 wall_ms=%2 playhead_delta_ms=%3 speed_ratio=%4 second=%5 mode=%6 "
                    "fallback_delta_ms=%7 audio_delta_ms=%8 audio_minus_fallback_ms=%9 time_authority=%10 "
                    "tick_exec_ms=%11 sync_media_ms=%12 notes_ms=%13 apply_position_ms=%14 smoothing_ms=%15 drain_events_ms=%16"
                )
                    .arg(state_.previewCanvas_ != nullptr ? state_.previewCanvas_->frameState().tickCount : 0)
                    .arg(static_cast<double>(wallDeltaNs) / 1000000.0, 0, 'f', 3)
                    .arg(playheadDeltaSeconds * 1000.0, 0, 'f', 3)
                    .arg(speedRatio, 0, 'f', 4)
                    .arg(second, 0, 'f', 6)
                    .arg(previewCanvasUsesFrameSwappedPacing()
                        ? QStringLiteral("display_refresh")
                        : QStringLiteral("fixed_interval"))
                    .arg(fallbackDeltaSeconds * 1000.0, 0, 'f', 3)
                    .arg(hasAudioClock ? QString::number(audioDeltaSeconds * 1000.0, 'f', 3) : QStringLiteral("na"))
                    .arg(hasAudioClock
                        ? QString::number(audioMinusFallbackSeconds * 1000.0, 'f', 3)
                        : QStringLiteral("na"))
                    .arg(hasAudioClock ? QStringLiteral("audio") : QStringLiteral("elapsed_fallback"))
                    .arg(static_cast<double>(totalTickElapsedNs) / 1000000.0, 0, 'f', 3)
                    .arg(static_cast<double>(syncMediaElapsedNs) / 1000000.0, 0, 'f', 3)
                    .arg(static_cast<double>(notesElapsedNs) / 1000000.0, 0, 'f', 3)
                    .arg(static_cast<double>(applyPositionElapsedNs) / 1000000.0, 0, 'f', 3)
                    .arg(static_cast<double>(smoothingElapsedNs) / 1000000.0, 0, 'f', 3)
                    .arg(static_cast<double>(drainEventsElapsedNs) / 1000000.0, 0, 'f', 3)
            );
        }
    }
    // NOTE: qtPreviewLastVisualTickNs_ is managed by advanceFixedIntervalGateAfterPresent BEFORE
    // the tick runs, using phase-locked advancement (prev + targetInterval). Assigning nowNs
    // here would push the baseline forward by the tick's work latency (2-5ms), causing every
    // other present at 60Hz to miss the gate and halving effective tick rate.
    if (previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        requestNextFixedIntervalPreviewFrame();
    }
}

void MainWindow::TimelineSection::jumpToNearestTimelineNote(double second, int lane)
{
    int line = 1;
    int col = 1;
    if (!resolveNearestTimelineNote(second, lane, &line, &col, nullptr)) {
        owner_.statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    if (!moveEditorCursorToTimelineLocation(line, col, true, true, true, false)) {
        owner_.statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    owner_.statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(qMax(0.0, second), 0, 'f', 3)
            .arg(line)
            .arg(col)
    );
}

void MainWindow::schedulePreviewSeek(double second, bool centerView)
{
    timelineSection_->schedulePreviewSeek(second, centerView);
}

void MainWindow::seekPreviewDiscreteToSecond(double second, bool centerView)
{
    timelineSection_->seekPreviewDiscreteToSecond(second, centerView);
}

void MainWindow::requestPausedPreviewSeek(
    double second,
    bool centerView,
    bool submitMediaImmediately,
    bool logHotPath)
{
    timelineSection_->requestPausedPreviewSeek(second, centerView, submitMediaImmediately, logHotPath);
}

void MainWindow::applyPausedPreviewVisualSecond(double second, bool centerView)
{
    timelineSection_->applyPausedPreviewVisualSecond(second, centerView);
}

void MainWindow::submitPausedMediaSeek(double second, quint64 generation)
{
    timelineSection_->submitPausedMediaSeek(second, generation);
}

void MainWindow::maybeSubmitLatestPausedMediaSeek()
{
    timelineSection_->maybeSubmitLatestPausedMediaSeek();
}

void MainWindow::handlePausedPreviewMediaSeekCompleted(double second, quint64 generation)
{
    timelineSection_->handlePausedPreviewMediaSeekCompleted(second, generation);
}

bool MainWindow::stepPreviewBySeconds(double deltaSeconds, bool centerView)
{
    return timelineSection_->stepPreviewBySeconds(deltaSeconds, centerView);
}

bool MainWindow::handlePreviewSeekWheel(QWheelEvent* event)
{
    return timelineSection_->handlePreviewSeekWheel(event);
}

void MainWindow::beginPreviewHeldSeek(int direction, int key)
{
    timelineSection_->beginPreviewHeldSeek(direction, key);
}

void MainWindow::stopPreviewHeldSeek(int key)
{
    timelineSection_->stopPreviewHeldSeek(key);
}

void MainWindow::applyPreviewHeldSeekTick()
{
    timelineSection_->applyPreviewHeldSeekTick();
}

void MainWindow::seekPreviewToSecond(double second, bool centerView)
{
    timelineSection_->seekPreviewToSecond(second, centerView);
}

void MainWindow::applyPreviewPlaybackRate(double rate)
{
    timelineSection_->applyPreviewPlaybackRate(rate);
}

QString MainWindow::formatPreviewPlaybackRateToastText(double rate) const
{
    const int percent = qRound(rate * 100.0);
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("当前倍速")
        : QStringLiteral("Playback Speed");
    return QStringLiteral(
               "<div style='text-align:center;'>"
               "<div style='font-size:14px;font-weight:600;line-height:1.2;'>%1</div>"
               "<div style='margin-top:6px;font-size:28px;font-weight:700;line-height:1.1;'>%2%%</div>"
               "</div>"
           )
        .arg(title.toHtmlEscaped())
        .arg(percent);
}

void MainWindow::showPreviewPlaybackRateToast(double rate)
{
    if (previewPlaybackRateToast_ == nullptr || previewPlaybackRateToastLabel_ == nullptr) {
        return;
    }
    if (previewPlaybackRateToastTimer_ != nullptr) {
        previewPlaybackRateToastTimer_->stop();
    }
    if (previewPlaybackRateToastOpacityAnimation_ != nullptr) {
        previewPlaybackRateToastOpacityAnimation_->stop();
    }
    previewPlaybackRateToastLabel_->setText(formatPreviewPlaybackRateToastText(rate));
    updatePreviewPlaybackRateToastGeometry();
    if (previewPlaybackRateToastOpacityEffect_ != nullptr) {
        previewPlaybackRateToastOpacityEffect_->setOpacity(1.0);
    }
    previewPlaybackRateToast_->show();
    previewPlaybackRateToast_->raise();
    if (previewPlaybackRateToastTimer_ != nullptr) {
        previewPlaybackRateToastTimer_->start();
    }
}

void MainWindow::updatePreviewPlaybackRateToastGeometry()
{
    if (previewPlaybackRateToast_ == nullptr || previewPlaybackRateToastLabel_ == nullptr) {
        return;
    }
    const QRect hostRect = contentsRect();
    if (!hostRect.isValid()) {
        return;
    }

    const QSize preferredSize = previewPlaybackRateToast_->sizeHint();
    const int availableWidth = qMax(1, hostRect.width() - kPreviewPlaybackRateToastHorizontalMargin * 2);
    int toastWidth = qMax(kPreviewPlaybackRateToastMinWidth, preferredSize.width());
    toastWidth = qMin(toastWidth, availableWidth);
    const int toastHeight = qMax(kPreviewPlaybackRateToastMinHeight, preferredSize.height());
    const int toastX = hostRect.x() + qMax(0, (hostRect.width() - toastWidth) / 2);
    const int toastY = hostRect.y() + qMax(0, (hostRect.height() - toastHeight) / 2);
    previewPlaybackRateToast_->setGeometry(toastX, toastY, toastWidth, toastHeight);
}

void MainWindow::hidePreviewPlaybackRateToast()
{
    if (previewPlaybackRateToastTimer_ != nullptr) {
        previewPlaybackRateToastTimer_->stop();
    }
    if (previewPlaybackRateToastOpacityAnimation_ != nullptr) {
        previewPlaybackRateToastOpacityAnimation_->stop();
    }
    if (previewPlaybackRateToastOpacityEffect_ != nullptr) {
        previewPlaybackRateToastOpacityEffect_->setOpacity(1.0);
    }
    if (previewPlaybackRateToast_ != nullptr) {
        previewPlaybackRateToast_->hide();
    }
}

bool MainWindow::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    return timelineSection_->startQtPreviewPlayback(second, resumeFromPause);
}

void MainWindow::pauseQtPreviewPlaybackExact()
{
    timelineSection_->pauseQtPreviewPlaybackExact();
}

void MainWindow::finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage)
{
    timelineSection_->finishQtPreviewPlaybackAndReturnToEntry(statusMessage);
}

void MainWindow::stopQtPreviewPlayback(bool keepPosition)
{
    timelineSection_->stopQtPreviewPlayback(keepPosition);
}

void MainWindow::applyQtPreviewPosition(double second, bool centerView)
{
    timelineSection_->applyQtPreviewPosition(second, centerView);
}

bool MainWindow::startExportEffectPreviewPlayback(
    const VideoExportTask& task,
    double outputSecond,
    QString* errorMessage)
{
    return timelineSection_->startExportEffectPreviewPlayback(task, outputSecond, errorMessage);
}

void MainWindow::seekExportEffectPreviewPlayback(double outputSecond)
{
    timelineSection_->seekExportEffectPreviewPlayback(outputSecond);
}

void MainWindow::stopExportEffectPreviewPlayback()
{
    timelineSection_->stopExportEffectPreviewPlayback();
}

bool MainWindow::isExportEffectPreviewPlaybackActive() const
{
    return timelineSection_->isExportEffectPreviewPlaybackActive();
}

double MainWindow::currentExportEffectPreviewOutputSecond() const
{
    return timelineSection_->currentExportEffectPreviewOutputSecond();
}

double MainWindow::exportEffectPreviewOutputSecondFromDisplaySecond(double displaySecond) const
{
    return timelineSection_->exportEffectPreviewOutputSecondFromDisplaySecond(displaySecond);
}

void MainWindow::applyExportEffectPreviewPosition(double outputSecond)
{
    timelineSection_->applyExportEffectPreviewPosition(outputSecond);
}

void MainWindow::syncPausedPreviewMediaTimestamps(double second)
{
    timelineSection_->syncPausedPreviewMediaTimestamps(second);
}

void MainWindow::flushQtPreviewTimelinePosition()
{
    timelineSection_->flushQtPreviewTimelinePosition();
}

void MainWindow::onQtPreviewTick()
{
    timelineSection_->onQtPreviewTick();
}

void MainWindow::jumpToNearestTimelineNote(double second, int lane)
{
    timelineSection_->jumpToNearestTimelineNote(second, lane);
}
