namespace {

int preferredOffscreenFramebufferSamples(QOpenGLContext* offscreenContext, const QSize& framebufferSize)
{
    bool overrideOk = false;
    const int overrideValue =
        qEnvironmentVariableIntValue("MIACODE_EXPORT_OFFSCREEN_SAMPLES", &overrideOk);
    if (overrideOk) {
        return qBound(0, overrideValue, 8);
    }

    const int preferredSamples = offscreenContext != nullptr ? offscreenContext->format().samples() : 0;
    if (preferredSamples <= 0) {
        return 0;
    }

    const qint64 pixelCount =
        static_cast<qint64>(qMax(1, framebufferSize.width())) * qMax(1, framebufferSize.height());
    if (pixelCount >= 1920LL * 1080LL) {
        return 0;
    }
    if (pixelCount >= 1600LL * 900LL) {
        return qBound(0, preferredSamples, 2);
    }
    return qBound(0, preferredSamples, 4);
}

}  // namespace

PreviewCanvas::PreviewCanvas(QWindow* parent)
    : QOpenGLWindow(NoPartialUpdate, parent)
{
    refreshTimingFromFlowSpeed();
    refreshOutlineAsset();
    judgeEffectTapImage_ = buildJudgeEffectTapFallbackImage();
    judgeEffectTapSourceRect_ = nonTransparentBounds(judgeEffectTapImage_);
    judgeEffectTapBreakImage_ = buildJudgeEffectTapBreakFallbackImage();
    judgeEffectTapBreakSourceRect_ = nonTransparentBounds(judgeEffectTapBreakImage_);
    judgeEffectFireworkImage_ = buildJudgeEffectFireworkFallbackImage();
    judgeEffectFireworkSourceRect_ = nonTransparentBounds(judgeEffectFireworkImage_);
    judgeEffectFireworkColorBallImage_ = buildJudgeEffectFireworkColorBallFallbackImage();
    judgeEffectFireworkColorBallSourceRect_ = nonTransparentBounds(judgeEffectFireworkColorBallImage_);
}

void PreviewCanvas::setStageMediaAvailable(bool hasMedia)
{
    if (stageMediaAvailable_ == hasMedia) {
        return;
    }
    stageMediaAvailable_ = hasMedia;
    refreshOutlineAsset();
}

void PreviewCanvas::refreshOutlineAsset()
{
    const QString outlinePath = defaultOutlinePath(stageMediaAvailable_);
    outlineImage_ = outlinePath.isEmpty() ? QImage() : QImage(outlinePath);
    const double textureRingRatio = detectLayoutRingDiameterRatio(outlineImage_);
    layoutRingDiameterRatio_ = qBound(
        miacode::layout_ring::kPlayfieldRatioMin,
        static_cast<double>(kOutlineTargetToPlayfieldRatio) * textureRingRatio,
        miacode::layout_ring::kPlayfieldRatioMax
    );
    brightnessMaskCache_ = QImage();
    brightnessMaskCacheSize_ = QSize();
    update();
}

PreviewCanvas::~PreviewCanvas()
{
    if (offscreenContext_ != nullptr || offscreenSurface_ != nullptr || offscreenFramebuffer_ != nullptr) {
        shutdownOffscreenRenderer();
    }

    if (context() != nullptr) {
        makeCurrent();
        if (gpuTimerQueriesSupported_) {
            QOpenGLContext* ctx = QOpenGLContext::currentContext();
            QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
            if (extra != nullptr) {
                extra->glDeleteQueries(4, gpuTimeQueries_);
            }
        }
        if (glRenderer_.isInitialized()) {
            glRenderer_.shutdown();
        }
        doneCurrent();
    }
}

PreviewCanvas::TapApproachSample PreviewCanvas::sampleTapApproach(qreal deltaSeconds) const
{
    TapApproachSample sample;
    const qreal logicalDistanceTap = static_cast<qreal>(miacode::preview_gameplay::kLogicalDistanceTap);
    const qreal logicalDistanceEdge = static_cast<qreal>(miacode::preview_gameplay::kLogicalDistanceEdge);
    sample.distance = logicalDistanceTap;
    sample.scale = 0.0;
    const qreal tapLifecycleDurationSeconds = static_cast<qreal>(tapLifecycleDurationSeconds_);
    const qreal tapSpawnDurationSeconds = static_cast<qreal>(tapSpawnDurationSeconds_);
    const qreal tapFlyDurationSeconds = static_cast<qreal>(tapFlyDurationSeconds_);
    if (deltaSeconds <= -tapLifecycleDurationSeconds) {
        return sample;
    }
    if (deltaSeconds < -tapFlyDurationSeconds) {
        sample.scale = qBound<qreal>(
            0.0,
            (deltaSeconds + tapLifecycleDurationSeconds) / qMax<qreal>(0.001, tapSpawnDurationSeconds),
            1.0
        );
        return sample;
    }
    if (deltaSeconds < 0.0) {
        sample.distance = qBound<qreal>(
            logicalDistanceTap,
            logicalDistanceTap + (deltaSeconds + tapFlyDurationSeconds) * static_cast<qreal>(tapUnitsPerSecond_),
            logicalDistanceEdge
        );
        sample.scale = 1.0;
        return sample;
    }
    sample.distance = logicalDistanceEdge;
    sample.scale = 1.0;
    return sample;
}

qreal PreviewCanvas::sampleSlideTrackPreTraceOpacity(qreal markerSecond, qreal playheadSecond) const
{
    const qreal deltaSeconds = playheadSecond - markerSecond;
    const qreal slideTrackAppearLeadInSeconds = static_cast<qreal>(slideTrackAppearLeadInSeconds_);
    const qreal slideTrackFullBrightLeadInSeconds = static_cast<qreal>(slideTrackFullBrightLeadInSeconds_);
    if (deltaSeconds < -slideTrackAppearLeadInSeconds) {
        return -1.0;
    }
    if (deltaSeconds < -slideTrackFullBrightLeadInSeconds) {
        const qreal appearDuration = qMax<qreal>(
            0.001,
            slideTrackAppearLeadInSeconds - slideTrackFullBrightLeadInSeconds);
        const qreal progress = qBound<qreal>(
            0.0,
            (deltaSeconds + slideTrackAppearLeadInSeconds) / appearDuration,
            1.0
        );
        return progress;
    }
    return 1.0;
}

