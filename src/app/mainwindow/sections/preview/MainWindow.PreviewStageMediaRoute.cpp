void MainWindow::applyPreviewStageMediaRouteVisualSettings()
{
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        if (previewMediaController_ != nullptr) {
            dispatchPreviewMediaControllerCall([this](PreviewMediaController* controller) {
                controller->setBackgroundBrightness(previewBackgroundBrightnessOuter_);
            });
        }
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        }
        break;
    }
}

MainWindow::PreviewStageMediaRoute MainWindow::previewStageMediaRoute() const
{
    return frontendHostMode_ == FrontendHostMode::QuickShellBackend
        ? PreviewStageMediaRoute::QuickShellStageHost
        : PreviewStageMediaRoute::WidgetMediaController;
}

bool MainWindow::previewUsesStageMediaHostRoute() const
{
    return previewStageMediaRoute() == PreviewStageMediaRoute::QuickShellStageHost;
}

bool MainWindow::shouldDeferQuickShellStartupStageMediaLoad() const
{
    return previewUsesStageMediaHostRoute() && quickShellStartupStageMediaLoadDeferred_;
}

void MainWindow::noteQuickShellStartupUiReady()
{
    if (!previewUsesStageMediaHostRoute()) {
        return;
    }
    quickShellStartupUiReady_ = true;
    scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::scheduleDeferredQuickShellStartupStageMediaLoadIfReady()
{
    if (!shouldDeferQuickShellStartupStageMediaLoad()
        || !quickShellStartupUiReady_
        || startupRestorePending_
        || deferredQuickShellStartupStageMediaFlushScheduled_) {
        return;
    }

    deferredQuickShellStartupStageMediaFlushScheduled_ = true;
    QTimer::singleShot(0, this, [this]() {
        deferredQuickShellStartupStageMediaFlushScheduled_ = false;
        if (!shouldDeferQuickShellStartupStageMediaLoad()
            || !quickShellStartupUiReady_
            || startupRestorePending_) {
            return;
        }

        quickShellStartupStageMediaLoadDeferred_ = false;
        if (!deferredQuickShellStartupStageMediaPending_ || previewStageMediaHost_ == nullptr) {
            return;
        }

        deferredQuickShellStartupStageMediaPending_ = false;
        previewStageMediaHost_->setChartPath(deferredQuickShellStartupStageMediaChartPath_);
        previewStageMediaHost_->setPlayheadSeconds(deferredQuickShellStartupStageMediaPausedSecond_);
        refreshPreviewStageMediaRouteDebugState(false);
    });
}

void MainWindow::updatePreviewStageMediaPresentationMode(bool requestUpdate)
{
    if (previewCanvas_ == nullptr) {
        return;
    }
    previewCanvas_->setStageMediaPresentationMode(
        previewUsesStageMediaHostRoute()
            ? miacode::preview::scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem
            : miacode::preview::scene::PreviewStageMediaPresentationMode::InternalLayer,
        requestUpdate
    );
}

void MainWindow::ensurePreviewStageMediaRouteInitialized()
{
    if (previewUsesStageMediaHostRoute()) {
        ensurePreviewStageMediaHostInitialized();
        return;
    }
    ensurePreviewMediaControllerInitialized();
}

void MainWindow::syncPreviewStageMediaRouteChartPath(
    const QString& chartPath,
    const QString& trackPath,
    double pausedSecond)
{
    const double clampedPausedSecond = qMax(0.0, pausedSecond);
    updatePreviewStageMediaPresentationMode(false);

    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        ensurePreviewMediaControllerInitialized();
        dispatchPreviewMediaControllerCall([chartPath, trackPath, clampedPausedSecond](PreviewMediaController* controller) {
            controller->setChartPath(chartPath);
            controller->setBackgroundTrackPath(trackPath);
            controller->setPlayheadSeconds(clampedPausedSecond);
        });
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        ensurePreviewStageMediaHostInitialized();
        deferredQuickShellStartupStageMediaChartPath_ = chartPath;
        deferredQuickShellStartupStageMediaPausedSecond_ = clampedPausedSecond;
        deferredQuickShellStartupStageMediaPending_ = true;
        if (!shouldDeferQuickShellStartupStageMediaLoad()) {
            deferredQuickShellStartupStageMediaPending_ = false;
            previewStageMediaHost_->setChartPath(chartPath);
            previewStageMediaHost_->setPlayheadSeconds(clampedPausedSecond);
        }
        break;
    }

    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::clearPreviewStageMediaRoute()
{
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        if (previewMediaController_ != nullptr) {
            dispatchPreviewMediaControllerCall([](PreviewMediaController* controller) {
                controller->setChartPath(QString());
                controller->setBackgroundTrackPath(QString());
                controller->setPlayheadSeconds(0.0);
            });
        }
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        deferredQuickShellStartupStageMediaPending_ = false;
        deferredQuickShellStartupStageMediaChartPath_.clear();
        deferredQuickShellStartupStageMediaPausedSecond_ = 0.0;
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->setChartPath(QString());
        }
        break;
    }

    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::applyPreviewMediaWarmupToStageMediaRoute(
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath)
{
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        ensurePreviewMediaControllerInitialized();
        dispatchPreviewMediaControllerCall([chartPath, resolvedMediaPath, trackPath](PreviewMediaController* controller) {
            controller->setWarmupResolvedMediaPath(chartPath, resolvedMediaPath);
            controller->setBackgroundTrackPath(trackPath);
            controller->setChartPath(chartPath);
        });
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        ensurePreviewStageMediaHostInitialized();
        previewStageMediaHost_->setWarmupResolvedMediaPath(chartPath, resolvedMediaPath);
        deferredQuickShellStartupStageMediaChartPath_ = chartPath;
        deferredQuickShellStartupStageMediaPending_ = true;
        if (!shouldDeferQuickShellStartupStageMediaLoad()) {
            deferredQuickShellStartupStageMediaPending_ = false;
            previewStageMediaHost_->setChartPath(chartPath);
        }
        break;
    }

    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::resetPreviewStageMediaRouteTimelineOffset()
{
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        if (previewMediaController_ != nullptr) {
            dispatchPreviewMediaControllerCall([](PreviewMediaController* controller) {
                controller->setTimelineOffsetSeconds(0.0);
            });
        }
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->setTimelineOffsetSeconds(0.0);
        }
        break;
    }
}

