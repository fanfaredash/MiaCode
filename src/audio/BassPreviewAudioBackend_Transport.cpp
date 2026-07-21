#include "BassPreviewAudioBackend.h"

#include "BassPreviewDebugLogRouting.h"
#include "BassPreviewRetainedState.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxTimeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QtMath>

#include <cstdio>   // G1 Commit 8 followup: std::snprintf for startup-beacon lines

#ifdef MIACODE_HAS_BASS_AUDIO
#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

void BassPreviewAudioBackend::setBackgroundTrackSampleSpeed(double rate)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (backgroundTrackSample_ != nullptr) {
        backgroundTrackSample_->setSpeed(rate);
    }
#else
    Q_UNUSED(rate);
#endif
}


void BassPreviewAudioBackend::suspendPlaybackTransport()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ == 0) {
        return;
    }
    logTrackFileMissingAfterLoadIfNeeded();
    const double pauseSecond = authoritativeSecond();
    playbackSession_.lastAuthoritativeSecond = pauseSecond;
    // G1 Commit 6 (corrected post-test): master mixer stays ACTIVE_PLAYING for the
    // engine's lifetime — what makes the BGM go silent is *this* call, setting
    // BASS_MIXER_CHAN_PAUSE on the BGM source. Pre-G1, BASS_ChannelPause on the
    // master silenced every channel implicitly; the original Commit 6 dropped
    // the master pause but left this site empty, which let BGM keep playing
    // through the pause UI state.
    //
    // beta50 followup: same blind spot applied to the touchhold voice. If a
    // touchhold span was active at the moment the user pressed ⏸, its sample
    // kept looping because the master-pause shortcut was gone and no per-
    // sample flag was being set on the touchhold source either. The earlier
    // comment claiming "touchhold is handled by separate pauseTouchholdVoices()
    // callers in the pause flow" was wrong — pauseTouchholdVoices() was only
    // invoked from the anchor / reposition / prepare paths, not from
    // suspendPlaybackTransport itself. Calling it here closes that hole.
    // SFX one-shots still self-terminate so they don't need explicit silencing.
    if (backgroundTrackSample_ != nullptr) {
        backgroundTrackSample_->pause();
    }
    pauseTouchholdVoices();
    playbackSession_.masterRunning = false;
    playbackSession_.backgroundTrackRunning = false;
    retainedPlaybackMode_ = RetainedPlaybackMode::PausedExact;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::PauseExact,
        QString("second=%1 mode=%2")
            .arg(pauseSecond, 0, 'f', 6)
            .arg(retainedPlaybackModeLabel(retainedPlaybackMode_)));
#endif
}

void BassPreviewAudioBackend::anchorTransportToSecond(double targetSecond, const QString& reason)
{
    QElapsedTimer timer;
    timer.start();
    const double anchoredSecond = clampTimelineSecond(targetSecond);
    preparedPlayback_ = PreparedPlaybackState();
    stopAllSamples();
    resetMasterMixerClock(anchoredSecond);
    configureBackgroundTrackForSecond(
        anchoredSecond,
        reason,
        miacode::preview_audio::bass::BassDebugRoute::Init);
    playbackSession_.lastAuthoritativeSecond = anchoredSecond;
    resetCursor(anchoredSecond, false);
    pauseTouchholdVoices();
    retainedPlaybackMode_ = RetainedPlaybackMode::PausedAnchored;
    logTrackFileMissingAfterLoadIfNeeded();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::AnchorTransport,
        QString("reason=%1 target=%2 elapsed_ms=%3 next_group_idx=%4")
            .arg(reason)
            .arg(anchoredSecond, 0, 'f', 6)
            .arg(timer.elapsed())
            .arg(playbackSession_.eventGroupIndex),
        true);
}

void BassPreviewAudioBackend::clearResidualVoicesForPausedReposition()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    Sample* residualSamples[] = {
        answerSample_.get(),
        judgeSample_.get(),
        judgeBreakSample_.get(),
        slideSample_.get(),
        breakSample_.get(),
        breakSlideStartSample_.get(),
        breakSlideFinishSample_.get(),
        breakSlideTailBreakSample_.get(),
        judgeBreakSlideSample_.get(),
        exSample_.get(),
        touchSample_.get(),
        touchholdSample_,
        fireworkSample_.get()
    };
    for (Sample* sample : residualSamples) {
        if (sample != nullptr) {
            sample->stop();
        }
    }
