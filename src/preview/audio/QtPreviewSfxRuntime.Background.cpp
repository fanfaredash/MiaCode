void QtPreviewSfxRuntime::startBackgroundTrack(double second)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    const bool useStretched = !qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0);
    if (useStretched && !prepareStretchedBackgroundTrack(second)) {
        playbackSession_.backgroundTrackLastTimelineSecond = qMax(0.0, second);
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackRunning = false;
        appendAudioDebugLog(QString("startBackgroundTrack deferred second=%1 rate=%2")
                                .arg(second, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }

    ma_sound* activeSound = nullptr;
    StretchedBackgroundState* activeStretchedState = nullptr;
    if (useStretched) {
        if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
            return;
        }
        activeSound = &stretchedBackgroundState_->sound;
        activeStretchedState = stretchedBackgroundState_;
    } else {
        if (backgroundTrackVoice_ == nullptr || !backgroundTrackVoice_->initialized) {
            return;
        }
        activeSound = &backgroundTrackVoice_->sound;
    }

    playbackSession_.backgroundTrackLastTimelineSecond = qMax(0.0, second);
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    ma_sound_stop(activeSound);
    const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
    const double mappedSecond = useStretched ? (rawSecond / playbackSession_.backgroundTrackPlaybackRate) : rawSecond;
    if (rawSecond < 0.0) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
        playbackSession_.backgroundTrackPendingStart = true;
        return;
    }
    if (useStretched && activeStretchedState != nullptr) {
        const double clampedMappedSecond = qMax(0.0, mappedSecond);
        ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
        if (activeStretchedState->stretchedFrameCount > 0) {
            targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
        }
        ma_sound_seek_to_pcm_frame(activeSound, targetFrame);
    } else if (ma_sound_seek_to_second(activeSound, static_cast<float>(qMax(0.0, mappedSecond))) != MA_SUCCESS) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
    }
    const ma_result startResult = ma_sound_start(activeSound);
    if (startResult == MA_SUCCESS) {
        playbackSession_.backgroundTrackRunning = true;
        appendAudioDebugLog(QString("startBackgroundTrack started second=%1 raw=%2 mapped=%3 rate=%4")
                                .arg(second, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    } else {
        playbackSession_.backgroundTrackRunning = false;
        playbackSession_.backgroundTrackPendingStart = true;
        appendAudioDebugLog(QString("startBackgroundTrack start failed rc=%1 second=%2 mapped=%3 rate=%4")
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

    const bool useStretched = !qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0);
    if (useStretched && !prepareStretchedBackgroundTrack(second)) {
        playbackSession_.backgroundTrackLastTimelineSecond = qMax(0.0, second);
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = false;
        appendAudioDebugLog(QString("seekBackgroundTrack deferred second=%1 rate=%2")
                                .arg(second, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }

    ma_sound* activeSound = nullptr;
    StretchedBackgroundState* activeStretchedState = nullptr;
    if (useStretched) {
        if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
            return;
        }
        activeSound = &stretchedBackgroundState_->sound;
        activeStretchedState = stretchedBackgroundState_;
    } else {
        if (backgroundTrackVoice_ == nullptr || !backgroundTrackVoice_->initialized) {
            return;
        }
        activeSound = &backgroundTrackVoice_->sound;
    }

    playbackSession_.backgroundTrackLastTimelineSecond = qMax(0.0, second);
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    ma_sound_stop(activeSound);
    const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
    const double mappedSecond = useStretched ? (rawSecond / playbackSession_.backgroundTrackPlaybackRate) : rawSecond;
    if (rawSecond < 0.0) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
        appendAudioDebugLog(QString("seekBackgroundTrack clamped_to_start second=%1 raw=%2 rate=%3")
                                .arg(second, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }

    if (useStretched && activeStretchedState != nullptr) {
        const double clampedMappedSecond = qMax(0.0, mappedSecond);
        ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
        if (activeStretchedState->stretchedFrameCount > 0) {
            targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
        }
        ma_sound_seek_to_pcm_frame(activeSound, targetFrame);
    } else if (ma_sound_seek_to_second(activeSound, static_cast<float>(qMax(0.0, mappedSecond))) != MA_SUCCESS) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
    }

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
    if (!qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0)) {
        if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
            ma_sound_stop(&stretchedBackgroundState_->sound);
        }
    } else if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::capturePausedPreviewTransaction()
{
    PausePreviewResult result;
    if (hasBackgroundTrack()) {
        result.usedBackgroundTrack = true;
        result.pauseSecond = backgroundPlaybackSecond();
        pauseBackgroundTrack();
    }
    return result;
}

double QtPreviewSfxRuntime::backgroundPlaybackSecond() const
{
    if (!hasBackgroundTrack()) {
        return 0.0;
    }
    if (!qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0)) {
        return stretchedBackgroundPlaybackSecond();
    }
    if (!playbackSession_.backgroundTrackRunning) {
        return qMax(0.0, playbackSession_.backgroundTrackLastTimelineSecond);
    }
    float cursorSeconds = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&backgroundTrackVoice_->sound, &cursorSeconds) != MA_SUCCESS) {
        return qMax(0.0, playbackSession_.backgroundTrackLastTimelineSecond);
    }
    return qMax(0.0, static_cast<double>(cursorSeconds) - playbackSession_.backgroundTrackOffsetSeconds);
}

