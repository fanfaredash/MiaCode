bool QtPreviewSfxRuntime::initializeAudioEngine()
{
    if (engineState_ != nullptr) {
        return true;
    }

    engineState_ = new EngineState();
    if (ma_engine_init(nullptr, &engineState_->engine) != MA_SUCCESS) {
        delete engineState_;
        engineState_ = nullptr;
        engineInitialized_ = false;
        appendAudioDebugLog("initializeAudioEngine failed");
        return false;
    }
    deviceSampleRate_ = ma_engine_get_sample_rate(&engineState_->engine);
    engineInitialized_ = true;
    appendAudioDebugLog(QString("initializeAudioEngine ok sampleRate=%1").arg(deviceSampleRate_));
    return true;
}

void QtPreviewSfxRuntime::initializeAssets()
{
    if (!engineInitialized_ || engineState_ == nullptr || sfxDir_.isEmpty()) {
        return;
    }

    const auto configureBank = [this](SfxBank& bank, const QString& kind, int voiceCount) {
        const QString path = miacode::preview_sfx::assetFilePathForKind(sfxDir_, kind);
        if (!QFileInfo::exists(path)) {
            return;
        }
        const QByteArray pathBytes = miacode::preview_sfx::encodedAssetFilePathForKind(sfxDir_, kind);
        bank.voices.reserve(voiceCount);
        for (int i = 0; i < voiceCount; ++i) {
            Voice* voice = new Voice();
            if (ma_sound_init_from_file(
                    &engineState_->engine,
                    pathBytes.constData(),
                    0,
                    nullptr,
                    nullptr,
                    &voice->sound
                ) == MA_SUCCESS) {
                voice->initialized = true;
                bank.voices.append(voice);
            } else {
                delete voice;
            }
        }
        bank.configured = !bank.voices.isEmpty();
    };

    configureBank(answerSfx_, "answer", 12);
    configureBank(judgeSfx_, "judge", 12);
    configureBank(judgeBreakSfx_, "judge_break", 1);
    configureBank(slideSfx_, "slide", 8);
    configureBank(breakSfx_, "break", 1);
    configureBank(breakSlideStartSfx_, "break_slide_start", 1);
    configureBank(breakSlideSfx_, "break_slide", 1);
    configureBank(judgeBreakSlideSfx_, "judge_break_slide", 1);
    configureBank(exSfx_, "ex", 8);
    configureBank(touchSfx_, "touch", 12);
    // Firework cheers should also be latest-wins, matching break cheer behavior.
    configureBank(fireworkSfx_, "firework", 1);

    const QString touchholdPath = miacode::preview_sfx::assetFilePathForKind(sfxDir_, "touchhold");
    if (QFileInfo::exists(touchholdPath)) {
        const QByteArray pathBytes = miacode::preview_sfx::encodedAssetFilePathForKind(sfxDir_, "touchhold");
        touchholdVoices_.reserve(8);
        for (int i = 0; i < 8; ++i) {
            TouchholdVoice touchholdVoice;
            touchholdVoice.voice = new Voice();
            if (ma_sound_init_from_file(
                    &engineState_->engine,
                    pathBytes.constData(),
                    0,
                    nullptr,
                    nullptr,
                    &touchholdVoice.voice->sound
                ) == MA_SUCCESS) {
                touchholdVoice.voice->initialized = true;
                ma_uint64 lengthFrames = 0;
                if (ma_sound_get_length_in_pcm_frames(&touchholdVoice.voice->sound, &lengthFrames) == MA_SUCCESS) {
                    touchholdSoundLengthFrames_ = qMax<quint64>(touchholdSoundLengthFrames_, lengthFrames);
                }
                touchholdVoices_.append(touchholdVoice);
            } else {
                delete touchholdVoice.voice;
            }
        }
    }

    applyVolumes();
}

void QtPreviewSfxRuntime::initializeBackgroundTrack()
{
    if (!engineInitialized_ || engineState_ == nullptr || trackPath_.isEmpty()) {
        return;
    }

    Voice* voice = new Voice();
    ma_result initResult = MA_ERROR;
#ifdef Q_OS_WIN
    const QString nativeTrackPath = QDir::toNativeSeparators(trackPath_);
    initResult = ma_sound_init_from_file_w(
        &engineState_->engine,
        reinterpret_cast<const wchar_t*>(nativeTrackPath.utf16()),
        0,
        nullptr,
        nullptr,
        &voice->sound
    );
#else
    const QByteArray pathBytes = QFile::encodeName(trackPath_);
    initResult = ma_sound_init_from_file(
        &engineState_->engine,
        pathBytes.constData(),
        0,
        nullptr,
        nullptr,
        &voice->sound
    );
#endif
    if (initResult != MA_SUCCESS) {
        delete voice;
        appendAudioDebugLog(QString("initializeBackgroundTrack failed path=%1").arg(trackPath_));
        return;
    }
    voice->initialized = true;
    backgroundTrackVoice_ = voice;
    backgroundTrackConfigured_ = true;
    backgroundTrackRunning_ = false;
    backgroundTrackPendingStart_ = false;
    ma_sound_set_volume(&backgroundTrackVoice_->sound, static_cast<float>(settings_.bgmVolume));
    appendAudioDebugLog(QString("initializeBackgroundTrack ok path=%1 volume=%2")
                            .arg(trackPath_)
                            .arg(settings_.bgmVolume, 0, 'f', 3));
}

