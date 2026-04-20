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
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewOpacityCurves.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/latency/LatencyDetectorDialog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

constexpr double kTimelineZeroSecondTolerance = 1e-6;
constexpr auto kQuickShellTransportSeekProperty = "miacode.quick_shell_transport_seek";

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

double previewVisualLeadInStartSecond(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double requestedSecond,
    double noteFlowSpeed)
{
    if (requestedSecond > kTimelineZeroSecondTolerance || noteMarkers.isEmpty()) {
        return requestedSecond;
    }

    const auto tapTiming = miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(noteFlowSpeed));
    const auto slideTrackTiming =
        miacode::preview::scene::previewSlideTrackTimingForFlowSpeed(static_cast<qreal>(noteFlowSpeed));

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
                earliestSecond = qMin(earliestSecond, marker.second - miacode::preview_gameplay::kTouchDurationSeconds);
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            if (qMax(marker.second, marker.endSecond) + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - miacode::preview_gameplay::kTouchDurationSeconds);
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

double MainWindow::parsedFirstSeconds(bool* ok) const
{
    return timelineSection_->parsedFirstSeconds(ok);
}

double MainWindow::parsedWholeBpm(bool* ok) const
{
    return timelineSection_->parsedWholeBpm(ok);
}

QString MainWindow::parsedLatencyMeterId() const
{
    return timelineSection_->parsedLatencyMeterId();
}

void MainWindow::TimelineSection::schedulePreviewSeek(double second, bool centerView)
{
    requestPausedPreviewSeek(second, centerView, false);
}

void MainWindow::TimelineSection::requestPausedPreviewSeek(
    double second,
    bool centerView,
    bool submitMediaImmediately
)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    const quint64 generation = ++state_.pausedSeekGeneration_;
    state_.pausedSeekTargetSecond_ = clampedSecond;
    state_.previewPendingSeekSecond_ = clampedSecond;
    state_.previewPendingSeekCenterView_ = centerView;
    appendQuickShellBackendLog(
        QStringLiteral("paused_seek_request"),
        QString("generation=%1 second=%2 center=%3 submit_now=%4 media_pending=%5")
            .arg(generation)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
            .arg(submitMediaImmediately ? 1 : 0)
            .arg(state_.pausedSeekMediaPending_ ? 1 : 0)
    );
    applyPausedPreviewVisualSecond(clampedSecond, centerView);
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
    const bool quickTimelineBridgeReady =
        !state_.quickShellUiFocusBridgeMode_ || state_.quickTimelineSurfaceReady_;
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
    if (state_.timelineQuickStateBridge_ != nullptr && quickTimelineBridgeReady) {
        state_.timelineQuickStateBridge_->focusPlayhead(centerView);
    }
    state_.pausedSeekAppliedVisualSecond_ = clampedSecond;
    appendQuickShellBackendLog(
        QStringLiteral("paused_seek_visual_apply"),
        QString("generation=%1 second=%2 center=%3")
            .arg(state_.pausedSeekGeneration_)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
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
    if (state_.qtPreviewPlaying_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        stopQtPreviewPlayback(true);
    }
    requestPausedPreviewSeek(clampedSecond, centerView, true);
}

void MainWindow::TimelineSection::applyPreviewPlaybackRate(double rate)
{
    owner_.ensurePreviewStageMediaRouteInitialized();
    const double clampedRate = qMax(0.25, rate);
    if (qFuzzyCompare(state_.previewPlaybackRate_ + 1.0, clampedRate + 1.0)) {
        return;
    }
    state_.previewPlaybackRate_ = clampedRate;
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
    owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state_.previewPlaybackRate_);
        if (!state_.qtPreviewPlaying_
            && !state_.previewStartupSyncPending_
            && !state_.previewLateVideoStartPending_
            && !state_.latestTimelineNoteMarkers_.isEmpty()) {
            state_.previewSfxRuntime_->applyPausedPreviewState(
                state_.latestTimelineNoteMarkers_,
                false,
                state_.qtPreviewPauseSecond_,
                state_.previewPlaybackRate_,
                state_.previewTimingSettings_);
        }
    }
    if (state_.qtPreviewPlaying_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        stopQtPreviewPlayback(true);
        startQtPreviewPlayback(state_.qtPreviewPauseSecond_, true);
    }
}