#endif
}

void BassPreviewAudioBackend::repositionMasterTransportClock(double targetSecond)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ == 0) {
        return;
    }
    // G1 Commit 6: master mixer position is no longer used to derive chart-second
    // (authoritativeSecond returns lastAuthoritativeSecond unconditionally). Resetting
    // the master here pre-G1 was about restoring a 0-based offset for that math, so
    // dropping the BASS calls is now purely a cleanup of dead I/O — and avoids one
    // more SoundTouch cold-start in BASS_FX. The session state below still gets
    // recomputed; that part remains load-bearing for downstream callers reading
    // sessionStartSecond / sessionPlaybackRate.
#else
    Q_UNUSED(targetSecond);
#endif
    playbackSession_.sessionStartSecond = clampTimelineSecond(targetSecond);
    playbackSession_.sessionPlaybackRate = qBound(
        kBassPreviewMinRate,
        qIsFinite(playbackSession_.backgroundTrackPlaybackRate) ? playbackSession_.backgroundTrackPlaybackRate : 1.0,
        kBassPreviewMaxRate);
    playbackSession_.lastAuthoritativeSecond = playbackSession_.sessionStartSecond;
    playbackSession_.lastStatusLogSecond = -1.0;
    playbackSession_.lastTriggeredGroupSecond = -1.0;
    playbackSession_.lastTriggeredGroupIndex = -1;
    playbackSession_.triggeredGroupCount = 0;
    playbackSession_.masterRunning = false;
}

void BassPreviewAudioBackend::repositionPausedTransportToSecond(double targetSecond, const QString& reason)
{
    QElapsedTimer timer;
    timer.start();
    const double repositionedSecond = clampTimelineSecond(targetSecond);
    preparedPlayback_ = PreparedPlaybackState();
    clearResidualVoicesForPausedReposition();
    repositionMasterTransportClock(repositionedSecond);
    configureBackgroundTrackForSecond(
        repositionedSecond,
        reason,
        miacode::preview_audio::bass::BassDebugRoute::Transport);
    resetCursor(repositionedSecond, false);
    pauseTouchholdVoices();
    retainedPlaybackMode_ = RetainedPlaybackMode::PausedAnchored;
    logTrackFileMissingAfterLoadIfNeeded();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RetainedReset,
        QString("reason=%1 action=reposition target=%2 elapsed_ms=%3 next_group_idx=%4")
            .arg(reason)
            .arg(repositionedSecond, 0, 'f', 6)
            .arg(timer.elapsed())
            .arg(playbackSession_.eventGroupIndex));
    noteTransportReady(reason);
}

void BassPreviewAudioBackend::startTransportFromCurrentAnchor()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ == 0) {
        return;
    }
    const RetainedPlaybackMode retainedMode = retainedPlaybackMode_;
    logTrackFileMissingAfterLoadIfNeeded();
    // G1 Commit 6: master mixer was started at engine init and stays running.
    // Resume is now purely the act of unsetting BASS_MIXER_CHAN_PAUSE on each
    // sample (done below via backgroundTrackSample_->play() and similar).
    playbackSession_.masterRunning = true;
    playbackSession_.lastAuthoritativeSecond = authoritativeSecond();
    // G1 followup: bass_play also fires on the retained-resume path (the
    // common case for ▶ after ⏸). The cold-start path emits it from
    // commitPreparedPreviewPlayback; without this line, the row never
    // appears for any session after the first prepare.
    appendAudioDebugLog(
        QString("bass_play txn=%1 start=%2 rate=%3 resume=1 reason=resume_anchor mode=%4")
            .arg(playbackTransactionId_)
            .arg(playbackSession_.lastAuthoritativeSecond, 0, 'f', 6)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(retainedPlaybackModeLabel(retainedMode)));
    if (backgroundTrackSample_ != nullptr) {
        if (!playbackSession_.backgroundTrackPendingStart) {
            backgroundTrackSample_->play();
            playbackSession_.backgroundTrackRunning = true;
            // G1 Commit 8: bass_sample_play per §7.2 — resume-from-anchor path.
            appendAudioDebugLog(
                QString("bass_sample_play kind=bgm rate_at_play=%1 offset_sec=%2 reason=resume_anchor")
                    .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
                    .arg(backgroundTrackSample_->currentSec(), 0, 'f', 6));
        } else {
            playbackSession_.backgroundTrackRunning = false;
        }
    } else {
        playbackSession_.backgroundTrackRunning = false;
    }
    if (retainedMode == RetainedPlaybackMode::PausedAnchored) {
        restoreTouchholdVoices(playbackSession_.lastAuthoritativeSecond);
    }
