QRectF PreviewCanvas::currentStageRect() const
{
    return stageRectForSize(size());
}

QRectF PreviewCanvas::stageRectForSize(const QSize& renderSize) const
{
    const int renderWidth = qMax(1, renderSize.width());
    const int renderHeight = qMax(1, renderSize.height());
    return QRectF(
        kMargin,
        kMargin,
        qMax<qreal>(1.0, renderWidth - kMargin * 2),
        qMax<qreal>(1.0, renderHeight - kMargin * 2)
    );
}

QRectF PreviewCanvas::stagePlayfieldRect(const QRectF& stageRect) const
{
    const QRectF innerRect = stageRect.adjusted(kPlayfieldInset, kPlayfieldInset, -kPlayfieldInset, -kPlayfieldInset);
    const qreal playfieldSide = qMax<qreal>(1.0, qMin(innerRect.width(), innerRect.height()));
    return QRectF(
        innerRect.center().x() - playfieldSide / 2.0,
        innerRect.center().y() - playfieldSide / 2.0,
        playfieldSide,
        playfieldSide
    );
}

QRectF PreviewCanvas::currentPlayfieldRect() const
{
    return stagePlayfieldRect(currentStageRect());
}

void PreviewCanvas::drawStageBackground(QPainter& painter, const QRectF& stageRect)
{
    QSize mediaSize = mediaFrame_.size();
    bool hasVideoFrame = false;
#ifdef HAVE_QT_MULTIMEDIA
    hasVideoFrame = videoFrame_.isValid();
    if (hasVideoFrame) {
        mediaSize = videoFrame_.surfaceFormat().viewport().size();
        if (mediaSize.isEmpty()) {
            mediaSize = videoFrame_.surfaceFormat().frameSize();
        }
    }
#endif
    const bool hasMedia = !mediaSize.isEmpty();
    painter.fillRect(stageRect, hasMedia ? QColor("#000000") : QColor("#1F2833"));

    if (hasMedia) {
        QSize fittedSize = mediaSize;
        const QSize mediaBounds(
            qMax(1, qRound(stageRect.width())),
            qMax(1, qRound(stageRect.height()))
        );
        const Qt::AspectRatioMode aspectMode = backgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? Qt::KeepAspectRatio
            : Qt::KeepAspectRatioByExpanding;
        fittedSize.scale(mediaBounds, aspectMode);
        if (!fittedSize.isEmpty()) {
            const QRectF targetRect(
                stageRect.center().x() - fittedSize.width() / 2.0,
                stageRect.center().y() - fittedSize.height() / 2.0,
                fittedSize.width(),
                fittedSize.height()
            );
            bool renderedByGl = false;
            if (glRenderer_.isInitialized()) {
                painter.beginNativePainting();
                if (hasVideoFrame) {
#ifdef HAVE_QT_MULTIMEDIA
                    renderedByGl = glRenderer_.drawVideoFrame(videoFrame_, targetRect, 1.0);
#endif
                } else if (!mediaFrame_.isNull()) {
                    renderedByGl = glRenderer_.drawImageQuad(mediaFrame_, targetRect, 0.0, 1.0, QRectF(), false);
                }
                painter.endNativePainting();
            }
            if (renderedByGl) {
                usedGpuRendererThisFrame_ = true;
            } else if (!mediaFrame_.isNull()) {
                ++cpuFallbackCount_;
                painter.drawImage(targetRect, mediaFrame_);
            } else if (hasVideoFrame) {
#ifdef HAVE_QT_MULTIMEDIA
                const QImage fallbackImage = videoFrame_.toImage();
                if (!fallbackImage.isNull()) {
                    ++cpuFallbackCount_;
                    painter.drawImage(targetRect, fallbackImage);
                }
#endif
            }
        }
    }

    const int outerDarkAlpha = qBound(0, qRound((1.0 - backgroundBrightnessOuter_) * 255.0), 255);
    const int innerDarkAlpha = qBound(0, qRound((1.0 - backgroundBrightnessInner_) * 255.0), 255);
    if (outerDarkAlpha > 0 || innerDarkAlpha > 0) {
        const QRectF playfieldRect = stagePlayfieldRect(stageRect);
        const qreal circleRadius = qMax<qreal>(
            1.0,
            playfieldRect.width()
                * qBound(
                    miacode::layout_ring::kPlayfieldRatioMin,
                    layoutRingDiameterRatio_,
                    miacode::layout_ring::kRenderRatioMax
                )
                * 0.5
        );
        QPainterPath circlePath;
        circlePath.addEllipse(playfieldRect.center(), circleRadius, circleRadius);
        if (outerDarkAlpha > 0) {
            painter.save();
            QPainterPath outerPath;
            outerPath.addRect(stageRect);
            outerPath -= circlePath;
            painter.setClipPath(outerPath);
            painter.fillRect(stageRect, QColor(0, 0, 0, outerDarkAlpha));
            painter.restore();
        }
        if (innerDarkAlpha > 0) {
            painter.save();
            painter.setClipPath(circlePath);
            painter.fillRect(stageRect, QColor(0, 0, 0, innerDarkAlpha));
            painter.restore();
        }
    }

}

