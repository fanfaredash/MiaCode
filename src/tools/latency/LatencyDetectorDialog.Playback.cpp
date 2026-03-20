void LatencyDetectorDialog::seekToSecond(double second, bool centerView)
{
    playheadSecond_ = qBound(0.0, second, trackDurationSeconds_);
    if (sfxRuntime_ != nullptr) {
        if (playing_) {
            sfxRuntime_->startBackgroundTrack(playheadSecond_);
            playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
        } else {
            sfxRuntime_->pauseBackgroundTrack();
            sfxRuntime_->startBackgroundTrack(playheadSecond_);
            sfxRuntime_->pauseBackgroundTrack();
            playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
        }
    }
    smoothFollowPlayhead(centerView);
    lastBeatAuditionSecond_ = playheadSecond_;
    updateVisibleRange(false);
}

void LatencyDetectorDialog::togglePlayback()
{
    if (playing_) {
        pausePlayback();
    } else {
        startPlayback();
    }
}

void LatencyDetectorDialog::startPlayback()
{
    if (sfxRuntime_ == nullptr) {
        return;
    }
    sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
    sfxRuntime_->startBackgroundTrack(playheadSecond_);
    playing_ = true;
    playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
    lastBeatAuditionSecond_ = playheadSecond_;
    playbackTimer_->start();
    updatePlaybackUi();
}

void LatencyDetectorDialog::pausePlayback()
{
    playing_ = false;
    if (sfxRuntime_ != nullptr) {
        sfxRuntime_->pauseBackgroundTrack();
    }
    if (playbackTimer_ != nullptr) {
        playbackTimer_->stop();
    }
    updatePlaybackUi();
}

void LatencyDetectorDialog::restartPlaybackAfterOffsetChange()
{
    updateBeatOverlay();
    if (!playing_) {
        return;
    }
    pausePlayback();
    offsetReplayTimer_->start(static_cast<int>(kOffsetReplayDelayMs));
}

void LatencyDetectorDialog::onPlaybackTick()
{
    if (sfxRuntime_ == nullptr) {
        return;
    }
    const double previousSecond = playheadSecond_;
    playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
    if (pendingBeatBpm_ > 0.0) {
        triggerBeatAudition(previousSecond, playheadSecond_);
    }
    if (playheadSecond_ >= trackDurationSeconds_ - 0.02) {
        pausePlayback();
        playheadSecond_ = trackDurationSeconds_;
    }
    smoothFollowPlayhead(false);
    updateVisibleRange(false);
}

void LatencyDetectorDialog::triggerBeatAudition(double fromSecond, double toSecond)
{
    if (sfxRuntime_ == nullptr || pendingBeatBpm_ <= 0.0 || toSecond + 1e-6 < fromSecond) {
        return;
    }
    const double beatPeriod = 60.0 / pendingBeatBpm_;
    if (beatPeriod <= 0.0) {
        return;
    }

    const double auditionPeriod = pendingBeatAuditionPeriodSeconds_ > 1e-6
        ? pendingBeatAuditionPeriodSeconds_
        : beatPeriod;
    if (auditionPeriod <= 0.0) {
        return;
    }

    const double playbackOffsetSeconds = parsedOffset();
    const double epsilon = 1e-5;
    qint64 stepIndex = static_cast<qint64>(qFloor((fromSecond - playbackOffsetSeconds) / auditionPeriod));
    if (playbackOffsetSeconds + static_cast<double>(stepIndex) * auditionPeriod < fromSecond - epsilon) {
        ++stepIndex;
    }

    for (;; ++stepIndex) {
        const double eventSecond = playbackOffsetSeconds + static_cast<double>(stepIndex) * auditionPeriod;
        if (eventSecond > toSecond + epsilon) {
            break;
        }
        if (eventSecond <= lastBeatAuditionSecond_ + epsilon) {
            continue;
        }

        double gain = 1.0;
        if (!pendingBeatForceUniformGain_ && !pendingBeatUseUniformAccent_ && !pendingBeatAccentWeights_.isEmpty()) {
            const int accentCount = pendingBeatAccentWeights_.size();
            const int beatIndex = static_cast<int>(qRound64((eventSecond - playbackOffsetSeconds) / beatPeriod));
            int accentIndex = (beatIndex - pendingBeatAccentAnchorIndex_) % accentCount;
            if (accentIndex < 0) {
                accentIndex += accentCount;
            }
            gain = pendingBeatAccentWeights_.at(accentIndex);
        }
        gain = qBound(0.0, (0.72 + 0.28 * gain) * beatSfxVolume_, 4.0);
        sfxRuntime_->audition("answer", gain);
        lastBeatAuditionSecond_ = eventSecond;
    }
}