#endif
    preparedPlayback_ = PreparedPlaybackState();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::ResumeTransport,
        QString("from=%1 second=%2 bg_pending=%3")
            .arg(retainedPlaybackModeLabel(retainedMode))
            .arg(playbackSession_.lastAuthoritativeSecond, 0, 'f', 6)
            .arg(playbackSession_.backgroundTrackPendingStart ? 1 : 0));
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
    noteTransportReady(QStringLiteral("resume_transport"));
}


void BassPreviewAudioBackend::setBackgroundTrackOffsetSeconds(double seconds)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    if (qAbs(playbackSession_.backgroundTrackOffsetSeconds - normalized) > kBassPreviewEpsilonSeconds) {
        invalidateRetainedPlaybackState(QStringLiteral("background_track_offset_changed"));
    }
    playbackSession_.backgroundTrackOffsetSeconds = normalized;
}

void BassPreviewAudioBackend::setBackgroundTrackPlaybackRate(double rate)
{
    MC_OP("BassPreviewAudioBackend::setBackgroundTrackPlaybackRate");
    const double normalizedRate = qBound(kBassPreviewMinRate, qIsFinite(rate) ? rate : 1.0, kBassPreviewMaxRate);
    // G2 Diag: paused-rate-change entry — paired with the live-rate-change path
    // (applyPlaybackRateAtChartSecond). Same justification: sync beacon
    // survives a fast-fail in setBackgroundTrackSampleSpeed → Sample::setSpeed
    // even when the async DebugLog queue does not.
    {
        char buf[180];
        std::snprintf(buf, sizeof(buf),
            "audio/rate/bass_paused_enter from=%.3f to=%.3f has_bgm=%d",
            playbackSession_.backgroundTrackPlaybackRate,
            normalizedRate,
            backgroundTrackSample_ != nullptr ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (qAbs(playbackSession_.backgroundTrackPlaybackRate - normalizedRate) > kBassPreviewEpsilonSeconds) {
        invalidateRetainedPlaybackState(QStringLiteral("background_track_rate_changed"));
    }
    playbackSession_.backgroundTrackPlaybackRate = normalizedRate;
    setBackgroundTrackSampleSpeed(playbackSession_.backgroundTrackPlaybackRate);
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_paused_exit");
}

void BassPreviewAudioBackend::applyPlaybackRateAtChartSecond(double rate, double chartSecond)
{
    MC_OP("BassPreviewAudioBackend::applyPlaybackRateAtChartSecond");
#ifdef MIACODE_HAS_BASS_AUDIO
    const double normalizedRate = qBound(kBassPreviewMinRate, qIsFinite(rate) ? rate : 1.0, kBassPreviewMaxRate);
    const double sanitizedChart = qIsFinite(chartSecond) ? chartSecond : 0.0;
    // G2 Diag: every leg of the pause-modify-resume sequence below maps to one
    // synchronous beacon line. Async DebugLog drops queue tail on fast-fail —
    // the user reported 0.5x crash where NO bass_live_rate_change row landed —
    // so the only reliable trail is the sync-fsync beacon. Each line is a
    // strncpy'd stack buffer to keep heap allocation off the critical path.
    {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "audio/rate/bass_enter from=%.3f to=%.3f chart=%.6f has_bgm=%d",
            playbackSession_.backgroundTrackPlaybackRate,
            normalizedRate,
            sanitizedChart,
            backgroundTrackSample_ != nullptr ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (backgroundTrackSample_ == nullptr) {
        // No BGM loaded — just record the rate so the next sample creation
        // picks it up. invalidateRetainedPlaybackState is intentionally NOT
        // called here; this entry is a live UI tweak, not a structural
        // change to the prepared transport.
        playbackSession_.backgroundTrackPlaybackRate = normalizedRate;
        playbackSession_.sessionPlaybackRate = normalizedRate;
        miacode::oplog::appendStartupBeaconLine(
            "audio/rate/bass_exit reason=no_bgm");
        return;
    }
    // G2 Commit 2: the pause-modify-resume sequence from
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §6.2. Pause first so the
    // BGM source stops being pulled by the master mixer, then
    // Sample::setSpeed passes its MIXER_CHAN_PAUSE check and writes the
    // selected rate attribute. Re-anchor the
    // cursor to the wall-clock chart-second to keep audio/visual aligned
    // across the rate change, then resume. The whole atomic step happens
    // while the master mixer continues to play silence — the only audible
    // event is the brief BGM gap (~50-100ms typically) covered by the
    // pause flag flip.
    const double oldRate = playbackSession_.backgroundTrackPlaybackRate;
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_about_to_pause");
    backgroundTrackSample_->pause();
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_about_to_setspeed");
    backgroundTrackSample_->setSpeed(normalizedRate);
    const double rawSecond = sanitizedChart + playbackSession_.backgroundTrackOffsetSeconds;
    if (rawSecond >= 0.0) {
        {
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/bass_about_to_seek raw=%.6f",
                rawSecond);
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        backgroundTrackSample_->setCurrentSec(rawSecond);
    }
    playbackSession_.backgroundTrackPlaybackRate = normalizedRate;
    playbackSession_.sessionPlaybackRate = normalizedRate;
    playbackSession_.sessionStartSecond = sanitizedChart;
    playbackSession_.lastAuthoritativeSecond = sanitizedChart;
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_about_to_resume");
    backgroundTrackSample_->play();
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_exit reason=ok");
    appendAudioDebugLog(
        QString("bass_live_rate_change from=%1 to=%2 chart=%3 raw=%4")
            .arg(oldRate, 0, 'f', 3)
            .arg(normalizedRate, 0, 'f', 3)
            .arg(sanitizedChart, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6));
    // G2 followup A2 (§7.2 consistency): bass_sample_play also gets a row
    // here so the per-session log has a clean "play flag flipped" record
    // for every flag-flip event (cold play, retained resume, live rate
    // change). reason=live_rate_resume distinguishes this from the other
    // two paths.
    appendAudioDebugLog(
        QString("bass_sample_play kind=bgm rate_at_play=%1 offset_sec=%2 reason=live_rate_resume")
            .arg(normalizedRate, 0, 'f', 3)
            .arg(backgroundTrackSample_->currentSec(), 0, 'f', 6));
#else
    Q_UNUSED(rate);
    Q_UNUSED(chartSecond);
#endif
}


void BassPreviewAudioBackend::resetMasterMixerClock(double startSecond)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ == 0) {
        return;
    }
    // G1 Commit 6: see comment in repositionMasterTransportClock. Same rationale here:
    // the master keeps running, and authoritativeSecond no longer reads its position.
    playbackSession_.sessionStartSecond = clampTimelineSecond(startSecond);
    playbackSession_.sessionPlaybackRate = qBound(
        kBassPreviewMinRate,
        qIsFinite(playbackSession_.backgroundTrackPlaybackRate) ? playbackSession_.backgroundTrackPlaybackRate : 1.0,
        kBassPreviewMaxRate);
    playbackSession_.lastAuthoritativeSecond = playbackSession_.sessionStartSecond;
    playbackSession_.lastStatusLogSecond = -1.0;
    playbackSession_.lastTriggeredGroupSecond = -1.0;
    playbackSession_.lastTriggeredGroupIndex = -1;
    playbackSession_.triggeredGroupCount = 0;
    playbackSession_.masterRunning = false;
#else
    Q_UNUSED(startSecond);
#endif
}

void BassPreviewAudioBackend::stopAllSamples()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    const Sample* uniqueSamples[] = {
        answerSample_.get(),
        judgeSample_.get(),
        judgeBreakSample_.get(),
        slideSample_.get(),
        breakSample_.get(),
        breakSlideStartSample_.get(),
        breakSlideFinishSample_.get(),
        breakSlideTailBreakSample_.get(),
        judgeBreakSlideSample_.get(),
        exSample_.get(),
        touchSample_.get(),
        touchholdSample_,
        fireworkSample_.get(),
        backgroundTrackSample_
    };
    for (const Sample* samplePtr : uniqueSamples) {
        Sample* sample = const_cast<Sample*>(samplePtr);
        if (sample != nullptr) {
            sample->stop();
        }
    }
#endif
}

