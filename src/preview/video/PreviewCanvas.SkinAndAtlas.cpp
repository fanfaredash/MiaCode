void PreviewCanvas::setSkinDirectory(const QString& skinDir)
{
    const quint64 generation = ++skinLoadGeneration_;
    lastSkinLoadDispatchMs_ = QDateTime::currentMSecsSinceEpoch();
    appendPreviewStartupTiming("preview_canvas/skin_load_dispatch", -1);
    QPointer<PreviewCanvas> guard(this);
    QThreadPool::globalInstance()->start([guard, skinDir, generation]() {
        QElapsedTimer workerTimer;
        workerTimer.start();
        SkinLoadResult result = loadSkinAssets(skinDir, generation);
        const qint64 workerElapsedMs = workerTimer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result), workerElapsedMs]() mutable {
                if (guard.isNull()) {
                    return;
                }
                appendPreviewStartupTiming("preview_canvas/skin_load_worker_done", workerElapsedMs);
                guard->applySkinLoadResult(std::move(result));
            },
            Qt::QueuedConnection
        );
    }, -1);
}

void PreviewCanvas::applySkinLoadResult(SkinLoadResult&& result)
{
    if (result.generation != skinLoadGeneration_) {
        return;
    }

    tapImage_ = std::move(result.tapImage);
    tapEachImage_ = std::move(result.tapEachImage);
    tapBreakImage_ = std::move(result.tapBreakImage);
    tapExImage_ = std::move(result.tapExImage);
    slideTrackImage_ = std::move(result.slideTrackImage);
    slideTrackEachImage_ = std::move(result.slideTrackEachImage);
    slideTrackBreakImage_ = std::move(result.slideTrackBreakImage);
    starImage_ = std::move(result.starImage);
    starEachImage_ = std::move(result.starEachImage);
    starBreakImage_ = std::move(result.starBreakImage);
    starBreakDoubleImage_ = std::move(result.starBreakDoubleImage);
    starDoubleImage_ = std::move(result.starDoubleImage);
    starEachDoubleImage_ = std::move(result.starEachDoubleImage);
    starExImage_ = std::move(result.starExImage);
    starExDoubleImage_ = std::move(result.starExDoubleImage);
    wifiImages_ = std::move(result.wifiImages);
    wifiEachImages_ = std::move(result.wifiEachImages);
    wifiBreakImages_ = std::move(result.wifiBreakImages);
    holdImage_ = std::move(result.holdImage);
    holdEachImage_ = std::move(result.holdEachImage);
    holdBreakImage_ = std::move(result.holdBreakImage);
    holdExImage_ = std::move(result.holdExImage);
    noteGuideNormalImage_ = std::move(result.noteGuideNormalImage);
    noteGuideBreakImage_ = std::move(result.noteGuideBreakImage);
    noteGuideEachImage_ = std::move(result.noteGuideEachImage);
    noteGuideEachLine1Image_ = std::move(result.noteGuideEachLine1Image);
    noteGuideEachLine2Image_ = std::move(result.noteGuideEachLine2Image);
    noteGuideEachLine3Image_ = std::move(result.noteGuideEachLine3Image);
    noteGuideEachLine4Image_ = std::move(result.noteGuideEachLine4Image);
    noteGuideHoldEndImage_ = std::move(result.noteGuideHoldEndImage);
    noteGuideHoldEachEndImage_ = std::move(result.noteGuideHoldEachEndImage);
    noteGuideHoldBreakEndImage_ = std::move(result.noteGuideHoldBreakEndImage);
    noteGuideSlideImage_ = std::move(result.noteGuideSlideImage);
    touchCornerImage_ = std::move(result.touchCornerImage);
    touchCornerEachImage_ = std::move(result.touchCornerEachImage);
    touchCornerBreakImage_ = std::move(result.touchCornerBreakImage);
    touchBorder2Image_ = std::move(result.touchBorder2Image);
    touchBorder2EachImage_ = std::move(result.touchBorder2EachImage);
    touchBorder2BreakImage_ = std::move(result.touchBorder2BreakImage);
    touchBorder3Image_ = std::move(result.touchBorder3Image);
    touchBorder3EachImage_ = std::move(result.touchBorder3EachImage);
    touchBorder3BreakImage_ = std::move(result.touchBorder3BreakImage);
    touchPointImage_ = std::move(result.touchPointImage);
    touchPointEachImage_ = std::move(result.touchPointEachImage);
    touchPointBreakImage_ = std::move(result.touchPointBreakImage);
    touchHold0Image_ = std::move(result.touchHold0Image);
    touchHold1Image_ = std::move(result.touchHold1Image);
    touchHold2Image_ = std::move(result.touchHold2Image);
    touchHold3Image_ = std::move(result.touchHold3Image);
    touchHoldBorderImage_ = std::move(result.touchHoldBorderImage);
    if (!result.judgeEffectTapImage.isNull()) {
        judgeEffectTapImage_ = std::move(result.judgeEffectTapImage);
    } else if (judgeEffectTapImage_.isNull()) {
        judgeEffectTapImage_ = buildJudgeEffectTapFallbackImage();
    }
    judgeEffectTapSourceRect_ = nonTransparentBounds(judgeEffectTapImage_);

    if (!result.judgeEffectTapBreakImage.isNull()) {
        judgeEffectTapBreakImage_ = std::move(result.judgeEffectTapBreakImage);
    } else if (judgeEffectTapBreakImage_.isNull()) {
        judgeEffectTapBreakImage_ = buildJudgeEffectTapBreakFallbackImage();
    }
    judgeEffectTapBreakSourceRect_ = nonTransparentBounds(judgeEffectTapBreakImage_);
    judgeEffectHoldSustainCircleImage_ = alphaTightenedSpriteImage(
        result.judgeEffectHoldSustainCircleImage,
        kJudgeEffectHoldSustainAlphaTightenGamma
    );

    judgeEffectTouchCircleImage_ = result.judgeEffectTouchCircleImage.isNull()
        ? QImage()
        : tintedSpriteImage(result.judgeEffectTouchCircleImage, kJudgeEffectTouchCircleTint);
    judgeEffectTouchPart01Image_ = result.judgeEffectTouchPart01Image.isNull()
        ? QImage()
        : tintedSpriteImage(result.judgeEffectTouchPart01Image, kJudgeEffectTouchPartTint);
    judgeEffectTouchPart02Image_ = result.judgeEffectTouchPart02Image.isNull()
        ? QImage()
        : tintedSpriteImage(result.judgeEffectTouchPart02Image, kJudgeEffectTouchPartTint);

    if (!result.judgeEffectFireworkImage.isNull()) {
        judgeEffectFireworkImage_ = std::move(result.judgeEffectFireworkImage);
    } else if (judgeEffectFireworkImage_.isNull()) {
        judgeEffectFireworkImage_ = buildJudgeEffectFireworkFallbackImage();
    }
    judgeEffectFireworkSourceRect_ = nonTransparentBounds(judgeEffectFireworkImage_);
    if (!result.judgeEffectFireworkColorBallImage.isNull()) {
        judgeEffectFireworkColorBallImage_ = std::move(result.judgeEffectFireworkColorBallImage);
    } else if (judgeEffectFireworkColorBallImage_.isNull()) {
        judgeEffectFireworkColorBallImage_ = buildJudgeEffectFireworkColorBallFallbackImage();
    }
    judgeEffectFireworkColorBallSourceRect_ = nonTransparentBounds(judgeEffectFireworkColorBallImage_);

    tapAtlasImage_ = std::move(result.tapAtlasImage);
    trackAtlasImage_ = std::move(result.trackAtlasImage);
    touchAtlasImage_ = std::move(result.touchAtlasImage);
    guideAtlasImage_ = std::move(result.guideAtlasImage);

    atlasRegions_.clear();
    const auto appendAtlasRegions =
        [this](const QHash<quint64, QRect>& regions, const QImage* atlasImage) {
            if (atlasImage == nullptr || atlasImage->isNull()) {
                return;
            }
            for (auto it = regions.cbegin(); it != regions.cend(); ++it) {
                AtlasRegionRef region;
                region.atlasImage = atlasImage;
                region.rect = it.value();
                atlasRegions_.insert(it.key(), region);
            }
        };
    appendAtlasRegions(result.tapAtlasRegions, &tapAtlasImage_);
    appendAtlasRegions(result.trackAtlasRegions, &trackAtlasImage_);
    appendAtlasRegions(result.touchAtlasRegions, &touchAtlasImage_);
    appendAtlasRegions(result.guideAtlasRegions, &guideAtlasImage_);

    overlayCache_.clear();
    guideTransformCache_.clear();
    guideTransformCacheOrder_.clear();
    spriteTransformCache_.clear();
    spriteTransformCacheOrder_.clear();
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();

    if (glRenderer_.isInitialized() && context() != nullptr) {
        scheduleTexturePrewarm();
    }
    if (lastSkinLoadDispatchMs_ >= 0) {
        const qint64 totalMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - lastSkinLoadDispatchMs_);
        appendPreviewStartupTiming("preview_canvas/skin_load_apply_done", totalMs);
        lastSkinLoadDispatchMs_ = -1;
    } else {
        appendPreviewStartupTiming("preview_canvas/skin_load_apply_done", -1);
    }
    update();
}