void PreviewCanvas::drawPlayfieldBackdrop(QPainter& painter, const QRectF& playfieldRect)
{
    if (outlineImage_.isNull()) {
        return;
    }

    const QPointF outlineTopLeft = mapLogicalPointToRect(QPointF(kLogicalOutlineInset, kLogicalOutlineInset), playfieldRect);
    const qreal outlineSide = mapLogicalLengthToRect(kLogicalCanvasSize - kLogicalOutlineInset * 2.0, playfieldRect);
    const QRectF targetRect(outlineTopLeft.x(), outlineTopLeft.y(), outlineSide, outlineSide);
    bool renderedByGl = false;
    if (glRenderer_.isInitialized()) {
        painter.beginNativePainting();
        renderedByGl = glRenderer_.drawImageQuad(outlineImage_, targetRect);
        painter.endNativePainting();
    }
    if (renderedByGl) {
        usedGpuRendererThisFrame_ = true;
        return;
    }

    ++cpuFallbackCount_;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(targetRect, outlineImage_);
    painter.restore();
}

#include "PreviewCanvas.Objects.cpp"

void PreviewCanvas::updateFpsSample()
{
    if (!fpsTimer_.isValid()) {
        fpsTimer_.start();
        fpsFrameCounter_ = 0;
        fpsDisplay_ = 0.0;
        lastFrameTimestampNs_ = 0;
        frameIntervalsMs_.clear();
        frameIntervalsMs_.resize(kFrameStatsWindowSize);
        frameIntervalsMs_.fill(0.0);
        frameIntervalWriteIndex_ = 0;
        frameIntervalCount_ = 0;
        frameMsAverage_ = 0.0;
        frameMsP95_ = 0.0;
        frameMsMax_ = 0.0;
    }
    const qint64 nowNs = fpsTimer_.nsecsElapsed();
    if (lastFrameTimestampNs_ > 0) {
        const double intervalMs = static_cast<double>(nowNs - lastFrameTimestampNs_) / 1000000.0;
        if (!frameIntervalsMs_.isEmpty() && intervalMs > 0.0 && intervalMs < 250.0) {
            frameIntervalsMs_[frameIntervalWriteIndex_] = intervalMs;
            frameIntervalWriteIndex_ = (frameIntervalWriteIndex_ + 1) % frameIntervalsMs_.size();
            frameIntervalCount_ = qMin(frameIntervalCount_ + 1, frameIntervalsMs_.size());
        }
    }
    lastFrameTimestampNs_ = nowNs;
    ++fpsFrameCounter_;
    const qint64 elapsedMs = nowNs / 1000000;
    if (elapsedMs < kFpsSampleWindowMs) {
        return;
    }
    fpsDisplay_ = static_cast<double>(fpsFrameCounter_) * 1000.0 / static_cast<double>(elapsedMs);
    if (frameIntervalCount_ > 0) {
        QVector<double> samples;
        samples.reserve(frameIntervalCount_);
        for (int i = 0; i < frameIntervalCount_; ++i) {
            samples.append(frameIntervalsMs_[i]);
        }
        const double sum = std::accumulate(samples.cbegin(), samples.cend(), 0.0);
        frameMsAverage_ = sum / static_cast<double>(samples.size());
        std::sort(samples.begin(), samples.end());
        frameMsMax_ = samples.constLast();
        const int p95Index = qBound(0, static_cast<int>(qCeil(samples.size() * 0.95)) - 1, samples.size() - 1);
        frameMsP95_ = samples[p95Index];
    }
    fpsFrameCounter_ = 0;
    fpsTimer_.restart();
    lastFrameTimestampNs_ = 0;
}