void MainWindow::applyPreviewStageMediaRoutePlaybackRate(double rate)
{
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        if (previewMediaController_ != nullptr) {
            dispatchPreviewMediaControllerCall([this, rate](PreviewMediaController* controller) {
                controller->setPlaybackRate(rate);
                controller->setBackgroundTrackPlaybackRate(rate);
                controller->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
            });
        }
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->setPlaybackRate(rate);
        }
        break;
    }
}

bool MainWindow::previewStageMediaRouteHasVideo() const
{
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        return previewMediaController_ != nullptr && queryPreviewMediaControllerHasVideoMedia();
    case PreviewStageMediaRoute::QuickShellStageHost:
        return previewStageMediaHost_ != nullptr && previewStageMediaHost_->hasVideoMedia();
    }
    return false;
}

double MainWindow::previewStageMediaRouteCurrentPlaybackSecond() const
{
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        return previewMediaController_ != nullptr ? queryPreviewMediaControllerCurrentPlaybackSecond() : 0.0;
    case PreviewStageMediaRoute::QuickShellStageHost:
        return previewStageMediaHost_ != nullptr ? previewStageMediaHost_->currentPlaybackSecond() : 0.0;
    }
    return 0.0;
}

void MainWindow::startPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        dispatchPreviewMediaControllerCall([second](PreviewMediaController* controller) {
            controller->startPlayback(second);
        });
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->startPlayback(second);
        }
        break;
    }
}

void MainWindow::syncPreviewStageMediaRoutePlayback(double second)
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        dispatchPreviewMediaControllerCall([second](PreviewMediaController* controller) {
            controller->syncPlayback(second);
        });
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->syncPlayback(second);
        }
        break;
    }
}

void MainWindow::pausePreviewStageMediaRoutePlayback()
{
    if (!previewStageMediaRouteHasVideo()) {
        return;
    }

    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        dispatchPreviewMediaControllerCall([](PreviewMediaController* controller) {
            controller->pausePlayback();
        });
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->pausePlayback();
        }
        break;
    }
}

void MainWindow::seekPreviewStageMediaRouteWhilePaused(double second)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (!previewStageMediaRouteHasVideo()) {
        pausedPreviewMediaSeekPending_ = false;
        return;
    }

    pausedPreviewMediaSeekPending_ = true;
    pausedPreviewMediaSeekSecond_ = clampedSecond;
    switch (previewStageMediaRoute()) {
    case PreviewStageMediaRoute::WidgetMediaController:
        dispatchPreviewMediaControllerCall([clampedSecond](PreviewMediaController* controller) {
            controller->setPlayheadSeconds(clampedSecond);
        });
        break;
    case PreviewStageMediaRoute::QuickShellStageHost:
        if (previewStageMediaHost_ != nullptr) {
            previewStageMediaHost_->setPlayheadSeconds(clampedSecond);
        }
        break;
    }
}

