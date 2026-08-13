MiniaudioPreviewAudioBackend::MiniaudioPreviewAudioBackend()
{
    appendAudioDebugLog("MiniaudioPreviewAudioBackend created");
}

MiniaudioPreviewAudioBackend::~MiniaudioPreviewAudioBackend()
{
    appendAudioDebugLog("MiniaudioPreviewAudioBackend destroying");
    resetBanks();
}

void MiniaudioPreviewAudioBackend::setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir)
{
    warmupPaths_.chartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    warmupPaths_.trackPath = trackPath.isEmpty() ? QString() : QDir::cleanPath(trackPath);
    warmupPaths_.sfxDir = sfxDir.isEmpty() ? QString() : QDir::cleanPath(sfxDir);
}

void MiniaudioPreviewAudioBackend::reloadAssets(const PreviewAudioSettings& settings)
{
    lastNativeErrorCode_ = 0;
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

bool MiniaudioPreviewAudioBackend::audioEngineInitialized() const
{
    return engineInitialized_ && engineState_ != nullptr;
}

void MiniaudioPreviewAudioBackend::setChartPath(const QString& chartPath)
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

void MiniaudioPreviewAudioBackend::setBackgroundTrackOffsetSeconds(double seconds)
{
    const double clamped = qIsFinite(seconds) ? seconds : 0.0;
    if (qAbs(playbackSession_.backgroundTrackOffsetSeconds - clamped) <= kMiniaudioPreviewSfxEpsilonSeconds) {
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

void MiniaudioPreviewAudioBackend::setBackgroundTrackPlaybackRate(double rate)
{
    const double clamped = qBound(0.25, qIsFinite(rate) ? rate : 1.0, 2.0);
    if (qAbs(playbackSession_.backgroundTrackPlaybackRate - clamped) <= kMiniaudioPreviewSfxEpsilonSeconds) {
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

void MiniaudioPreviewAudioBackend::applyLevels(const PreviewAudioSettings& settings)
{
    const bool rebuildMineSfx = settings_.mineSfxEnabled != settings.mineSfxEnabled;
    const double cursorSecond = playbackSession_.backgroundTrackLastTimelineSecond;
    settings_ = settings;
    settings_.normalize();
    if (rebuildMineSfx && !preparedTimeline_.sourceNoteMarkers.isEmpty()) {
        rebuildPreparedTimeline(
            preparedTimeline_.sourceNoteMarkers,
            preparedTimelinePlaybackRate_,
            timingSettings_);
        resetCursor(cursorSecond, false);
        pauseTouchholdVoices();
        if (playbackSession_.backgroundTrackRunning) {
            restoreTouchholdVoices(cursorSecond);
        }
    }
    applyVolumes();
}

void MiniaudioPreviewAudioBackend::setPlaybackTransactionId(quint64 transactionId)
{
    playbackTransactionId_ = transactionId;
}

double MiniaudioPreviewAudioBackend::preparePreviewPlaybackTransaction(
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

void MiniaudioPreviewAudioBackend::commitPreparedPreviewPlayback()
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

void MiniaudioPreviewAudioBackend::cancelPreparedPreviewPlayback()
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

double MiniaudioPreviewAudioBackend::preparedStartSecond() const
{
    return preparedPlayback_.pending ? preparedPlayback_.startSecond : playbackSession_.backgroundTrackLastTimelineSecond;
}

void MiniaudioPreviewAudioBackend::configureTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
}

void MiniaudioPreviewAudioBackend::clearTimeline()
{
    clearPreparedTimeline();
    stopAll();
}

void MiniaudioPreviewAudioBackend::applyPausedPreviewState(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool noteMarkersChanged,
    double pauseSecond,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    preparedPlayback_ = PreparedPlaybackState();
    if (noteMarkersChanged
        || (preparedTimeline_.sourceNoteMarkers.isEmpty() && !noteMarkers.isEmpty())
        || qAbs(preparedTimelinePlaybackRate_ - playbackRate) > kMiniaudioPreviewSfxEpsilonSeconds
        || !previewTimingSettingsEqual(timingSettings_, timingSettings)) {
        rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
    }
    resetCursor(pauseSecond, false);
    pauseTouchholdVoices();
}

double MiniaudioPreviewAudioBackend::startPreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    const double effectiveStartSecond = preparePreviewPlaybackTransaction(startSecond, resumeFromPause, playbackRate);
    commitPreparedPreviewPlayback();
    return effectiveStartSecond;
}

void MiniaudioPreviewAudioBackend::resetCursor(double second, bool includeCurrentSecond)
{
    playbackSession_.eventIndex = 0;
    while (playbackSession_.eventIndex < preparedTimeline_.events.size()) {
        const double eventSecond = preparedTimeline_.events[playbackSession_.eventIndex].second;
        const bool beforeStart = includeCurrentSecond
            ? (eventSecond + kMiniaudioPreviewSfxEpsilonSeconds < second)
            : (eventSecond <= second + kMiniaudioPreviewSfxEpsilonSeconds);
        if (!beforeStart) {
            break;
        }
        ++playbackSession_.eventIndex;
    }
}

void MiniaudioPreviewAudioBackend::drainEvents(double second)
{
    while (playbackSession_.eventIndex < preparedTimeline_.events.size()) {
        const int groupStart = playbackSession_.eventIndex;
        const double groupSecond = preparedTimeline_.events[groupStart].second;
        if (groupSecond > second + kMiniaudioPreviewSfxEpsilonSeconds) {
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
            if (event.kind == "touchhold_start" || event.kind == "touchhold_stop") {
                // Latest-wins ownership of the single shared touch-hold voice; see
                // reconcileTouchholdVoice. Order-independent, so a seamless join or
                // overlap lets the newer span take over instead of the older one
                // clobbering it.
                reconcileTouchholdVoice(event.second);
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

void MiniaudioPreviewAudioBackend::reconcileTouchholdVoice(double second)
{
    if (touchholdVoice_ == nullptr || !touchholdVoice_->initialized) {
        return;
    }
    const int owner = miacode::preview_sfx_timeline::touchholdOwnerSpanIndexAt(
        preparedTimeline_.touchholdSpans, second);
    if (owner == touchholdOwnerSpanIndex_) {
        return;  // voice already belongs to the right span — leave it playing
    }
    touchholdOwnerSpanIndex_ = owner;
    if (owner < 0) {
        ma_sound_stop(&touchholdVoice_->sound);
        return;
    }
    const TouchholdSpan& span = preparedTimeline_.touchholdSpans[owner];
    const ma_uint64 offsetFrames =
        static_cast<ma_uint64>(qMax(0.0, second - span.startSecond) * deviceSampleRate_);
    if (touchholdSoundLengthFrames_ > 0 && offsetFrames >= touchholdSoundLengthFrames_) {
        ma_sound_stop(&touchholdVoice_->sound);
        return;
    }
    ma_sound_stop(&touchholdVoice_->sound);
    ma_sound_seek_to_pcm_frame(&touchholdVoice_->sound, offsetFrames);
    ma_sound_start(&touchholdVoice_->sound);
}

void MiniaudioPreviewAudioBackend::pauseTouchholdVoices()
{
    if (touchholdVoice_ != nullptr && touchholdVoice_->initialized) {
        ma_sound_stop(&touchholdVoice_->sound);
    }
    touchholdOwnerSpanIndex_ = -1;
}

void MiniaudioPreviewAudioBackend::restoreTouchholdVoices(double second)
{
    pauseTouchholdVoices();
    reconcileTouchholdVoice(second);
}

bool MiniaudioPreviewAudioBackend::hasBackgroundTrack() const
{
    if (!preparedAssets_.trackPath.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
        return true;
    }
    return stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized;
}

bool MiniaudioPreviewAudioBackend::isBackgroundTrackRunning() const
{
    return playbackSession_.backgroundTrackRunning;
}

void MiniaudioPreviewAudioBackend::rebuildPreparedTimeline(
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
        &preparedTimeline_.touchholdSpans,
        settings_.mineSfxEnabled);
    playbackSession_.eventIndex = 0;
}

void MiniaudioPreviewAudioBackend::clearPreparedTimeline()
{
    preparedTimeline_.events.clear();
    preparedTimeline_.touchholdSpans.clear();
    preparedTimeline_.sourceNoteMarkers.clear();
    playbackSession_.eventIndex = 0;
    preparedPlayback_ = PreparedPlaybackState();
}

void MiniaudioPreviewAudioBackend::resetBackgroundTrackSessionState(double timelineSecond)
{
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackRunning = false;
    playbackSession_.backgroundTrackLastTimelineSecond = timelineSecond;
    clearBackgroundTrackClockAnchor();
}