void PreviewCanvas::paintGL()
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    collectGpuProfilingResults(false);

    if (profileSessionClock_.isValid() && pendingTickToPaintStartNs_ >= 0) {
        const qint64 nowNs = profileSessionClock_.nsecsElapsed();
        profileTickToPaintSamplesMs_.append(
            static_cast<double>(qMax<qint64>(0, nowNs - pendingTickToPaintStartNs_)) / 1000000.0
        );
        pendingTickToPaintStartNs_ = -1;
    }

    if (!profileSessionClock_.isValid()) {
        profileSessionClock_.start();
        lastProfileFrameStartNs_ = 0;
    } else {
        const qint64 frameStartNs = profileSessionClock_.nsecsElapsed();
        if (lastProfileFrameStartNs_ >= 0) {
            const qint64 intervalNs = frameStartNs - lastProfileFrameStartNs_;
            const qint64 residualNs = qMax<qint64>(0, intervalNs - lastProfileCpuFrameNs_);
            profilePresentApproxSamplesMs_.append(static_cast<double>(residualNs) / 1000000.0);
        }
        lastProfileFrameStartNs_ = frameStartNs;
    }

    bool gpuQueryActive = false;
    if (gpuTimerQueriesSupported_ && extra != nullptr && !gpuTimeQueryPending_[gpuTimeQueryCursor_]) {
        extra->glBeginQuery(GL_TIME_ELAPSED, gpuTimeQueries_[gpuTimeQueryCursor_]);
        gpuQueryActive = true;
    }

    QElapsedTimer cpuFrameTimer;
    cpuFrameTimer.start();
    glRenderer_.beginFrame(size(), devicePixelRatioF());
    {
        QPainter painter(this);
        renderCanvas(painter);
    }
    const qint64 cpuFrameNs = cpuFrameTimer.nsecsElapsed();
    const qint64 cpuUploadNs = static_cast<qint64>(glRenderer_.frameCpuUploadNs());
    const qint64 videoMapNs = static_cast<qint64>(glRenderer_.frameVideoMapNs());
    const qint64 videoUploadNs = static_cast<qint64>(glRenderer_.frameVideoUploadNs());
    glRenderer_.endFrame();

    if (gpuQueryActive && extra != nullptr) {
        extra->glEndQuery(GL_TIME_ELAPSED);
        gpuTimeQueryPending_[gpuTimeQueryCursor_] = true;
        gpuTimeQueryCursor_ = (gpuTimeQueryCursor_ + 1) % 4;
    }

    const double cpuUploadMs = static_cast<double>(cpuUploadNs) / 1000000.0;
    const double cpuPrepMs = static_cast<double>(qMax<qint64>(0, cpuFrameNs - cpuUploadNs)) / 1000000.0;
    lastProfileCpuFrameNs_ = cpuFrameNs;
    profileCpuPrepTotalMs_ += cpuPrepMs;
    profileCpuUploadTotalMs_ += cpuUploadMs;
    profileCpuPrepSamplesMs_.append(cpuPrepMs);
    profileCpuUploadSamplesMs_.append(cpuUploadMs);
    if (videoMapNs > 0) {
        profileVideoMapSamplesMs_.append(static_cast<double>(videoMapNs) / 1000000.0);
    }
    if (videoUploadNs > 0) {
        profileVideoUploadSamplesMs_.append(static_cast<double>(videoUploadNs) / 1000000.0);
    }
    ++profileFrameCount_;
}

void PreviewCanvas::renderCanvas(QPainter& painter)
{
    renderCanvas(painter, size(), true, true, false);
}

void PreviewCanvas::renderCanvas(
    QPainter& painter,
    const QSize& canvasSize,
    bool drawStageBackgroundEnabled,
    bool clearToStageColor,
    bool highQualityRender
)
{
    usedGpuRendererThisFrame_ = false;
    cpuFallbackCount_ = 0;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, highQualityRender);
    if (clearToStageColor) {
        painter.fillRect(QRect(QPoint(0, 0), canvasSize), QColor("#1F2833"));
    }

    const QRectF stageRect = stageRectForSize(canvasSize);
    const QRectF playfieldRect = stagePlayfieldRect(stageRect);

    if (drawStageBackgroundEnabled) {
        drawStageBackground(painter, stageRect);
    }
    drawPlayfieldBackdrop(painter, playfieldRect);
    const bool batchNative = glRenderer_.isInitialized();
    if (batchNative) {
        beginNativeBatch(painter);
    }
    drawJudgeEffectFireworkLayer(painter, playfieldRect);
    drawGuideLayer(painter, playfieldRect);
    drawTrackLayer(painter, playfieldRect);
    drawSlideMotionLayer(painter, playfieldRect);
    drawJudgeEffectLayer(painter, playfieldRect);
    drawJudgeEffectTouchLayer(painter, playfieldRect);
    drawHoldLayer(painter, playfieldRect);
    drawTapLayer(painter, playfieldRect);
    drawTouchLayer(painter, playfieldRect);
    drawTouchHoldLayer(painter, playfieldRect);
    if (batchNative) {
        endNativeBatch(painter);
    }

    updateFpsSample();
    drawHud(painter, stageRect);
}

void PreviewCanvas::collectGpuProfilingResults(bool waitForAll)
{
    if (!gpuTimerQueriesSupported_) {
        return;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    if (extra == nullptr) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!gpuTimeQueryPending_[i]) {
            continue;
        }

        bool ready = waitForAll;
        if (!waitForAll) {
            GLuint available = 0;
            extra->glGetQueryObjectuiv(gpuTimeQueries_[i], GL_QUERY_RESULT_AVAILABLE, &available);
            ready = available != 0;
        }

        if (!ready) {
            continue;
        }

        GLuint resultNs = 0;
        extra->glGetQueryObjectuiv(gpuTimeQueries_[i], GL_QUERY_RESULT, &resultNs);
        const double gpuMs = static_cast<double>(resultNs) / 1000000.0;
        profileGpuDrawTotalMs_ += gpuMs;
        ++profileGpuSampleCount_;
        profileGpuDrawSamplesMs_.append(gpuMs);
        gpuTimeQueryPending_[i] = false;
    }
}

QString PreviewCanvas::profilingSummaryPath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.filePath("preview_profile_summary.txt"));
}

QSize PreviewCanvas::preferredSize() const
{
    return QSize(620, 620);
}

