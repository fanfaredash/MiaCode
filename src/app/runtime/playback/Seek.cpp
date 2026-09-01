#include "runtime/playback/PlaybackHost.h"
#include "runtime/Shared.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "common/IntroConfig.h"
#include "tools/video_export/VideoExportController.h"
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
#include "runtime/playback/Playback.Internal.h"

using namespace miacode::runtime::shared;
using namespace miacode::runtime::playback_detail;

void miacode::runtime::PlaybackHost::scheduleDeferredPreviewUiTail(
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
        &session_,
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
                session_.applyEffectivePreviewOutlineVariantToCanvas();
                session_.applyPreviewStageMediaRouteVisualSettings();
            }
            if (applyDeferredAnalysis) {
                session_.applyDeferredAnalysisUiUpdates();
            }
            if (dispatchTimelineAnalysis && state_.pendingTimelineAnalysisRefresh_.revision != 0) {
                requestTimelineAnalysisDispatch(0);
            }
            if (writeProfilingSummary && state_.runtimeDebugOutputEnabled_ && state_.scene_ != nullptr) {
                state_.scene_->writeProfilingSummaryToFile();
            }
            if (updateObjectStats) {
                updatePreviewObjectStats(objectStatsSecond);
            }
            if (refreshStageMediaDebugState) {
                session_.refreshPreviewStageMediaRouteDebugState(true);
            }
            if (updatePausedPreviewFollowDecoration && !state_.playing_) {
                updatePreviewFollowDecorationForTimelineBlueLine(objectStatsSecond, true);
            }
            if (updatePauseButton) {
                session_.updatePauseButtonAppearance();
            }
        });
}

void miacode::runtime::PlaybackHost::schedulePreviewSeek(double second, bool centerView)
{
    requestPausedPreviewSeek(second, centerView, false, false);
}

quint64 miacode::runtime::PlaybackHost::requestPausedPreviewVisualSeek(
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

void miacode::runtime::PlaybackHost::requestPausedPreviewSeek(
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
    if (!session_.previewStageMediaRouteHasVideo()) {
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

void miacode::runtime::PlaybackHost::applyPausedPreviewVisualSecond(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    state_.qtPreviewStartSecond_ = clampedSecond;
    miacode::runtime::shared::writePreviewPauseSecond(
        state_.pauseSecond_, clampedSecond, state_.playing_, "apply_paused_preview_visual_second");
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

void miacode::runtime::PlaybackHost::submitPausedMediaSeek(double second, quint64 generation)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (ui_.previewSeekDebounceTimer_ != nullptr) {
        ui_.previewSeekDebounceTimer_->stop();
    }
    if (!session_.previewStageMediaRouteHasVideo()) {
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
    session_.submitPreviewStageMediaRoutePausedSeek(clampedSecond, generation);
}

void miacode::runtime::PlaybackHost::maybeSubmitLatestPausedMediaSeek()
{
    if (state_.playing_ || state_.pausedSeekMediaPending_ || !session_.previewStageMediaRouteHasVideo()) {
        return;
    }
    if (state_.pausedSeekMediaAckGeneration_ >= state_.pausedSeekGeneration_) {
        return;
    }
    submitPausedMediaSeek(state_.pausedSeekTargetSecond_, state_.pausedSeekGeneration_);
}

void miacode::runtime::PlaybackHost::handlePausedPreviewMediaSeekCompleted(double second, quint64 generation)
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
    session_.refreshPreviewStageMediaRouteDebugState(false);
    if (generation < state_.pausedSeekGeneration_) {
        appendQuickShellBackendLog(
            QStringLiteral("paused_seek_media_drop"),
            QString("acked=%1 latest=%2").arg(generation).arg(state_.pausedSeekGeneration_)
        );
        maybeSubmitLatestPausedMediaSeek();
    }
}

bool miacode::runtime::PlaybackHost::stepPreviewBySeconds(double deltaSeconds, bool centerView)
{
    if (!qIsFinite(deltaSeconds)) {
        return false;
    }
    if (qAbs(deltaSeconds) < 1e-9) {
        return false;
    }
    const double nextSecond = qBound(
        0.0,
        state_.pauseSecond_ + deltaSeconds,
        previewDurationSeconds()
    );
    const bool moved = qAbs(nextSecond - state_.pauseSecond_) >= 1e-9;
    if (moved) {
        seekPreviewToSecond(nextSecond, centerView);
    }
    return moved;
}

bool miacode::runtime::PlaybackHost::handlePreviewSeekWheel(QWheelEvent* event)
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

void miacode::runtime::PlaybackHost::beginPreviewHeldSeek(int direction, int key)
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

void miacode::runtime::PlaybackHost::stopPreviewHeldSeek(int key)
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
    session_.setProperty(kQuickShellTransportSeekProperty, false);
    if (ui_.previewHeldSeekTimer_ != nullptr) {
        ui_.previewHeldSeekTimer_->stop();
    }
}

void miacode::runtime::PlaybackHost::applyPreviewHeldSeekTick()
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
            .arg(positionSeconds(), 0, 'f', 6)
    );
    QToolTip::hideText();
    const double nextSecond = qBound(
        0.0,
        state_.pauseSecond_ + deltaSeconds,
        previewDurationSeconds()
    );
    if (qAbs(nextSecond - state_.pauseSecond_) >= 1e-9) {
        seekPreviewToSecond(nextSecond, true);
    }
}