void QtPreviewSfxRuntime::syncBackgroundTrack(double timelineSecond)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    const bool useStretched = !qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0);
    if (useStretched && !prepareStretchedBackgroundTrack(timelineSecond)) {
        playbackSession_.backgroundTrackLastTimelineSecond = qMax(0.0, timelineSecond);
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackRunning = false;
        appendAudioDebugLog(QString("syncBackgroundTrack deferred second=%1 rate=%2")
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
        return;
    }

    ma_sound* activeSound = nullptr;
    StretchedBackgroundState* activeStretchedState = nullptr;
    if (useStretched) {
        if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
            return;
        }
        activeSound = &stretchedBackgroundState_->sound;
        activeStretchedState = stretchedBackgroundState_;
    } else {
        if (backgroundTrackVoice_ == nullptr || !backgroundTrackVoice_->initialized) {
            return;
        }
        activeSound = &backgroundTrackVoice_->sound;
    }

    playbackSession_.backgroundTrackLastTimelineSecond = qMax(0.0, timelineSecond);
    if (!playbackSession_.backgroundTrackPendingStart) {
        return;
    }
    const double rawSecond = timelineSecond + playbackSession_.backgroundTrackOffsetSeconds;
    const double mappedSecond = useStretched ? (rawSecond / playbackSession_.backgroundTrackPlaybackRate) : rawSecond;
    if (rawSecond < 0.0) {
        return;
    }
    if (useStretched && activeStretchedState != nullptr) {
        const double clampedMappedSecond = qMax(0.0, mappedSecond);
        ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
        if (activeStretchedState->stretchedFrameCount > 0) {
            targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
        }
        ma_sound_seek_to_pcm_frame(activeSound, targetFrame);
    } else if (ma_sound_seek_to_second(activeSound, static_cast<float>(qMax(0.0, mappedSecond))) != MA_SUCCESS) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
    }
    const ma_result startResult = ma_sound_start(activeSound);
    if (startResult == MA_SUCCESS) {
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = true;
        appendAudioDebugLog(QString("syncBackgroundTrack started second=%1 raw=%2 mapped=%3 rate=%4")
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    } else {
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackRunning = false;
        appendAudioDebugLog(QString("syncBackgroundTrack start failed rc=%1 second=%2 mapped=%3 rate=%4")
                                .arg(static_cast<int>(startResult))
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    }
}

double QtPreviewSfxRuntime::syncPreviewPlaybackClockTransaction(double fallbackSecond)
{
    double second = qMax(0.0, fallbackSecond);
    if (hasBackgroundTrack() && isBackgroundTrackRunning()) {
        second = qMax(0.0, backgroundPlaybackSecond());
    }
    syncBackgroundTrack(second);
    return second;
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
    pauseTouchholdVoices();
}

