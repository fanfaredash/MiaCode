#include "runtime/playback/PlaybackCoordinator.h"
#include "app/v2/ApplicationServices.h"
#include "app/v2/EditorSyncController.h"
#include "runtime/Shared.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/ProcessDiagnostics.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewInteractionConfig.h"
#include "audio/PreviewAudioPlaybackFlowPolicy.h"
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

void miacode::runtime::PlaybackCoordinator::clearPreviewPlayingRetainedSeek()
{
    state_.previewPlayingSeekAudioGeneration_ = 0;
    state_.previewPlayingSeekPendingSequence_ = 0;
    state_.previewPlayingSeekCenterView_ = false;
    state_.previewPlayingSeekVisualSecond_ = 0.0;
    state_.previewPlayingSeekWorkerSecond_ = 0.0;
}

void miacode::runtime::PlaybackCoordinator::cancelPreviewStartupSync(const char* cause)
{
    const bool hadStartup = state_.previewStartupSyncPending_
        || state_.previewLateVideoStartPending_
        || state_.previewStartupStrongGroupCommitted_;
    const bool hadPendingPause = state_.previewPendingManualPauseSequence_ != 0
        || (state_.previewPendingDevicePauseToken_ != 0
            && !state_.previewDevicePauseSubmissionInProgress_);
    if (!hadStartup && !hadPendingPause) {
        return;
    }
    if (hadStartup) {
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
        playbackState_.previewStartupSyncPending_ = false;
        playbackState_.previewLateVideoStartPending_ = false;
        state_.previewStartupAudioPrepared_ = false;
        state_.previewStartupCanvasPresented_ = false;
        state_.previewStartupStrongGroupCommitted_ = false;
        state_.previewStartupResumeFromPause_ = false;
        state_.previewStartupVideoPrepareStarted_ = false;
        state_.previewStartupVideoPrepared_ = false;
        state_.previewStartupVideoStarted_ = false;
        state_.previewStartupAudioGeneration_ = 0;
        state_.previewStartupPendingPrepareSequence_ = 0;
        state_.previewStartupPendingRetainedSequence_ = 0;
        state_.previewStartupRequestedSecond_ = 0.0;
        state_.previewStartupVisualSecond_ = 0.0;
        playbackState_.previewStartupPreparedSecond_ = 0.0;
    }
    if (hadPendingPause) {
        appendPreviewPlaybackLog(
            QStringLiteral("pause_completion_superseded"),
            QString("cause=%1 manual_generation=%2 manual_txn=%3 manual_sequence=%4 manual_visual=%5 "
                    "device_token=%6 next_txn=%7")
                .arg(QString::fromLatin1(cause != nullptr ? cause : "unknown"))
                .arg(state_.previewPendingManualPauseGeneration_)
                .arg(state_.previewPendingManualPauseTransactionId_)
                .arg(state_.previewPendingManualPauseSequence_)
                .arg(state_.previewPendingManualPauseWallSecond_, 0, 'f', 6)
                .arg(state_.previewPendingDevicePauseToken_)
                .arg(state_.activePreviewPlaybackTransactionId_));
        state_.previewPendingManualPauseGeneration_ = 0;
        state_.previewPendingManualPauseTransactionId_ = 0;
        state_.previewPendingManualPauseSequence_ = 0;
        state_.previewPendingDevicePauseGeneration_ = 0;
        state_.previewPendingDevicePauseTransactionId_ = 0;
        state_.previewPendingDevicePauseSequence_ = 0;
        state_.previewPendingDevicePauseToken_ = 0;
    }
}

void miacode::runtime::PlaybackCoordinator::handlePreviewStartupCanvasPresented()
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
            .arg(state_.previewStartupVisualSecond_, 0, 'f', 6));
    tryCommitPreviewStartupSync();
}

void miacode::runtime::PlaybackCoordinator::handlePreviewStartupVideoPrepared(double second, quint64 transactionId)
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
        const double currentSecond = authoritativeAudioClockSecond();
        state_.previewStageMediaHost_->commitPreparedPlaybackStart(currentSecond);
        state_.previewStartupVideoStarted_ = true;
        playbackState_.previewLateVideoStartPending_ = false;
        appendPreviewPlaybackLog(
            QStringLiteral("late_video_start_after_commit"),
            QString("txn=%1 second=%2")
                .arg(transactionId)
                .arg(currentSecond, 0, 'f', 6));
    }
}

void miacode::runtime::PlaybackCoordinator::handlePreviewAudioPrepared(
    const miacode::preview_audio::PreviewAudioCompletion& completion)
{
    handlePreviewAudioStartupCompletion(completion);
}