void PreviewCanvas::refreshTimingFromFlowSpeed()
{
    noteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(noteFlowSpeed_);
    const double timingScaleFrames =
        miacode::preview_gameplay::previewTimingScaleFramesAt120Fps(noteFlowSpeed_);
    const double tapFrames = timingScaleFrames;
    tapSpawnDurationSeconds_ = miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(tapFrames);
    tapFlyDurationSeconds_ = tapSpawnDurationSeconds_;
    tapLifecycleDurationSeconds_ =
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(tapFrames * 2.0);
    tapUnitsPerSecond_ = (miacode::preview_gameplay::kLogicalDistanceEdge - miacode::preview_gameplay::kLogicalDistanceTap)
        / qMax(0.0001, tapFlyDurationSeconds_);
    slideTrackAppearLeadInSeconds_ =
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(timingScaleFrames + 2.0);
    slideTrackFullBrightLeadInSeconds_ =
        miacode::preview_gameplay::previewTimingSecondsFromFramesAt120Fps(
            miacode::preview_gameplay::kSlideTrackFullBrightLeadInFramesAt120Fps);
}

void PreviewCanvas::setPlayheadSeconds(double seconds)
{
    const double clamped = seconds < 0.0 ? 0.0 : seconds;
    if (qFuzzyCompare(playheadSeconds_ + 1.0, clamped + 1.0)) {
        return;
    }
    playheadSeconds_ = clamped;
    update();
}

void PreviewCanvas::setMediaFrame(const QImage& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    videoFrame_ = QVideoFrame();
#endif
    mediaFrame_ = frame;
    setStageMediaAvailable(!frame.isNull());
}

void PreviewCanvas::setVideoFrame(const QVideoFrame& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    mediaFrame_ = QImage();
    videoFrame_ = frame;
    setStageMediaAvailable(frame.isValid());
#else
    Q_UNUSED(frame);
#endif
}

void PreviewCanvas::setNoteMarkers(const QVector<TimelineNoteMarker>& notes)
{
    noteMarkers_ = notes;
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
    update();
}

void PreviewCanvas::setCpuTrackAreaCachingEnabled(bool enabled)
{
    if (cpuTrackAreaCachingEnabled_ == enabled) {
        return;
    }
    cpuTrackAreaCachingEnabled_ = enabled;
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
}

void PreviewCanvas::setShowTimestamp(bool show)
{
    if (showTimestamp_ == show) {
        return;
    }
    showTimestamp_ = show;
    update();
}

bool PreviewCanvas::showTimestamp() const
{
    return showTimestamp_;
}

void PreviewCanvas::setShowObjectStatsHud(bool show)
{
    if (showObjectStatsHud_ == show) {
        return;
    }
    showObjectStatsHud_ = show;
    update();
}

bool PreviewCanvas::showObjectStatsHud() const
{
    return showObjectStatsHud_;
}

void PreviewCanvas::copyRenderStateFrom(const PreviewCanvas& source)
{
    tapImage_ = source.tapImage_;
    tapEachImage_ = source.tapEachImage_;
    tapBreakImage_ = source.tapBreakImage_;
    tapExImage_ = source.tapExImage_;
    slideTrackImage_ = source.slideTrackImage_;
    slideTrackEachImage_ = source.slideTrackEachImage_;
    slideTrackBreakImage_ = source.slideTrackBreakImage_;
    starImage_ = source.starImage_;
    starEachImage_ = source.starEachImage_;
    starBreakImage_ = source.starBreakImage_;
    starBreakDoubleImage_ = source.starBreakDoubleImage_;
    starDoubleImage_ = source.starDoubleImage_;
    starEachDoubleImage_ = source.starEachDoubleImage_;
    starExImage_ = source.starExImage_;
    starExDoubleImage_ = source.starExDoubleImage_;
    wifiImages_ = source.wifiImages_;
    wifiEachImages_ = source.wifiEachImages_;
    wifiBreakImages_ = source.wifiBreakImages_;
    holdImage_ = source.holdImage_;
    holdEachImage_ = source.holdEachImage_;
    holdBreakImage_ = source.holdBreakImage_;
    holdExImage_ = source.holdExImage_;
    noteGuideNormalImage_ = source.noteGuideNormalImage_;
    noteGuideBreakImage_ = source.noteGuideBreakImage_;
    noteGuideEachImage_ = source.noteGuideEachImage_;
    noteGuideEachLine1Image_ = source.noteGuideEachLine1Image_;
    noteGuideEachLine2Image_ = source.noteGuideEachLine2Image_;
    noteGuideEachLine3Image_ = source.noteGuideEachLine3Image_;
    noteGuideEachLine4Image_ = source.noteGuideEachLine4Image_;
    noteGuideHoldEndImage_ = source.noteGuideHoldEndImage_;
    noteGuideHoldEachEndImage_ = source.noteGuideHoldEachEndImage_;
    noteGuideHoldBreakEndImage_ = source.noteGuideHoldBreakEndImage_;
    noteGuideSlideImage_ = source.noteGuideSlideImage_;
    touchCornerImage_ = source.touchCornerImage_;
    touchCornerEachImage_ = source.touchCornerEachImage_;
    touchCornerBreakImage_ = source.touchCornerBreakImage_;
    touchBorder2Image_ = source.touchBorder2Image_;
    touchBorder2EachImage_ = source.touchBorder2EachImage_;
    touchBorder2BreakImage_ = source.touchBorder2BreakImage_;
    touchBorder3Image_ = source.touchBorder3Image_;
    touchBorder3EachImage_ = source.touchBorder3EachImage_;
    touchBorder3BreakImage_ = source.touchBorder3BreakImage_;
    touchPointImage_ = source.touchPointImage_;
    touchPointEachImage_ = source.touchPointEachImage_;
    touchPointBreakImage_ = source.touchPointBreakImage_;
    touchHold0Image_ = source.touchHold0Image_;
    touchHold1Image_ = source.touchHold1Image_;
    touchHold2Image_ = source.touchHold2Image_;
    touchHold3Image_ = source.touchHold3Image_;
    touchHoldBorderImage_ = source.touchHoldBorderImage_;
    judgeEffectTapImage_ = source.judgeEffectTapImage_;
    judgeEffectTapSourceRect_ = source.judgeEffectTapSourceRect_;
    judgeEffectTapBreakImage_ = source.judgeEffectTapBreakImage_;
    judgeEffectTapBreakSourceRect_ = source.judgeEffectTapBreakSourceRect_;
    judgeEffectHoldSustainCircleImage_ = source.judgeEffectHoldSustainCircleImage_;
    judgeEffectTouchCircleImage_ = source.judgeEffectTouchCircleImage_;
    judgeEffectTouchPart01Image_ = source.judgeEffectTouchPart01Image_;
    judgeEffectTouchPart02Image_ = source.judgeEffectTouchPart02Image_;
    judgeEffectFireworkImage_ = source.judgeEffectFireworkImage_;
    judgeEffectFireworkSourceRect_ = source.judgeEffectFireworkSourceRect_;
    judgeEffectFireworkColorBallImage_ = source.judgeEffectFireworkColorBallImage_;
    judgeEffectFireworkColorBallSourceRect_ = source.judgeEffectFireworkColorBallSourceRect_;
    outlineImage_ = source.outlineImage_;
    tapAtlasImage_ = source.tapAtlasImage_;
    trackAtlasImage_ = source.trackAtlasImage_;
    touchAtlasImage_ = source.touchAtlasImage_;
    guideAtlasImage_ = source.guideAtlasImage_;
    atlasRegions_ = source.atlasRegions_;
    noteMarkers_ = source.noteMarkers_;
    stageMediaAvailable_ = source.stageMediaAvailable_;
    backgroundBrightnessOuter_ = source.backgroundBrightnessOuter_;
    backgroundBrightnessInner_ = source.backgroundBrightnessInner_;
    layoutSquareScale_ = source.layoutSquareScale_;
    smoothBrightness_ = source.smoothBrightness_;
    backgroundScaleMode_ = source.backgroundScaleMode_;
    noteFlowSpeed_ = source.noteFlowSpeed_;
    tapLifecycleDurationSeconds_ = source.tapLifecycleDurationSeconds_;
    tapSpawnDurationSeconds_ = source.tapSpawnDurationSeconds_;
    tapFlyDurationSeconds_ = source.tapFlyDurationSeconds_;
    tapUnitsPerSecond_ = source.tapUnitsPerSecond_;
    slideTrackAppearLeadInSeconds_ = source.slideTrackAppearLeadInSeconds_;
    slideTrackFullBrightLeadInSeconds_ = source.slideTrackFullBrightLeadInSeconds_;
    layoutRingDiameterRatio_ = source.layoutRingDiameterRatio_;
    showDebugInfo_ = source.showDebugInfo_;
    showTimestamp_ = source.showTimestamp_;
    showObjectStatsHud_ = source.showObjectStatsHud_;

    overlayCache_.clear();
    brightnessMaskCache_ = QImage();
    brightnessMaskCacheSize_ = QSize();
    guideTransformCache_.clear();
    guideTransformCacheOrder_.clear();
    spriteTransformCache_.clear();
    spriteTransformCacheOrder_.clear();
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
    cpuTrackAreaCachingEnabled_ = source.cpuTrackAreaCachingEnabled_;
}

