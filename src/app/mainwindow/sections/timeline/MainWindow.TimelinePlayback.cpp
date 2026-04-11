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
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
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
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    state_.previewPendingSeekSecond_ = clampedSecond;
    state_.previewPendingSeekCenterView_ = centerView;
    updatePreviewSliderPosition(clampedSecond);
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->start();
    } else {
        seekPreviewToSecond(clampedSecond, centerView);
    }
}

bool MainWindow::TimelineSection::stepPreviewSliderBySeconds(double deltaSeconds, bool centerView)
{
    if (ui_.previewSlider_ == nullptr || !qIsFinite(deltaSeconds)) {
        return false;
    }
    const int deltaMs = qRound(deltaSeconds * 1000.0);
    if (deltaMs == 0) {
        return false;
    }
    const int value = qBound(
        ui_.previewSlider_->minimum(),
        ui_.previewSlider_->value() + deltaMs,
        ui_.previewSlider_->maximum()
    );
    if (value == ui_.previewSlider_->value()) {
        showPreviewSliderTimeHint(value);
        return true;
    }
    ui_.previewSlider_->setValue(value);
    showPreviewSliderTimeHint(value);
    seekPreviewToSecond(static_cast<double>(value) / 1000.0, centerView);
    return true;
}

bool MainWindow::TimelineSection::handlePreviewSliderWheel(QWheelEvent* event)
{
    if (ui_.previewSlider_ == nullptr || event == nullptr) {
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
    ui_.previewSlider_->setFocus(Qt::MouseFocusReason);
    const bool handled = stepPreviewSliderBySeconds(
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
    if (direction == 0 || ui_.previewSlider_ == nullptr) {
        return;
    }
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
    state_.previewHeldSeekDirection_ = 0;
    state_.previewSeekHeldArrowKey_ = 0;
    state_.previewSeekHeldArrowLastElapsedMs_ = 0;
    state_.previewSeekHeldArrowElapsed_.invalidate();
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
    stepPreviewSliderBySeconds(
        static_cast<double>(state_.previewHeldSeekDirection_)
            * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds),
        true
    );
}

void MainWindow::TimelineSection::seekPreviewToSecond(double second, bool centerView)
{
    owner_.ensurePreviewStageMediaRouteInitialized();
    owner_.ensurePreviewSfxRuntimePrepared();
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (state_.qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
    }
    state_.qtPreviewStartSecond_ = clampedSecond;
    state_.qtPreviewPauseSecond_ = clampedSecond;
    state_.qtPreviewTimelineStartSecond_ = clampedSecond;
    state_.qtPreviewTimelineElapsed_.restart();
    state_.qtPreviewPendingTimelineSecond_ = clampedSecond;
    state_.qtPreviewPendingTimelineCenterView_ = centerView;
    state_.qtPreviewTimelineDirty_ = true;
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    syncPausedPreviewMediaTimestamps(clampedSecond);
    applyQtPreviewPosition(clampedSecond, centerView);
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->focusPlayhead(centerView);
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->update();
    }
    updatePreviewSliderPosition(clampedSecond);
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
    }
    if (state_.qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
        startQtPreviewPlayback(state_.qtPreviewPauseSecond_, true);
    }
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
    applyLatestTimelinePreviewStateToPausedPreview();
    const double startSecond = qBound(0.0, second, previewDurationSeconds());
    const bool hasVideoMedia = owner_.previewStageMediaRouteHasVideo();
    const auto applyPlaybackClockState = [this](double initialSecond) {
        state_.qtPreviewStartSecond_ = initialSecond;
        state_.qtPreviewPauseSecond_ = initialSecond;
        state_.qtPreviewLastTimelineSecond_ = initialSecond;
        state_.qtPreviewPendingTimelineSecond_ = initialSecond;
        state_.qtPreviewPendingTimelineCenterView_ = true;
        state_.qtPreviewTimelineDirty_ = false;
        state_.qtPreviewTimelineStartSecond_ = initialSecond;
    };

    state_.qtPreviewPlaybackReturnSecond_ = startSecond;
    state_.qtPreviewPlaybackEndSecond_ = qMax(0.0, previewPlaybackEndSeconds());
    owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_);

    double effectiveStartSecond = startSecond;
    if (state_.previewSfxRuntime_ != nullptr) {
        effectiveStartSecond = state_.previewSfxRuntime_->startPreviewPlaybackTransaction(
            startSecond,
            resumeFromPause,
            state_.previewPlaybackRate_);
    }

    applyPlaybackClockState(effectiveStartSecond);
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewElapsed_.restart();
    state_.qtPreviewTimelineElapsed_.restart();
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->setPlaybackEntrySeconds(state_.qtPreviewPlaybackReturnSecond_);
        ui_.timelineView_->setPlayheadUpperLimitSeconds(state_.qtPreviewPlaybackEndSecond_);
        if (qFuzzyCompare(ui_.timelineView_->playheadSeconds() + 1.0, effectiveStartSecond + 1.0)) {
            ui_.timelineView_->focusPlayhead(true);
        } else {
            ui_.timelineView_->setPlayheadSeconds(effectiveStartSecond, true);
            ui_.timelineView_->focusPlayhead(false);
        }
    }
    if (state_.previewCanvas_ != nullptr) {
        if (!resumeFromPause) {
            state_.previewCanvas_->resetProfilingSession();
        }
        state_.previewCanvas_->setPlayheadSeconds(effectiveStartSecond, false);
    }
    if (hasVideoMedia) {
        owner_.startPreviewStageMediaRoutePlayback(effectiveStartSecond);
    }

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
    bool pauseSecondCaptured = false;
    if (state_.previewSfxRuntime_ != nullptr) {
        const QtPreviewSfxRuntime::PausePreviewResult pauseResult =
            state_.previewSfxRuntime_->capturePausedPreviewTransaction();
        if (pauseResult.usedBackgroundTrack) {
            state_.qtPreviewPauseSecond_ = pauseResult.pauseSecond;
            pauseSecondCaptured = true;
        }
    }
    if (!pauseSecondCaptured) {
        if (owner_.previewStageMediaRouteHasVideo()) {
            state_.qtPreviewPauseSecond_ = owner_.previewStageMediaRouteCurrentPlaybackSecond();
        }
    }
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
    if (wasPlaying) {
        state_.qtPreviewPendingTimelineSecond_ = state_.qtPreviewPauseSecond_;
        state_.qtPreviewPendingTimelineCenterView_ = false;
        state_.qtPreviewTimelineDirty_ = true;
    }
    state_.qtPreviewPlaying_ = false;
    owner_.applyEffectivePreviewOutlineVariantToCanvas();
    owner_.applyPreviewStageMediaRouteVisualSettings();
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    flushQtPreviewTimelinePosition();
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->focusPlayhead(false);
        ui_.timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
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
    state_.qtPreviewPauseSecond_ = second;
    if (!state_.qtPreviewPlaying_
        && ui_.timelineView_ != nullptr
        && (state_.qtPreviewLastTimelineSecond_ < 0.0
            || qAbs(second - state_.qtPreviewLastTimelineSecond_) >= kTimelineUiCadenceSeconds)) {
        state_.qtPreviewPendingTimelineSecond_ = second;
        state_.qtPreviewPendingTimelineCenterView_ = state_.qtPreviewPendingTimelineCenterView_ || centerView;
        state_.qtPreviewTimelineDirty_ = true;
        flushQtPreviewTimelinePosition();
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
    if (state_.qtPreviewPlaying_) {
        syncEditorCursorToPreviewSecond(second, centerView);
    }
}