void miacode::runtime::PlaybackCoordinator::handlePreviewRetainedPlaybackCompleted(
    const miacode::preview_audio::PreviewAudioCompletion& completion)
{
    using namespace miacode::preview_audio;
    using namespace miacode::preview_audio::playback_flow;

    if (completion.kind == CommandKind::ManualPause
        || completion.kind == CommandKind::DeviceChangePause) {
        const PauseKind pauseKind = completion.kind == CommandKind::ManualPause
            ? PauseKind::Manual
            : PauseKind::DeviceChange;
        PauseState pending;
        pending.currentGeneration = state_.previewSfxRuntime_ != nullptr
            ? state_.previewSfxRuntime_->playbackGeneration()
            : (pauseKind == PauseKind::Manual
                   ? state_.previewPendingManualPauseGeneration_
                   : state_.previewPendingDevicePauseGeneration_);
        pending.deviceSequence = state_.previewAudioDeviceChangeSequence_;
        pending.pendingManualPauseGeneration = state_.previewPendingManualPauseGeneration_;
        pending.pendingManualPauseTransactionId = state_.previewPendingManualPauseTransactionId_;
        pending.pendingManualPauseSequence = state_.previewPendingManualPauseSequence_;
        pending.manualPauseVisualSecond = state_.previewPendingManualPauseWallSecond_;
        pending.pendingDevicePauseGeneration = state_.previewPendingDevicePauseGeneration_;
        pending.pendingDevicePauseTransactionId = state_.previewPendingDevicePauseTransactionId_;
        pending.pendingDevicePauseSequence = state_.previewPendingDevicePauseSequence_;
        pending.pendingDevicePauseToken = state_.previewPendingDevicePauseToken_;
        pending.devicePauseVisualSecond = state_.previewPendingDevicePauseWallSecond_;
        pending.visualSecond = state_.pauseSecond_;
        pending.authoritativeRetainedSecond = state_.previewRetainedAuthoritativeSecond_;
        pending.retainedMode = state_.previewRetainedPlaybackMode_;
        pending.retainedBgmState = state_.previewRetainedBgmState_;

        const bool degraded = !completion.success
            || state_.previewSfxRuntime_ == nullptr
            || !state_.previewSfxRuntime_->audioEngineInitialized();
        const PauseDecision decision = decidePauseCompletion(
            pending,
            PauseCompletion{
                pauseKind,
                completion.identity.generation,
                completion.identity.transactionId,
                completion.identity.sequence,
                completion.identity.deviceSequence,
                completion.identity.pauseToken,
                completion.pauseResult.pauseSecond,
                static_cast<int>(completion.pauseResult.retainedMode),
                static_cast<int>(completion.pauseResult.retainedBgmState),
                completion.success,
                degraded,
            });
        if (!decision.matchesPending) {
            appendPreviewPlaybackLog(
                QStringLiteral("pause_completion_drop"),
                QString("kind=%1 txn=%2 generation=%3 sequence=%4 token=%5 reason=%6 "
                        "manual_pending=%7 device_pending=%8")
                    .arg(static_cast<int>(completion.kind))
                    .arg(completion.identity.transactionId)
                    .arg(completion.identity.generation)
                    .arg(completion.identity.sequence)
                    .arg(completion.identity.pauseToken)
                    .arg(QString::fromLatin1(pauseCompletionRejectionLabel(decision.rejectionReason)))
                    .arg(state_.previewPendingManualPauseSequence_)
                    .arg(state_.previewPendingDevicePauseToken_));
            return;
        }

        state_.previewPendingManualPauseGeneration_ = decision.state.pendingManualPauseGeneration;
        state_.previewPendingManualPauseTransactionId_ = decision.state.pendingManualPauseTransactionId;
        state_.previewPendingManualPauseSequence_ = decision.state.pendingManualPauseSequence;
        state_.previewPendingDevicePauseGeneration_ = decision.state.pendingDevicePauseGeneration;
        state_.previewPendingDevicePauseTransactionId_ = decision.state.pendingDevicePauseTransactionId;
        state_.previewPendingDevicePauseSequence_ = decision.state.pendingDevicePauseSequence;
        state_.previewPendingDevicePauseToken_ = decision.state.pendingDevicePauseToken;
        if (pauseKind == PauseKind::Manual) {
            if (!decision.commitsRetainedState) {
                appendPreviewPlaybackLog(
                    QStringLiteral("manual_pause_completion_failed"),
                    QString("txn=%1 generation=%2 sequence=%3 success=%4 error=%5 detail=%6")
                        .arg(completion.identity.transactionId)
                        .arg(completion.identity.generation)
                        .arg(completion.identity.sequence)
                        .arg(completion.success ? 1 : 0)
                        .arg(static_cast<int>(completion.error))
                        .arg(completion.detail));
                return;
            }
            const double wallSecond = state_.previewPendingManualPauseWallSecond_;
            state_.previewRetainedAuthoritativeSecond_ = decision.state.authoritativeRetainedSecond;
            state_.previewRetainedPlaybackMode_ = decision.state.retainedMode;
            state_.previewRetainedBgmState_ = decision.state.retainedBgmState;
            appendPreviewPlaybackLog(
                QStringLiteral("manual_pause_completion_accepted"),
                QString("txn=%1 generation=%2 sequence=%3 visual_second=%4 backend_second=%5 delta_ms=%6 retained_mode=%7 retained_bgm=%8")
                    .arg(completion.identity.transactionId)
                    .arg(completion.identity.generation)
                    .arg(completion.identity.sequence)
                    .arg(wallSecond, 0, 'f', 6)
                    .arg(completion.pauseResult.pauseSecond, 0, 'f', 6)
                    .arg((completion.pauseResult.pauseSecond - wallSecond) * 1000.0, 0, 'f', 3)
                    .arg(state_.previewRetainedPlaybackMode_)
                    .arg(state_.previewRetainedBgmState_));
            return;
        }

        appendPreviewPlaybackLog(
            QStringLiteral("device_change_pause_completion_accepted"),
            QString("txn=%1 generation=%2 sequence=%3 token=%4 visual_second=%5 backend_second=%6 delta_ms=%7 success=%8")
                .arg(completion.identity.transactionId)
                .arg(completion.identity.generation)
                .arg(completion.identity.sequence)
                .arg(completion.identity.pauseToken)
                .arg(state_.previewPendingDevicePauseWallSecond_, 0, 'f', 6)
                .arg(completion.pauseResult.pauseSecond, 0, 'f', 6)
                .arg((completion.pauseResult.pauseSecond - state_.previewPendingDevicePauseWallSecond_) * 1000.0, 0, 'f', 3)
                .arg(completion.success ? 1 : 0));
        return;
    }

    if (completion.kind == miacode::preview_audio::CommandKind::SeekRetained
        && state_.previewPlayingSeekPendingSequence_ != 0) {
        handlePreviewPlayingRetainedSeekCompletion(completion);
        return;
    }
    handlePreviewAudioStartupCompletion(completion);
}

