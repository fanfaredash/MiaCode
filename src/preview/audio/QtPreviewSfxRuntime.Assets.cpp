QString QtPreviewSfxRuntime::resolveTrackPath(const QString& chartPath) const
{
    const QString normalizedChartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    if (!warmupPaths_.chartPath.isEmpty() && normalizedChartPath == warmupPaths_.chartPath) {
        return warmupPaths_.trackPath;
    }
    return miacode::chart_assets::resolveTrackPath(chartPath);
}

QString QtPreviewSfxRuntime::resolveSfxDir() const
{
    if (!warmupPaths_.sfxDir.isEmpty()) {
        return warmupPaths_.sfxDir;
    }
    return miacode::preview_sfx::resolveSfxDirectory();
}

void QtPreviewSfxRuntime::resetBackgroundTrack()
{
    resetStretchedBackgroundTrack();
    if (backgroundTrackVoice_ != nullptr) {
        if (backgroundTrackVoice_->initialized) {
            ma_sound_uninit(&backgroundTrackVoice_->sound);
            backgroundTrackVoice_->initialized = false;
        }
        if (backgroundTrackVoice_->decoderInitialized) {
            ma_decoder_uninit(&backgroundTrackVoice_->decoder);
            backgroundTrackVoice_->decoderInitialized = false;
        }
        delete backgroundTrackVoice_;
        backgroundTrackVoice_ = nullptr;
    }
    backgroundTrackConfigured_ = false;
    resetBackgroundTrackSessionState();
}

void QtPreviewSfxRuntime::resetStretchedBackgroundTrack()
{
    if (stretchedBackgroundState_ == nullptr) {
        return;
    }
    if (stretchedBackgroundState_->soundInitialized) {
        ma_sound_uninit(&stretchedBackgroundState_->sound);
        stretchedBackgroundState_->soundInitialized = false;
    }
    ma_data_source_uninit(reinterpret_cast<ma_data_source*>(&stretchedBackgroundState_->dataSource));
    if (stretchedBackgroundState_->decoderInitialized) {
        ma_decoder_uninit(&stretchedBackgroundState_->decoder);
        stretchedBackgroundState_->decoderInitialized = false;
    }
    delete stretchedBackgroundState_;
    stretchedBackgroundState_ = nullptr;
}

void QtPreviewSfxRuntime::resetBanks()
{
    const auto resetBank = [](SfxBank& bank) {
        for (Voice* voice : bank.voices) {
            if (voice == nullptr) {
                continue;
            }
            if (voice->initialized) {
                ma_sound_uninit(&voice->sound);
                voice->initialized = false;
            }
            if (voice->decoderInitialized) {
                ma_decoder_uninit(&voice->decoder);
                voice->decoderInitialized = false;
            }
            delete voice;
        }
        bank.voices.clear();
        bank.nextVoice = 0;
        bank.configured = false;
    };

    resetBank(answerSfx_);
    resetBank(judgeSfx_);
    resetBank(judgeBreakSfx_);
    resetBank(slideSfx_);
    resetBank(breakSfx_);
    resetBank(breakSlideBreakSfx_);
    resetBank(breakSlideStartSfx_);
    resetBank(breakSlideSfx_);
    resetBank(judgeBreakSlideSfx_);
    resetBank(exSfx_);
    resetBank(touchSfx_);
    resetBank(fireworkSfx_);
    resetBackgroundTrack();
    if (touchholdVoice_ != nullptr) {
        if (touchholdVoice_->initialized) {
            ma_sound_uninit(&touchholdVoice_->sound);
            touchholdVoice_->initialized = false;
        }
        if (touchholdVoice_->decoderInitialized) {
            ma_decoder_uninit(&touchholdVoice_->decoder);
            touchholdVoice_->decoderInitialized = false;
        }
        delete touchholdVoice_;
        touchholdVoice_ = nullptr;
    }
    activeTouchholdSpanIndices_.clear();
    touchholdSoundLengthFrames_ = 0;

    if (engineState_ != nullptr) {
        ma_engine_uninit(&engineState_->engine);
        delete engineState_;
        engineState_ = nullptr;
    }
    engineInitialized_ = false;
}