void BassPreviewAudioBackend::stopPlaybackSession()
{
    stopAllSamples();
    resetMasterMixerClock(playbackSession_.lastAuthoritativeSecond);
    playbackSession_.backgroundTrackRunning = false;
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackPendingStartSecond = playbackSession_.lastAuthoritativeSecond;
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
}

// G2 followup A2: armNextGroupSync / clearScheduledGroupSync deleted outright.
// They were empty no-op stubs left behind by G1 Commit 7's BASS_SYNC_POS
// removal; with every call site in this file now also gone, the surface area
// has nothing left to support.


double BassPreviewAudioBackend::authoritativeSecond() const
{
    // G1 Commit 6: BASS_ChannelGetPosition(masterMixer_) is no longer queried.
    // Wall-clock is the chart-second master (MainWindow's qtPreviewElapsed_), and
    // the master mixer keeps running indefinitely with a position that bears no
    // relation to session time. Return the last externally-recorded snapshot
    // (set at prepare / pause / seek / start transitions). MainWindow has
    // stopped reading this through currentPreviewAuthoritativeAudioClockSecond
    // (see Commit 4); the only remaining consumers are internal — seek-action
    // selection in seekRetainedPreviewPlaybackTransaction and the
    // PausePreviewResult.pauseSecond round-trip, both of which now compare
    // against the wall-clock pause-second the caller already supplied at the
    // last suspendPlaybackTransport.
    return playbackSession_.lastAuthoritativeSecond;
}

