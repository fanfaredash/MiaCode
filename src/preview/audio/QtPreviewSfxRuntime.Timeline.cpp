namespace {

struct AggregatedPlayback {
    QString kind;
    int count = 0;
    double maxGain = 0.0;
};

bool shouldAggregatePlaybackKind(const QString& kind)
{
    return previewSfxShouldAggregateKind(kind);
}

void accumulatePlayback(QVector<AggregatedPlayback>* playbacks, const QString& kind, double gain)
{
    if (playbacks == nullptr || kind.isEmpty()) {
        return;
    }
    for (AggregatedPlayback& playback : *playbacks) {
        if (playback.kind != kind) {
            continue;
        }
        ++playback.count;
        playback.maxGain = qMax(playback.maxGain, qMax(0.0, gain));
        return;
    }
    AggregatedPlayback playback;
    playback.kind = kind;
    playback.count = 1;
    playback.maxGain = qMax(0.0, gain);
    playbacks->append(playback);
}

double playbackGain(const AggregatedPlayback& playback)
{
    return previewSfxPlaybackGainForAggregate(playback.kind, playback.count, playback.maxGain);
}

}

QtPreviewSfxRuntime::QtPreviewSfxRuntime(QObject* parent)
    : QObject(parent)
{
    appendAudioDebugLog("QtPreviewSfxRuntime created");
}

QtPreviewSfxRuntime::~QtPreviewSfxRuntime()
{
    appendAudioDebugLog("QtPreviewSfxRuntime destroying");
    resetBanks();
}

void QtPreviewSfxRuntime::setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir)
{
    warmupChartPath_ = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    warmupTrackPath_ = trackPath.isEmpty() ? QString() : QDir::cleanPath(trackPath);
    warmupSfxDir_ = sfxDir.isEmpty() ? QString() : QDir::cleanPath(sfxDir);
}

void QtPreviewSfxRuntime::reloadAssets(const PreviewAudioSettings& settings)
{
    settings_ = settings;
    settings_.normalize();
    resetBanks();
    sfxDir_ = resolveSfxDir();
    if (!initializeAudioEngine()) {
        return;
    }
    initializeAssets();
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        initializeBackgroundTrack();
    } else {
        prepareStretchedBackgroundTrack(backgroundTrackLastTimelineSecond_);
    }
}

void QtPreviewSfxRuntime::setChartPath(const QString& chartPath)
{
    const QString normalizedChartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    if (normalizedChartPath == chartPath_) {
        return;
    }

    chartPath_ = normalizedChartPath;
    trackPath_ = resolveTrackPath(chartPath_);
    const QString resolvedSfxDir = resolveSfxDir();
    if (resolvedSfxDir != sfxDir_) {
        sfxDir_ = resolvedSfxDir;
        if (engineInitialized_ && engineState_ != nullptr) {
            initializeAssets();
        }
    }
    appendAudioDebugLog(QString("setChartPath chart=%1 track=%2").arg(chartPath_, trackPath_));
    resetBackgroundTrack();
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        initializeBackgroundTrack();
    } else {
        prepareStretchedBackgroundTrack(backgroundTrackLastTimelineSecond_);
    }
}

void QtPreviewSfxRuntime::setBackgroundTrackOffsetSeconds(double seconds)
{
    const double clamped = qIsFinite(seconds) ? seconds : 0.0;
    if (qAbs(backgroundTrackOffsetSeconds_ - clamped) <= kQtPreviewSfxEpsilonSeconds) {
        return;
    }
    backgroundTrackOffsetSeconds_ = clamped;
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    backgroundTrackLastTimelineSecond_ = 0.0;
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
        ma_sound_seek_to_second(&backgroundTrackVoice_->sound, 0.0f);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
        ma_sound_seek_to_second(&stretchedBackgroundState_->sound, 0.0f);
    }
}

