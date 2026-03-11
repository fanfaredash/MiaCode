QString QtPreviewSfxRuntime::resolveTrackPath(const QString& chartPath) const
{
    if (chartPath.isEmpty()) {
        return QString();
    }
    const QFileInfo chartInfo(chartPath);
    const QString path = QDir(chartInfo.absolutePath()).filePath("track.mp3");
    if (QFileInfo::exists(path)) {
        return QDir::cleanPath(path);
    }
    return QString();
}

QString QtPreviewSfxRuntime::resolveSfxDir() const
{
    const QString envDir = QDir::cleanPath(
        qEnvironmentVariable("MIACODE_PREVIEW_SFX_DIR", qEnvironmentVariable("MAIMURI_PREVIEW_SFX_DIR")).trimmed()
    );
    if (!envDir.isEmpty() && QFileInfo::exists(QDir(envDir).filePath("answer.wav"))) {
        return envDir;
    }

    QStringList candidates;
    const QString assetSfxUpper = miacode::assets::assetPath("SFX");
    if (!assetSfxUpper.isEmpty()) {
        candidates << assetSfxUpper;
    }
    const QString assetSfxLower = miacode::assets::assetPath("sfx");
    if (!assetSfxLower.isEmpty()) {
        candidates << assetSfxLower;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates << QDir::cleanPath(appDir.filePath("SFX"));
    candidates << QDir::cleanPath(appDir.filePath("sfx"));

    for (const QString& path : candidates) {
        if (QFileInfo::exists(QDir(path).filePath("answer.wav"))) {
            return path;
        }
    }
    return QString();
}

void QtPreviewSfxRuntime::resetBackgroundTrack()
{
    resetStretchedBackgroundTrack();
    if (backgroundTrackVoice_ != nullptr) {
        if (backgroundTrackVoice_->initialized) {
            ma_sound_uninit(&backgroundTrackVoice_->sound);
        }
        delete backgroundTrackVoice_;
        backgroundTrackVoice_ = nullptr;
    }
    backgroundTrackConfigured_ = false;
    backgroundTrackRunning_ = false;
    backgroundTrackPendingStart_ = false;
    backgroundTrackLastTimelineSecond_ = 0.0;
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
            }
            delete voice;
        }
        bank.voices.clear();
        bank.nextVoice = 0;
        bank.configured = false;
    };

    resetBank(answerSfx_);
    resetBank(slideSfx_);
    resetBank(breakSfx_);
    resetBank(exSfx_);
    resetBank(touchSfx_);
    resetBank(fireworkSfx_);
    resetBackgroundTrack();
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr) {
            if (voice.voice->initialized) {
                ma_sound_uninit(&voice.voice->sound);
            }
            delete voice.voice;
            voice.voice = nullptr;
        }
        voice.activeSpanIndex = -1;
    }
    touchholdVoices_.clear();
    touchholdSoundLengthFrames_ = 0;

    if (engineState_ != nullptr) {
        ma_engine_uninit(&engineState_->engine);
        delete engineState_;
        engineState_ = nullptr;
    }
    engineInitialized_ = false;
}