QImage PreviewCanvas::renderOverlayFrame(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud
)
{
    const QSize safeSize(qMax(1, outputSize.width()), qMax(1, outputSize.height()));
    const double originalPlayhead = playheadSeconds_;
    const bool originalShowTimestamp = showTimestamp_;
    const bool originalShowObjectStatsHud = showObjectStatsHud_;
    const bool originalHighQualityRender = highQualityRender_;

    playheadSeconds_ = playheadSeconds;
    showTimestamp_ = showTimestamp;
    showObjectStatsHud_ = showObjectStatsHud;
    highQualityRender_ = true;

    QImage frame(safeSize, QImage::Format_RGBA8888);
    frame.fill(Qt::transparent);
    {
        QPainter painter(&frame);
        renderCanvas(painter, safeSize, false, false, true);
    }

    playheadSeconds_ = originalPlayhead;
    showTimestamp_ = originalShowTimestamp;
    showObjectStatsHud_ = originalShowObjectStatsHud;
    highQualityRender_ = originalHighQualityRender;
    return frame;
}

bool PreviewCanvas::initializeOffscreenRenderer(
    const QSurfaceFormat& requestedFormat,
    QOpenGLContext* shareContext,
    QString* errorMessage
)
{
    if (offscreenContext_ != nullptr && offscreenSurface_ != nullptr && glRenderer_.isInitialized()) {
        return true;
    }

    shutdownOffscreenRenderer();

    QSurfaceFormat surfaceFormat = requestedFormat;
    if (shareContext != nullptr && shareContext->isValid()) {
        surfaceFormat = shareContext->format();
    }
    if (surfaceFormat.renderableType() == QSurfaceFormat::DefaultRenderableType) {
        surfaceFormat = QSurfaceFormat::defaultFormat();
    }

    QOffscreenSurface* surface = new QOffscreenSurface();
    surface->setFormat(surfaceFormat);
    surface->create();
    if (!surface->isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to create offscreen surface");
        }
        delete surface;
        return false;
    }

    QOpenGLContext* offscreenContext = new QOpenGLContext();
    offscreenContext->setFormat(surface->format());
    if (shareContext != nullptr && shareContext->isValid()) {
        offscreenContext->setShareContext(shareContext);
    }
    if (!offscreenContext->create()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to create offscreen OpenGL context");
        }
        delete offscreenContext;
        delete surface;
        return false;
    }
    if (!offscreenContext->makeCurrent(surface)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to make offscreen OpenGL context current");
        }
        delete offscreenContext;
        delete surface;
        return false;
    }

    glRenderer_.initialize();
    const QVector<const QImage*> prewarmImages{
        &tapAtlasImage_,
        &trackAtlasImage_,
        &touchAtlasImage_,
        &guideAtlasImage_,
        &outlineImage_,
        &judgeEffectTapImage_,
        &judgeEffectTapBreakImage_,
        &judgeEffectHoldSustainCircleImage_,
        &judgeEffectTouchCircleImage_,
        &judgeEffectTouchPart01Image_,
        &judgeEffectTouchPart02Image_,
        &judgeEffectFireworkImage_,
        &judgeEffectFireworkColorBallImage_,
    };
    for (const QImage* image : prewarmImages) {
        if (image != nullptr && !image->isNull()) {
            glRenderer_.prewarmTexture(*image);
        }
    }
    offscreenContext->doneCurrent();

    offscreenSurface_ = surface;
    offscreenContext_ = offscreenContext;
    return true;
}