void BassPreviewAudioBackend::configureBackgroundTrackForSecond(
    double second,
    const QString& reason,
    miacode::preview_audio::bass::BassDebugRoute route)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    QElapsedTimer timer;
    timer.start();
    if (backgroundTrackSample_ == nullptr) {
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = false;
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::ConfigureBackgroundTrack,
            QString("reason=%1 second=%2 elapsed_ms=%3 has_bgm=0")
                .arg(reason)
                .arg(second, 0, 'f', 6)
                .arg(timer.elapsed()),
            route == miacode::preview_audio::bass::BassDebugRoute::Init);
        return;
    }

    backgroundTrackSample_->setLoop(false);
    // G1 Commit 6: setSpeed is no longer called from this per-seek / per-pause helper.
    // BASS_ATTRIB_TEMPO is written exactly twice in a sample's lifetime: at
    // initializeAssets() right after creation, and from
    // setBackgroundTrackPlaybackRate() when the user moves the rate slider — and the
    // latter is now guarded by Sample::setSpeed so it only takes effect when the
    // sample is in MIXER_CHAN_PAUSE state. Calling it here on every configure step
    // was the proximate trigger for the 0.5x rate-change crash (scenario (b) in
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1).
    const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
    if (rawSecond < 0.0) {
        backgroundTrackSample_->setCurrentSec(0.0);
        backgroundTrackSample_->pause();
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackPendingStartSecond = second - rawSecond;
        playbackSession_.backgroundTrackRunning = false;
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::ConfigureBackgroundTrack,
            QString("reason=%1 second=%2 raw=%3 elapsed_ms=%4 pending_start=1 pending_second=%5")
                .arg(reason)
                .arg(second, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(timer.elapsed())
                .arg(playbackSession_.backgroundTrackPendingStartSecond, 0, 'f', 6),
            route == miacode::preview_audio::bass::BassDebugRoute::Init);
        return;
    }

    backgroundTrackSample_->setCurrentSec(rawSecond);
    backgroundTrackSample_->pause();
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackPendingStartSecond = second;
    playbackSession_.backgroundTrackRunning = false;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::ConfigureBackgroundTrack,
        QString("reason=%1 second=%2 raw=%3 elapsed_ms=%4 pending_start=0")
            .arg(reason)
            .arg(second, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(timer.elapsed()),
        route == miacode::preview_audio::bass::BassDebugRoute::Init);
