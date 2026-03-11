void QtPreviewSfxRuntime::startBackgroundTrack(double second)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    const bool useStretched = !qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0);
    if (useStretched && !prepareStretchedBackgroundTrack(second)) {
        backgroundTrackLastTimelineSecond_ = qMax(0.0, second);
        backgroundTrackPendingStart_ = true;
        backgroundTrackRunning_ = false;
        appendAudioDebugLog(QString("startBackgroundTrack deferred second=%1 rate=%2")
                                .arg(second, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
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

    backgroundTrackLastTimelineSecond_ = qMax(0.0, second);
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    ma_sound_stop(activeSound);
    const double rawSecond = second + backgroundTrackOffsetSeconds_;
    const double mappedSecond = useStretched ? (rawSecond / backgroundTrackPlaybackRate_) : rawSecond;
    if (rawSecond < 0.0) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
        backgroundTrackPendingStart_ = true;
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
        backgroundTrackRunning_ = true;
        appendAudioDebugLog(QString("startBackgroundTrack started second=%1 raw=%2 mapped=%3 rate=%4")
                                .arg(second, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    } else {
        backgroundTrackRunning_ = false;
        backgroundTrackPendingStart_ = true;
        appendAudioDebugLog(QString("startBackgroundTrack start failed rc=%1 second=%2 mapped=%3 rate=%4")
                                .arg(static_cast<int>(startResult))
                                .arg(second, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    }
}

void QtPreviewSfxRuntime::pauseBackgroundTrack()
{
    if (!hasBackgroundTrack()) {
        return;
    }
    if (!qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
            ma_sound_stop(&stretchedBackgroundState_->sound);
        }
    } else if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
}

double QtPreviewSfxRuntime::backgroundPlaybackSecond() const
{
    if (!hasBackgroundTrack()) {
        return 0.0;
    }
    if (!qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        return stretchedBackgroundPlaybackSecond();
    }
    if (!backgroundTrackRunning_) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }
    float cursorSeconds = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&backgroundTrackVoice_->sound, &cursorSeconds) != MA_SUCCESS) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }
    return qMax(0.0, static_cast<double>(cursorSeconds) - backgroundTrackOffsetSeconds_);
}

void QtPreviewSfxRuntime::syncBackgroundTrack(double timelineSecond)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    const bool useStretched = !qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0);
    if (useStretched && !prepareStretchedBackgroundTrack(timelineSecond)) {
        backgroundTrackLastTimelineSecond_ = qMax(0.0, timelineSecond);
        backgroundTrackPendingStart_ = true;
        backgroundTrackRunning_ = false;
        appendAudioDebugLog(QString("syncBackgroundTrack deferred second=%1 rate=%2")
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
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

    backgroundTrackLastTimelineSecond_ = qMax(0.0, timelineSecond);
    if (!backgroundTrackPendingStart_) {
        return;
    }
    const double rawSecond = timelineSecond + backgroundTrackOffsetSeconds_;
    const double mappedSecond = useStretched ? (rawSecond / backgroundTrackPlaybackRate_) : rawSecond;
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
        backgroundTrackPendingStart_ = false;
        backgroundTrackRunning_ = true;
        appendAudioDebugLog(QString("syncBackgroundTrack started second=%1 raw=%2 mapped=%3 rate=%4")
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    } else {
        backgroundTrackPendingStart_ = true;
        backgroundTrackRunning_ = false;
        appendAudioDebugLog(QString("syncBackgroundTrack start failed rc=%1 second=%2 mapped=%3 rate=%4")
                                .arg(static_cast<int>(startResult))
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    }
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
    stopBank(slideSfx_);
    stopBank(breakSfx_);
    stopBank(exSfx_);
    stopBank(touchSfx_);
    stopBank(fireworkSfx_);
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
    }
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_stop(&voice.voice->sound);
        }
        voice.activeSpanIndex = -1;
    }
}

