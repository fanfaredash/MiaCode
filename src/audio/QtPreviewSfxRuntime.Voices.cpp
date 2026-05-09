bool QtPreviewSfxRuntime::playKindInternal(const QString& kind, double gain)
{
    const QString lowered = previewSfxNormalizedKind(kind);
    if (lowered.isEmpty()) {
        return false;
    }

    SfxBank* bank = nullptr;
    if (lowered == "answer") {
        bank = &answerSfx_;
    } else if (lowered == "judge") {
        bank = &judgeSfx_;
    } else if (lowered == "judge_break" || lowered == "break_touch") {
        bank = &judgeBreakSfx_;
    } else if (lowered == "slide") {
        bank = &slideSfx_;
    } else if (lowered == "break" || lowered == "break_slide_tail_break") {
        bank = &breakSfx_;
    } else if (lowered == "break_slide" || lowered == "break_slide_start") {
        bank = &breakSlideStartSfx_;
    } else if (lowered == "break_slide_finish") {
        bank = &breakSlideSfx_;
    } else if (lowered == "judge_break_slide") {
        bank = &judgeBreakSlideSfx_;
    } else if (lowered == "ex") {
        bank = &exSfx_;
    } else if (lowered == "touch") {
        bank = &touchSfx_;
    } else if (lowered == "firework") {
        bank = &fireworkSfx_;
    } else if (lowered == "touchhold") {
        return playTouchholdAudition();
    }

    if (bank == nullptr || !bank->configured || bank->voices.isEmpty()) {
        return false;
    }
    const double volume = previewSfxVolumeForKind(settings_, lowered);
    if (volume <= 0.0) {
        return true;
    }

    Voice* voice = bank->voices[bank->nextVoice % bank->voices.size()];
    bank->nextVoice = (bank->nextVoice + 1) % bank->voices.size();
    if (voice == nullptr || !voice->initialized) {
        return true;
    }
    const double effectiveGain = qMax(0.0, gain);
    const double effectiveVolume = qBound(0.0, volume * effectiveGain, 1.5);
    ma_sound_stop(&voice->sound);
    ma_sound_set_volume(&voice->sound, static_cast<float>(effectiveVolume));
    ma_sound_seek_to_pcm_frame(&voice->sound, 0);
    ma_sound_start(&voice->sound);
    return true;
}

void QtPreviewSfxRuntime::startTouchholdSpan(int spanIndex, double offsetSeconds)
{
    if (previewSfxVolumeForKind(settings_, QStringLiteral("touchhold")) <= 0.0) {
        return;
    }
    if (spanIndex < 0 || spanIndex >= preparedTimeline_.touchholdSpans.size()) {
        return;
    }
    if (touchholdVoice_ == nullptr || !touchholdVoice_->initialized) {
        return;
    }

    const TouchholdSpan& span = preparedTimeline_.touchholdSpans[spanIndex];
    if (span.endSecond <= span.startSecond) {
        return;
    }

    const ma_uint64 offsetFrames = static_cast<ma_uint64>(qMax(0.0, offsetSeconds) * deviceSampleRate_);
    if (touchholdSoundLengthFrames_ > 0 && offsetFrames >= touchholdSoundLengthFrames_) {
        return;
    }

    if (activeTouchholdSpanIndices_.contains(spanIndex)) {
        return;
    }

    const bool shouldStartPlayback = activeTouchholdSpanIndices_.isEmpty();
    activeTouchholdSpanIndices_.append(spanIndex);
    if (!shouldStartPlayback) {
        return;
    }

    ma_sound_stop(&touchholdVoice_->sound);
    ma_sound_seek_to_pcm_frame(&touchholdVoice_->sound, offsetFrames);
    ma_sound_start(&touchholdVoice_->sound);
}

void QtPreviewSfxRuntime::stopTouchholdSpan(int spanIndex)
{
    if (spanIndex < 0) {
        return;
    }
    if (!activeTouchholdSpanIndices_.removeOne(spanIndex)) {
        return;
    }
    if (!activeTouchholdSpanIndices_.isEmpty()) {
        return;
    }
    if (touchholdVoice_ != nullptr && touchholdVoice_->initialized) {
        ma_sound_stop(&touchholdVoice_->sound);
    }
}

bool QtPreviewSfxRuntime::playTouchholdAudition()
{
    if (previewSfxVolumeForKind(settings_, QStringLiteral("touchhold")) <= 0.0 || touchholdVoice_ == nullptr
        || !touchholdVoice_->initialized) {
        return touchholdVoice_ != nullptr && touchholdVoice_->initialized;
    }

    activeTouchholdSpanIndices_.clear();
    ma_sound_stop(&touchholdVoice_->sound);
    ma_sound_seek_to_pcm_frame(&touchholdVoice_->sound, 0);
    ma_sound_start(&touchholdVoice_->sound);
    return true;
}