void PreviewCanvas::shutdownOffscreenRenderer()
{
    if (offscreenContext_ != nullptr && offscreenSurface_ != nullptr) {
        if (offscreenContext_->makeCurrent(offscreenSurface_)) {
            destroyOffscreenReadbackPbos();
            if (offscreenFramebuffer_ != nullptr) {
                delete offscreenFramebuffer_;
                offscreenFramebuffer_ = nullptr;
                offscreenFramebufferSize_ = QSize();
            }
            if (glRenderer_.isInitialized()) {
                glRenderer_.shutdown();
            }
            offscreenContext_->doneCurrent();
        } else {
            destroyOffscreenReadbackPbos();
            if (offscreenFramebuffer_ != nullptr) {
                delete offscreenFramebuffer_;
                offscreenFramebuffer_ = nullptr;
                offscreenFramebufferSize_ = QSize();
            }
        }
    } else if (offscreenFramebuffer_ != nullptr) {
        destroyOffscreenReadbackPbos();
        delete offscreenFramebuffer_;
        offscreenFramebuffer_ = nullptr;
        offscreenFramebufferSize_ = QSize();
    } else {
        destroyOffscreenReadbackPbos();
    }

    if (offscreenContext_ != nullptr) {
        delete offscreenContext_;
        offscreenContext_ = nullptr;
    }
    if (offscreenSurface_ != nullptr) {
        offscreenSurface_->destroy();
        delete offscreenSurface_;
        offscreenSurface_ = nullptr;
    }
}

bool PreviewCanvas::supportsOffscreenPboReadback(QOpenGLContext* context) const
{
    QOpenGLContext* activeContext = context != nullptr ? context : QOpenGLContext::currentContext();
    if (activeContext == nullptr) {
        return false;
    }
    const QSurfaceFormat format = activeContext->format();
    const bool versionSupported = format.majorVersion() > 2
        || (format.majorVersion() == 2 && format.minorVersion() >= 1);
    return versionSupported || activeContext->hasExtension(QByteArrayLiteral("GL_ARB_pixel_buffer_object"));
}

bool PreviewCanvas::supportsOffscreenPboReadback(QString* errorMessage)
{
    QOpenGLContext* activeContext = offscreenContext_ != nullptr ? offscreenContext_ : QOpenGLContext::currentContext();
    if (!supportsOffscreenPboReadback(activeContext)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("OpenGL context does not expose pixel pack buffer support");
        }
        return false;
    }
    return true;
}

void PreviewCanvas::destroyOffscreenReadbackPbos()
{
    QOpenGLContext* activeContext = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = activeContext != nullptr ? activeContext->extraFunctions() : nullptr;
    if (extra != nullptr && (offscreenReadbackPbos_[0] != 0 || offscreenReadbackPbos_[1] != 0)) {
        extra->glDeleteBuffers(2, offscreenReadbackPbos_);
    }
    offscreenReadbackPbos_[0] = 0;
    offscreenReadbackPbos_[1] = 0;
    offscreenReadbackPboSize_ = QSize();
    offscreenReadbackPboBytes_ = 0;
    offscreenReadbackPboWriteIndex_ = 0;
    offscreenReadbackPendingIndex_ = -1;
}

void PreviewCanvas::resetOffscreenPboReadback()
{
    if (offscreenContext_ != nullptr && offscreenSurface_ != nullptr) {
        if (offscreenContext_->makeCurrent(offscreenSurface_)) {
            destroyOffscreenReadbackPbos();
            offscreenContext_->doneCurrent();
            return;
        }
    }
    destroyOffscreenReadbackPbos();
}

bool PreviewCanvas::ensureOffscreenReadbackPbos(const QSize& framebufferSize, QString* errorMessage)
{
    const QSize safeSize(qMax(1, framebufferSize.width()), qMax(1, framebufferSize.height()));
    const qsizetype byteCount = static_cast<qsizetype>(safeSize.width()) * safeSize.height() * 4;
    if (offscreenReadbackPbos_[0] != 0
        && offscreenReadbackPbos_[1] != 0
        && offscreenReadbackPboSize_ == safeSize
        && offscreenReadbackPboBytes_ == byteCount) {
        return true;
    }

    if (!supportsOffscreenPboReadback(errorMessage)) {
        return false;
    }

    QOpenGLContext* activeContext = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = activeContext != nullptr ? activeContext->extraFunctions() : nullptr;
    if (extra == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("OpenGL extra functions are unavailable for PBO readback");
        }
        return false;
    }

    destroyOffscreenReadbackPbos();
    extra->glGenBuffers(2, offscreenReadbackPbos_);
    if (offscreenReadbackPbos_[0] == 0 || offscreenReadbackPbos_[1] == 0) {
        destroyOffscreenReadbackPbos();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to allocate pixel pack buffers");
        }
        return false;
    }

    for (GLuint pboId : offscreenReadbackPbos_) {
        extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, pboId);
        extra->glBufferData(GL_PIXEL_PACK_BUFFER, byteCount, nullptr, GL_STREAM_READ);
    }
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    offscreenReadbackPboSize_ = safeSize;
    offscreenReadbackPboBytes_ = byteCount;
    offscreenReadbackPboWriteIndex_ = 0;
    offscreenReadbackPendingIndex_ = -1;
    return true;
}

bool PreviewCanvas::mapOffscreenReadbackPbo(int pboIndex, const QSize& imageSize, QImage* frame, QString* errorMessage)
{
    if (frame == nullptr) {
        return false;
    }
    if (pboIndex < 0 || pboIndex >= 2 || offscreenReadbackPbos_[pboIndex] == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid offscreen readback PBO index");
        }
        return false;
    }

    QOpenGLContext* activeContext = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = activeContext != nullptr ? activeContext->extraFunctions() : nullptr;
    if (extra == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("OpenGL extra functions are unavailable while mapping PBO");
        }
        return false;
    }

    const QSize safeSize(qMax(1, imageSize.width()), qMax(1, imageSize.height()));
    const qsizetype bytesPerRow = static_cast<qsizetype>(safeSize.width()) * 4;
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, offscreenReadbackPbos_[pboIndex]);
    void* mapped = extra->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, offscreenReadbackPboBytes_, GL_MAP_READ_BIT);
    if (mapped == nullptr) {
        extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to map offscreen readback PBO");
        }
        return false;
    }

    QImage output(safeSize, QImage::Format_RGBA8888);
    const uchar* sourceBytes = static_cast<const uchar*>(mapped);
    for (int row = 0; row < safeSize.height(); ++row) {
        std::memcpy(output.scanLine(row), sourceBytes + (static_cast<qsizetype>(row) * bytesPerRow), bytesPerRow);
    }
    extra->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    *frame = output;
    return true;
}