void miacode::runtime::PlaybackCoordinator::handlePreviewAudioStartupCompletion(
    const miacode::preview_audio::PreviewAudioCompletion& completion)
{
    using namespace miacode::preview_audio;
    using namespace miacode::preview_audio::playback_flow;

    CompletionKind kind;
    switch (completion.kind) {
    case CommandKind::Prepare:
        kind = CompletionKind::Prepare;
        break;
    case CommandKind::ResumeRetained:
        kind = CompletionKind::RetainedResume;
        break;
    case CommandKind::SeekRetained:
        kind = CompletionKind::RetainedSeek;
        break;
    default:
        return;
    }

    State pending;
    pending.currentGeneration = state_.previewStartupAudioGeneration_;
    pending.activeTransactionId = state_.activePreviewPlaybackTransactionId_;
    pending.pendingPrepareSequence = state_.previewStartupPendingPrepareSequence_;
    pending.pendingRetainedSequence = state_.previewStartupPendingRetainedSequence_;
    pending.visualSecond = state_.previewStartupVisualSecond_;
    pending.requestedVisualSecond = state_.previewStartupRequestedSecond_;
    pending.effectiveWorkerSecond = state_.previewStartupPreparedSecond_;
    pending.audioPrepared = state_.previewStartupAudioPrepared_;
    pending.uiPlaying = state_.playing_;

    Completion flowCompletion;
    flowCompletion.kind = kind;
    flowCompletion.generation = completion.identity.generation;
    flowCompletion.transactionId = completion.identity.transactionId;
    flowCompletion.sequence = completion.identity.sequence;
    flowCompletion.effectiveSecond = completion.value;
    flowCompletion.success = completion.success;
    flowCompletion.degraded = !completion.success
        || state_.previewSfxRuntime_ == nullptr
        || !state_.previewSfxRuntime_->audioEngineInitialized();
    const Decision decision = decideCompletion(pending, flowCompletion);
    if (!decision.matchesPending) {
        appendPreviewPlaybackLog(
            QStringLiteral("audio_startup_completion_drop"),
            QString("kind=%1 txn=%2 active_txn=%3 generation=%4 current_generation=%5 sequence=%6 "
                    "pending_prepare=%7 pending_retained=%8 success=%9 error=%10")
                .arg(static_cast<int>(completion.kind))
                .arg(completion.identity.transactionId)
                .arg(state_.activePreviewPlaybackTransactionId_)
                .arg(completion.identity.generation)
                .arg(state_.previewStartupAudioGeneration_)
                .arg(completion.identity.sequence)
                .arg(state_.previewStartupPendingPrepareSequence_)
                .arg(state_.previewStartupPendingRetainedSequence_)
                .arg(completion.success ? 1 : 0)
                .arg(static_cast<int>(completion.error)));
        return;
    }
    if (!decision.commitsAudioPreparation) {
        appendPreviewPlaybackLog(
            QStringLiteral("audio_startup_completion_failed"),
            QString("kind=%1 txn=%2 generation=%3 sequence=%4 success=%5 degraded=%6 error=%7 "
                    "detail=%8 native_error=%9")
                .arg(static_cast<int>(completion.kind))
                .arg(completion.identity.transactionId)
                .arg(completion.identity.generation)
                .arg(completion.identity.sequence)
                .arg(completion.success ? 1 : 0)
                .arg(flowCompletion.degraded ? 1 : 0)
                .arg(static_cast<int>(completion.error))
                .arg(completion.detail)
                .arg(completion.nativeErrorCode));
        cancelPreviewStartupSync("audio_startup_completion_failed");
        return;
    }

    state_.previewStartupAudioPrepared_ = decision.state.audioPrepared;
    playbackState_.previewStartupPreparedSecond_ = decision.state.effectiveWorkerSecond;
    state_.previewStartupPendingPrepareSequence_ = decision.state.pendingPrepareSequence;
    state_.previewStartupPendingRetainedSequence_ = decision.state.pendingRetainedSequence;
    appendPreviewPlaybackLog(
        QStringLiteral("audio_startup_completion_accepted"),
        QString("kind=%1 txn=%2 generation=%3 sequence=%4 requested=%5 effective=%6")
            .arg(static_cast<int>(completion.kind))
            .arg(completion.identity.transactionId)
            .arg(completion.identity.generation)
            .arg(completion.identity.sequence)
            .arg(state_.previewStartupRequestedSecond_, 0, 'f', 6)
            .arg(state_.previewStartupPreparedSecond_, 0, 'f', 6));
    tryCommitPreviewStartupSync();
}

void miacode::runtime::PlaybackCoordinator::handlePreviewPlayingRetainedSeekCompletion(
    const miacode::preview_audio::PreviewAudioCompletion& completion)
{
    using namespace miacode::preview_audio;
    using namespace miacode::preview_audio::playback_flow;

    State pending;
    pending.currentGeneration = state_.previewPlayingSeekAudioGeneration_;
    pending.activeTransactionId = state_.activePreviewPlaybackTransactionId_;
    pending.pendingPlayingSeekSequence = state_.previewPlayingSeekPendingSequence_;
    pending.pendingPlayingSeekCenterView = state_.previewPlayingSeekCenterView_;
    pending.visualSecond = state_.previewPlayingSeekVisualSecond_;
    pending.requestedVisualSecond = state_.previewPlayingSeekVisualSecond_;
    pending.effectiveWorkerSecond = state_.previewPlayingSeekWorkerSecond_;
    pending.transportAnchorSecond = state_.previewPlayingSeekWorkerSecond_;
    pending.audioPrepared = true;
    pending.uiPlaying = state_.playing_;

    Completion flowCompletion;
    flowCompletion.kind = CompletionKind::RetainedSeek;
    flowCompletion.generation = completion.identity.generation;
    flowCompletion.transactionId = completion.identity.transactionId;
    flowCompletion.sequence = completion.identity.sequence;
    flowCompletion.effectiveSecond = completion.value;
    flowCompletion.success = completion.success;
    flowCompletion.degraded = !completion.success
        || state_.previewSfxRuntime_ == nullptr
        || !state_.previewSfxRuntime_->audioEngineInitialized();
    const Decision decision = decideCompletion(pending, flowCompletion);
    if (!decision.matchesPending) {
        appendPreviewPlaybackLog(
            QStringLiteral("playing_seek_completion_drop"),
            QString("txn=%1 active_txn=%2 generation=%3 current_generation=%4 sequence=%5 pending=%6")
                .arg(completion.identity.transactionId)
                .arg(state_.activePreviewPlaybackTransactionId_)
                .arg(completion.identity.generation)
                .arg(state_.previewPlayingSeekAudioGeneration_)
                .arg(completion.identity.sequence)
                .arg(state_.previewPlayingSeekPendingSequence_));
        return;
    }
    if (!decision.commitsWorkerSecond) {
        appendPreviewPlaybackLog(
            QStringLiteral("playing_seek_completion_failed"),
            QString("txn=%1 generation=%2 sequence=%3 success=%4 degraded=%5 error=%6 detail=%7 native_error=%8")
                .arg(completion.identity.transactionId)
                .arg(completion.identity.generation)
                .arg(completion.identity.sequence)
                .arg(completion.success ? 1 : 0)
                .arg(flowCompletion.degraded ? 1 : 0)
                .arg(static_cast<int>(completion.error))
                .arg(completion.detail)
                .arg(completion.nativeErrorCode));
        clearPreviewPlayingRetainedSeek();
        return;
    }

    const double effectiveSecond = decision.state.transportAnchorSecond;
    const bool centerView = decision.state.pendingPlayingSeekCenterView;
    clearPreviewPlayingRetainedSeek();
    playbackState_.qtPreviewStartSecond_ = effectiveSecond;
    miacode::runtime::shared::writePreviewPauseSecond(
        playbackState_.pauseSecond_, effectiveSecond, playbackState_.playing_, "playing_seek_completion");
    playbackState_.qtPreviewElapsed_.restart();
    state_.qtPreviewTimelineElapsed_.restart();
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->armDeviceChangeCutoffClock(
            effectiveSecond,
            state_.previewPlaybackRate_,
            state_.activePreviewPlaybackTransactionId_);
    }
    resetVisualClockSmoothing();
    syncPreviewStageMediaRoutePlayback(effectiveSecond);
    applyQtPreviewPosition(effectiveSecond, centerView);
    appendPreviewPlaybackLog(
        QStringLiteral("playing_seek_completion_accepted"),
        QString("txn=%1 generation=%2 sequence=%3 visual=%4 effective=%5")
            .arg(completion.identity.transactionId)
            .arg(completion.identity.generation)
            .arg(completion.identity.sequence)
            .arg(pending.visualSecond, 0, 'f', 6)
            .arg(effectiveSecond, 0, 'f', 6));
}