void QtPreviewSfxRuntime::setBackgroundTrackPlaybackRate(double rate)
{
    const double clamped = qBound(0.25, qIsFinite(rate) ? rate : 1.0, 2.0);
    if (qAbs(backgroundTrackPlaybackRate_ - clamped) <= kQtPreviewSfxEpsilonSeconds) {
        return;
    }
    backgroundTrackPlaybackRate_ = clamped;
    appendAudioDebugLog(QString("setBackgroundTrackPlaybackRate rate=%1").arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    backgroundTrackLastTimelineSecond_ = qMax(0.0, backgroundTrackLastTimelineSecond_);
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
    }
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        resetStretchedBackgroundTrack();
        if (backgroundTrackVoice_ == nullptr && !trackPath_.isEmpty()) {
            initializeBackgroundTrack();
        }
    } else if (!trackPath_.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
        if (backgroundTrackVoice_ != nullptr) {
            if (backgroundTrackVoice_->initialized) {
                ma_sound_uninit(&backgroundTrackVoice_->sound);
            }
            delete backgroundTrackVoice_;
            backgroundTrackVoice_ = nullptr;
            backgroundTrackConfigured_ = false;
        }
        prepareStretchedBackgroundTrack(backgroundTrackLastTimelineSecond_);
    }
}

void QtPreviewSfxRuntime::applyLevels(const PreviewAudioSettings& settings)
{
    settings_ = settings;
    settings_.normalize();
    applyVolumes();
}

void QtPreviewSfxRuntime::configureTimeline(const QVector<TimelineNoteMarker>& noteMarkers)
{
    miacode::preview_sfx_timeline::buildTimeline(noteMarkers, &events_, &touchholdSpans_);
    eventIndex_ = 0;
}

void QtPreviewSfxRuntime::clearTimeline()
{
    events_.clear();
    eventIndex_ = 0;
    touchholdSpans_.clear();
    stopAll();
}

void QtPreviewSfxRuntime::resetCursor(double second, bool includeCurrentSecond)
{
    eventIndex_ = 0;
    while (eventIndex_ < events_.size()) {
        const double eventSecond = events_[eventIndex_].second;
        const bool beforeStart = includeCurrentSecond
            ? (eventSecond + kQtPreviewSfxEpsilonSeconds < second)
            : (eventSecond <= second + kQtPreviewSfxEpsilonSeconds);
        if (!beforeStart) {
            break;
        }
        ++eventIndex_;
    }
}

void QtPreviewSfxRuntime::drainEvents(double second)
{
    while (eventIndex_ < events_.size()) {
        const int groupStart = eventIndex_;
        const double groupSecond = events_[groupStart].second;
        if (groupSecond > second + kQtPreviewSfxEpsilonSeconds) {
            break;
        }

        int groupEnd = groupStart + 1;
        while (groupEnd < events_.size()
               && qAbs(events_[groupEnd].second - groupSecond) <= kQtPreviewSfxEpsilonSeconds) {
            ++groupEnd;
        }

        QVector<AggregatedPlayback> playbacks;
        for (int i = groupStart; i < groupEnd; ++i) {
            const Event& event = events_[i];
            if (event.kind == "touchhold_start") {
                startTouchholdSpan(event.spanIndex, 0.0);
                continue;
            }
            if (event.kind == "touchhold_stop") {
                stopTouchholdSpan(event.spanIndex);
                continue;
            }
            if (shouldAggregatePlaybackKind(event.kind)) {
                accumulatePlayback(&playbacks, event.kind, event.gain);
                continue;
            }
            playKindInternal(event.kind, event.gain);
        }
        for (const AggregatedPlayback& playback : playbacks) {
            playKindInternal(playback.kind, playbackGain(playback));
        }

        eventIndex_ = groupEnd;
    }
}

void QtPreviewSfxRuntime::pauseTouchholdVoices()
{
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_stop(&voice.voice->sound);
        }
        voice.activeSpanIndex = -1;
    }
}

void QtPreviewSfxRuntime::restoreTouchholdVoices(double second)
{
    pauseTouchholdVoices();
    for (int spanIndex = 0; spanIndex < touchholdSpans_.size(); ++spanIndex) {
        const TouchholdSpan& span = touchholdSpans_[spanIndex];
        if (second + kQtPreviewSfxEpsilonSeconds < span.startSecond) {
            continue;
        }
        if (second >= span.endSecond - kQtPreviewSfxEpsilonSeconds) {
            continue;
        }
        startTouchholdSpan(spanIndex, second - span.startSecond);
    }
    updateTouchholdVoiceVolumes();
}

bool QtPreviewSfxRuntime::hasBackgroundTrack() const
{
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        return backgroundTrackConfigured_ && backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized;
    }
    if (!trackPath_.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
        return true;
    }
    return stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized;
}

bool QtPreviewSfxRuntime::isBackgroundTrackRunning() const
{
    return backgroundTrackRunning_;
}