void MainWindow::setPreviewStageMediaRouteObservedPlayheadSecond(double second)
{
    if (!previewUsesStageMediaHostRoute() || previewStageMediaHost_ == nullptr) {
        return;
    }
    previewStageMediaHost_->setObservedPlayheadSecond(second);
}

void MainWindow::ensurePreviewStageMediaHostInitialized()
{
    if (previewStageMediaHost_ != nullptr) {
        return;
    }

    previewStageMediaHost_ = new PreviewStageMediaHost(this);
    previewStageMediaHost_->setBackgroundScaleMode(previewBackgroundScaleMode_);
    connect(previewStageMediaHost_, &PreviewStageMediaHost::mediaStateChanged, this, [this]() {
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setStageMediaAvailable(previewStageMediaHost_->hasResolvedMedia());
        }
        refreshPreviewStageMediaRouteDebugState(false);
    });
    connect(previewStageMediaHost_, &PreviewStageMediaHost::playbackPositionChanged, this, [this](double second) {
        if (qtPreviewPlaying_) {
            return;
        }
        if (pausedPreviewMediaSeekPending_) {
            if (qAbs(second - pausedPreviewMediaSeekSecond_) > 0.05) {
                return;
            }
            second = pausedPreviewMediaSeekSecond_;
            pausedPreviewMediaSeekPending_ = false;
        }
        qtPreviewStartSecond_ = second;
        qtPreviewElapsed_.restart();
        applyQtPreviewPosition(second, true);
        refreshPreviewStageMediaRouteDebugState(false);
    });
    connect(previewStageMediaHost_, &PreviewStageMediaHost::playbackFinished, this, [this]() {
        finishQtPreviewPlaybackAndReturnToEntry("Qt preview reached the end of current media.");
    });
    connect(previewStageMediaHost_, &PreviewStageMediaHost::diagnosticsChanged, this, [this]() {
        refreshPreviewStageMediaRouteDebugState(!qtPreviewPlaying_);
    });
    previewStageMediaHost_->setWarmupResolvedMediaPath(previewMediaWarmupChartPath_, previewMediaWarmupResolvedPath_);
    deferredQuickShellStartupStageMediaChartPath_ = currentFilePath_;
    deferredQuickShellStartupStageMediaPausedSecond_ = qMax(0.0, qtPreviewPauseSecond_);
    deferredQuickShellStartupStageMediaPending_ = !currentFilePath_.isEmpty();
    if (!shouldDeferQuickShellStartupStageMediaLoad()) {
        deferredQuickShellStartupStageMediaPending_ = false;
        previewStageMediaHost_->setChartPath(currentFilePath_);
        previewStageMediaHost_->setPlayheadSeconds(qtPreviewPauseSecond_);
    }
    refreshPreviewStageMediaRouteDebugState(false);
}

void MainWindow::shutdownPreviewStageMediaHost()
{
    if (previewStageMediaHost_ == nullptr) {
        return;
    }
    delete previewStageMediaHost_;
    previewStageMediaHost_ = nullptr;
}

void MainWindow::refreshPreviewStageMediaRouteDebugState(bool requestUpdate)
{
    if (previewCanvas_ == nullptr) {
        return;
    }
    miacode::preview::scene::PreviewExternalStageMediaType mediaType =
        miacode::preview::scene::PreviewExternalStageMediaType::None;
    bool videoPlaybackActive = false;
    double playbackSecond = 0.0;
    double clockDeltaSeconds = 0.0;
    qint64 videoFrameAgeMs = -1;
    if (previewUsesStageMediaHostRoute() && previewStageMediaHost_ != nullptr) {
        if (previewStageMediaHost_->hasVideoMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Video;
        } else if (previewStageMediaHost_->hasResolvedMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Image;
        }
        videoPlaybackActive = previewStageMediaHost_->videoPlaybackActive();
        playbackSecond = previewStageMediaHost_->currentPlaybackSecond();
        clockDeltaSeconds = previewStageMediaHost_->clockDeltaSeconds();
        videoFrameAgeMs = previewStageMediaHost_->videoFrameAgeMs();
    }
    previewCanvas_->setExternalStageMediaDebugState(
        mediaType,
        videoPlaybackActive,
        playbackSecond,
        clockDeltaSeconds,
        videoFrameAgeMs,
        requestUpdate
    );
}
