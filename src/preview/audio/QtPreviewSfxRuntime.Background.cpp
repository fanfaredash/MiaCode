void QtPreviewSfxRuntime::startBackgroundTrack(double second)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    if (!prepareStretchedBackgroundTrack(second)) {
        playbackSession_.backgroundTrackLastTimelineSecond = second;
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackRunning = false;
        appendAudioDebugLog(QString("startBackgroundTrack deferred second=%1 rate=%2")
                                .arg(second, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }

    if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
        return;
    }
    ma_sound* activeSound = &stretchedBackgroundState_->sound;
    StretchedBackgroundState* activeStretchedState = stretchedBackgroundState_;

    playbackSession_.backgroundTrackLastTimelineSecond = second;
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    clearBackgroundTrackClockAnchor();
    ma_sound_stop(activeSound);
    const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
    const double mappedSecond = rawSecond / playbackSession_.backgroundTrackPlaybackRate;
    if (rawSecond < 0.0) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
        playbackSession_.backgroundTrackPendingStart = true;
        return;
    }
    const double clampedMappedSecond = qMax(0.0, mappedSecond);
    ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
    if (activeStretchedState->stretchedFrameCount > 0) {
        targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
    }
    ma_sound_seek_to_pcm_frame(activeSound, targetFrame);
    const ma_result startResult = ma_sound_start(activeSound);
    if (startResult == MA_SUCCESS) {
        playbackSession_.backgroundTrackRunning = true;
        armBackgroundTrackClock(second);
        appendAudioDebugLog(QString("startBackgroundTrack started txn=%1 second=%2 raw=%3 mapped=%4 rate=%5")
                                .arg(playbackTransactionId_)
                                .arg(second, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    } else {
        playbackSession_.backgroundTrackRunning = false;
        playbackSession_.backgroundTrackPendingStart = true;
        clearBackgroundTrackClockAnchor();
        appendAudioDebugLog(QString("startBackgroundTrack start failed txn=%1 rc=%2 second=%3 mapped=%4 rate=%5")
                                .arg(playbackTransactionId_)
                                .arg(static_cast<int>(startResult))
                                .arg(second, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    }
}

void QtPreviewSfxRuntime::seekBackgroundTrack(double second)
{
    if (!hasBackgroundTrack()) {
        return;
    }

    if (!prepareStretchedBackgroundTrack(second)) {
        playbackSession_.backgroundTrackLastTimelineSecond = second;
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = false;
        appendAudioDebugLog(QString("seekBackgroundTrack deferred second=%1 rate=%2")
                                .arg(second, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }

    if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
        return;
    }
    ma_sound* activeSound = &stretchedBackgroundState_->sound;
    StretchedBackgroundState* activeStretchedState = stretchedBackgroundState_;

    playbackSession_.backgroundTrackLastTimelineSecond = second;
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    clearBackgroundTrackClockAnchor();
    ma_sound_stop(activeSound);
    const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
    const double mappedSecond = rawSecond / playbackSession_.backgroundTrackPlaybackRate;
    if (rawSecond < 0.0) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
        appendAudioDebugLog(QString("seekBackgroundTrack clamped_to_start second=%1 raw=%2 rate=%3")
                                .arg(second, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }

    const double clampedMappedSecond = qMax(0.0, mappedSecond);
    ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
    if (activeStretchedState->stretchedFrameCount > 0) {
        targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
    }
    ma_sound_seek_to_pcm_frame(activeSound, targetFrame);

    appendAudioDebugLog(QString("seekBackgroundTrack positioned second=%1 raw=%2 mapped=%3 rate=%4")
                            .arg(second, 0, 'f', 3)
                            .arg(rawSecond, 0, 'f', 3)
                            .arg(mappedSecond, 0, 'f', 3)
                            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
}

void QtPreviewSfxRuntime::pauseBackgroundTrack()
{
    if (!hasBackgroundTrack()) {
        return;
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
    }
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    clearBackgroundTrackClockAnchor();
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::capturePausedPreviewTransaction()
{
    return pausePreviewPlaybackTransaction();
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::pausePreviewPlaybackTransaction()
{
    PausePreviewResult result;
    result.usedBackgroundTrack = hasBackgroundTrack();
    result.pauseSecond = authoritativePlaybackSecond();
    result.retainedMode = RetainedPlaybackMode::PausedExact;
    result.retainedBgmState = hasBackgroundTrack() ? RetainedBgmState::LoadedUsable : RetainedBgmState::NoneLoaded;
    retainedPlayback_.mode = result.retainedMode;
    retainedPlayback_.bgmState = result.retainedBgmState;
    retainedPlayback_.second = result.pauseSecond;
    if (hasBackgroundTrack()) {
        pauseBackgroundTrack();
    }
    return result;
}

double QtPreviewSfxRuntime::backgroundPlaybackSecond() const
{
    if (!hasBackgroundTrack()) {
        return 0.0;
    }
    if (!stretchedBackgroundClockReady()) {
        return playbackSession_.backgroundTrackLastTimelineSecond;
    }
    return stretchedBackgroundPlaybackSecond();
}

void QtPreviewSfxRuntime::syncBackgroundTrack(double timelineSecond)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    if (!prepareStretchedBackgroundTrack(timelineSecond)) {
        playbackSession_.backgroundTrackLastTimelineSecond = timelineSecond;
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackRunning = false;
        appendAudioDebugLog(QString("syncBackgroundTrack deferred second=%1 rate=%2")
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }
    if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
        return;
    }
    ma_sound* activeSound = &stretchedBackgroundState_->sound;
    StretchedBackgroundState* activeStretchedState = stretchedBackgroundState_;

    playbackSession_.backgroundTrackLastTimelineSecond = timelineSecond;
    if (!playbackSession_.backgroundTrackPendingStart) {
        return;
    }
    const double rawSecond = timelineSecond + playbackSession_.backgroundTrackOffsetSeconds;
    const double mappedSecond = rawSecond / playbackSession_.backgroundTrackPlaybackRate;
    if (rawSecond < 0.0) {
        return;
    }
    const double clampedMappedSecond = qMax(0.0, mappedSecond);
    ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
    if (activeStretchedState->stretchedFrameCount > 0) {
        targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
    }
    ma_sound_seek_to_pcm_frame(activeSound, targetFrame);
    const ma_result startResult = ma_sound_start(activeSound);
    if (startResult == MA_SUCCESS) {
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = true;
        armBackgroundTrackClock(timelineSecond);
        appendAudioDebugLog(QString("syncBackgroundTrack started txn=%1 second=%2 raw=%3 mapped=%4 rate=%5")
                                .arg(playbackTransactionId_)
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    } else {
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackRunning = false;
        clearBackgroundTrackClockAnchor();
        appendAudioDebugLog(QString("syncBackgroundTrack start failed txn=%1 rc=%2 second=%3 mapped=%4 rate=%5")
                                .arg(playbackTransactionId_)
                                .arg(static_cast<int>(startResult))
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    }
}

double QtPreviewSfxRuntime::syncPreviewPlaybackClockTransaction(double fallbackSecond)
{
    double second = fallbackSecond;
    if (hasBackgroundTrack() && isBackgroundTrackRunning() && stretchedBackgroundClockReady()) {
        second = backgroundPlaybackSecond();
        const double deltaMs = (second - fallbackSecond) * 1000.0;
        const quint64 engineNowFrame =
            engineState_ != nullptr ? ma_engine_get_time_in_pcm_frames(&engineState_->engine) : 0;
        if (qAbs(deltaMs) >= 8.0
            && (lastStretchedClockDriftLogSecond_ < 0.0
                || qAbs(second - lastStretchedClockDriftLogSecond_) >= 0.5
                || qAbs(deltaMs - lastStretchedClockDriftDeltaMs_) >= 2.0)) {
            lastStretchedClockDriftLogSecond_ = second;
            lastStretchedClockDriftDeltaMs_ = deltaMs;
            appendAudioDebugLog(
                QString("stretched_clock_drift fallback=%1 bg=%2 delta_ms=%3 rate=%4 engine_now_frame=%5 start_engine_frame=%6 tick_bg_gap_ms=%7")
                    .arg(fallbackSecond, 0, 'f', 6)
                    .arg(second, 0, 'f', 6)
                    .arg(deltaMs, 0, 'f', 3)
                    .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
                    .arg(engineNowFrame)
                    .arg(playbackSession_.backgroundTrackStartEngineFrame)
                    .arg((fallbackSecond - playbackSession_.backgroundTrackLastTimelineSecond) * 1000.0, 0, 'f', 3));
        }
    }
    syncBackgroundTrack(second);
    return second;
}

double QtPreviewSfxRuntime::resumeRetainedPreviewPlaybackTransaction()
{
    const double second = retainedPlayback_.second;
    if (hasBackgroundTrack()) {
        startBackgroundTrack(second);
    }
    restoreTouchholdVoices(second);
    retainedPlayback_.mode = RetainedPlaybackMode::None;
    return second;
}

double QtPreviewSfxRuntime::seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying)
{
    const double second = qMax(0.0, targetSecond);
    resetCursor(second, false);
    pauseTouchholdVoices();
    retainedPlayback_.second = second;
    retainedPlayback_.bgmState = hasBackgroundTrack() ? RetainedBgmState::LoadedUsable : RetainedBgmState::NoneLoaded;
    if (hasBackgroundTrack()) {
        if (continuePlaying) {
            startBackgroundTrack(second);
        } else {
            seekBackgroundTrack(second);
        }
    }
    if (continuePlaying) {
        restoreTouchholdVoices(second);
        retainedPlayback_.mode = RetainedPlaybackMode::None;
    } else {
        retainedPlayback_.mode = RetainedPlaybackMode::PausedAnchored;
    }
    return second;
}

void QtPreviewSfxRuntime::resetRetainedPreviewPlaybackTransaction(double targetSecond)
{
    retainedPlayback_.second = seekRetainedPreviewPlaybackTransaction(targetSecond, false);
}

void QtPreviewSfxRuntime::clearRetainedPreviewPlaybackTransaction()
{
    retainedPlayback_.mode = RetainedPlaybackMode::None;
}

QtPreviewSfxRuntime::RetainedPlaybackMode QtPreviewSfxRuntime::retainedPlaybackMode() const
{
    return retainedPlayback_.mode;
}

QtPreviewSfxRuntime::RetainedBgmState QtPreviewSfxRuntime::retainedBgmState() const
{
    return retainedPlayback_.bgmState;
}

double QtPreviewSfxRuntime::authoritativePlaybackSecond() const
{
    if (hasBackgroundTrack()) {
        return backgroundPlaybackSecond();
    }
    if (retainedPlayback_.mode != RetainedPlaybackMode::None) {
        return retainedPlayback_.second;
    }
    return playbackSession_.backgroundTrackLastTimelineSecond;
}

bool QtPreviewSfxRuntime::audition(const QString& kind, double gain)
{
    return playKindInternal(kind, gain);
}

void QtPreviewSfxRuntime::stopAll()
{
    const auto stopBank = [](SfxBank& bank) {
        for (Voice* voice : bank.voices) {
            if (voice != nullptr && voice->initialized) {
                ma_sound_stop(&voice->sound);
            }
        }
    };

    stopBank(answerSfx_);
    stopBank(judgeSfx_);
    stopBank(judgeBreakSfx_);
    stopBank(slideSfx_);
    stopBank(breakSfx_);
    stopBank(breakSlideStartSfx_);
    stopBank(breakSlideSfx_);
    stopBank(judgeBreakSlideSfx_);
    stopBank(exSfx_);
    stopBank(touchSfx_);
    stopBank(fireworkSfx_);
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
    }
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    preparedPlayback_ = PreparedPlaybackState();
    pauseTouchholdVoices();
}

