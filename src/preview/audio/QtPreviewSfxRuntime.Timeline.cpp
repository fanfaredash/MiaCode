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
    warmupPaths_.chartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    warmupPaths_.trackPath = trackPath.isEmpty() ? QString() : QDir::cleanPath(trackPath);
    warmupPaths_.sfxDir = sfxDir.isEmpty() ? QString() : QDir::cleanPath(sfxDir);
}

void QtPreviewSfxRuntime::reloadAssets(const PreviewAudioSettings& settings)
{
    settings_ = settings;
    settings_.normalize();
    resetBanks();
    preparedAssets_.sfxDir = resolveSfxDir();
    if (!initializeAudioEngine()) {
        return;
    }
    initializeAssets();
    if (qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0)) {
        initializeBackgroundTrack();
    } else {
        prepareStretchedBackgroundTrack(playbackSession_.backgroundTrackLastTimelineSecond);
    }
}

void QtPreviewSfxRuntime::setChartPath(const QString& chartPath)
{
    const QString normalizedChartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    if (normalizedChartPath == preparedAssets_.chartPath) {
        return;
    }

    preparedAssets_.chartPath = normalizedChartPath;
    preparedAssets_.trackPath = resolveTrackPath(preparedAssets_.chartPath);
    const QString resolvedSfxDir = resolveSfxDir();
    if (resolvedSfxDir != preparedAssets_.sfxDir) {
        preparedAssets_.sfxDir = resolvedSfxDir;
        if (engineInitialized_ && engineState_ != nullptr) {
            initializeAssets();
        }
    }
    appendAudioDebugLog(QString("setChartPath chart=%1 track=%2")
                            .arg(preparedAssets_.chartPath, preparedAssets_.trackPath));
    resetBackgroundTrack();
    if (qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0)) {
        initializeBackgroundTrack();
    } else {
        prepareStretchedBackgroundTrack(playbackSession_.backgroundTrackLastTimelineSecond);
    }
}

void QtPreviewSfxRuntime::setBackgroundTrackOffsetSeconds(double seconds)
{
    const double clamped = qIsFinite(seconds) ? seconds : 0.0;
    if (qAbs(playbackSession_.backgroundTrackOffsetSeconds - clamped) <= kQtPreviewSfxEpsilonSeconds) {
        return;
    }
    playbackSession_.backgroundTrackOffsetSeconds = clamped;
    resetBackgroundTrackSessionState();
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
    if (qAbs(playbackSession_.backgroundTrackPlaybackRate - clamped) <= kQtPreviewSfxEpsilonSeconds) {
        return;
    }
    playbackSession_.backgroundTrackPlaybackRate = clamped;
    appendAudioDebugLog(
        QString("setBackgroundTrackPlaybackRate rate=%1").arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3));
    resetBackgroundTrackSessionState(qMax(0.0, playbackSession_.backgroundTrackLastTimelineSecond));
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
    }
    if (qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0)) {
        resetStretchedBackgroundTrack();
        if (backgroundTrackVoice_ == nullptr && !preparedAssets_.trackPath.isEmpty()) {
            initializeBackgroundTrack();
        }
    } else if (!preparedAssets_.trackPath.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
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
            backgroundTrackConfigured_ = false;
        }
        prepareStretchedBackgroundTrack(playbackSession_.backgroundTrackLastTimelineSecond);
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
    rebuildPreparedTimeline(noteMarkers);
}

void QtPreviewSfxRuntime::clearTimeline()
{
    clearPreparedTimeline();
    stopAll();
}

void QtPreviewSfxRuntime::applyPausedPreviewState(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool noteMarkersChanged,
    double pauseSecond)
{
    if (noteMarkersChanged) {
        rebuildPreparedTimeline(noteMarkers);
    }
    resetCursor(qMax(0.0, pauseSecond), false);
    pauseTouchholdVoices();
}

double QtPreviewSfxRuntime::startPreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    const double clampedStartSecond = qMax(0.0, startSecond);
    setBackgroundTrackPlaybackRate(playbackRate);
    startBackgroundTrack(clampedStartSecond);

    double effectiveStartSecond = clampedStartSecond;
    if (hasBackgroundTrack() && isBackgroundTrackRunning()) {
        effectiveStartSecond = qMax(0.0, backgroundPlaybackSecond());
    }

    resetCursor(effectiveStartSecond, !resumeFromPause);
    if (!resumeFromPause) {
        drainEvents(effectiveStartSecond);
    }
    restoreTouchholdVoices(effectiveStartSecond);
    return effectiveStartSecond;
}

void QtPreviewSfxRuntime::resetCursor(double second, bool includeCurrentSecond)
{
    playbackSession_.eventIndex = 0;
    while (playbackSession_.eventIndex < preparedTimeline_.events.size()) {
        const double eventSecond = preparedTimeline_.events[playbackSession_.eventIndex].second;
        const bool beforeStart = includeCurrentSecond
            ? (eventSecond + kQtPreviewSfxEpsilonSeconds < second)
            : (eventSecond <= second + kQtPreviewSfxEpsilonSeconds);
        if (!beforeStart) {
            break;
        }
        ++playbackSession_.eventIndex;
    }
}

void QtPreviewSfxRuntime::drainEvents(double second)
{
    while (playbackSession_.eventIndex < preparedTimeline_.events.size()) {
        const int groupStart = playbackSession_.eventIndex;
        const double groupSecond = preparedTimeline_.events[groupStart].second;
        if (groupSecond > second + kQtPreviewSfxEpsilonSeconds) {
            break;
        }

        int groupEnd = groupStart + 1;
        while (groupEnd < preparedTimeline_.events.size()
               && qAbs(preparedTimeline_.events[groupEnd].second - groupSecond) <= kQtPreviewSfxEpsilonSeconds) {
            ++groupEnd;
        }

        QVector<AggregatedPlayback> playbacks;
        for (int i = groupStart; i < groupEnd; ++i) {
            const Event& event = preparedTimeline_.events[i];
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

        playbackSession_.eventIndex = groupEnd;
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
    for (int spanIndex = 0; spanIndex < preparedTimeline_.touchholdSpans.size(); ++spanIndex) {
        const TouchholdSpan& span = preparedTimeline_.touchholdSpans[spanIndex];
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
    if (qFuzzyCompare(playbackSession_.backgroundTrackPlaybackRate + 1.0, 2.0)) {
        return backgroundTrackConfigured_ && backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized;
    }
    if (!preparedAssets_.trackPath.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
        return true;
    }
    return stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized;
}

bool QtPreviewSfxRuntime::isBackgroundTrackRunning() const
{
    return playbackSession_.backgroundTrackRunning;
}

void QtPreviewSfxRuntime::rebuildPreparedTimeline(const QVector<TimelineNoteMarker>& noteMarkers)
{
    miacode::preview_sfx_timeline::buildTimeline(
        noteMarkers,
        &preparedTimeline_.events,
        &preparedTimeline_.touchholdSpans);
    playbackSession_.eventIndex = 0;
}

void QtPreviewSfxRuntime::clearPreparedTimeline()
{
    preparedTimeline_.events.clear();
    preparedTimeline_.touchholdSpans.clear();
    playbackSession_.eventIndex = 0;
}

void QtPreviewSfxRuntime::resetBackgroundTrackSessionState(double timelineSecond)
{
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    playbackSession_.backgroundTrackLastTimelineSecond = qMax(0.0, timelineSecond);
}