bool PreviewCanvas::ensureOffscreenFramebuffer(const QSize& framebufferSize, QString* errorMessage)
{
    const QSize safeSize(qMax(1, framebufferSize.width()), qMax(1, framebufferSize.height()));
    if (offscreenFramebuffer_ != nullptr
        && offscreenFramebufferSize_ == safeSize
        && offscreenFramebuffer_->isValid()) {
        return true;
    }

    if (offscreenFramebuffer_ != nullptr) {
        delete offscreenFramebuffer_;
        offscreenFramebuffer_ = nullptr;
        offscreenFramebufferSize_ = QSize();
    }

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    const int requestedSamples = preferredOffscreenFramebufferSamples(offscreenContext_, safeSize);
    const auto tryCreateFramebuffer = [&](int sampleCount) {
        format.setSamples(sampleCount);
        offscreenFramebuffer_ = new QOpenGLFramebufferObject(safeSize, format);
        return offscreenFramebuffer_ != nullptr && offscreenFramebuffer_->isValid();
    };

    int activeSamples = requestedSamples;
    bool created = tryCreateFramebuffer(activeSamples);
    if (!created && activeSamples > 0) {
        delete offscreenFramebuffer_;
        offscreenFramebuffer_ = nullptr;
        activeSamples = 0;
        created = tryCreateFramebuffer(activeSamples);
    }
    if (!created) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to create offscreen framebuffer samples=%1")
                .arg(activeSamples);
        }
        if (offscreenFramebuffer_ != nullptr) {
            delete offscreenFramebuffer_;
            offscreenFramebuffer_ = nullptr;
        }
        return false;
    }
    offscreenFramebufferSize_ = safeSize;
    return true;
}

QImage PreviewCanvas::renderOverlayFrameOffscreen(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud
)
{
    const QSize safeSize(qMax(1, outputSize.width()), qMax(1, outputSize.height()));
    offscreenDrawNsLastFrame_ = 0;
    offscreenReadbackNsLastFrame_ = 0;
    if (offscreenContext_ == nullptr || offscreenSurface_ == nullptr || !glRenderer_.isInitialized()) {
        return QImage();
    }
    if (!offscreenContext_->makeCurrent(offscreenSurface_)) {
        return QImage();
    }
    if (!ensureOffscreenFramebuffer(safeSize, nullptr)) {
        offscreenContext_->doneCurrent();
        return QImage();
    }

    const double originalPlayhead = playheadSeconds_;
    const bool originalShowTimestamp = showTimestamp_;
    const bool originalShowObjectStatsHud = showObjectStatsHud_;
    const bool originalHighQualityRender = highQualityRender_;

    playheadSeconds_ = playheadSeconds;
    showTimestamp_ = showTimestamp;
    showObjectStatsHud_ = showObjectStatsHud;
    highQualityRender_ = true;

    QImage frame;
    if (offscreenFramebuffer_->bind()) {
        QOpenGLFunctions* gl = offscreenContext_->functions();
        if (gl != nullptr) {
            gl->glViewport(0, 0, safeSize.width(), safeSize.height());
            gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        }

        QElapsedTimer drawTimer;
        drawTimer.start();
        glRenderer_.beginFrame(safeSize, 1.0);
        {
            QOpenGLPaintDevice paintDevice(safeSize);
            paintDevice.setDevicePixelRatio(1.0);
            QPainter painter(&paintDevice);
            renderCanvas(painter, safeSize, false, false, true);
        }
        glRenderer_.endFrame();
        offscreenDrawNsLastFrame_ = drawTimer.nsecsElapsed();
        QElapsedTimer readbackTimer;
        readbackTimer.start();
        frame = offscreenFramebuffer_->toImage(false);
        offscreenReadbackNsLastFrame_ = readbackTimer.nsecsElapsed();
        offscreenFramebuffer_->release();
    }

    playheadSeconds_ = originalPlayhead;
    showTimestamp_ = originalShowTimestamp;
    showObjectStatsHud_ = originalShowObjectStatsHud;
    highQualityRender_ = originalHighQualityRender;
    offscreenContext_->doneCurrent();

    if (!frame.isNull() && frame.format() != QImage::Format_RGBA8888) {
        frame = frame.convertToFormat(QImage::Format_RGBA8888);
    }
    return frame;
}

