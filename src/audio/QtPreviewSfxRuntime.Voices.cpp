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
    } else if (lowered == "track_start") {
        bank = &trackStartSfx_;
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
    const double effectiveVolume = qBound(0.0, volume * effectiveGain, 2.0);
    ma_sound_stop(&voice->sound);
    ma_sound_set_volume(&voice->sound, static_cast<float>(effectiveVolume));
    ma_sound_seek_to_pcm_frame(&voice->sound, 0);
    ma_sound_start(&voice->sound);
    return true;
}

bool QtPreviewSfxRuntime::playTouchholdAudition()
{
    if (previewSfxVolumeForKind(settings_, QStringLiteral("touchhold")) <= 0.0 || touchholdVoice_ == nullptr
        || !touchholdVoice_->initialized) {
        return touchholdVoice_ != nullptr && touchholdVoice_->initialized;
    }

    touchholdOwnerSpanIndex_ = -1;
    ma_sound_stop(&touchholdVoice_->sound);
    ma_sound_seek_to_pcm_frame(&touchholdVoice_->sound, 0);
    ma_sound_start(&touchholdVoice_->sound);
    return true;
}