double MainWindow::currentPreviewAuthoritativeAudioClockSecond() const
{
    if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
        return previewSfxRuntime_->backgroundPlaybackSecond();
    }
    if (qtPreviewPlaying_) {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        return qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    }
    if (previewStartupSyncPending_ || previewLateVideoStartPending_) {
        return previewStartupPreparedSecond_;
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

    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewElapsed_.restart();
    state_.qtPreviewTimelineElapsed_.restart();
    state_.qtPreviewPlaying_ = true;
    owner_.applyEffectivePreviewOutlineVariantToCanvas();
    owner_.applyPreviewStageMediaRouteVisualSettings();
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    resetQtPreviewFixedFramePacing();
    if (state_.previewCanvas_ != nullptr && !previewCanvasUsesFrameSwappedPacing()) {
        state_.previewCanvas_->update();
    }
    if (state_.previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        scheduleNextQtPreviewTick();
    }
    if (ui_.qtPreviewTimelineTimer_ != nullptr && !ui_.qtPreviewTimelineTimer_->isActive()) {
        ui_.qtPreviewTimelineTimer_->start();
    }
    if (ui_.previewStatsUiTimer_ != nullptr && !ui_.previewStatsUiTimer_->isActive()) {
        ui_.previewStatsUiTimer_->start();
    }
    syncEditorCursorToPreviewSecond(effectiveStartSecond, false);
    updatePreviewSliderPosition(effectiveStartSecond);
    owner_.updatePauseButtonAppearance();
    appendPreviewPlaybackLog(
        QStringLiteral("commit"),
        QString("txn=%1 effective=%2 late_video_pending=%3")
            .arg(state_.activePreviewPlaybackTransactionId_)
            .arg(effectiveStartSecond, 0, 'f', 6)
            .arg(state_.previewLateVideoStartPending_ ? 1 : 0));
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
    cancelPreviewStartupSync();
    applyLatestTimelinePreviewStateToPausedPreview();
    const double requestedSecond = qBound(0.0, second, previewDurationSeconds());
    const double startSecond = (!resumeFromPause && requestedSecond <= kTimelineZeroSecondTolerance)
        ? previewVisualLeadInStartSecond(state_.latestTimelineNoteMarkers_, requestedSecond, state_.previewNoteFlowSpeed_)
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
    };

    state_.qtPreviewPlaybackReturnSecond_ = requestedSecond;
    state_.qtPreviewPlaybackEndSecond_ = qMax(0.0, previewPlaybackEndSeconds());
    owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_);
    state_.pausedSeekMediaPending_ = false;
    state_.pausedSeekMediaSubmittedGeneration_ = 0;
    state_.pausedSeekMediaAckGeneration_ = 0;
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
    const bool wasPlaying = state_.qtPreviewPlaying_;
    const bool hadStartupSync = state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_;
    const quint64 playbackTxn = state_.activePreviewPlaybackTransactionId_;
    bool pauseSecondCaptured = false;
    if (wasPlaying && state_.previewSfxRuntime_ != nullptr) {
        const QtPreviewSfxRuntime::PausePreviewResult pauseResult =
            state_.previewSfxRuntime_->capturePausedPreviewTransaction();
        if (pauseResult.usedBackgroundTrack) {
            state_.qtPreviewPauseSecond_ = pauseResult.pauseSecond;
            pauseSecondCaptured = true;
        }
    }
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
        state_.qtPreviewPendingTimelineCenterView_ = false;
        state_.qtPreviewTimelineDirty_ = true;
    }
    state_.qtPreviewPlaying_ = false;
    state_.qtPreviewFollowDirty_ = false;
    state_.qtPreviewPendingFollowCenterView_ = false;
    state_.activePreviewPlaybackTransactionId_ = 0;
    owner_.applyEffectivePreviewOutlineVariantToCanvas();
    owner_.applyPreviewStageMediaRouteVisualSettings();
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    flushQtPreviewTimelinePosition();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->focusPlayhead(false);
        state_.timelineQuickStateBridge_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->stopAll();
    }
    applyLatestTimelinePreviewStateToPausedPreview();
    owner_.applyDeferredAnalysisUiUpdates();
    if (state_.pendingTimelineAnalysisRefresh_.revision != 0) {
        requestTimelineAnalysisDispatch(0);
    }
    if (state_.runtimeDebugOutputEnabled_ && wasPlaying && state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->writeProfilingSummaryToFile();
    }
    updatePreviewSliderPosition(state_.qtPreviewPauseSecond_);
    updatePreviewObjectStats(state_.qtPreviewPauseSecond_);
    owner_.updatePauseButtonAppearance();
}

