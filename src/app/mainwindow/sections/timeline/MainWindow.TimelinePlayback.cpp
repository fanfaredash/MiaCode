#include "MainWindow.TimelineSection.h"
#include "app/v2/EditorSyncController.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "audio/PreviewAudioPlaybackFlowPolicy.h"
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
#include "MainWindow.TimelinePlayback.Internal.h"

using namespace miacode::mainwindow::shared;

using namespace miacode::mainwindow::timeline_playback_detail;


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


void MainWindow::TimelineSection::seekPreviewToSecond(double second, bool centerView)
{
    owner_.ensurePreviewStageMediaRouteInitialized();
    owner_.ensurePreviewSfxRuntimePrepared();
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    // The negative-time 片头 region is preview-only and lives left of chart 0;
    // ANY funnel seek targets a real chart position (clamped >= 0), so it must
    // leave the region. Otherwise exportIntroRegionActive_ lingers and
    // shellPreviewPositionSeconds() keeps reporting the frozen negative intro
    // playhead (stuck thumb) while the play toggle takes the dead region branch.
    if (state_.exportIntroRegionActive_) {
        exitExportIntroRegion();
    }
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
        const double workerSecondBeforeSeek = state_.previewSfxRuntime_->authoritativePlaybackSecond();
        const QtPreviewSfxRuntime::PlaybackSubmission submission =
            state_.previewSfxRuntime_->seekRetainedPreviewPlaybackTransaction(clampedSecond, true);
        if (!submission.post.accepted) {
            appendPreviewPlaybackLog(
                QStringLiteral("playing_seek_submission_rejected"),
                QString("txn=%1 generation=%2 sequence=%3 error=%4")
                    .arg(submission.identity.transactionId)
                    .arg(submission.identity.generation)
                    .arg(submission.identity.sequence)
                    .arg(static_cast<int>(submission.post.error)));
            return;
        }
        miacode::preview_audio::playback_flow::State running;
        running.audioPrepared = true;
        running.uiPlaying = state_.qtPreviewPlaying_;
        const miacode::preview_audio::playback_flow::State pending =
            miacode::preview_audio::playback_flow::beginPlayingRetainedSeek(
                running,
                miacode::preview_audio::playback_flow::Request{
                    submission.identity.generation,
                    submission.identity.transactionId,
                    submission.identity.sequence,
                    clampedSecond,
                },
                workerSecondBeforeSeek,
                centerView);
        state_.previewPlayingSeekAudioGeneration_ = pending.currentGeneration;
        state_.previewPlayingSeekPendingSequence_ = pending.pendingPlayingSeekSequence;
        state_.previewPlayingSeekCenterView_ = pending.pendingPlayingSeekCenterView;
        state_.previewPlayingSeekVisualSecond_ = pending.visualSecond;
        state_.previewPlayingSeekWorkerSecond_ = pending.effectiveWorkerSecond;
        state_.qtPreviewPendingTimelineSecond_ = clampedSecond;
        state_.qtPreviewPendingTimelineCenterView_ = centerView;
        state_.qtPreviewTimelineDirty_ = true;
        state_.qtPreviewLastTimelineSecond_ = -1.0;
        if (state_.previewCanvas_ != nullptr) {
            state_.previewCanvas_->setPlayheadSeconds(clampedSecond, false);
        }
        owner_.syncPreviewStageMediaRoutePlayback(clampedSecond);
        updatePreviewSliderPosition(clampedSecond);
        appendPreviewPlaybackLog(
            QStringLiteral("playing_seek_requested"),
            QString("txn=%1 generation=%2 sequence=%3 visual=%4 fallback=%5")
                .arg(submission.identity.transactionId)
                .arg(submission.identity.generation)
                .arg(submission.identity.sequence)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(submission.fallbackSecond, 0, 'f', 6));
        return;
    }
    if (state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        cancelPreviewStartupSync("seek_preview_to_second");
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
    // Leave the negative-time 片头 region on any discrete seek to a chart
    // position (see seekPreviewToSecond) — keeps the region flag from going
    // stale when an arrow/jump/stop lands the playhead back at chart >= 0.
    if (state_.exportIntroRegionActive_) {
        exitExportIntroRegion();
    }
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
        cancelPreviewStartupSync("seek_preview_discrete_to_second");
    } else if (state_.qtPreviewPlaying_) {
        pauseQtPreviewPlaybackForReanchor();
    }

    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    miacode::mainwindow::shared::writePreviewPauseSecond(
        state_.qtPreviewPauseSecond_, clampedSecond, state_.qtPreviewPlaying_, "seek_preview_discrete_to_second");
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = clampedSecond;
    state_.qtPreviewPendingTimelineCenterView_ = centerView;
    state_.qtPreviewTimelineDirty_ = true;
    owner_.setPreviewPlayingFlag(false);
    owner_.editorSyncController().setPlaybackActive(false);
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
        if (state_.previewSfxRuntime_ != nullptr) {
            state_.previewSfxRuntime_->armDeviceChangeCutoffClock(
                chartNow,
                state_.previewPlaybackRate_,
                state_.activePreviewPlaybackTransactionId_);
        }
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
    //
    // BRANCH ORDER IS LOAD-BEARING — the playing branch MUST stay above the
    // startup-freeze branch. The two startup flags below mean opposite things:
    //
    //   previewStartupSyncPending_     pre-commit. Audio has not started; the
    //                                  UI must not run ahead. Freezing is right.
    //   previewLateVideoStartPending_  POST-commit. tryCommitPreviewStartupSync
    //                                  already ran finalizeQtPreviewPlaybackStart,
    //                                  so qtPreviewPlaying_ is true and
    //                                  qtPreviewElapsed_ has been restarted — the
    //                                  wall clock is authoritative. Only the weak
    //                                  PV member is still catching up. Freezing
    //                                  here is WRONG.
    //
    // d65de51e hoisted the freeze branch to the top of this function because the
    // branch that then sat below it (previewSfxRuntime_->authoritativePlaybackSecond())
    // was unconditional, which would have made the freeze unreachable. 90ec7c48
    // deleted that BASS-cursor branch but left the hoist behind. With retained
    // resume folded into the async strong group (0d013404) the leftover hoist
    // became reachable on every mid-chart play of a PV chart: the timeline
    // playhead froze at the start second for up to kMediaSeekPrepareTimeoutMs
    // (800 ms) while the preview canvas and transport slider kept advancing on
    // the wall clock, then jumped when the PV seek acked. Same defect also made
    // a pause/stop/rate-change inside that window re-anchor to the frozen second.
    //
    // "lateVideoPending && !playing" cannot occur: the flag is only raised in
    // tryCommitPreviewStartupSync, which sets qtPreviewPlaying_ in the same call.
    if (qtPreviewPlaying_) {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        return qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    }
    if (previewStartupSyncPending_ || previewLateVideoStartPending_) {
        return previewStartupPreparedSecond_;
    }
    return qtPreviewPauseSecond_;
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
    cancelPreviewStartupSync("start_qt_preview_playback");
    clearPreviewPlayingRetainedSeek();
    const double startupWorkerSecond = state_.previewSfxRuntime_ != nullptr
        ? state_.previewSfxRuntime_->authoritativePlaybackSecond()
        : 0.0;
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
    // Stamp the transaction tag on both backends HERE, above the retained/cold branch,
    // so both start paths carry it by construction.
    //
    // These two setters are log tags only — every use of playbackTransactionId_ inside
    // BassPreviewAudioBackend and PreviewStageMediaHost is string formatting. The
    // functional staleness guard is a different member,
    // PreviewStageMediaHost::preparedPlaybackTransaction_, set by preparePlaybackStart()
    // further down; that stays on the cold path because it protects the async
    // prepare/commit handshake, which the retained path does not have.
    //
    // They used to live in the cold path only. The retained fast-resume path returns
    // before reaching them, and in practice nearly every real playback takes it (a
    // paused scrub re-anchors the retained state, so the resume is always the retained
    // branch), which left the audio backend stamping txn=0 on every bass_status line
    // for the whole session. That silently removed the only handle for correlating
    // audio-channel lines with the runtime channel's preview/resource_gauge — the
    // correlation docs/audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md phase A asks an
    // investigator to use.
    bool forceFreshAudioPrepare = false;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setPlaybackTransactionId(playbackTxn);
        // Only an explicit Play request reopens command submission after a physical
        // device cutoff. The previous session's queued/tail audio stays discarded.
        forceFreshAudioPrepare = state_.previewSfxRuntime_->beginManualPlaybackAfterDeviceCutoff();
    }
    if (state_.previewStageMediaHost_ != nullptr) {
        state_.previewStageMediaHost_->setPlaybackTransactionId(playbackTxn);
    }
    const auto applyPlaybackClockState = [this](double initialSecond) {
        state_.qtPreviewStartSecond_ = initialSecond;
        miacode::mainwindow::shared::writePreviewPauseSecond(
            state_.qtPreviewPauseSecond_, initialSecond, state_.qtPreviewPlaying_, "start_qt_preview_playback");
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
    state_.previewStartupSyncPending_ = true;
    state_.previewLateVideoStartPending_ = false;
    state_.previewStartupAudioPrepared_ = false;
    state_.previewStartupCanvasPresented_ = state_.previewCanvas_ == nullptr;
    state_.previewStartupStrongGroupCommitted_ = false;
    state_.previewStartupResumeFromPause_ = resumeFromPause;
    state_.previewStartupVideoPrepareStarted_ = hasVideoMedia;
    state_.previewStartupVideoPrepared_ = false;
    state_.previewStartupVideoStarted_ = false;
    state_.previewStartupAudioGeneration_ = 0;
    state_.previewStartupPendingPrepareSequence_ = 0;
    state_.previewStartupPendingRetainedSequence_ = 0;
    state_.previewStartupRequestedSecond_ = 0.0;
    state_.previewStartupVisualSecond_ = 0.0;
    state_.previewStartupPreparedSecond_ = 0.0;
    appendPreviewPlaybackLog(
        QStringLiteral("start_request"),
        QString("txn=%1 requested=%2 resume=%3 rate=%4 has_video=%5 duration=%6")
            .arg(playbackTxn)
            .arg(startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(state_.previewPlaybackRate_, 0, 'f', 3)
            .arg(hasVideoMedia ? 1 : 0)
            .arg(previewDurationSeconds(), 0, 'f', 6));

    QtPreviewSfxRuntime::PlaybackSubmission audioSubmission;
    bool retainedStartup = false;
    bool retainedStartupUsedSeek = false;
    int retainedModeValue = -1;
    if (resumeFromPause && !forceFreshAudioPrepare && state_.previewSfxRuntime_ != nullptr) {
        const QtPreviewSfxRuntime::RetainedPlaybackMode retainedMode =
            state_.previewSfxRuntime_->retainedPlaybackMode();
        if (retainedMode == QtPreviewSfxRuntime::RetainedPlaybackMode::PausedExact
            || retainedMode == QtPreviewSfxRuntime::RetainedPlaybackMode::PausedAnchored) {
            retainedStartup = true;
            retainedModeValue = static_cast<int>(retainedMode);
            if (retainedMode == QtPreviewSfxRuntime::RetainedPlaybackMode::PausedExact
                && qAbs(state_.previewSfxRuntime_->authoritativePlaybackSecond() - requestedSecond)
                    <= kTimelineZeroSecondTolerance) {
                audioSubmission = state_.previewSfxRuntime_->resumeRetainedPreviewPlaybackTransaction();
            } else {
                retainedStartupUsedSeek = true;
                audioSubmission = state_.previewSfxRuntime_->seekRetainedPreviewPlaybackTransaction(
                    requestedSecond,
                    true);
            }
        }
    }
    if (!retainedStartup && state_.previewSfxRuntime_ != nullptr) {
        audioSubmission = state_.previewSfxRuntime_->preparePreviewPlaybackTransaction(
            startSecond,
            resumeFromPause,
            state_.previewPlaybackRate_);
    }
    state_.previewStartupAudioGeneration_ = audioSubmission.identity.generation;
    if (retainedStartup) {
        state_.previewStartupPendingRetainedSequence_ = audioSubmission.identity.sequence;
    } else {
        state_.previewStartupPendingPrepareSequence_ = audioSubmission.identity.sequence;
    }
    if (!audioSubmission.post.accepted) {
        appendPreviewPlaybackLog(
            QStringLiteral("audio_submission_rejected"),
            QString("txn=%1 generation=%2 sequence=%3 retained=%4 error=%5")
                .arg(playbackTxn)
                .arg(audioSubmission.identity.generation)
                .arg(audioSubmission.identity.sequence)
                .arg(retainedStartup ? 1 : 0)
                .arg(static_cast<int>(audioSubmission.post.error)));
        cancelPreviewStartupSync("audio_submission_rejected");
        return false;
    }

    // The visual playhead is intentionally optimistic. The completion handler
    // writes previewStartupPreparedSecond_ only after the worker confirms it.
    const double visualStartSecond = retainedStartup ? requestedSecond : startSecond;
    const miacode::preview_audio::playback_flow::CompletionKind startupCompletionKind =
        !retainedStartup
            ? miacode::preview_audio::playback_flow::CompletionKind::Prepare
            : (retainedStartupUsedSeek
                   ? miacode::preview_audio::playback_flow::CompletionKind::RetainedSeek
                   : miacode::preview_audio::playback_flow::CompletionKind::RetainedResume);
    const miacode::preview_audio::playback_flow::State pendingStartup =
        miacode::preview_audio::playback_flow::beginStartupPlayback(
            {},
            miacode::preview_audio::playback_flow::Request{
                audioSubmission.identity.generation,
                audioSubmission.identity.transactionId,
                audioSubmission.identity.sequence,
                visualStartSecond,
            },
            startupCompletionKind,
            startupWorkerSecond);
    state_.previewStartupAudioGeneration_ = pendingStartup.currentGeneration;
    state_.previewStartupPendingPrepareSequence_ = pendingStartup.pendingPrepareSequence;
    state_.previewStartupPendingRetainedSequence_ = pendingStartup.pendingRetainedSequence;
    state_.previewStartupRequestedSecond_ = pendingStartup.requestedVisualSecond;
    state_.previewStartupVisualSecond_ = pendingStartup.visualSecond;
    state_.previewStartupPreparedSecond_ = pendingStartup.effectiveWorkerSecond;
    applyPlaybackClockState(visualStartSecond);
    state_.pausedPreviewMediaSeekPending_ = false;
    state_.qtPreviewPendingTimelineSecond_ = visualStartSecond;
    state_.qtPreviewPendingTimelineCenterView_ = true;
    state_.qtPreviewTimelineDirty_ = true;
    state_.qtPreviewLastTimelineSecond_ = -1.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setPlaybackEntrySeconds(state_.qtPreviewPlaybackReturnSecond_);
        state_.timelineQuickStateBridge_->setPlayheadUpperLimitSeconds(state_.qtPreviewPlaybackEndSecond_);
        state_.timelineQuickStateBridge_->setPlayheadSeconds(visualStartSecond, false);
    }
    if (state_.previewCanvas_ != nullptr) {
        if (!resumeFromPause) {
            state_.previewCanvas_->resetProfilingSession();
        }
        state_.previewCanvas_->setPlayheadSeconds(visualStartSecond, true);
    }
    if (hasVideoMedia && state_.previewStageMediaHost_ != nullptr) {
        // The stage-media callback stays on the GUI thread and remains part of
        // the existing strong-group prepare/commit handshake.
        state_.previewStageMediaHost_->preparePlaybackStart(visualStartSecond, playbackTxn);
    }
    appendPreviewPlaybackLog(
        retainedStartup ? QStringLiteral("retained_start_request") : QStringLiteral("audio_prepare_requested"),
        QString("txn=%1 generation=%2 sequence=%3 visual=%4 fallback=%5 retained_mode=%6")
            .arg(playbackTxn)
            .arg(audioSubmission.identity.generation)
            .arg(audioSubmission.identity.sequence)
            .arg(visualStartSecond, 0, 'f', 6)
            .arg(audioSubmission.fallbackSecond, 0, 'f', 6)
            .arg(retainedModeValue));
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
    if (hadStartupSync && !wasPlaying) {
        miacode::preview_audio::playback_flow::State pendingStartup;
        pendingStartup.visualSecond = state_.previewStartupVisualSecond_;
        miacode::mainwindow::shared::writePreviewPauseSecond(
            state_.qtPreviewPauseSecond_,
            miacode::preview_audio::playback_flow::visualSecondAfterStartupCancellation(pendingStartup),
            state_.qtPreviewPlaying_,
            "stop_qt_preview_playback");
        pauseSecondCaptured = true;
    }
    if (!pauseSecondCaptured) {
        miacode::mainwindow::shared::writePreviewPauseSecond(
            state_.qtPreviewPauseSecond_, owner_.currentPreviewAuthoritativeAudioClockSecond(), state_.qtPreviewPlaying_, "stop_qt_preview_playback");
        pauseSecondCaptured = true;
    }
    cancelPreviewStartupSync("stop_qt_preview_playback");
    clearPreviewPlayingRetainedSeek();
    owner_.pausePreviewStageMediaRoutePlayback();
    stopQtPreviewTimers();
    if (!keepPosition) {
        miacode::mainwindow::shared::writePreviewPauseSecond(
            state_.qtPreviewPauseSecond_, 0.0, state_.qtPreviewPlaying_, "stop_qt_preview_playback");
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
    owner_.setPreviewPlayingFlag(false);
    owner_.editorSyncController().setPlaybackActive(false);
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
        state_.previewSfxRuntime_->disarmDeviceChangeCutoffClock();
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
    const QString title = UiText::text(QStringLiteral("timeline.playback_speed"));
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

void MainWindow::syncPausedPreviewMediaTimestamps(double second)
{
    timelineSection_->syncPausedPreviewMediaTimestamps(second);
}

void MainWindow::flushQtPreviewTimelinePosition()
{
    timelineSection_->flushQtPreviewTimelinePosition();
}

void MainWindow::onTimelineRenderCadenceTick()
{
    timelineSection_->onTimelineRenderCadenceTick();
}

void MainWindow::onTimelineCadenceWatchdogTick()
{
    timelineSection_->onTimelineCadenceWatchdogTick();
}

void MainWindow::onQtPreviewTick()
{
    timelineSection_->onQtPreviewTick();
}

void MainWindow::jumpToNearestTimelineNote(double second, int lane)
{
    timelineSection_->jumpToNearestTimelineNote(second, lane);
}
