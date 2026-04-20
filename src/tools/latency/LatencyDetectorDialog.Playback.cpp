void LatencyDetectorDialog::seekToSecond(double second, bool centerView)
{
    pendingPausedSeekActive_ = false;
    if (pausedSeekTimer_ != nullptr) {
        pausedSeekTimer_->stop();
    }
    playheadSecond_ = qBound(0.0, second, trackDurationSeconds_);
    transportAnchorSecond_ = playheadSecond_;
    if (playing_) {
        transportElapsed_.restart();
    } else {
        transportElapsed_.invalidate();
    }
    if (sfxRuntime_ != nullptr) {
        if (playing_) {
            sfxRuntime_->startBackgroundTrack(playheadSecond_);
        } else {
            sfxRuntime_->pauseBackgroundTrack();
            sfxRuntime_->seekBackgroundTrack(playheadSecond_);
            sfxRuntime_->pauseBackgroundTrack();
        }
    }
    lastBeatAuditionSecond_ = playheadSecond_;
    updateVisibleRange(centerView);
}

double LatencyDetectorDialog::currentTransportSecond() const
{
    if (!transportElapsed_.isValid()) {
        return qBound(0.0, transportAnchorSecond_, trackDurationSeconds_);
    }
    const double elapsedSeconds = static_cast<double>(transportElapsed_.elapsed()) / 1000.0;
    return qBound(0.0, transportAnchorSecond_ + (elapsedSeconds * playbackRate_), trackDurationSeconds_);
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
    pendingPausedSeekActive_ = false;
    if (pausedSeekTimer_ != nullptr) {
        pausedSeekTimer_->stop();
    }
    if (offsetReplayTimer_ != nullptr) {
        offsetReplayTimer_->stop();
    }
    if (trackDurationSeconds_ > 0.0 && playheadSecond_ >= trackDurationSeconds_ - 0.02) {
        playheadSecond_ = 0.0;
    }
    transportAnchorSecond_ = playheadSecond_;
    transportElapsed_.restart();
    sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
    sfxRuntime_->startBackgroundTrack(playheadSecond_);
    playing_ = true;
    lastBeatAuditionSecond_ = playheadSecond_;
    playbackTimer_->start();
    updateVisibleRange(true);
}

void LatencyDetectorDialog::pausePlayback()
{
    if (playing_) {
        playheadSecond_ = currentTransportSecond();
    }
    playing_ = false;
    transportAnchorSecond_ = playheadSecond_;
    transportElapsed_.invalidate();
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
    playheadSecond_ = currentTransportSecond();
    sfxRuntime_->syncBackgroundTrack(playheadSecond_);
    if (pendingBeatBpm_ > 0.0) {
        triggerBeatAudition(previousSecond, playheadSecond_);
    }
    if (playheadSecond_ >= trackDurationSeconds_ - 0.02) {
        pausePlayback();
        playheadSecond_ = trackDurationSeconds_;
        updateVisibleRange(true);
        return;
    }
    updateVisibleRange(true);
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