void MainWindow::TimelineSection::syncPausedPreviewMediaTimestamps(double second)
{
    owner_.seekPreviewStageMediaRouteWhilePaused(second);
}

void MainWindow::TimelineSection::flushQtPreviewTimelinePosition()
{
    if (ui_.timelineView_ == nullptr) {
        return;
    }
    if (state_.qtPreviewPlaying_) {
        double second = qMax(
            0.0,
            state_.qtPreviewTimelineStartSecond_ + ((state_.qtPreviewTimelineElapsed_.elapsed() / 1000.0) * state_.previewPlaybackRate_)
        );
        if (state_.previewSfxRuntime_ != nullptr
            && state_.previewSfxRuntime_->hasBackgroundTrack()
            && state_.previewSfxRuntime_->isBackgroundTrackRunning()) {
            second = qMax(0.0, state_.previewSfxRuntime_->backgroundPlaybackSecond());
        }
        ui_.timelineView_->setPlayheadSeconds(second, true);
        ui_.timelineView_->focusPlayhead(false);
        state_.qtPreviewLastTimelineSecond_ = second;
        return;
    }
    if (!state_.qtPreviewTimelineDirty_) {
        return;
    }
    ui_.timelineView_->setPlayheadSeconds(state_.qtPreviewPendingTimelineSecond_, state_.qtPreviewPendingTimelineCenterView_);
    ui_.timelineView_->focusPlayhead(state_.qtPreviewPendingTimelineCenterView_);
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

bool MainWindow::stepPreviewSliderBySeconds(double deltaSeconds, bool centerView)
{
    return timelineSection_->stepPreviewSliderBySeconds(deltaSeconds, centerView);
}

bool MainWindow::handlePreviewSliderWheel(QWheelEvent* event)
{
    return timelineSection_->handlePreviewSliderWheel(event);
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
