bool QtPreviewSfxRuntime::playKindInternal(const QString& kind, double gain)
{
    const QString lowered = kind.trimmed().toLower();
    if (lowered.isEmpty()) {
        return false;
    }

    SfxBank* bank = nullptr;
    double volume = 0.0;
    if (lowered == "answer") {
        bank = &answerSfx_;
        volume = settings_.answerVolume;
    } else if (lowered == "judge") {
        bank = &judgeSfx_;
        volume = settings_.judgeVolume;
    } else if (lowered == "judge_break" || lowered == "break_touch") {
        bank = &judgeBreakSfx_;
        volume = settings_.breakVolume;
    } else if (lowered == "slide") {
        bank = &slideSfx_;
        volume = settings_.slideVolume;
    } else if (lowered == "break") {
        bank = &breakSfx_;
        volume = qMin(settings_.breakVolume * 1.5, 1.5);
    } else if (lowered == "break_slide" || lowered == "break_slide_start") {
        bank = &breakSlideStartSfx_;
        volume = settings_.breakSlideVolume;
    } else if (lowered == "break_slide_finish") {
        bank = &breakSlideSfx_;
        volume = settings_.breakSlideVolume;
    } else if (lowered == "judge_break_slide") {
        bank = &judgeBreakSlideSfx_;
        volume = settings_.breakSlideVolume;
    } else if (lowered == "ex") {
        bank = &exSfx_;
        volume = settings_.exVolume;
    } else if (lowered == "touch") {
        bank = &touchSfx_;
        volume = settings_.touchVolume;
    } else if (lowered == "firework") {
        bank = &fireworkSfx_;
        volume = settings_.fireworkVolume;
    } else if (lowered == "touchhold") {
        return playTouchholdAudition();
    }

    if (bank == nullptr || !bank->configured || bank->voices.isEmpty()) {
        return false;
    }
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
    if (settings_.touchVolume <= 0.0) {
        return;
    }
    if (spanIndex < 0 || spanIndex >= touchholdSpans_.size()) {
        return;
    }
    if (touchholdVoices_.isEmpty()) {
        return;
    }

    const TouchholdSpan& span = touchholdSpans_[spanIndex];
    if (span.endSecond <= span.startSecond) {
        return;
    }

    const ma_uint64 offsetFrames = static_cast<ma_uint64>(qMax(0.0, offsetSeconds) * deviceSampleRate_);
    if (touchholdSoundLengthFrames_ > 0 && offsetFrames >= touchholdSoundLengthFrames_) {
        return;
    }

    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex == spanIndex) {
            if (voice.voice == nullptr || !voice.voice->initialized) {
                return;
            }
            ma_sound_stop(&voice.voice->sound);
            ma_sound_seek_to_pcm_frame(&voice.voice->sound, offsetFrames);
            ma_sound_start(&voice.voice->sound);
            return;
        }
    }

    TouchholdVoice* freeVoice = nullptr;
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex < 0) {
            freeVoice = &voice;
            break;
        }
    }
    if (freeVoice == nullptr) {
        freeVoice = &touchholdVoices_.first();
    }
    if (freeVoice == nullptr || freeVoice->voice == nullptr || !freeVoice->voice->initialized) {
        return;
    }

    ma_sound_stop(&freeVoice->voice->sound);
    ma_sound_seek_to_pcm_frame(&freeVoice->voice->sound, offsetFrames);
    ma_sound_start(&freeVoice->voice->sound);
    freeVoice->activeSpanIndex = spanIndex;
}

void QtPreviewSfxRuntime::stopTouchholdSpan(int spanIndex)
{
    if (spanIndex < 0) {
        return;
    }
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex != spanIndex) {
            continue;
        }
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_stop(&voice.voice->sound);
        }
        voice.activeSpanIndex = -1;
        return;
    }
}

bool QtPreviewSfxRuntime::playTouchholdAudition()
{
    if (settings_.touchVolume <= 0.0 || touchholdVoices_.isEmpty()) {
        return !touchholdVoices_.isEmpty();
    }

    TouchholdVoice* voiceToUse = nullptr;
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex < 0) {
            voiceToUse = &voice;
            break;
        }
    }
    if (voiceToUse == nullptr) {
        voiceToUse = &touchholdVoices_.first();
    }
    if (voiceToUse == nullptr || voiceToUse->voice == nullptr || !voiceToUse->voice->initialized) {
        return false;
    }

    voiceToUse->activeSpanIndex = -1;
    ma_sound_stop(&voiceToUse->voice->sound);
    ma_sound_seek_to_pcm_frame(&voiceToUse->voice->sound, 0);
    ma_sound_start(&voiceToUse->voice->sound);
    return true;
}