void miacode::runtime::PlaybackCoordinator::tryCommitPreviewStartupSync()
{
    if (!state_.previewStartupSyncPending_ || state_.previewStartupStrongGroupCommitted_) {
        return;
    }
    if (!state_.previewStartupAudioPrepared_) {
        return;
    }
    if (state_.scene_ != nullptr && !state_.previewStartupCanvasPresented_) {
        return;
    }

    state_.previewStartupStrongGroupCommitted_ = true;
    playbackState_.previewStartupSyncPending_ = false;
    const double effectiveStartSecond = state_.previewStartupPreparedSecond_;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->commitPreparedPreviewPlayback();
    }
    if (state_.previewStageMediaHost_ != nullptr
        && state_.previewStartupVideoPrepareStarted_
        && state_.previewStartupVideoPrepared_) {
        state_.previewStageMediaHost_->commitPreparedPlaybackStart(effectiveStartSecond);
        state_.previewStartupVideoStarted_ = true;
        playbackState_.previewLateVideoStartPending_ = false;
        appendPreviewPlaybackLog(
            QStringLiteral("weak_video_ready_before_commit"),
            QString("txn=%1 second=%2")
                .arg(state_.activePreviewPlaybackTransactionId_)
                .arg(effectiveStartSecond, 0, 'f', 6));
    } else {
        playbackState_.previewLateVideoStartPending_ = state_.previewStartupVideoPrepareStarted_;
    }
    finalizeQtPreviewPlaybackStart(effectiveStartSecond);
    appendPreviewPlaybackLog(
        QStringLiteral("commit"),
        QString("txn=%1 effective=%2 late_video_pending=%3")
            .arg(state_.activePreviewPlaybackTransactionId_)
            .arg(effectiveStartSecond, 0, 'f', 6)
            .arg(state_.previewLateVideoStartPending_ ? 1 : 0));
}

void miacode::runtime::PlaybackCoordinator::stopQtPreviewTimers()
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
    // Forget the render-cadence liveness marker so the next playback starts on the watchdog
    // and only hands the sampling phase over once a real afterAnimating tick has arrived.
    // Dropping the cadence flag also lets the timeline window go idle again when paused.
    state_.qtPreviewLastTimelineCadenceMs_ = -1;
    state_.qtPreviewLastTimelineCadenceFlushNs_ = -1;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setPlaybackCadenceActive(false);
    }
    setPreviewFixedTimerHighResolutionActive(state_, false);
}

void miacode::runtime::PlaybackCoordinator::finalizeQtPreviewPlaybackStart(double effectiveStartSecond)
{
    state_.pausedPreviewMediaSeekPending_ = false;
    // Position the clock count-in cursor for this playback start (skips ticks before
    // the start second without replaying them; the downbeat at 0 fires on the first
    // tick after the 片头 hand-off).
    resetExportAuditionClockCursor(effectiveStartSecond);
    playbackState_.qtPreviewStartSecond_ = effectiveStartSecond;
    miacode::runtime::shared::writePreviewPauseSecond(
        playbackState_.pauseSecond_, effectiveStartSecond, playbackState_.playing_, "finalize_qt_preview_playback_start");
    playbackState_.qtPreviewElapsed_.restart();
    state_.qtPreviewTimelineElapsed_.restart();
    miacode::runtime::shared::writePreviewPlayingFlag(playbackState_, services_.shellNotifications(), true);
    services_.editorSync().setPlaybackActive(true);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->armDeviceChangeCutoffClock(
            effectiveStartSecond,
            state_.previewPlaybackRate_,
            state_.activePreviewPlaybackTransactionId_);
    }
    // beta7 leak gauge — anchor private bytes at play start so the pause handler can report the
    // playback-window delta (d_play), the largest previously-unbracketed slice of the cycle.
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::diag::leak_gauge::notePlayStartPrivateBytes(
            miacode::diag::processPrivateBytes());
        miacode::diag::leak_gauge::markPlayStartTimelinePresents();
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
    if (state_.scene_ != nullptr) {
        state_.scene_->setActivePlaybackProfilingEnabled(true);
    }
    setPreviewFixedTimerHighResolutionActive(state_, !previewCanvasUsesFrameSwappedPacing());
    if (state_.scene_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        requestNextFixedIntervalPreviewFrame();
    }
    // Start on the watchdog: the first afterAnimating tick claims the sampling phase.
    state_.qtPreviewLastTimelineCadenceMs_ = -1;
    state_.qtPreviewLastTimelineCadenceFlushNs_ = -1;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setPlaybackCadenceActive(true);
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