bool PreviewCanvas::renderOverlayFrameOffscreenPboStep(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud,
    QImage* completedFrame,
    bool* completedFrameReady,
    bool drainOnly,
    QString* errorMessage
)
{
    if (completedFrame != nullptr) {
        *completedFrame = QImage();
    }
    if (completedFrameReady != nullptr) {
        *completedFrameReady = false;
    }
    offscreenDrawNsLastFrame_ = 0;
    offscreenReadbackNsLastFrame_ = 0;

    const QSize safeSize(qMax(1, outputSize.width()), qMax(1, outputSize.height()));
    if (offscreenContext_ == nullptr || offscreenSurface_ == nullptr || !glRenderer_.isInitialized()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("offscreen renderer is unavailable");
        }
        return false;
    }
    if (!offscreenContext_->makeCurrent(offscreenSurface_)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to make offscreen OpenGL context current");
        }
        return false;
    }
    if (!ensureOffscreenFramebuffer(safeSize, errorMessage)) {
        offscreenContext_->doneCurrent();
        return false;
    }
    if (!ensureOffscreenReadbackPbos(safeSize, errorMessage)) {
        offscreenContext_->doneCurrent();
        return false;
    }

    if (drainOnly) {
        if (offscreenReadbackPendingIndex_ >= 0) {
            QElapsedTimer readbackTimer;
            readbackTimer.start();
            QImage drainedFrame;
            if (!mapOffscreenReadbackPbo(offscreenReadbackPendingIndex_, safeSize, &drainedFrame, errorMessage)) {
                offscreenContext_->doneCurrent();
                return false;
            }
            offscreenReadbackNsLastFrame_ = readbackTimer.nsecsElapsed();
            offscreenReadbackPendingIndex_ = -1;
            if (completedFrame != nullptr) {
                *completedFrame = drainedFrame;
            }
            if (completedFrameReady != nullptr) {
                *completedFrameReady = !drainedFrame.isNull();
            }
        }
        offscreenContext_->doneCurrent();
        return true;
    }

    const double originalPlayhead = playheadSeconds_;
    const bool originalShowTimestamp = showTimestamp_;
    const bool originalShowObjectStatsHud = showObjectStatsHud_;
    const bool originalHighQualityRender = highQualityRender_;

    playheadSeconds_ = playheadSeconds;
    showTimestamp_ = showTimestamp;
    showObjectStatsHud_ = showObjectStatsHud;
    highQualityRender_ = true;

    bool stepOk = true;
    if (offscreenFramebuffer_->bind()) {
        QOpenGLFunctions* gl = offscreenContext_->functions();
        QOpenGLExtraFunctions* extra = offscreenContext_->extraFunctions();
        if (gl != nullptr) {
            gl->glViewport(0, 0, safeSize.width(), safeSize.height());
            gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        }

        QElapsedTimer drawTimer;
        drawTimer.start();
        glRenderer_.beginFrame(safeSize, 1.0);
        {
            QOpenGLPaintDevice paintDevice(safeSize);
            paintDevice.setDevicePixelRatio(1.0);
            QPainter painter(&paintDevice);
            renderCanvas(painter, safeSize, false, false, true);
        }
        glRenderer_.endFrame();
        offscreenDrawNsLastFrame_ = drawTimer.nsecsElapsed();

        if (extra != nullptr) {
            const int writeIndex = offscreenReadbackPboWriteIndex_;
            extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, offscreenReadbackPbos_[writeIndex]);
            extra->glReadPixels(0, 0, safeSize.width(), safeSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

            if (offscreenReadbackPendingIndex_ >= 0) {
                QElapsedTimer readbackTimer;
                readbackTimer.start();
                QImage readyFrame;
                if (!mapOffscreenReadbackPbo(offscreenReadbackPendingIndex_, safeSize, &readyFrame, errorMessage)) {
                    stepOk = false;
                } else {
                    offscreenReadbackNsLastFrame_ = readbackTimer.nsecsElapsed();
                    if (completedFrame != nullptr) {
                        *completedFrame = readyFrame;
                    }
                    if (completedFrameReady != nullptr) {
                        *completedFrameReady = !readyFrame.isNull();
                    }
                }
            }

            offscreenReadbackPendingIndex_ = writeIndex;
            offscreenReadbackPboWriteIndex_ = (writeIndex + 1) % 2;
        } else {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("OpenGL extra functions are unavailable for PBO readback");
            }
            stepOk = false;
        }

        offscreenFramebuffer_->release();
    } else {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to bind offscreen framebuffer");
        }
        stepOk = false;
    }

    playheadSeconds_ = originalPlayhead;
    showTimestamp_ = originalShowTimestamp;
    showObjectStatsHud_ = originalShowObjectStatsHud;
    highQualityRender_ = originalHighQualityRender;
    offscreenContext_->doneCurrent();
    return stepOk;
}

struct PreviewCanvas::SkinLoadResult {
    quint64 generation = 0;
    QImage tapImage;
    QImage tapEachImage;
    QImage tapBreakImage;
    QImage tapExImage;
    QImage slideTrackImage;
    QImage slideTrackEachImage;
    QImage slideTrackBreakImage;
    QImage starImage;
    QImage starEachImage;
    QImage starBreakImage;
    QImage starBreakDoubleImage;
    QImage starDoubleImage;
    QImage starEachDoubleImage;
    QImage starExImage;
    QImage starExDoubleImage;
    QVector<QImage> wifiImages;
    QVector<QImage> wifiEachImages;
    QVector<QImage> wifiBreakImages;
    QImage holdImage;
    QImage holdEachImage;
    QImage holdBreakImage;
    QImage holdExImage;
    QImage noteGuideNormalImage;
    QImage noteGuideBreakImage;
    QImage noteGuideEachImage;
    QImage noteGuideEachLine1Image;
    QImage noteGuideEachLine2Image;
    QImage noteGuideEachLine3Image;
    QImage noteGuideEachLine4Image;
    QImage noteGuideHoldEndImage;
    QImage noteGuideHoldEachEndImage;
    QImage noteGuideHoldBreakEndImage;
    QImage noteGuideSlideImage;
    QImage touchCornerImage;
    QImage touchCornerEachImage;
    QImage touchCornerBreakImage;
    QImage touchBorder2Image;
    QImage touchBorder2EachImage;
    QImage touchBorder2BreakImage;
    QImage touchBorder3Image;
    QImage touchBorder3EachImage;
    QImage touchBorder3BreakImage;
    QImage touchPointImage;
    QImage touchPointEachImage;
    QImage touchPointBreakImage;
    QImage touchHold0Image;
    QImage touchHold1Image;
    QImage touchHold2Image;
    QImage touchHold3Image;
    QImage touchHoldBorderImage;
    QImage judgeEffectTapImage;
    QImage judgeEffectTapBreakImage;
    QImage judgeEffectHoldSustainCircleImage;
    QImage judgeEffectTouchCircleImage;
    QImage judgeEffectTouchPart01Image;
    QImage judgeEffectTouchPart02Image;
    QImage judgeEffectFireworkImage;
    QImage judgeEffectFireworkColorBallImage;
    QImage tapAtlasImage;
    QImage trackAtlasImage;
    QImage touchAtlasImage;
    QImage guideAtlasImage;
    QHash<quint64, QRect> tapAtlasRegions;
    QHash<quint64, QRect> trackAtlasRegions;
    QHash<quint64, QRect> touchAtlasRegions;
    QHash<quint64, QRect> guideAtlasRegions;
};

namespace {
struct AtlasBuildResult {
    QImage atlasImage;
    QHash<quint64, QRect> regions;
};

QImage loadImageIfExists(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        return QImage();
    }
    return QImage(path);
}

