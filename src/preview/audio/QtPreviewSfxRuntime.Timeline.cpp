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
    retainedPlayback_.mode = RetainedPlaybackMode::None;
    retainedPlayback_.bgmState = RetainedBgmState::NoneLoaded;
    retainedPlayback_.second = 0.0;
    resetBanks();
    preparedAssets_.sfxDir = resolveSfxDir();
    if (!initializeAudioEngine()) {
        return;
    }
    initializeAssets();
    prepareStretchedBackgroundTrack(playbackSession_.backgroundTrackLastTimelineSecond);
    retainedPlayback_.bgmState = hasBackgroundTrack() ? RetainedBgmState::LoadedUsable : RetainedBgmState::NoneLoaded;
}

bool QtPreviewSfxRuntime::audioEngineInitialized() const
{
    return engineInitialized_ && engineState_ != nullptr;
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
    prepareStretchedBackgroundTrack(playbackSession_.backgroundTrackLastTimelineSecond);
    retainedPlayback_.mode = RetainedPlaybackMode::Invalidated;
    retainedPlayback_.bgmState = hasBackgroundTrack() ? RetainedBgmState::LoadedUsable : RetainedBgmState::NoneLoaded;
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
    if (!preparedAssets_.trackPath.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
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

void QtPreviewSfxRuntime::setPlaybackTransactionId(quint64 transactionId)
{
    playbackTransactionId_ = transactionId;
}

double QtPreviewSfxRuntime::preparePreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    appendAudioDebugLog(
        QString("preview_prepare txn=%1 requested=%2 resume=%3 playback_rate=%4 has_track=%5 events=%6 touchhold_spans=%7")
            .arg(playbackTransactionId_)
            .arg(startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(playbackRate, 0, 'f', 3)
            .arg(hasBackgroundTrack() ? 1 : 0)
            .arg(preparedTimeline_.events.size())
            .arg(preparedTimeline_.touchholdSpans.size()));
    setBackgroundTrackPlaybackRate(playbackRate);
    if (hasBackgroundTrack()) {
        seekBackgroundTrack(startSecond);
    }
    resetCursor(startSecond, !resumeFromPause);
    pauseTouchholdVoices();
    preparedPlayback_.pending = true;
    preparedPlayback_.resumeFromPause = resumeFromPause;
    preparedPlayback_.startSecond = startSecond;

    QString nextEventDescription = QStringLiteral("none");
    if (playbackSession_.eventIndex >= 0 && playbackSession_.eventIndex < preparedTimeline_.events.size()) {
        const auto& nextEvent = preparedTimeline_.events[playbackSession_.eventIndex];
        nextEventDescription = QStringLiteral("%1@%2").arg(nextEvent.kind).arg(nextEvent.second, 0, 'f', 6);
    }
    appendAudioDebugLog(
        QString("preview_prepare_ready txn=%1 effective=%2 event_index=%3 next_event=%4")
            .arg(playbackTransactionId_)
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(playbackSession_.eventIndex)
            .arg(nextEventDescription));
    return preparedPlayback_.startSecond;
}

void QtPreviewSfxRuntime::commitPreparedPreviewPlayback()
{
    if (!preparedPlayback_.pending) {
        return;
    }

    const double startSecond = preparedPlayback_.startSecond;
    const bool resumeFromPause = preparedPlayback_.resumeFromPause;
    if (hasBackgroundTrack()) {
        startBackgroundTrack(startSecond);
    }
    if (!resumeFromPause) {
        drainEvents(startSecond);
    }
    restoreTouchholdVoices(startSecond);
    appendAudioDebugLog(
        QString("preview_commit txn=%1 effective=%2 resume=%3 background_running=%4 pending=%5")
            .arg(playbackTransactionId_)
            .arg(startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackRunning ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPendingStart ? 1 : 0));
    preparedPlayback_ = PreparedPlaybackState();
}

void QtPreviewSfxRuntime::cancelPreparedPreviewPlayback()
{
    if (!preparedPlayback_.pending) {
        return;
    }
    pauseTouchholdVoices();
    pauseBackgroundTrack();
    appendAudioDebugLog(
        QString("preview_prepare_cancel txn=%1 effective=%2")
            .arg(playbackTransactionId_)
            .arg(preparedPlayback_.startSecond, 0, 'f', 6));
    preparedPlayback_ = PreparedPlaybackState();
}

double QtPreviewSfxRuntime::preparedStartSecond() const
{
    return preparedPlayback_.pending ? preparedPlayback_.startSecond : playbackSession_.backgroundTrackLastTimelineSecond;
}

void QtPreviewSfxRuntime::configureTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
}

void QtPreviewSfxRuntime::clearTimeline()
{
    clearPreparedTimeline();
    stopAll();
}

void QtPreviewSfxRuntime::applyPausedPreviewState(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool noteMarkersChanged,
    double pauseSecond,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    preparedPlayback_ = PreparedPlaybackState();
    if (noteMarkersChanged
        || (preparedTimeline_.sourceNoteMarkers.isEmpty() && !noteMarkers.isEmpty())
        || qAbs(preparedTimelinePlaybackRate_ - playbackRate) > kQtPreviewSfxEpsilonSeconds
        || !previewTimingSettingsEqual(timingSettings_, timingSettings)) {
        rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
    }
    resetCursor(pauseSecond, false);
    pauseTouchholdVoices();
}

double QtPreviewSfxRuntime::startPreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    const double effectiveStartSecond = preparePreviewPlaybackTransaction(startSecond, resumeFromPause, playbackRate);
    commitPreparedPreviewPlayback();
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

        const int groupEnd =
            miacode::preview_sfx_timeline::eventGroupEndIndex(preparedTimeline_.events, groupStart);
        const miacode::preview_sfx_timeline::CollapsedEventGroup group =
            miacode::preview_sfx_timeline::collapseEventGroup(
                preparedTimeline_.events,
                groupStart,
                groupEnd,
                settings_.breakSlideTailCheerMuted);

        for (const Event& event : group.orderedEvents) {
            if (event.kind == "touchhold_start") {
                startTouchholdSpan(event.spanIndex, 0.0);
                continue;
            }
            if (event.kind == "touchhold_stop") {
                stopTouchholdSpan(event.spanIndex);
                continue;
            }
            playKindInternal(event.kind, event.gain);
        }
        for (const miacode::preview_sfx_timeline::AggregatedPlayback& playback : group.aggregatedPlaybacks) {
            playKindInternal(playback.kind, miacode::preview_sfx_timeline::aggregatedPlaybackGain(playback));
        }

        playbackSession_.eventIndex = groupEnd;
    }
}