#else
    Q_UNUSED(second);
    Q_UNUSED(reason);
    Q_UNUSED(route);
#endif
}

bool BassPreviewAudioBackend::maybeStartPendingBackgroundTrack(double second)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (backgroundTrackSample_ == nullptr) {
        return false;
    }
    if (playbackSession_.backgroundTrackPendingStart
        && second + kBassPreviewEpsilonSeconds >= playbackSession_.backgroundTrackPendingStartSecond) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = true;
        return true;
    }
    return false;
#else
    Q_UNUSED(second);
    return false;
#endif
}


void BassPreviewAudioBackend::syncBackgroundTrack(double timelineSecond)
{
    maybeStartPendingBackgroundTrack(timelineSecond);
    // G1 followup: restore the per-second bass_status row. Pre-G1 it was driven
    // from syncPreviewPlaybackClockTransaction; that path is gone (Commit 5),
    // and MainWindow now calls syncBackgroundTrack on every tick as the BGM-
    // pending-start hook. Riding that schedule keeps bass_status emitting at
    // the same ~16ms cadence the old call had, then rate-limited to once per
    // second internally by logPlaybackStatus. authoritativeSecond returns the
    // last-recorded snapshot now, so we pass MainWindow's wall-clock second
    // directly into both arguments — `auth` will track wall-clock and the
    // row's drift_ms collapses to ~0 as long as the chart is on rate.
    logPlaybackStatus(timelineSecond, timelineSecond);
}

bool BassPreviewAudioBackend::hasBackgroundTrack() const
{
    return backgroundTrackSample_ != nullptr;
}

bool BassPreviewAudioBackend::isBackgroundTrackRunning() const
{
    return playbackSession_.backgroundTrackRunning;
}

void BassPreviewAudioBackend::startBackgroundTrack(double second)
{
    MC_OP("BassPreviewAudioBackend::startBackgroundTrack");
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ != 0 && !playbackSession_.masterRunning) {
        resetMasterMixerClock(second);
        // G1 Commit 6: master mixer was started at engine init and never stops.
        playbackSession_.masterRunning = true;
        playbackSession_.lastAuthoritativeSecond = clampTimelineSecond(second);
    }
    configureBackgroundTrackForSecond(
        second,
        QStringLiteral("start_background_track"),
        miacode::preview_audio::bass::BassDebugRoute::Transport);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::StartBackgroundTrack,
        QString("second=%1").arg(second, 0, 'f', 6));
    if (backgroundTrackSample_ != nullptr && !playbackSession_.backgroundTrackPendingStart) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackRunning = true;
    }
    noteTransportReady(QStringLiteral("start_background_track"));
#else
    Q_UNUSED(second);
#endif
}

void BassPreviewAudioBackend::seekBackgroundTrack(double second)
{
    MC_OP("BassPreviewAudioBackend::seekBackgroundTrack");
    configureBackgroundTrackForSecond(
        second,
        QStringLiteral("seek_background_track"),
        miacode::preview_audio::bass::BassDebugRoute::Transport);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::SeekBackgroundTrack,
        QString("second=%1").arg(second, 0, 'f', 6));
    noteTransportReady(QStringLiteral("seek_background_track"));
}

void BassPreviewAudioBackend::pauseBackgroundTrack()
{
    MC_OP("BassPreviewAudioBackend::pauseBackgroundTrack");
#ifdef MIACODE_HAS_BASS_AUDIO
    if (backgroundTrackSample_ != nullptr) {
        backgroundTrackSample_->pause();
    }
    playbackSession_.backgroundTrackRunning = false;
#endif
}

double BassPreviewAudioBackend::backgroundPlaybackSecond() const
{
    return authoritativeSecond();
}