void PreviewCanvas::setBackgroundBrightness(double brightness)
{
    setBackgroundBrightnessOuter(brightness);
    setBackgroundBrightnessInner(brightness);
}

void PreviewCanvas::setBackgroundBrightnessOuter(double brightness)
{
    const double clamped = qBound(0.0, brightness, 1.0);
    if (qFuzzyCompare(backgroundBrightnessOuter_ + 1.0, clamped + 1.0)) {
        return;
    }
    backgroundBrightnessOuter_ = clamped;
    brightnessMaskCache_ = QImage();
    brightnessMaskCacheSize_ = QSize();
    update();
}

void PreviewCanvas::setBackgroundBrightnessInner(double brightness)
{
    const double clamped = qBound(0.0, brightness, 1.0);
    if (qFuzzyCompare(backgroundBrightnessInner_ + 1.0, clamped + 1.0)) {
        return;
    }
    backgroundBrightnessInner_ = clamped;
    brightnessMaskCache_ = QImage();
    brightnessMaskCacheSize_ = QSize();
    update();
}

void PreviewCanvas::setLayoutSquareScale(double scale)
{
    const double normalized = miacode::preview_video::normalizedLayoutSquareScale(scale);
    if (qFuzzyCompare(layoutSquareScale_ + 1.0, normalized + 1.0)) {
        return;
    }
    layoutSquareScale_ = normalized;
    brightnessMaskCache_ = QImage();
    brightnessMaskCacheSize_ = QSize();
    update();
}