void miacode::runtime::PlaybackCoordinator::pauseQtPreviewPlaybackExact(PauseSecondSource pauseSecondSource)
{
    const bool wasPlaying = state_.playing_;
    const quint64 playbackTxn = state_.activePreviewPlaybackTransactionId_;
    const bool deviceChangePause = pauseSecondSource == PauseSecondSource::AudioPosition
        || pauseSecondSource == PauseSecondSource::NativeDeviceCutoff;
    const bool nativeDeviceCutoff = pauseSecondSource == PauseSecondSource::NativeDeviceCutoff;
    if (!wasPlaying || state_.previewSfxRuntime_ == nullptr) {
        stopQtPreviewPlayback(true);
        return;
    }

    // G1 Commit 6: capture the canonical pause-second from the wall-clock master
    // BEFORE handing pause control to the runtime. With a channel-position query no
    // longer consulted in BassPreviewAudioBackend::authoritativeSecond
    // (PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §5.3 / §6.1 step 6), the runtime's
    // returned pauseSecond would be stale; the wall-clock value is the truth, so we
    // overwrite the runtime's PausePreviewResult.pauseSecond with it below.
    const double wallClockPauseSecond = deviceChangePause
        ? state_.previewPendingDevicePauseWallSecond_
        : authoritativeAudioClockSecond();
    // Disarming happens before the GUI freezes. A later Qt/IMM duplicate observes
    // the same physical cutoff as already handled and cannot create a second clock
    // sample or a second worker pause.
    state_.previewSfxRuntime_->disarmDeviceChangeCutoffClock();
    // Freeze the UI-side playback state before entering the audio backend. A device
    // switch can make pausePreviewPlaybackTransaction() block long enough for the
    // preview timers/frame callbacks to advance the timeline after the pause was
    // requested. The captured wall-clock second remains the pause anchor; the
    // backend pause is allowed to complete after the UI has become inert.
    cancelPreviewStartupSync("pause_qt_preview_playback_exact");
    clearPreviewPlayingRetainedSeek();
    pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    miacode::runtime::shared::writePreviewPlayingFlag(playbackState_, services_.shellNotifications(), false);
    services_.editorSync().setPlaybackActive(false);
    miacode::runtime::shared::writePreviewPauseSecond(
        playbackState_.pauseSecond_, wallClockPauseSecond, playbackState_.playing_, "pause_qt_preview_playback_exact");
    state_.pausedPreviewMediaSeekPending_ = false;
    // This is deliberately inline with changing playing_.  Deferring the
    // visibility policy through the UI-tail QTimer made the pause-hide PV/BG setting
    // expose a stale playing frame after a device cutoff had already frozen the chart.
    QElapsedTimer visualStateTimer;
    visualStateTimer.start();
    preview_.applyEffectivePreviewOutlineVariantToCanvas();
    applyPreviewStageMediaRouteVisualSettings(state_);
    appendPreviewPlaybackLog(
        QStringLiteral("pause_visual_state_applied"),
        QString("txn=%1 source=%2 inline=1 elapsed_ms=%3")
            .arg(playbackTxn)
            .arg(nativeDeviceCutoff
                     ? QStringLiteral("native_device_cutoff")
                     : (deviceChangePause ? QStringLiteral("device") : QStringLiteral("manual")))
            .arg(visualStateTimer.elapsed()));
    appendPreviewPlaybackLog(
        QStringLiteral("pause_exact"),
        QString("txn=%1 pause_second=%2 source=%3")
            .arg(playbackTxn)
            .arg(state_.pauseSecond_, 0, 'f', 6)
            .arg(nativeDeviceCutoff
                     ? QStringLiteral("native_device_cutoff")
                     : (deviceChangePause ? QStringLiteral("device") : QStringLiteral("manual"))));
    state_.qtPreviewPendingTimelineSecond_ = state_.pauseSecond_;
    // Per docs/specs/timeline/TIMELINE_COORDINATE_FOCUS_SPEC.md §5: 播放中 → 点击暂停 → 暂停-R,
    // Timeline 聚焦 R. The next paused-flush must call
    // setPlayheadSeconds(s, /*centerView=*/true) so centerOnSecond
    // recomputes horizontalScrollValue around the freeze point. Was
    // false here, which left scroll wherever it was at the moment of
    // pause (often 0 if a startup race kept the playback timer's
    // ticks blocked) — visible symptom: timeline body locked at
    // chart start with playhead off-screen to the right.
    state_.qtPreviewPendingTimelineCenterView_ = true;
    state_.qtPreviewTimelineDirty_ = true;
    if (state_.scene_ != nullptr) {
        state_.scene_->setActivePlaybackProfilingEnabled(false);
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
    updatePreviewSliderPosition(state_.pauseSecond_);
    // beta4 leak gauge (LOW-FREQUENCY — once per user pause, never per frame): sample
    // process resource counters so a monotonic climb across edit→play→pause cycles localises
    // the reported "改多了就掉帧" leak. Gated on runtime debug output so it only costs the
    // resource-gauge query in --debug / diagnostic builds. See
    // docs/PREVIEW_FRAMEDROP_DIAGNOSIS_AND_FIX_SPEC_ZH.md.
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        // beta7 leak gauge — d_play_kb = private-bytes grown over the playback window (the big
        // previously-unmeasured slice); inflight/inflight_peak = outstanding worker→GUI queued
        // lambdas (≈0 here exonerates async backlog). Then arm the render thread to emit one
        // timeline/leak_gauge line (nodes/tex/d_render) on its next present, correlated by txn.
        const qint64 pausePrivBytes = miacode::diag::processPrivateBytes();
        const qint64 playStartPrivBytes = miacode::diag::leak_gauge::playStartPrivateBytes();
        const qint64 dPlayKb = (pausePrivBytes >= 0 && playStartPrivBytes >= 0)
            ? (pausePrivBytes - playStartPrivBytes) / 1024
            : 0;
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview/resource_gauge"),
            QStringLiteral("reason=pause_exact txn=%1 d_play_kb=%2 "
                           "inflight=%3 inflight_peak=%4 presents_in_play=%5 %6 %7")
                .arg(playbackTxn)
                .arg(dPlayKb)
                .arg(miacode::diag::leak_gauge::inflightDepth())
                .arg(miacode::diag::leak_gauge::inflightPeak())
                .arg(miacode::diag::leak_gauge::timelinePresentsSincePlayStart())
                .arg(miacode::diag::processResourceGaugePayload())
                .arg(state_.scene_ != nullptr
                         ? state_.scene_->resourceGaugePayload()
                         : QStringLiteral("scene_revision=-")));
        miacode::diag::leak_gauge::armRenderSample(pausePrivBytes, playbackTxn);
    }
    scheduleDeferredPreviewUiTail(
        false,
        true,
        true,
        false,
        false,
        true,
        state_.pauseSecond_,
        true,
        state_.previewFollowEnabled_);

    if (deviceChangePause) {
        if (nativeDeviceCutoff) {
            state_.previewDevicePauseSubmissionInProgress_ = false;
            appendPreviewPlaybackLog(
                QStringLiteral("device_change_pause_already_submitted"),
                QString("txn=%1 generation=%2 sequence=%3 token=%4 visual_second=%5")
                    .arg(state_.previewPendingDevicePauseTransactionId_)
                    .arg(state_.previewPendingDevicePauseGeneration_)
                    .arg(state_.previewPendingDevicePauseSequence_)
                    .arg(state_.previewPendingDevicePauseToken_)
                    .arg(state_.previewPendingDevicePauseWallSecond_, 0, 'f', 6));
            return;
        }
        const QtPreviewSfxRuntime::DevicePauseRequest request =
            state_.previewSfxRuntime_->requestDeviceChangePause(
                state_.previewPendingDevicePauseTransactionId_,
                state_.previewAudioDeviceChangeSequence_,
                state_.previewPendingDevicePauseToken_,
                state_.previewPendingDevicePauseWallSecond_);
        state_.previewDevicePauseSubmissionInProgress_ = false;
        if (request.post.accepted) {
            state_.previewPendingDevicePauseGeneration_ = request.identity.generation;
            state_.previewPendingDevicePauseSequence_ = request.identity.sequence;
            appendPreviewPlaybackLog(
                QStringLiteral("device_change_pause_submitted"),
                QString("txn=%1 generation=%2 sequence=%3 token=%4 visual_second=%5")
                    .arg(request.identity.transactionId)
                    .arg(request.identity.generation)
                    .arg(request.identity.sequence)
                    .arg(request.identity.pauseToken)
                    .arg(state_.previewPendingDevicePauseWallSecond_, 0, 'f', 6));
        } else {
            appendPreviewPlaybackLog(
                QStringLiteral("device_change_pause_submission_rejected"),
                QString("txn=%1 token=%2 error=%3")
                    .arg(state_.previewPendingDevicePauseTransactionId_)
                    .arg(state_.previewPendingDevicePauseToken_)
                    .arg(static_cast<int>(request.post.error)));
            state_.previewPendingDevicePauseGeneration_ = 0;
            state_.previewPendingDevicePauseTransactionId_ = 0;
            state_.previewPendingDevicePauseSequence_ = 0;
            state_.previewPendingDevicePauseToken_ = 0;
        }
        return;
    }

    const QtPreviewSfxRuntime::PauseSubmission request =
        state_.previewSfxRuntime_->requestManualPause(playbackTxn, wallClockPauseSecond);
    if (request.post.accepted) {
        state_.previewPendingManualPauseGeneration_ = request.identity.generation;
        state_.previewPendingManualPauseTransactionId_ = request.identity.transactionId;
        state_.previewPendingManualPauseSequence_ = request.identity.sequence;
        state_.previewPendingManualPauseWallSecond_ = request.visualFallbackSecond;
        appendPreviewPlaybackLog(
            QStringLiteral("manual_pause_submitted"),
            QString("txn=%1 generation=%2 sequence=%3 visual_second=%4")
                .arg(request.identity.transactionId)
                .arg(request.identity.generation)
                .arg(request.identity.sequence)
                .arg(request.visualFallbackSecond, 0, 'f', 6));
    } else {
        appendPreviewPlaybackLog(
            QStringLiteral("manual_pause_submission_rejected"),
            QString("txn=%1 error=%2")
                .arg(playbackTxn)
                .arg(static_cast<int>(request.post.error)));
    }
}