void QtPreviewSfxRuntime::applyVolumes()
{
    const auto applyVolume = [](SfxBank& bank, double volume) {
        for (Voice* voice : bank.voices) {
            if (voice != nullptr && voice->initialized) {
                ma_sound_set_volume(&voice->sound, static_cast<float>(volume));
            }
        }
    };

    applyVolume(answerSfx_, previewSfxVolumeForKind(settings_, "answer"));
    applyVolume(judgeSfx_, previewSfxVolumeForKind(settings_, "judge"));
    applyVolume(judgeBreakSfx_, previewSfxVolumeForKind(settings_, "judge_break"));
    applyVolume(slideSfx_, previewSfxVolumeForKind(settings_, "slide"));
    applyVolume(breakSfx_, previewSfxVolumeForKind(settings_, "break"));
    applyVolume(breakSlideStartSfx_, previewSfxVolumeForKind(settings_, "break_slide_start"));
    applyVolume(breakSlideSfx_, previewSfxVolumeForKind(settings_, "break_slide_finish"));
    applyVolume(judgeBreakSlideSfx_, previewSfxVolumeForKind(settings_, "judge_break_slide"));
    applyVolume(exSfx_, previewSfxVolumeForKind(settings_, "ex"));
    applyVolume(touchSfx_, previewSfxVolumeForKind(settings_, "touch"));
    applyVolume(fireworkSfx_, previewSfxVolumeForKind(settings_, "firework"));
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_set_volume(&backgroundTrackVoice_->sound, static_cast<float>(settings_.bgmVolume));
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_set_volume(&stretchedBackgroundState_->sound, static_cast<float>(settings_.bgmVolume));
    }
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_set_volume(&voice.voice->sound, static_cast<float>(previewSfxVolumeForKind(settings_, "touchhold")));
        }
    }
    updateTouchholdVoiceVolumes();
}

void QtPreviewSfxRuntime::updateTouchholdVoiceVolumes()
{
    const double baseVolume = previewSfxVolumeForKind(settings_, "touchhold");
    if (touchholdVoices_.isEmpty()) {
        return;
    }

    int activeCount = 0;
    for (const TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex >= 0 && voice.voice != nullptr && voice.voice->initialized) {
            ++activeCount;
        }
    }

    const double activeCopies = previewTouchholdAggregatePlaybackCopies(activeCount);
    const double activeVoiceVolume = (activeCount > 0)
        ? qBound(0.0, baseVolume * (activeCopies / static_cast<double>(activeCount)), 1.5)
        : qBound(0.0, baseVolume, 1.5);
    const float inactiveVoiceVolume = static_cast<float>(qBound(0.0, baseVolume, 1.5));
    const float activeVoiceVolumeF = static_cast<float>(activeVoiceVolume);

    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice == nullptr || !voice.voice->initialized) {
            continue;
        }
        ma_sound_set_volume(
            &voice.voice->sound,
            voice.activeSpanIndex >= 0 ? activeVoiceVolumeF : inactiveVoiceVolume
        );
    }
}

bool QtPreviewSfxRuntime::prepareStretchedBackgroundTrack(double timelineSecond)
{
    Q_UNUSED(timelineSecond);
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        return false;
    }
    if (!engineInitialized_ || engineState_ == nullptr || trackPath_.isEmpty()) {
        return false;
    }
    if (stretchedBackgroundState_ != nullptr
        && stretchedBackgroundState_->soundInitialized
        && stretchedBackgroundState_->trackPath == trackPath_
        && qAbs(stretchedBackgroundState_->playbackRate - backgroundTrackPlaybackRate_) <= kQtPreviewSfxEpsilonSeconds) {
        return true;
    }
    resetStretchedBackgroundTrack();
    StretchedBackgroundState* state = new StretchedBackgroundState();
    state->trackPath = trackPath_;
    state->playbackRate = backgroundTrackPlaybackRate_;

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_result decoderInitResult = MA_ERROR;
#ifdef Q_OS_WIN
    const QString nativeTrackPath = QDir::toNativeSeparators(trackPath_);
    decoderInitResult = ma_decoder_init_file_w(
        reinterpret_cast<const wchar_t*>(nativeTrackPath.utf16()),
        &decoderConfig,
        &state->decoder
    );