QImage loadGuideImageScaled(const QDir& noteGuideDir, const QString& name)
{
    const QString path = noteGuideDir.filePath(name);
    if (!QFileInfo::exists(path)) {
        return QImage();
    }
    QImage image(path);
    if (image.isNull()) {
        return QImage();
    }
    const int width = qMax(1, qRound(image.width() * kSkinAssetScale));
    const int height = qMax(1, qRound(image.height() * kSkinAssetScale));
    if (width != image.width() || height != image.height()) {
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

AtlasBuildResult buildAtlasFromImages(const QVector<const QImage*>& images)
{
    AtlasBuildResult result;

    struct Placement {
        const QImage* source = nullptr;
        QRect rect;
    };

    QVector<Placement> placements;
    QHash<quint64, bool> seen;
    int x = kAtlasPadding;
    int y = kAtlasPadding;
    int rowHeight = 0;
    int atlasWidth = 0;

    for (const QImage* image : images) {
        if (image == nullptr || image->isNull()) {
            continue;
        }

        const quint64 key = image->cacheKey();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key, true);

        const int width = image->width();
        const int height = image->height();
        if (width <= 0 || height <= 0) {
            continue;
        }

        if (x > kAtlasPadding && x + width + kAtlasPadding > kAtlasMaxWidth) {
            x = kAtlasPadding;
            y += rowHeight + kAtlasPadding;
            rowHeight = 0;
        }

        Placement placement;
        placement.source = image;
        placement.rect = QRect(x, y, width, height);
        placements.append(placement);

        x += width + kAtlasPadding;
        rowHeight = qMax(rowHeight, height);
        atlasWidth = qMax(atlasWidth, x);
    }

    if (placements.isEmpty()) {
        return result;
    }

    const int finalWidth = qMax(kAtlasPadding * 2 + 1, atlasWidth);
    const int finalHeight = qMax(kAtlasPadding * 2 + 1, y + rowHeight + kAtlasPadding);
    result.atlasImage = QImage(finalWidth, finalHeight, QImage::Format_ARGB32_Premultiplied);
    result.atlasImage.fill(Qt::transparent);

    QPainter atlasPainter(&result.atlasImage);
    atlasPainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Placement& placement : placements) {
        atlasPainter.drawImage(placement.rect.topLeft(), *placement.source);
        result.regions.insert(placement.source->cacheKey(), placement.rect);
    }
    atlasPainter.end();
    return result;
}