void PreviewCanvas::setSmoothBrightness(bool smooth)
{
    if (smoothBrightness_ == smooth) {
        return;
    }
    smoothBrightness_ = smooth;
    brightnessMaskCache_ = QImage();
    brightnessMaskCacheSize_ = QSize();
    update();
}

void PreviewCanvas::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    if (backgroundScaleMode_ == mode) {
        return;
    }
    backgroundScaleMode_ = mode;
    update();
}

void PreviewCanvas::setShowDebugInfo(bool show)
{
    if (showDebugInfo_ == show) {
        return;
    }
    showDebugInfo_ = show;
    update();
}

void PreviewCanvas::reset()
{
    overlayCache_.clear();
    brightnessMaskCache_ = QImage();
    brightnessMaskCacheSize_ = QSize();
    guideTransformCache_.clear();
    guideTransformCacheOrder_.clear();
    spriteTransformCache_.clear();
    spriteTransformCacheOrder_.clear();
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
    fpsTimer_.invalidate();
    fpsFrameCounter_ = 0;
    fpsDisplay_ = 0.0;
    lastFrameTimestampNs_ = 0;
    frameIntervalsMs_.clear();
    frameIntervalWriteIndex_ = 0;
    frameIntervalCount_ = 0;
    frameMsAverage_ = 0.0;
    frameMsP95_ = 0.0;
    frameMsMax_ = 0.0;
    playheadSeconds_ = 0.0;
    mediaFrame_ = QImage();
#ifdef HAVE_QT_MULTIMEDIA
    videoFrame_ = QVideoFrame();
#endif
    resetProfilingSession();
    update();
}