void miacode::runtime::PlaybackCoordinator::applyPreviewAudioDeviceCutoff(
    const miacode::preview_audio::PreviewAudioDeviceCutoff& cutoff)
{
    using miacode::preview_audio::device_change::Change;

    if (!cutoff.armedPlaybackWasCut || cutoff.change == Change::None) {
        return;
    }
    state_.previewAudioDeviceChangeSequence_ = qMax(
        state_.previewAudioDeviceChangeSequence_, cutoff.identity.deviceSequence);
    if (state_.previewPendingDevicePauseToken_ != 0) {
        appendPreviewPlaybackLog(
            QStringLiteral("device_change_pause_coalesced"),
            QString("device_change_seq=%1 pending_txn=%2 token=%3 wall_second=%4 source=native")
                .arg(cutoff.identity.deviceSequence)
                .arg(state_.previewPendingDevicePauseTransactionId_)
                .arg(state_.previewPendingDevicePauseToken_)
                .arg(state_.previewPendingDevicePauseWallSecond_, 0, 'f', 6));
        return;
    }
    if (!miacode::preview_audio::device_change::shouldPausePreview(
            cutoff.change, state_.playing_)) {
        appendPreviewPlaybackLog(
            QStringLiteral("device_change_ignored"),
            QString("device_change_seq=%1 change=%2 playing=%3 cutoff_second=%4 source=native")
                .arg(cutoff.identity.deviceSequence)
                .arg(QLatin1String(miacode::preview_audio::device_change::changeName(cutoff.change)))
                .arg(state_.playing_ ? 1 : 0)
                .arg(cutoff.cutoffSecond, 0, 'f', 6));
        return;
    }

    // Preserve the identity created on the native callback thread. This GUI path
    // only freezes visuals at that captured time; it never posts a second pause.
    state_.previewPendingDevicePauseGeneration_ = cutoff.identity.generation;
    state_.previewPendingDevicePauseTransactionId_ = cutoff.identity.transactionId;
    state_.previewPendingDevicePauseSequence_ = cutoff.identity.sequence;
    state_.previewPendingDevicePauseToken_ = cutoff.identity.pauseToken;
    state_.previewPendingDevicePauseWallSecond_ = cutoff.cutoffSecond;
    state_.previewDevicePauseSubmissionInProgress_ = false;
    appendPreviewPlaybackLog(
        QStringLiteral("device_change_pause_begin"),
        QString("device_change_seq=%1 txn=%2 change=%3 cutoff_second=%4 event_ns=%5 "
                "gui_delivery_ns=%6 gui_delay_ms=%7 emergency_attempted=%8 emergency_ok=%9 "
                "emergency_device=%10 emergency_error=%11 emergency_begin_ns=%12 emergency_end_ns=%13 "
                "generation=%14 sequence=%15 token=%16 posted=%17 source=native")
            .arg(cutoff.identity.deviceSequence)
            .arg(cutoff.identity.transactionId)
            .arg(QLatin1String(miacode::preview_audio::device_change::changeName(cutoff.change)))
            .arg(cutoff.cutoffSecond, 0, 'f', 6)
            .arg(cutoff.eventMonotonicNs)
            .arg(cutoff.guiDeliveryMonotonicNs)
            .arg(cutoff.eventMonotonicNs > 0 && cutoff.guiDeliveryMonotonicNs > 0
                     ? static_cast<double>(cutoff.guiDeliveryMonotonicNs - cutoff.eventMonotonicNs) / 1'000'000.0
                     : -1.0,
                 0,
                 'f',
                 3)
            .arg(cutoff.emergencyPauseAttempted ? 1 : 0)
            .arg(cutoff.emergencyPauseSucceeded ? 1 : 0)
            .arg(cutoff.emergencyPauseDeviceIndex)
            .arg(cutoff.emergencyPauseError)
            .arg(cutoff.emergencyPauseStartedNs)
            .arg(cutoff.emergencyPauseFinishedNs)
            .arg(cutoff.identity.generation)
            .arg(cutoff.identity.sequence)
            .arg(cutoff.identity.pauseToken)
            .arg(cutoff.post.accepted ? 1 : 0));
    pauseQtPreviewPlaybackExact(PauseSecondSource::NativeDeviceCutoff);
    // The worker can finish a tiny pause transaction before its queued GUI event is
    // delivered. Replay the retained completion from the runtime in that race so the
    // pending identity is not left behind; the later queued duplicate is harmlessly
    // rejected by the normal identity check.
    if (state_.previewSfxRuntime_ != nullptr) {
        if (const auto completed = state_.previewSfxRuntime_->takeCompletedDeviceChangeCutoff(
                cutoff.identity.sequence)) {
            handlePreviewRetainedPlaybackCompleted(*completed);
        }
    }
    appendPreviewPlaybackLog(
        QStringLiteral("device_change_pause_complete"),
        QString("device_change_seq=%1 txn=%2 generation=%3 sequence=%4 token=%5 playing=%6 "
                "pause_second=%7 source=native")
            .arg(cutoff.identity.deviceSequence)
            .arg(state_.previewPendingDevicePauseTransactionId_)
            .arg(state_.previewPendingDevicePauseGeneration_)
            .arg(state_.previewPendingDevicePauseSequence_)
            .arg(state_.previewPendingDevicePauseToken_)
            .arg(state_.playing_ ? 1 : 0)
            .arg(state_.pauseSecond_, 0, 'f', 6));
    updatePauseButtonAppearance();
}