PreviewCanvas::SkinLoadResult loadSkinAssets(const QString& skinDir, quint64 generation)
{
    PreviewCanvas::SkinLoadResult result;
    result.generation = generation;

    if (skinDir.isEmpty()) {
        return result;
    }

    const QDir dir(skinDir);
    const QDir noteGuideDir(defaultNoteGuideDir());

    result.tapImage = loadImageIfExists(dir.filePath("tap.png"));
    result.tapEachImage = loadImageIfExists(dir.filePath("tap_each.png"));
    result.tapBreakImage = loadImageIfExists(dir.filePath("tap_break.png"));
    result.tapExImage = loadImageIfExists(dir.filePath("tap_ex.png"));
    result.slideTrackImage = loadImageIfExists(dir.filePath("slide.png"));
    result.slideTrackEachImage = loadImageIfExists(dir.filePath("slide_each.png"));
    result.slideTrackBreakImage = loadImageIfExists(dir.filePath("slide_break.png"));
    result.starImage = loadImageIfExists(dir.filePath("star.png"));
    result.starEachImage = loadImageIfExists(dir.filePath("star_each.png"));
    result.starBreakImage = loadImageIfExists(dir.filePath("star_break.png"));
    result.starBreakDoubleImage = loadImageIfExists(dir.filePath("star_break_double.png"));
    result.starDoubleImage = loadImageIfExists(dir.filePath("star_double.png"));
    result.starEachDoubleImage = loadImageIfExists(dir.filePath("star_each_double.png"));
    result.starExImage = loadImageIfExists(dir.filePath("star_ex.png"));
    result.starExDoubleImage = loadImageIfExists(dir.filePath("star_ex_double.png"));
    result.holdImage = loadImageIfExists(dir.filePath("hold.png"));
    result.holdEachImage = loadImageIfExists(dir.filePath("hold_each.png"));
    result.holdBreakImage = loadImageIfExists(dir.filePath("hold_break.png"));
    result.holdExImage = loadImageIfExists(dir.filePath("hold_ex.png"));
    result.touchCornerImage = loadImageIfExists(dir.filePath("touch.png"));
    result.touchCornerEachImage = loadImageIfExists(dir.filePath("touch_each.png"));
    result.touchCornerBreakImage = loadImageIfExists(dir.filePath("touch_break.png"));
    result.touchBorder2Image = loadImageIfExists(dir.filePath("touch_border_2.png"));
    result.touchBorder2EachImage = loadImageIfExists(dir.filePath("touch_border_2_each.png"));
    result.touchBorder2BreakImage = loadImageIfExists(dir.filePath("touch_break_border_2.png"));
    result.touchBorder3Image = loadImageIfExists(dir.filePath("touch_border_3.png"));
    result.touchBorder3EachImage = loadImageIfExists(dir.filePath("touch_border_3_each.png"));
    result.touchBorder3BreakImage = loadImageIfExists(dir.filePath("touch_break_border_3.png"));
    result.touchPointImage = loadImageIfExists(dir.filePath("touch_point.png"));
    result.touchPointEachImage = loadImageIfExists(dir.filePath("touch_point_each.png"));
    result.touchPointBreakImage = loadImageIfExists(dir.filePath("touch_break_point.png"));
    result.touchHold0Image = loadImageIfExists(dir.filePath("touchhold_0.png"));
    result.touchHold1Image = loadImageIfExists(dir.filePath("touchhold_1.png"));
    result.touchHold2Image = loadImageIfExists(dir.filePath("touchhold_2.png"));
    result.touchHold3Image = loadImageIfExists(dir.filePath("touchhold_3.png"));
    result.touchHoldBorderImage = loadImageIfExists(dir.filePath("touchhold_border.png"));
    result.judgeEffectTapImage = loadImageIfExists(dir.filePath("judge_effect_tap.png"));
    result.judgeEffectTapBreakImage = loadImageIfExists(dir.filePath("judge_effect_tap_break.png"));
    result.judgeEffectHoldSustainCircleImage = loadImageIfExists(dir.filePath("judge_effect_hold_sustain_circle.png"));
    if (result.judgeEffectHoldSustainCircleImage.isNull()) {
        result.judgeEffectHoldSustainCircleImage = loadImageIfExists(dir.filePath("circle.png"));
    }
    result.judgeEffectTouchCircleImage = loadImageIfExists(dir.filePath("judge_effect_touch_circle.png"));
    result.judgeEffectTouchPart01Image = loadImageIfExists(dir.filePath("judge_effect_touch_part_01.png"));
    result.judgeEffectTouchPart02Image = loadImageIfExists(dir.filePath("judge_effect_touch_part_02.png"));
    result.judgeEffectFireworkImage = loadImageIfExists(dir.filePath("judge_effect_firework_tinted_sample.png"));
    if (result.judgeEffectFireworkImage.isNull()) {
        result.judgeEffectFireworkImage = loadImageIfExists(dir.filePath("judge_effect_firework.png"));
    }
    result.judgeEffectFireworkColorBallImage = loadImageIfExists(dir.filePath("judge_effect_firework_color_ball.png"));
    if (result.judgeEffectFireworkColorBallImage.isNull()) {
        result.judgeEffectFireworkColorBallImage = loadImageIfExists(dir.filePath("ColorBall.png"));
    }

    result.noteGuideNormalImage = loadGuideImageScaled(noteGuideDir, "Normal.png");
    result.noteGuideBreakImage = loadGuideImageScaled(noteGuideDir, "Break.png");
    if (result.noteGuideBreakImage.isNull()) {
        result.noteGuideBreakImage = result.noteGuideNormalImage;
    }
    result.noteGuideEachImage = loadGuideImageScaled(noteGuideDir, "Each.png");
    if (result.noteGuideEachImage.isNull()) {
        result.noteGuideEachImage = result.noteGuideNormalImage;
    }
    result.noteGuideEachLine1Image = loadGuideImageScaled(noteGuideDir, "EachLine1.png");
    result.noteGuideEachLine2Image = loadGuideImageScaled(noteGuideDir, "EachLine2.png");
    result.noteGuideEachLine3Image = loadGuideImageScaled(noteGuideDir, "EachLine3.png");
    result.noteGuideEachLine4Image = loadGuideImageScaled(noteGuideDir, "EachLine4.png");
    result.noteGuideHoldEndImage = loadGuideImageScaled(noteGuideDir, "Hold_End.png");
    result.noteGuideHoldEachEndImage = loadGuideImageScaled(noteGuideDir, "Hold_Each_End.png");
    if (result.noteGuideHoldEachEndImage.isNull()) {
        result.noteGuideHoldEachEndImage = result.noteGuideHoldEndImage;
    }
    result.noteGuideHoldBreakEndImage = loadGuideImageScaled(noteGuideDir, "Hold_Break_End.png");
    if (result.noteGuideHoldBreakEndImage.isNull()) {
        result.noteGuideHoldBreakEndImage = result.noteGuideHoldEndImage;
    }
    result.noteGuideSlideImage = loadGuideImageScaled(noteGuideDir, "Slide.png");
    if (result.noteGuideSlideImage.isNull()) {
        result.noteGuideSlideImage = result.noteGuideNormalImage;
    }

    for (int i = 0; i <= 10; ++i) {
        const QImage wifiImage = loadImageIfExists(dir.filePath(QString("wifi_%1.png").arg(i)));
        if (!wifiImage.isNull()) {
            result.wifiImages.append(wifiImage);
        }
        const QImage wifiEachImage = loadImageIfExists(dir.filePath(QString("wifi_each_%1.png").arg(i)));
        if (!wifiEachImage.isNull()) {
            result.wifiEachImages.append(wifiEachImage);
        }
        const QImage wifiBreakImage = loadImageIfExists(dir.filePath(QString("wifi_break_%1.png").arg(i)));
        if (!wifiBreakImage.isNull()) {
            result.wifiBreakImages.append(wifiBreakImage);
        }
    }

    {
        const AtlasBuildResult tapAtlas = buildAtlasFromImages(
            QVector<const QImage*>{
                &result.tapImage,
                &result.tapEachImage,
                &result.tapBreakImage,
                &result.starImage,
                &result.starEachImage,
                &result.starBreakImage,
                &result.holdImage,
                &result.holdEachImage,
                &result.holdBreakImage,
            }
        );
        result.tapAtlasImage = tapAtlas.atlasImage;
        result.tapAtlasRegions = tapAtlas.regions;
    }

    {
        QVector<const QImage*> trackImages{
            &result.slideTrackImage,
            &result.slideTrackEachImage,
            &result.slideTrackBreakImage,
        };
        for (const QImage& image : result.wifiImages) {
            trackImages.append(&image);
        }
        for (const QImage& image : result.wifiEachImages) {
            trackImages.append(&image);
        }
        for (const QImage& image : result.wifiBreakImages) {
            trackImages.append(&image);
        }
        const AtlasBuildResult trackAtlas = buildAtlasFromImages(trackImages);
        result.trackAtlasImage = trackAtlas.atlasImage;
        result.trackAtlasRegions = trackAtlas.regions;
    }

    {
        const AtlasBuildResult touchAtlas = buildAtlasFromImages(
            QVector<const QImage*>{
                &result.touchCornerImage,
                &result.touchCornerEachImage,
                &result.touchCornerBreakImage,
                &result.touchBorder2Image,
                &result.touchBorder2EachImage,
                &result.touchBorder2BreakImage,
                &result.touchBorder3Image,
                &result.touchBorder3EachImage,
                &result.touchBorder3BreakImage,
                &result.touchPointImage,
                &result.touchPointEachImage,
                &result.touchPointBreakImage,
                &result.touchHold0Image,
                &result.touchHold1Image,
                &result.touchHold2Image,
                &result.touchHold3Image,
                &result.touchHoldBorderImage,
            }
        );
        result.touchAtlasImage = touchAtlas.atlasImage;
        result.touchAtlasRegions = touchAtlas.regions;
    }

    {
        const AtlasBuildResult guideAtlas = buildAtlasFromImages(
            QVector<const QImage*>{
                &result.noteGuideNormalImage,
                &result.noteGuideBreakImage,
                &result.noteGuideEachImage,
                &result.noteGuideEachLine1Image,
                &result.noteGuideEachLine2Image,
                &result.noteGuideEachLine3Image,
                &result.noteGuideEachLine4Image,
                &result.noteGuideHoldEndImage,
                &result.noteGuideHoldEachEndImage,
                &result.noteGuideHoldBreakEndImage,
                &result.noteGuideSlideImage,
            }
        );
        result.guideAtlasImage = guideAtlas.atlasImage;
        result.guideAtlasRegions = guideAtlas.regions;
    }

    return result;
}
} // namespace

