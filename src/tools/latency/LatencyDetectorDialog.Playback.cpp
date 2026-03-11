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

    const double epsilon = 1e-5;
    const int beatIndex = static_cast<int>(qFloor((toSecond - pendingBeatOffset_) / beatPeriod));
    const double beatSecond = pendingBeatOffset_ + beatIndex * beatPeriod;
    if (beatSecond < fromSecond - epsilon || beatSecond > toSecond + epsilon) {
        return;
    }
    if (beatSecond <= lastBeatAuditionSecond_ + epsilon) {
        return;
    }

    double gain = 1.0;
    if (!pendingBeatUseUniformAccent_ && !pendingBeatAccentWeights_.isEmpty()) {
        const int accentCount = pendingBeatAccentWeights_.size();
        int accentIndex = (beatIndex - pendingBeatAccentAnchorIndex_) % accentCount;
        if (accentIndex < 0) {
            accentIndex += accentCount;
        }
        gain = pendingBeatAccentWeights_.at(accentIndex);
    }
    gain = qBound(0.0, (0.72 + 0.28 * gain) * beatSfxVolume_, 4.0);
    sfxRuntime_->audition("answer", gain);
    lastBeatAuditionSecond_ = beatSecond;
}

