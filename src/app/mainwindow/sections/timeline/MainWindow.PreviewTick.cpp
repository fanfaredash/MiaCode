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
#include "audio/PreviewAudioRecoveryPolicy.h"
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
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("preview.position.changed"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("preview")},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("second"), fallbackSecond}}},
        }, true);
    }
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
        state_.previewSfxRuntime_->syncBackgroundTrack(fallbackSecond);
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

double MainWindow::TimelineSection::sfxDrainSecond(double wallClockSecond)
{
    // SFX and BGM are already mixed and output together; what used to separate
    // them was the trigger DECISION being made against the wall clock while the
    // BGM advanced on the device clock. Two crystals, no correction, divergence
    // measured every second into bgm_delta_ms and acted on by nobody. Draining
    // against the audio clock closes that loop.
    //
    // The visual chart-second is deliberately NOT moved: a BASS-cursor-driven
    // visual clock is exactly the "smear" G1 Commit 4 removed, and it needed ~130
    // lines of drift/catch-up/snap smoothing to hide. This narrows the audio seam
    // without reopening the visual one.
    if (state_.previewSfxRuntime_ == nullptr) {
        return wallClockSecond;
    }
    double audioSecond = 0.0;
    bool useAudioClock = state_.previewSfxRuntime_->audioClockChartSecond(&audioSecond);
    if (useAudioClock && !qIsFinite(audioSecond)) {
        useAudioClock = false;
    }
    if (useAudioClock) {
        const double clockDeltaSeconds = qAbs(wallClockSecond - audioSecond);
        const auto recovery = miacode::preview_audio::recovery::decidePreviewAudioRecovery(
            state_.qtPreviewPlaying_,
            /*defaultOutputChanged=*/false,
            /*audioClockAvailable=*/true,
            clockDeltaSeconds);
        if (recovery == miacode::preview_audio::recovery::Reason::DriftExceeded) {
            requestPreviewAudioReanchor(QStringLiteral("clock_divergence"));
            // The queued anchor runs after this tick. Do not emit one more SFX
            // on a clock that has demonstrably stopped following the chart.
            useAudioClock = false;
        }
    }
    if (useAudioClock != state_.sfxAudioClockActive_) {
        state_.sfxAudioClockActive_ = useAudioClock;
        if (miacode::debug_options::runtimeDebugOutputEnabled()) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("preview/sfx_clock"),
                QStringLiteral("action=source_changed source=%1 wall_second=%2 audio_second=%3 delta_ms=%4")
                    .arg(useAudioClock ? QStringLiteral("audio") : QStringLiteral("wall"))
                    .arg(wallClockSecond, 0, 'f', 6)
                    .arg(audioSecond, 0, 'f', 6)
                    .arg((wallClockSecond - audioSecond) * 1000.0, 0, 'f', 3));
        }
    }
    return useAudioClock ? audioSecond : wallClockSecond;
}

void MainWindow::TimelineSection::onPreviewAudioOutputDevicesChanged(bool defaultOutputChanged)
{
    const auto recovery = miacode::preview_audio::recovery::decidePreviewAudioRecovery(
        state_.qtPreviewPlaying_,
        defaultOutputChanged,
        /*audioClockAvailable=*/false,
        /*absoluteDeltaSeconds=*/0.0);
    if (recovery == miacode::preview_audio::recovery::Reason::DefaultOutputChanged) {
        requestPreviewAudioReanchor(QStringLiteral("default_output_changed"));
    }
}

void MainWindow::TimelineSection::requestPreviewAudioReanchor(const QString& reason)
{
    if (state_.previewAudioReanchorPending_ || state_.previewSfxRuntime_ == nullptr) {
        return;
    }
    state_.previewAudioReanchorPending_ = true;
    QTimer::singleShot(0, &owner_, [this, reason]() {
        state_.previewAudioReanchorPending_ = false;
        if (!state_.qtPreviewPlaying_ || state_.previewSfxRuntime_ == nullptr) {
            return;
        }

        const double wallClockSecond = owner_.currentPreviewAuthoritativeAudioClockSecond();
        double audioClockSecond = 0.0;
        const bool hasAudioClock = state_.previewSfxRuntime_->audioClockChartSecond(&audioClockSecond);
        const bool reanchored =
            state_.previewSfxRuntime_->reanchorPlayingTransportAtChartSecond(wallClockSecond, reason);
        double reanchoredAudioSecond = 0.0;
        const bool hasReanchoredAudioClock =
            state_.previewSfxRuntime_->audioClockChartSecond(&reanchoredAudioSecond);
        if (miacode::debug_options::runtimeDebugOutputEnabled()) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("preview/audio_reanchor"),
                QStringLiteral("action=completed reason=%1 wall_second=%2 audio_second=%3 delta_ms=%4 applied=%5 post_audio_second=%6 post_delta_ms=%7")
                    .arg(reason)
                    .arg(wallClockSecond, 0, 'f', 6)
                    .arg(audioClockSecond, 0, 'f', 6)
                    .arg(hasAudioClock ? (wallClockSecond - audioClockSecond) * 1000.0 : 0.0, 0, 'f', 3)
                    .arg(reanchored ? 1 : 0)
                    .arg(reanchoredAudioSecond, 0, 'f', 6)
                    .arg(hasReanchoredAudioClock ? (wallClockSecond - reanchoredAudioSecond) * 1000.0 : 0.0, 0, 'f', 3));
        }
    });
}

void MainWindow::TimelineSection::onQtPreviewTickAtSecond(double second, double fallbackSecond, bool hasAudioClock)
{
    if (!state_.qtPreviewPlaying_) {
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
            // Deliberately the wall-clock end-second, NOT sfxDrainSecond(): this is
            // the terminal flush, not a timing decision. Playback is ending on this
            // tick, so everything still queued has to fire now regardless of where
            // the audio clock got to — including the case where the BGM ended early
            // and the audio clock stopped advancing before the last note.
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
        // Audio-domain SFX scheduling. `second` (wall clock) still drives every
        // visual below; only the SFX trigger instant moves onto the audio clock,
        // so SFX and BGM stop being able to separate from each other.
        state_.previewSfxRuntime_->drainEvents(sfxDrainSecond(second));
    }
    maybeFireExportAuditionClockTicks(second);
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
            qint64 previewTickCount = 0;
            if (state_.previewCanvas_ != nullptr) {
                const auto snapshot = state_.previewCanvas_->frameStateSnapshot();
                if (snapshot != nullptr) {
                    previewTickCount = snapshot->tickCount;
                }
            }
            appendPreviewFramePacingDiagLog(
                isAnomaly ? QStringLiteral("tick_large_step") : QStringLiteral("tick_sample"),
                QStringLiteral(
                    "tick=%1 wall_ms=%2 playhead_delta_ms=%3 speed_ratio=%4 second=%5 mode=%6 "
                    "fallback_delta_ms=%7 audio_delta_ms=%8 audio_minus_fallback_ms=%9 time_authority=%10 "
                    "tick_exec_ms=%11 sync_media_ms=%12 notes_ms=%13 apply_position_ms=%14 smoothing_ms=%15 drain_events_ms=%16"
                )
                    .arg(previewTickCount)
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