void miacode::runtime::PlaybackCoordinator::pausePreviewForAudioDeviceChange(
    miacode::preview_audio::device_change::Change change)
{
    const quint64 deviceChangeSequence = ++state_.previewAudioDeviceChangeSequence_;
    if (change == miacode::preview_audio::device_change::Change::None) {
        appendPreviewPlaybackLog(
            QStringLiteral("device_change_ignored"),
            QString("device_change_seq=%1 change=none playing=%2")
                .arg(deviceChangeSequence)
                .arg(state_.playing_ ? 1 : 0));
        return;
    }
    if (state_.previewPendingDevicePauseToken_ != 0) {
        appendPreviewPlaybackLog(
            QStringLiteral("device_change_pause_coalesced"),
            QString("device_change_seq=%1 pending_txn=%2 token=%3 wall_second=%4")
                .arg(deviceChangeSequence)
                .arg(state_.previewPendingDevicePauseTransactionId_)
                .arg(state_.previewPendingDevicePauseToken_)
                .arg(state_.previewPendingDevicePauseWallSecond_, 0, 'f', 6));
        return;
    }
    if (!miacode::preview_audio::device_change::shouldPausePreview(change, state_.playing_)) {
        appendPreviewPlaybackLog(
            QStringLiteral("device_change_ignored"),
            QString("device_change_seq=%1 change=%2 playing=%3 startup_pending=%4 pause_second=%5")
                .arg(deviceChangeSequence)
                .arg(QLatin1String(miacode::preview_audio::device_change::changeName(change)))
                .arg(state_.playing_ ? 1 : 0)
                .arg(state_.previewStartupSyncPending_ ? 1 : 0)
                .arg(state_.pauseSecond_, 0, 'f', 6));
        return;
    }

    state_.previewPendingDevicePauseTransactionId_ = state_.activePreviewPlaybackTransactionId_;
    state_.previewPendingDevicePauseToken_ = deviceChangeSequence;
    state_.previewPendingDevicePauseWallSecond_ =
        authoritativeAudioClockSecond();
    state_.previewDevicePauseSubmissionInProgress_ = true;
    appendPreviewPlaybackLog(
        QStringLiteral("device_change_pause_begin"),
        QString("device_change_seq=%1 txn=%2 change=%3 playing=%4 startup_pending=%5 "
                "pause_second=%6 authoritative_second=%7 pending_play_op=%8")
            .arg(deviceChangeSequence)
            .arg(state_.previewPendingDevicePauseTransactionId_)
            .arg(QLatin1String(miacode::preview_audio::device_change::changeName(change)))
            .arg(state_.playing_ ? 1 : 0)
            .arg(state_.previewStartupSyncPending_ ? 1 : 0)
            .arg(state_.pauseSecond_, 0, 'f', 6)
            .arg(state_.previewPendingDevicePauseWallSecond_, 0, 'f', 6)
            .arg(state_.previewPendingPlayInteractionId_));
    pauseQtPreviewPlaybackExact(PauseSecondSource::AudioPosition);
    if (state_.previewDevicePauseSubmissionInProgress_) {
        state_.previewDevicePauseSubmissionInProgress_ = false;
        appendPreviewPlaybackLog(
            QStringLiteral("device_change_pause_submission_skipped"),
            QString("device_change_seq=%1 txn=%2 reason=no_runtime")
                .arg(deviceChangeSequence)
                .arg(state_.previewPendingDevicePauseTransactionId_));
        state_.previewPendingDevicePauseGeneration_ = 0;
        state_.previewPendingDevicePauseTransactionId_ = 0;
        state_.previewPendingDevicePauseSequence_ = 0;
        state_.previewPendingDevicePauseToken_ = 0;
    }
    appendPreviewPlaybackLog(
        QStringLiteral("device_change_pause_complete"),
        QString("device_change_seq=%1 txn=%2 generation=%3 sequence=%4 token=%5 playing=%6 pause_second=%7")
            .arg(deviceChangeSequence)
            .arg(state_.previewPendingDevicePauseTransactionId_)
            .arg(state_.previewPendingDevicePauseGeneration_)
            .arg(state_.previewPendingDevicePauseSequence_)
            .arg(state_.previewPendingDevicePauseToken_)
            .arg(state_.playing_ ? 1 : 0)
            .arg(state_.pauseSecond_, 0, 'f', 6));
    updatePauseButtonAppearance();
}

void miacode::runtime::PlaybackCoordinator::emitChartSwitchResourceGauge()
{
    // Chart-switch leak gauge. The pause handler above is the ONLY other site that
    // arms a render sample, which left a chart switch (the scenario in
    // docs/audit/CHART_SWITCH_RESOURCE_RELEASE_AUDIT_ZH.md) as a zero-log event:
    // measuring it required an artificial "play a few seconds, then pause" step
    // after every switch, and any round where the timeline tab happened to be
    // hidden silently dropped its sample. Arming here makes the switch itself the
    // sampling point, which also covers the common "only ever switches, never
    // plays" usage.
    //
    // Semantics: the counters this arms are read on the NEXT timeline present, so
    // a line reports the state the process is ENTERING this chart with — i.e. it
    // carries the accumulation of every previous switch, not this switch's own
    // texture builds. That is exactly what detecting accumulation needs: across an
    // A→B→A→B… loop, `tex` / `tex_pix` at "entering A" must be flat. A monotonic
    // climb is the leak signature (F-1); a flat count with climbing private bytes
    // is Qt RHI deferred release, not our accumulation.
    //
    // Same gate as every other leak-gauge site, so it costs nothing outside --debug.
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    // Disjoint from the playback transaction sequence (a small counter starting at
    // 1), so the render-thread `timeline/leak_gauge txn=` value pairs with exactly
    // one `preview/resource_gauge` line and never straddles the two sources.
    constexpr quint64 kChartSwitchGaugeTxnBase = 1ULL << 32;
    const quint64 gaugeTxn = kChartSwitchGaugeTxnBase + (++state_.chartSwitchGaugeTxnCounter_);
    const qint64 switchPrivBytes = miacode::diag::processPrivateBytes();
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/resource_gauge"),
        QStringLiteral("reason=chart_switch txn=%1 switch_seq=%2 difficulty=%3 "
                       "inflight=%4 inflight_peak=%5 %6 %7")
            .arg(gaugeTxn)
            .arg(state_.chartSwitchGaugeTxnCounter_)
            .arg(state_.activeDifficultyId_)
            .arg(miacode::diag::leak_gauge::inflightDepth())
            .arg(miacode::diag::leak_gauge::inflightPeak())
            .arg(miacode::diag::processResourceGaugePayload())
            .arg(state_.scene_ != nullptr
                     ? state_.scene_->resourceGaugePayload()
                     : QStringLiteral("scene_revision=-")));
    miacode::diag::leak_gauge::armRenderSample(switchPrivBytes, gaugeTxn);
}