void PreviewCanvas::resetProfilingSession()
{
    if (context() != nullptr && gpuTimerQueriesSupported_) {
        makeCurrent();
        collectGpuProfilingResults(true);
        doneCurrent();
    }
    profileCpuPrepTotalMs_ = 0.0;
    profileCpuUploadTotalMs_ = 0.0;
    profileGpuDrawTotalMs_ = 0.0;
    profileFrameCount_ = 0;
    profileGpuSampleCount_ = 0;
    profileCpuPrepSamplesMs_.clear();
    profileCpuUploadSamplesMs_.clear();
    profileGpuDrawSamplesMs_.clear();
    profilePresentApproxSamplesMs_.clear();
    profileTickToPaintSamplesMs_.clear();
    profileVideoMapSamplesMs_.clear();
    profileVideoUploadSamplesMs_.clear();
    profileSessionClock_.invalidate();
    lastProfileFrameStartNs_ = -1;
    lastProfileCpuFrameNs_ = 0;
    pendingTickToPaintStartNs_ = -1;
}

void PreviewCanvas::noteTickForProfiling()
{
    if (!profileSessionClock_.isValid()) {
        profileSessionClock_.start();
        lastProfileFrameStartNs_ = 0;
    }
    pendingTickToPaintStartNs_ = profileSessionClock_.nsecsElapsed();
}

QString PreviewCanvas::writeProfilingSummaryToFile()
{
    if (profileFrameCount_ == 0) {
        return QString();
    }

    if (context() != nullptr && gpuTimerQueriesSupported_) {
        makeCurrent();
        collectGpuProfilingResults(true);
        doneCurrent();
    }

    QFile file(profilingSummaryPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return QString();
    }

    const SampleStats cpuPrepStats = computeSampleStats(profileCpuPrepSamplesMs_);
    const SampleStats cpuUploadStats = computeSampleStats(profileCpuUploadSamplesMs_);
    const SampleStats gpuDrawStats = computeSampleStats(profileGpuDrawSamplesMs_);
    const SampleStats presentApproxStats = computeSampleStats(profilePresentApproxSamplesMs_);
    const SampleStats tickToPaintStats = computeSampleStats(profileTickToPaintSamplesMs_);
    const SampleStats videoMapStats = computeSampleStats(profileVideoMapSamplesMs_);
    const SampleStats videoUploadStats = computeSampleStats(profileVideoUploadSamplesMs_);

    const auto writeStats = [](QTextStream& stream, const QString& prefix, const SampleStats& stats) {
        if (!stats.hasValue) {
            stream << prefix << "_avg_ms=N/A\n";
            stream << prefix << "_p95_ms=N/A\n";
            stream << prefix << "_max_ms=N/A\n";
            return;
        }
        stream << prefix << "_avg_ms=" << QString::number(stats.avgMs, 'f', 4) << '\n';
        stream << prefix << "_p95_ms=" << QString::number(stats.p95Ms, 'f', 4) << '\n';
        stream << prefix << "_max_ms=" << QString::number(stats.maxMs, 'f', 4) << '\n';
    };

    QTextStream stream(&file);
    stream << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    stream << "frame_samples=" << profileFrameCount_ << '\n';
    stream << "gpu_frame_samples=" << profileGpuSampleCount_ << '\n';
    stream << "present_approx_note=frame interval residual; includes event loop/compositor/vsync and is not exact swap time\n";
    writeStats(stream, "cpu_prepare", cpuPrepStats);
    writeStats(stream, "cpu_upload", cpuUploadStats);
    writeStats(stream, "gpu_draw", gpuDrawStats);
    writeStats(stream, "present_approx", presentApproxStats);
    writeStats(stream, "tick_to_paint", tickToPaintStats);
    writeStats(stream, "video_frame_map", videoMapStats);
    writeStats(stream, "video_frame_upload", videoUploadStats);
    file.close();
    return file.fileName();
}

