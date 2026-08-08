bool QtPreviewSfxRuntime::initializeAudioEngine()
{
    MC_OP("QtPreviewSfxRuntime::initializeAudioEngine");
    if (engineState_ != nullptr) {
        return true;
    }

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.channels = miacode::preview_audio::kMixChannels;
    engineConfig.sampleRate = miacode::preview_audio::kMixSampleRate;

    engineState_ = new EngineState();
    const ma_result engineInitResult = ma_engine_init(&engineConfig, &engineState_->engine);
    if (engineInitResult != MA_SUCCESS) {
        lastNativeErrorCode_ = static_cast<int>(engineInitResult);
        delete engineState_;
        engineState_ = nullptr;
        engineInitialized_ = false;
        _mc_op_.fail(QStringLiteral("ma_engine_init failed"));
        appendAudioDebugLog("initializeAudioEngine failed");
        return false;
    }
    deviceSampleRate_ = ma_engine_get_sample_rate(&engineState_->engine);
    engineInitialized_ = true;
    appendAudioDebugLog(
        QString("initializeAudioEngine ok sampleRate=%1 channels=%2")
            .arg(deviceSampleRate_)
            .arg(ma_engine_get_channels(&engineState_->engine)));
    return true;
}

void QtPreviewSfxRuntime::initializeAssets()
{
    MC_OP("QtPreviewSfxRuntime::initializeAssets");
    _mc_op_.note(QStringLiteral("sfx_dir=%1").arg(preparedAssets_.sfxDir));
    if (!engineInitialized_ || engineState_ == nullptr || preparedAssets_.sfxDir.isEmpty()) {
        return;
    }

    const auto configureBank = [this](SfxBank& bank, const QString& kind, int voiceCount) {
        const QString path = miacode::preview_sfx::assetFilePathForKind(preparedAssets_.sfxDir, kind);
        if (!QFileInfo::exists(path)) {
            return;
        }
        bank.voices.reserve(voiceCount);
        for (int i = 0; i < voiceCount; ++i) {
            Voice* voice = new Voice();
            if (miacode::audio_io::soundInitFromFile(
                    &engineState_->engine,
                    path,
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

    configureBank(answerSfx_, "answer", 1);
    configureBank(judgeSfx_, "judge", 1);
    configureBank(judgeBreakSfx_, "judge_break", 1);
    configureBank(slideSfx_, "slide", 1);
    configureBank(breakSfx_, "break", 1);
    configureBank(breakSlideStartSfx_, "break_slide_start", 1);
    configureBank(breakSlideSfx_, "break_slide", 1);
    configureBank(judgeBreakSlideSfx_, "judge_break_slide", 1);
    configureBank(exSfx_, "ex", 1);
    configureBank(touchSfx_, "touch", 1);
    // All note SFX kinds are now latest-wins to mirror MajdataPlay's runtime behavior.
    configureBank(fireworkSfx_, "firework", 1);
    configureBank(trackStartSfx_, "track_start", 1);

    const QString touchholdPath = miacode::preview_sfx::assetFilePathForKind(preparedAssets_.sfxDir, "touchhold");
    if (QFileInfo::exists(touchholdPath)) {
        Voice* voice = new Voice();
        if (miacode::audio_io::soundInitFromFile(
                &engineState_->engine,
                touchholdPath,
                0,
                nullptr,
                nullptr,
                &voice->sound
            ) == MA_SUCCESS) {
            voice->initialized = true;
            ma_uint64 lengthFrames = 0;
            if (ma_sound_get_length_in_pcm_frames(&voice->sound, &lengthFrames) == MA_SUCCESS) {
                touchholdSoundLengthFrames_ = lengthFrames;
            }
            touchholdVoice_ = voice;
        } else {
            delete voice;
        }
    }

    applyVolumes();
}

void QtPreviewSfxRuntime::initializeBackgroundTrack()
{
    if (!prepareStretchedBackgroundTrack(playbackSession_.backgroundTrackLastTimelineSecond)) {
        appendAudioDebugLog(QString("initializeBackgroundTrack deferred_stretched path=%1 rate=%2")
                                .arg(preparedAssets_.trackPath)
                                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    }
}

void QtPreviewSfxRuntime::armBackgroundTrackClock(double timelineSecond)
{
    if (engineState_ == nullptr) {
        return;
    }
    playbackSession_.backgroundTrackClockArmed = true;
    playbackSession_.backgroundTrackStartEngineFrame = ma_engine_get_time_in_pcm_frames(&engineState_->engine);
    playbackSession_.backgroundTrackStartTimelineSecond = timelineSecond;
    playbackSession_.backgroundTrackClockPlaybackRate = playbackSession_.backgroundTrackPlaybackRate;
}

void QtPreviewSfxRuntime::clearBackgroundTrackClockAnchor()
{
    playbackSession_.backgroundTrackClockArmed = false;
    playbackSession_.backgroundTrackStartEngineFrame = 0;
    playbackSession_.backgroundTrackStartTimelineSecond = playbackSession_.backgroundTrackLastTimelineSecond;
    playbackSession_.backgroundTrackClockPlaybackRate = playbackSession_.backgroundTrackPlaybackRate;
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
    applyVolume(trackStartSfx_, previewSfxVolumeForKind(settings_, "track_start"));
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_set_volume(&backgroundTrackVoice_->sound, static_cast<float>(previewTrackVolume(settings_)));
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_set_volume(&stretchedBackgroundState_->sound, static_cast<float>(previewTrackVolume(settings_)));
    }
    if (touchholdVoice_ != nullptr && touchholdVoice_->initialized) {
        ma_sound_set_volume(&touchholdVoice_->sound, static_cast<float>(previewSfxVolumeForKind(settings_, "touchhold")));
    }
}

bool QtPreviewSfxRuntime::prepareStretchedBackgroundTrack(double timelineSecond)
{
    Q_UNUSED(timelineSecond);
    if (!engineInitialized_ || engineState_ == nullptr || preparedAssets_.trackPath.isEmpty()) {
        return false;
    }
    if (stretchedBackgroundState_ != nullptr
        && stretchedBackgroundState_->soundInitialized
        && stretchedBackgroundState_->trackPath == preparedAssets_.trackPath
        && qAbs(stretchedBackgroundState_->playbackRate - playbackSession_.backgroundTrackPlaybackRate)
            <= kQtPreviewSfxEpsilonSeconds) {
        return true;
    }
    resetStretchedBackgroundTrack();
    StretchedBackgroundState* state = new StretchedBackgroundState();
    state->trackPath = preparedAssets_.trackPath;
    state->playbackRate = playbackSession_.backgroundTrackPlaybackRate;

    ma_decoder_config decoderConfig = ma_decoder_config_init(
        ma_format_f32,
        miacode::preview_audio::kMixChannels,
        deviceSampleRate_);
    const ma_result decoderInitResult =
        miacode::audio_io::decoderInitFile(preparedAssets_.trackPath, &decoderConfig, &state->decoder);
    if (decoderInitResult != MA_SUCCESS) {
        appendAudioDebugLog(QString("prepareStretchedBackgroundTrack decoder_init_file failed rc=%1 track=%2")
                                .arg(static_cast<int>(decoderInitResult))
                                .arg(preparedAssets_.trackPath));
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
                                    .arg(preparedAssets_.trackPath));
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
            ma_sound_set_volume(&state->sound, static_cast<float>(previewTrackVolume(settings_)));
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
        return playbackSession_.backgroundTrackLastTimelineSecond;
    }
    if (!playbackSession_.backgroundTrackRunning || !playbackSession_.backgroundTrackClockArmed) {
        return playbackSession_.backgroundTrackLastTimelineSecond;
    }
    if (engineState_ == nullptr) {
        return playbackSession_.backgroundTrackLastTimelineSecond;
    }
    const quint64 engineNowFrame = ma_engine_get_time_in_pcm_frames(&engineState_->engine);
    const quint64 elapsedFrames =
        engineNowFrame >= playbackSession_.backgroundTrackStartEngineFrame
            ? (engineNowFrame - playbackSession_.backgroundTrackStartEngineFrame)
            : 0;
    const double timelineSecond =
        playbackSession_.backgroundTrackStartTimelineSecond
        + (static_cast<double>(elapsedFrames) / static_cast<double>(deviceSampleRate_))
            * playbackSession_.backgroundTrackClockPlaybackRate;
    return qMax(0.0, timelineSecond);
}

bool QtPreviewSfxRuntime::stretchedBackgroundClockReady() const
{
    if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
        return false;
    }
    QMutexLocker locker(&stretchedBackgroundState_->mutex);
    return stretchedBackgroundState_->authoritativeClockReady;
}