void miacode::runtime::PlaybackCoordinator::pauseQtPreviewPlaybackForReanchor()
{
    const bool wasPlaying = state_.playing_;
    const quint64 playbackTxn = state_.activePreviewPlaybackTransactionId_;
    if (!wasPlaying || state_.previewSfxRuntime_ == nullptr) {
        stopQtPreviewPlayback(true);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    // G1 Commit 6: pause-second from wall-clock master; see pauseQtPreviewPlaybackExact
    // for rationale.
    const double wallClockPauseSecond = authoritativeAudioClockSecond();
    miacode::runtime::shared::writePreviewPauseSecond(
        playbackState_.pauseSecond_, wallClockPauseSecond, playbackState_.playing_, "pause_qt_preview_playback_for_reanchor");
    cancelPreviewStartupSync("pause_qt_preview_playback_for_reanchor");
    clearPreviewPlayingRetainedSeek();
    pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = state_.pauseSecond_;
    // Spec: pause-for-reanchor lands in 暂停-R, so request R-centring.
    state_.qtPreviewPendingTimelineCenterView_ = true;
    state_.qtPreviewTimelineDirty_ = true;
    miacode::runtime::shared::writePreviewPlayingFlag(playbackState_, services_.shellNotifications(), false);
    services_.editorSync().setPlaybackActive(false);
    if (state_.scene_ != nullptr) {
        state_.scene_->setActivePlaybackProfilingEnabled(false);
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
        QString("txn=%1 pause_second=%2 elapsed_ms=%3")
            .arg(playbackTxn)
            .arg(state_.pauseSecond_, 0, 'f', 6)
            .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3));
    const QtPreviewSfxRuntime::PauseSubmission request =
        state_.previewSfxRuntime_->requestManualPause(playbackTxn, wallClockPauseSecond);
    if (request.post.accepted) {
        state_.previewPendingManualPauseGeneration_ = request.identity.generation;
        state_.previewPendingManualPauseTransactionId_ = request.identity.transactionId;
        state_.previewPendingManualPauseSequence_ = request.identity.sequence;
        state_.previewPendingManualPauseWallSecond_ = request.visualFallbackSecond;
    }
}

void miacode::runtime::PlaybackCoordinator::softStopQtPreviewPlaybackToSecond(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        stopQtPreviewPlayback(true);
    } else if (state_.playing_) {
        pauseQtPreviewPlaybackExact();
    }
    anchorQtPreviewPlaybackToSecond(clampedSecond, centerView);
}

void miacode::runtime::PlaybackCoordinator::anchorQtPreviewPlaybackToSecond(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    preview_.ensurePreviewStageMediaRouteInitialized();
    cancelPreviewStartupSync("anchor_qt_preview_playback_to_second");
    clearPreviewPlayingRetainedSeek();
    pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    miacode::runtime::shared::writePreviewPauseSecond(
        playbackState_.pauseSecond_, clampedSecond, playbackState_.playing_, "anchor_qt_preview_playback_to_second");
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = clampedSecond;
    state_.qtPreviewPendingTimelineCenterView_ = centerView;
    state_.qtPreviewTimelineDirty_ = true;
    miacode::runtime::shared::writePreviewPlayingFlag(playbackState_, services_.shellNotifications(), false);
    services_.editorSync().setPlaybackActive(false);
    if (state_.scene_ != nullptr) {
        state_.scene_->setActivePlaybackProfilingEnabled(false);
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
        appendPreviewPlaybackLog(
            QStringLiteral("retained_reset_submitted"),
            QString("cause=anchor_qt_preview_playback_to_second target=%1 generation_before=%2 "
                    "manual_pause_generation=%3 manual_pause_txn=%4 manual_pause_sequence=%5")
                .arg(clampedSecond, 0, 'f', 6)
                .arg(state_.previewSfxRuntime_->playbackGeneration())
                .arg(state_.previewPendingManualPauseGeneration_)
                .arg(state_.previewPendingManualPauseTransactionId_)
                .arg(state_.previewPendingManualPauseSequence_));
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

// Stage 4.9d-3 (D-class): moved verbatim from DocumentSessionHost::updatePauseButtonAppearance
// (document/DocumentEditorState.cpp) — pure ui_/state_ widget presentation, no
// DocumentSessionHost-own state. Stage 4.9d-4b-1 retargeted the queued emit
// from Session::presentationChanged (which only forwarded it) straight to
// ShellNotifications, the signal's real destination — see SessionBootstrap.cpp
// for the forwarding connect this bypasses.
void miacode::runtime::PlaybackCoordinator::updatePauseButtonAppearance()
{
    // The play/pause presentation changed, and the QML transport's button reads
    // the same condition this one does — playing_ OR the export
    // intro's lead-in. setPreviewPlayingFlag announces the first; nothing
    // announced the second, so the intro played with the transport still
    // showing a play button. Announced before the widget guard, because a v1
    // action that no longer exists is not a reason to leave the shell stale,
    // and queued for the same reason that writer is: callers reach here from
    // the middle of a transition.
    QMetaObject::invokeMethod(
        &services_.shellNotifications(),
        [this]() { emit services_.shellNotifications().presentationChanged(); },
        Qt::QueuedConnection);
    const bool previewPlaying = state_.playing_ || state_.exportIntroLeadInActive_;
    if (ui_.pausePreviewButton_ != nullptr) {
        ui_.pausePreviewButton_->setText(
            previewPlaying
                ? UiText::text(QStringLiteral("preview.pause"))
                : UiText::text(QStringLiteral("preview.play"))
        );
        ui_.pausePreviewButton_->setStyleSheet(
            state_.previewFullscreenActive_
                ? previewFullscreenPauseButtonStyleSheet(previewPlaying)
                : UiTheme::pausePreviewButtonStyleSheet(previewPlaying)
        );
    }
}