#else
    const QByteArray pathBytes = QFile::encodeName(trackPath_);
    decoderInitResult = ma_decoder_init_file(pathBytes.constData(), &decoderConfig, &state->decoder);
#endif
    if (decoderInitResult != MA_SUCCESS) {
        appendAudioDebugLog(QString("prepareStretchedBackgroundTrack decoder_init_file failed rc=%1 track=%2")
                                .arg(static_cast<int>(decoderInitResult))
                                .arg(trackPath_));
        delete state;
        return false;
    }
    state->decoderInitialized = true;
    state->channels = state->decoder.outputChannels;
    state->sampleRate = state->decoder.outputSampleRate;
    if (state->channels == 0 || state->sampleRate == 0) {
        ma_result formatResult = ma_data_source_get_data_format(
            reinterpret_cast<ma_data_source*>(&state->decoder),
            nullptr,
            &state->channels,
            &state->sampleRate,
            nullptr,
            0
        );
        if (formatResult != MA_SUCCESS || state->channels == 0 || state->sampleRate == 0) {
            appendAudioDebugLog(QString("prepareStretchedBackgroundTrack get_data_format failed rc=%1 track=%2")
                                    .arg(static_cast<int>(formatResult))
                                    .arg(trackPath_));
            ma_decoder_uninit(&state->decoder);
            delete state;
            return false;
        }
    }
    state->stretcher.setSampleRate(state->sampleRate);
    state->stretcher.setChannels(state->channels);
    state->stretcher.setPitch(1.0f);
    state->stretcher.setTempo(static_cast<float>(state->playbackRate));
    state->decodeChunk.resize(static_cast<int>(4096 * state->channels));
    state->stretchChunk.resize(static_cast<int>(4096 * state->channels));

    ma_uint64 sourceLengthFrames = 0;
    if (ma_data_source_get_length_in_pcm_frames(
            reinterpret_cast<ma_data_source*>(&state->decoder),
            &sourceLengthFrames
        ) == MA_SUCCESS
        && sourceLengthFrames > 0) {
        state->sourceFrameCount = sourceLengthFrames;
        state->stretchedFrameCount = static_cast<ma_uint64>(
            qMax(1.0, static_cast<double>(qCeil(static_cast<double>(sourceLengthFrames) / state->playbackRate)))
        );
    }

    ma_data_source_config dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = StretchedBackgroundState::dataSourceVTable();
    if (ma_data_source_init(&dataSourceConfig, reinterpret_cast<ma_data_source*>(&state->dataSource)) != MA_SUCCESS) {
        appendAudioDebugLog("prepareStretchedBackgroundTrack data_source_init failed");
        ma_decoder_uninit(&state->decoder);
        delete state;
        return false;
    }

    const ma_result soundInitResult = ma_sound_init_from_data_source(
        &engineState_->engine,
        reinterpret_cast<ma_data_source*>(&state->dataSource),
        0,
        nullptr,
        &state->sound
    );
    if (soundInitResult != MA_SUCCESS) {
        appendAudioDebugLog(QString("prepareStretchedBackgroundTrack sound_init_from_data_source failed rc=%1")
                                .arg(static_cast<int>(soundInitResult)));
        ma_data_source_uninit(reinterpret_cast<ma_data_source*>(&state->dataSource));
        ma_decoder_uninit(&state->decoder);
        delete state;
        return false;
    }
    state->soundInitialized = true;
    ma_sound_set_volume(&state->sound, static_cast<float>(settings_.bgmVolume));
    stretchedBackgroundState_ = state;
    appendAudioDebugLog(QString("prepareStretchedBackgroundTrack ready track=%1 rate=%2 channels=%3 sampleRate=%4")
                            .arg(state->trackPath)
                            .arg(state->playbackRate, 0, 'f', 3)
                            .arg(state->channels)
                            .arg(state->sampleRate));
    return true;
}

double QtPreviewSfxRuntime::stretchedBackgroundPlaybackSecond() const
{
    if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }
    if (!backgroundTrackRunning_) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }

    float cursorSeconds = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&stretchedBackgroundState_->sound, &cursorSeconds) != MA_SUCCESS) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }

    const double timelineSecond =
        (static_cast<double>(cursorSeconds) * backgroundTrackPlaybackRate_) - backgroundTrackOffsetSeconds_;
    return qMax(0.0, timelineSecond);
}