void QtPreviewSfxRuntime::pauseTouchholdVoices()
{
    if (touchholdVoice_ != nullptr && touchholdVoice_->initialized) {
        ma_sound_stop(&touchholdVoice_->sound);
    }
    activeTouchholdSpanIndices_.clear();
}

void QtPreviewSfxRuntime::restoreTouchholdVoices(double second)
{
    pauseTouchholdVoices();
    if (touchholdVoice_ == nullptr || !touchholdVoice_->initialized) {
        return;
    }

    int startSpanIndex = -1;
    double startOffsetSeconds = 0.0;
    for (int spanIndex = 0; spanIndex < preparedTimeline_.touchholdSpans.size(); ++spanIndex) {
        const TouchholdSpan& span = preparedTimeline_.touchholdSpans[spanIndex];
        if (second + kQtPreviewSfxEpsilonSeconds < span.startSecond) {
            continue;
        }
        if (second >= span.endSecond - kQtPreviewSfxEpsilonSeconds) {
            continue;
        }
        activeTouchholdSpanIndices_.append(spanIndex);
        if (startSpanIndex < 0 || span.startSecond < preparedTimeline_.touchholdSpans[startSpanIndex].startSecond) {
            startSpanIndex = spanIndex;
            startOffsetSeconds = second - span.startSecond;
        }
    }

    if (startSpanIndex < 0) {
        activeTouchholdSpanIndices_.clear();
        return;
    }

    const ma_uint64 offsetFrames = static_cast<ma_uint64>(qMax(0.0, startOffsetSeconds) * deviceSampleRate_);
    if (touchholdSoundLengthFrames_ > 0 && offsetFrames >= touchholdSoundLengthFrames_) {
        return;
    }

    ma_sound_stop(&touchholdVoice_->sound);
    ma_sound_seek_to_pcm_frame(&touchholdVoice_->sound, offsetFrames);
    ma_sound_start(&touchholdVoice_->sound);
}

bool QtPreviewSfxRuntime::hasBackgroundTrack() const
{
    if (!preparedAssets_.trackPath.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
        return true;
    }
    return stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized;
}

bool QtPreviewSfxRuntime::isBackgroundTrackRunning() const
{
    return playbackSession_.backgroundTrackRunning;
}

void QtPreviewSfxRuntime::rebuildPreparedTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    timingSettings_ = timingSettings;
    timingSettings_.normalize();
    preparedTimelinePlaybackRate_ = qIsFinite(playbackRate) && playbackRate > 0.0 ? playbackRate : 1.0;
    preparedTimeline_.sourceNoteMarkers = noteMarkers;
    miacode::preview_sfx_timeline::buildTimeline(
        noteMarkers,
        preparedTimelinePlaybackRate_,
        timingSettings_,
        &preparedTimeline_.events,
        &preparedTimeline_.touchholdSpans);
    playbackSession_.eventIndex = 0;
}

void QtPreviewSfxRuntime::clearPreparedTimeline()
{
    preparedTimeline_.events.clear();
    preparedTimeline_.touchholdSpans.clear();
    preparedTimeline_.sourceNoteMarkers.clear();
    playbackSession_.eventIndex = 0;
    preparedPlayback_ = PreparedPlaybackState();
}

void QtPreviewSfxRuntime::resetBackgroundTrackSessionState(double timelineSecond)
{
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    playbackSession_.backgroundTrackLastTimelineSecond = timelineSecond;
    clearBackgroundTrackClockAnchor();
}
