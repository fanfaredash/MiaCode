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
#include "common/ProcessDiagnostics.h"
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
    // Position the clock count-in cursor for this playback start (skips ticks before
    // the start second without replaying them; the downbeat at 0 fires on the first
    // tick after the 片头 hand-off).
    resetExportAuditionClockCursor(effectiveStartSecond);
    state_.qtPreviewStartSecond_ = effectiveStartSecond;
    state_.qtPreviewPauseSecond_ = effectiveStartSecond;
    state_.qtPreviewElapsed_.restart();
    state_.qtPreviewTimelineElapsed_.restart();
    state_.qtPreviewPlaying_ = true;
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("preview.playback.changed"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("preview")},
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("state"), QStringLiteral("playing")},
                {QStringLiteral("second"), effectiveStartSecond},
            }},
        });
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

void MainWindow::TimelineSection::pauseQtPreviewPlaybackExact(PauseSecondSource pauseSecondSource)
{
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
    double effectivePauseSecond = wallClockPauseSecond;
    // The one case where the wall clock is NOT the truth. An output-device switch stalls
    // the whole process; the wall clock is a QElapsedTimer and sails straight through it,
    // while the audio does not. Measured on a Windows capture: wall 6.071 s against a BGM
    // that had only reached 5.090 s — 0.98 s that was never rendered and never heard.
    // Pausing at the wall value parks the playhead almost a second past the last sound the
    // user got, and leaves the GUI and the backend disagreeing by exactly the stall.
    //
    // Only accepted when it is BEHIND the wall clock: the BGM position can never
    // legitimately lead it, so a leading value means something other than a stall and the
    // wall clock stays authoritative.
    if (pauseSecondSource == PauseSecondSource::AudioPosition
        && pauseResult.usedBackgroundTrack
        && qIsFinite(pauseResult.pauseSecond)
        && pauseResult.pauseSecond >= 0.0
        && pauseResult.pauseSecond < wallClockPauseSecond) {
        effectivePauseSecond = pauseResult.pauseSecond;
        appendPreviewPlaybackLog(
            QStringLiteral("pause_second_from_audio"),
            QString("txn=%1 wall_second=%2 audio_second=%3 stall_ms=%4")
                .arg(playbackTxn)
                .arg(wallClockPauseSecond, 0, 'f', 6)
                .arg(pauseResult.pauseSecond, 0, 'f', 6)
                .arg((wallClockPauseSecond - pauseResult.pauseSecond) * 1000.0, 0, 'f', 3));
    }
    state_.qtPreviewPauseSecond_ = effectivePauseSecond;
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
    state_.qtPreviewPlaying_ = false;
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("preview.playback.changed"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("preview")},
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("state"), QStringLiteral("paused")},
                {QStringLiteral("second"), state_.qtPreviewPauseSecond_},
            }},
        });
    }
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
        const qint64 pausePrivBytes = miacode::diag::processPrivateBytes();
        const qint64 playStartPrivBytes = miacode::diag::leak_gauge::playStartPrivateBytes();
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
                .arg(miacode::diag::leak_gauge::inflightDepth())
                .arg(miacode::diag::leak_gauge::inflightPeak())
                .arg(miacode::diag::leak_gauge::timelinePresentsSincePlayStart())
                .arg(miacode::diag::processResourceGaugePayload())
                .arg(state_.previewCanvas_ != nullptr
                         ? state_.previewCanvas_->resourceGaugePayload()
                         : QStringLiteral("scene_revision=-")));
        miacode::diag::leak_gauge::armRenderSample(pausePrivBytes, playbackTxn);
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

void MainWindow::TimelineSection::pausePreviewForAudioDeviceChange(
    miacode::preview_audio::device_change::Change change)
{
    // The whole fix for 问题 3 (docs/audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md §1). The preview
    // clock anchors and advances; it does not follow the device. A hotplug or default-output
    // switch leaves that anchor stale for the rest of the session, and three attempts to
    // correct it silently while playback continued did not work. Pause is the correction the
    // audit proved works (F3-2) — pause anchors, resume re-starts from the fresh anchor — so
    // the app now performs it the moment the device configuration changes and hands the
    // resume back to the user.
    if (!miacode::preview_audio::device_change::shouldPausePreview(change, state_.qtPreviewPlaying_)) {
        // Not playing: nothing has a stale anchor. This is also what makes the burst of
        // notifications Qt emits for one physical hotplug safe, and what keeps a video
        // export untouched (MainWindow.ExportFlow.cpp pauses the preview before it starts).
        return;
    }
    appendPreviewPlaybackLog(
        QStringLiteral("pause_audio_device_change"),
        QString("txn=%1 change=%2 second=%3")
            .arg(state_.activePreviewPlaybackTransactionId_)
            .arg(QLatin1String(miacode::preview_audio::device_change::changeName(change)))
            .arg(owner_.currentPreviewAuthoritativeAudioClockSecond(), 0, 'f', 6));
    // Same path as the pause button -- timeline centring and the
    // preview.playback.changed extension event are identical to a manual pause -- except
    // for where the pause second comes from. See PauseSecondSource: the device switch
    // stalls the process, and the wall clock is the one thing that does not stall with it.
    pauseQtPreviewPlaybackExact(PauseSecondSource::AudioPosition);
    // The one behaviour that differs from a manual pause, and it lives here rather
    // than inside suspendPlaybackTransport so the pause button keeps its feel.
    //
    // Measured on two captures: the last group before the pause started `answer`
    // (0.352 s) AND `judge` -> tap_perfect.wav (0.875 s), only 58 ms and 286 ms
    // before the pause line. Nothing stops a one-shot -- suspendPlaybackTransport
    // silences BGM and touchhold and lets note sounds self-terminate -- so up to
    // 0.8 s of note audio outlives the visible pause, and the macOS route switch
    // mutes the output in the middle of it. What the user hears is silence, then a
    // stray note, after the preview has already stopped.
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->stopSfxVoices();
    }
    // Not a notification — state correctness. Without it the button keeps reading
    // "playing" while playback is stopped. The pause itself stays silent: no status-bar
    // message, no dialog.
    owner_.updatePauseButtonAppearance();
}

void MainWindow::TimelineSection::emitChartSwitchResourceGauge()
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
                       "qobject_descendants=%4 inflight=%5 inflight_peak=%6 %7 %8")
            .arg(gaugeTxn)
            .arg(state_.chartSwitchGaugeTxnCounter_)
            .arg(state_.activeDifficultyId_)
            .arg(static_cast<qint64>(owner_.findChildren<QObject*>().size()))
            .arg(miacode::diag::leak_gauge::inflightDepth())
            .arg(miacode::diag::leak_gauge::inflightPeak())
            .arg(miacode::diag::processResourceGaugePayload())
            .arg(state_.previewCanvas_ != nullptr
                     ? state_.previewCanvas_->resourceGaugePayload()
                     : QStringLiteral("scene_revision=-")));
    miacode::diag::leak_gauge::armRenderSample(switchPrivBytes, gaugeTxn);
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