void MainWindow::TimelineSection::applyQtPreviewPosition(double second, bool centerView)
{
    const bool quickTimelineBridgeReady =
        !state_.quickShellUiFocusBridgeMode_ || state_.quickTimelineSurfaceReady_;
    const double timelineCadenceSeconds =
        static_cast<double>(qMax<qint64>(1, timelineTargetFrameIntervalNs())) / 1000000000.0;
    state_.qtPreviewPauseSecond_ = second;
    if (!state_.qtPreviewPlaying_
        && state_.timelineQuickStateBridge_ != nullptr
        && (state_.qtPreviewLastTimelineSecond_ < 0.0
            || qAbs(second - state_.qtPreviewLastTimelineSecond_) >= timelineCadenceSeconds)) {
        state_.qtPreviewPendingTimelineSecond_ = second;
        state_.qtPreviewPendingTimelineCenterView_ = state_.qtPreviewPendingTimelineCenterView_ || centerView;
        state_.qtPreviewTimelineDirty_ = true;
        if (quickTimelineBridgeReady) {
            flushQtPreviewTimelinePosition();
        }
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setPlayheadSeconds(second, !state_.qtPreviewPlaying_);
    }
    owner_.setPreviewStageMediaRouteObservedPlayheadSecond(second);
    owner_.refreshPreviewStageMediaRouteDebugState(!state_.qtPreviewPlaying_);
    updatePreviewSliderPosition(second);
    if (!state_.qtPreviewPlaying_) {
        updatePreviewObjectStats(second);
    }
    if (state_.timelineQuickStateBridge_ != nullptr && state_.timelineQuickStateBridge_->followPreviewEnabled()) {
        if (state_.qtPreviewPlaying_) {
            owner_.queueQtPreviewFollowUiUpdate(second, centerView);
        } else {
            updatePreviewFollowDecorationForTimelineBlueLine(second, true);
        }
    }
}

void MainWindow::TimelineSection::syncPausedPreviewMediaTimestamps(double second)
{
    owner_.seekPreviewStageMediaRouteWhilePaused(second);
}

void MainWindow::TimelineSection::flushQtPreviewTimelinePosition()
{
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    if (state_.quickShellUiFocusBridgeMode_ && !state_.quickTimelineSurfaceReady_) {
        return;
    }
    if (state_.qtPreviewPlaying_) {
        const double second = qMax(0.0, owner_.currentPreviewAuthoritativeAudioClockSecond());
        state_.timelineQuickStateBridge_->setPlayheadSeconds(second, true);
        state_.timelineQuickStateBridge_->focusPlayhead(false);
        state_.qtPreviewLastTimelineSecond_ = second;
        if (state_.qtPreviewFollowDirty_) {
            if (state_.timelineQuickStateBridge_->followPreviewEnabled()) {
                const double followSecond = state_.qtPreviewPendingFollowSecond_;
                const bool followCenterView = state_.qtPreviewPendingFollowCenterView_;
                state_.qtPreviewFollowDirty_ = false;
                state_.qtPreviewPendingFollowCenterView_ = false;
                syncEditorCursorToPreviewSecond(followSecond, followCenterView, false);
            } else {
                state_.qtPreviewFollowDirty_ = false;
                state_.qtPreviewPendingFollowCenterView_ = false;
            }
        }
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
    double second = 0.0;
    if (state_.previewSfxRuntime_ == nullptr) {
        const double elapsedSeconds = static_cast<double>(state_.qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        second = state_.qtPreviewStartSecond_ + (elapsedSeconds * state_.previewPlaybackRate_);
    } else {
        const double elapsedSeconds = static_cast<double>(state_.qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        const double fallbackSecond = state_.qtPreviewStartSecond_ + (elapsedSeconds * state_.previewPlaybackRate_);
        second = state_.previewSfxRuntime_->syncPreviewPlaybackClockTransaction(fallbackSecond);
    }
    onQtPreviewTickAtSecond(second);
}

void MainWindow::TimelineSection::onQtPreviewTickAtSecond(double second)
{
    if (!state_.qtPreviewPlaying_) {
        return;
    }
    owner_.syncPreviewStageMediaRoutePlayback(second);
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

    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->noteTickForProfiling();
    }
    applyQtPreviewPosition(second, true);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->drainEvents(second);
    }
    requestNextDisplayRefreshPreviewFrame();
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

void MainWindow::requestPausedPreviewSeek(double second, bool centerView, bool submitMediaImmediately)
{
    timelineSection_->requestPausedPreviewSeek(second, centerView, submitMediaImmediately);
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

bool MainWindow::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    return timelineSection_->startQtPreviewPlayback(second, resumeFromPause);
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
