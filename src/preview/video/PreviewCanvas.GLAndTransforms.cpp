void PreviewCanvas::initializeGL()
{
    glRenderer_.initialize();
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    if (extra != nullptr && (ctx->hasExtension("GL_ARB_timer_query")
        || ctx->hasExtension("GL_EXT_disjoint_timer_query")
        || ctx->format().majorVersion() >= 3)) {
        extra->glGenQueries(4, gpuTimeQueries_);
        gpuTimerQueriesSupported_ = true;
    }
    scheduleTexturePrewarm();
}

void PreviewCanvas::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void PreviewCanvas::beginNativeBatch(QPainter& painter)
{
    if (nativePaintingActive_ || !glRenderer_.isInitialized()) {
        return;
    }
    painter.beginNativePainting();
    nativePaintingActive_ = true;
}

void PreviewCanvas::endNativeBatch(QPainter& painter)
{
    if (!nativePaintingActive_) {
        return;
    }
    painter.endNativePainting();
    nativePaintingActive_ = false;
}

void PreviewCanvas::scheduleTexturePrewarm()
{
    pendingTexturePrewarmImages_.clear();
    pendingTexturePrewarmImages_.append(tapAtlasImage_);
    pendingTexturePrewarmImages_.append(trackAtlasImage_);
    pendingTexturePrewarmImages_.append(touchAtlasImage_);
    pendingTexturePrewarmImages_.append(guideAtlasImage_);
    pendingTexturePrewarmImages_.append(outlineImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTapImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTapBreakImage_);
    pendingTexturePrewarmImages_.append(judgeEffectHoldSustainCircleImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTouchCircleImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTouchPart01Image_);
    pendingTexturePrewarmImages_.append(judgeEffectTouchPart02Image_);
    pendingTexturePrewarmImages_.append(judgeEffectFireworkImage_);
    pendingTexturePrewarmImages_.append(judgeEffectFireworkColorBallImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeSimpleNormalImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeSimpleBreakImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeStraightLeftImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeStraightRightImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeCircleLeftImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeCircleRightImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeWifiUpImage_);
    pendingTexturePrewarmImages_.append(reviewJudgeWifiDownImage_);
    pendingTexturePrewarmImages_.append(muriJudgeSimpleImage_);
    pendingTexturePrewarmImages_.append(muriJudgeStraightLeftImage_);
    pendingTexturePrewarmImages_.append(muriJudgeStraightRightImage_);
    pendingTexturePrewarmImages_.append(muriJudgeCircleLeftImage_);
    pendingTexturePrewarmImages_.append(muriJudgeCircleRightImage_);
    pendingTexturePrewarmImages_.append(muriJudgeWifiUpImage_);
    pendingTexturePrewarmImages_.append(muriJudgeWifiDownImage_);
    texturePrewarmStartMs_ = QDateTime::currentMSecsSinceEpoch();
    appendPreviewStartupTiming("preview_canvas/texture_prewarm_schedule", -1);

    if (texturePrewarmTimer_ == nullptr) {
        texturePrewarmTimer_ = new QTimer(this);
        texturePrewarmTimer_->setInterval(16);
        texturePrewarmTimer_->setTimerType(Qt::CoarseTimer);
        connect(texturePrewarmTimer_, &QTimer::timeout, this, &PreviewCanvas::processTexturePrewarmQueue);
    }
    if (!texturePrewarmTimer_->isActive()) {
        texturePrewarmTimer_->start();
    }
}

void PreviewCanvas::processTexturePrewarmQueue()
{
    if (pendingTexturePrewarmImages_.isEmpty()) {
        if (texturePrewarmTimer_ != nullptr) {
            texturePrewarmTimer_->stop();
        }
        if (texturePrewarmStartMs_ >= 0) {
            const qint64 elapsedMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - texturePrewarmStartMs_);
            appendPreviewStartupTiming("preview_canvas/texture_prewarm_done", elapsedMs);
            texturePrewarmStartMs_ = -1;
        }
        return;
    }
    if (!glRenderer_.isInitialized() || context() == nullptr) {
        return;
    }

    const QImage image = pendingTexturePrewarmImages_.takeFirst();
    if (!image.isNull()) {
        makeCurrent();
        glRenderer_.prewarmTexture(image);
        doneCurrent();
    }

    if (pendingTexturePrewarmImages_.isEmpty() && texturePrewarmTimer_ != nullptr) {
        texturePrewarmTimer_->stop();
    }
}

const QImage* PreviewCanvas::selectTapImage(const TimelineNoteMarker& marker) const
{
    const bool slideHeadTapMaterial =
        (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi"))
        && marker.slideHeadUsesTapMaterial;
    const bool isBreak = slideHeadTapMaterial ? marker.headBreak : marker.isBreak;
    const bool isEach = slideHeadTapMaterial ? marker.headEach : marker.isEach;
    const QImage* tapImage = &tapImage_;
    if (isBreak && !tapBreakImage_.isNull()) {
        tapImage = &tapBreakImage_;
    } else if (isEach && !tapEachImage_.isNull()) {
        tapImage = &tapEachImage_;
    }
    return tapImage;
}

const QImage* PreviewCanvas::selectHoldImage(const TimelineNoteMarker& marker) const
{
    const QImage* holdImage = &holdImage_;
    if (marker.isBreak && !holdBreakImage_.isNull()) {
        holdImage = &holdBreakImage_;
    } else if (marker.isEach && !holdEachImage_.isNull()) {
        holdImage = &holdEachImage_;
    }
    return holdImage;
}

const QImage* PreviewCanvas::selectTapNoteGuideImage(const TimelineNoteMarker& marker) const
{
    const bool slideLike = marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
    if (slideLike && !marker.hasHeadStar) {
        return nullptr;
    }

    const bool starMaterialHead = slideLike ? !marker.slideHeadUsesTapMaterial : marker.tapUsesStarMaterial;
    const bool isBreak = slideLike ? marker.headBreak : marker.isBreak;
    const bool isEach = slideLike ? marker.headEach : marker.isEach;

    if (isBreak && !noteGuideBreakImage_.isNull()) {
        return &noteGuideBreakImage_;
    }
    if (isEach && !noteGuideEachImage_.isNull()) {
        return &noteGuideEachImage_;
    }
    if (starMaterialHead) {
        return noteGuideSlideImage_.isNull() ? &noteGuideNormalImage_ : &noteGuideSlideImage_;
    }
    return &noteGuideNormalImage_;
}

const QImage* PreviewCanvas::selectHoldEndNoteGuideImage(const TimelineNoteMarker& marker) const
{
    if (marker.isBreak && !noteGuideHoldBreakEndImage_.isNull()) {
        return &noteGuideHoldBreakEndImage_;
    }
    if (marker.isEach && !noteGuideHoldEachEndImage_.isNull()) {
        return &noteGuideHoldEachEndImage_;
    }
    return &noteGuideHoldEndImage_;
}

QImage PreviewCanvas::composeOverlay(
    const QImage& base,
    const QImage& overlay,
    qreal mix,
    qreal lighten,
    const QImage* accentSource,
    const QColor* accentOverride
)
{
    if (base.isNull() || overlay.isNull()) {
        return base;
    }
    const quint64 key = static_cast<quint64>(base.cacheKey())
        ^ (static_cast<quint64>(overlay.cacheKey()) << 1)
        ^ (static_cast<quint64>(qRound(mix * 1000.0)) << 2)
        ^ (static_cast<quint64>(qRound(lighten * 1000.0)) << 12)
        ^ (accentSource != nullptr ? static_cast<quint64>(accentSource->cacheKey()) : 0ULL)
        ^ (accentOverride != nullptr ? (static_cast<quint64>(accentOverride->rgba()) << 3) : 0ULL);
    if (kEnablePreviewCaches) {
        const auto cached = overlayCache_.constFind(key);
        if (cached != overlayCache_.cend()) {
            return cached.value();
        }
    }

    QColor tint = accentOverride != nullptr ? *accentOverride : QColor(255, 255, 255);
    if (accentOverride == nullptr && accentSource != nullptr && !accentSource->isNull()) {
        const QImage source = accentSource->convertToFormat(QImage::Format_ARGB32);
        qint64 r = 0;
        qint64 g = 0;
        qint64 b = 0;
        qint64 n = 0;
        for (int y = 0; y < source.height(); ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                const QColor c = QColor::fromRgba(line[x]);
                if (c.alpha() == 0) {
                    continue;
                }
                r += c.red();
                g += c.green();
                b += c.blue();
                ++n;
            }
        }
        if (n > 0) {
            tint = QColor(static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n));
        }
    }

    QImage overlayTinted = overlay.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < overlayTinted.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(overlayTinted.scanLine(y));
        for (int x = 0; x < overlayTinted.width(); ++x) {
            QColor c = QColor::fromRgba(line[x]);
            if (c.alpha() == 0) {
                continue;
            }
            const int alpha = c.alpha();
            const int nr = qBound(0, qRound(c.red() * (1.0 - mix) + tint.red() * mix), 255);
            const int ng = qBound(0, qRound(c.green() * (1.0 - mix) + tint.green() * mix), 255);
            const int nb = qBound(0, qRound(c.blue() * (1.0 - mix) + tint.blue() * mix), 255);
            const int outR = qBound(0, qRound(nr + (255 - nr) * lighten), 255);
            const int outG = qBound(0, qRound(ng + (255 - ng) * lighten), 255);
            const int outB = qBound(0, qRound(nb + (255 - nb) * lighten), 255);
            line[x] = qRgba(outR, outG, outB, alpha);
        }
    }

    const int width = qMax(base.width(), overlayTinted.width());
    const int height = qMax(base.height(), overlayTinted.height());
    QImage composed(width, height, QImage::Format_ARGB32);
    composed.fill(Qt::transparent);
    QPainter p(&composed);
    const QPoint baseTopLeft((width - base.width()) / 2, (height - base.height()) / 2);
    const QPoint overlayTopLeft((width - overlayTinted.width()) / 2, (height - overlayTinted.height()) / 2);
    p.drawImage(baseTopLeft, base);
    p.drawImage(overlayTopLeft, overlayTinted);
    p.end();
    if (kEnablePreviewCaches) {
        overlayCache_.insert(key, composed);
    }
    return composed;
}

void PreviewCanvas::rebuildAtlases()
{
    atlasRegions_.clear();

    rebuildAtlas(
        tapAtlasImage_,
        QVector<const QImage*>{
            &tapImage_,
            &tapEachImage_,
            &tapBreakImage_,
            &starImage_,
            &starEachImage_,
            &starBreakImage_,
            &holdImage_,
            &holdEachImage_,
            &holdBreakImage_,
        }
    );

    QVector<const QImage*> trackImages{
        &slideTrackImage_,
        &slideTrackEachImage_,
        &slideTrackBreakImage_,
    };
    for (const QImage& image : wifiImages_) {
        trackImages.append(&image);
    }
    for (const QImage& image : wifiEachImages_) {
        trackImages.append(&image);
    }
    for (const QImage& image : wifiBreakImages_) {
        trackImages.append(&image);
    }
    rebuildAtlas(trackAtlasImage_, trackImages);

    rebuildAtlas(
        touchAtlasImage_,
        QVector<const QImage*>{
            &touchCornerImage_,
            &touchCornerEachImage_,
            &touchCornerBreakImage_,
            &touchBorder2Image_,
            &touchBorder2EachImage_,
            &touchBorder2BreakImage_,
            &touchBorder3Image_,
            &touchBorder3EachImage_,
            &touchBorder3BreakImage_,
            &touchPointImage_,
            &touchPointEachImage_,
            &touchPointBreakImage_,
            &touchHold0Image_,
            &touchHold1Image_,
            &touchHold2Image_,
            &touchHold3Image_,
            &touchHoldBorderImage_,
        }
    );

    rebuildAtlas(
        guideAtlasImage_,
        QVector<const QImage*>{
            &noteGuideNormalImage_,
            &noteGuideBreakImage_,
            &noteGuideEachImage_,
            &noteGuideEachLine1Image_,
            &noteGuideEachLine2Image_,
            &noteGuideEachLine3Image_,
            &noteGuideEachLine4Image_,
            &noteGuideHoldEndImage_,
            &noteGuideHoldEachEndImage_,
            &noteGuideHoldBreakEndImage_,
            &noteGuideSlideImage_,
        }
    );
}

void PreviewCanvas::rebuildAtlas(QImage& atlasImage, const QVector<const QImage*>& images)
{
    atlasImage = QImage();

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
        return;
    }

    const int finalWidth = qMax(kAtlasPadding * 2 + 1, atlasWidth);
    const int finalHeight = qMax(kAtlasPadding * 2 + 1, y + rowHeight + kAtlasPadding);
    atlasImage = QImage(finalWidth, finalHeight, QImage::Format_ARGB32_Premultiplied);
    atlasImage.fill(Qt::transparent);

    QPainter atlasPainter(&atlasImage);
    atlasPainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Placement& placement : placements) {
        atlasPainter.drawImage(placement.rect.topLeft(), *placement.source);
        AtlasRegionRef region;
        region.atlasImage = &atlasImage;
        region.rect = placement.rect;
        atlasRegions_.insert(placement.source->cacheKey(), region);
    }
    atlasPainter.end();
}

bool PreviewCanvas::resolveAtlasImage(
    const QImage& image,
    const QRectF& sourceRect,
    const QImage*& atlasImage,
    QRectF& atlasSourceRect
) const
{
    const auto it = atlasRegions_.constFind(image.cacheKey());
    if (it == atlasRegions_.cend() || it.value().atlasImage == nullptr || it.value().atlasImage->isNull()) {
        atlasImage = &image;
        atlasSourceRect = sourceRect;
        return false;
    }

    atlasImage = it.value().atlasImage;
    const QRectF atlasRegion(it.value().rect);
    if (sourceRect.isValid() && !sourceRect.isEmpty()) {
        atlasSourceRect = QRectF(
            atlasRegion.left() + sourceRect.left(),
            atlasRegion.top() + sourceRect.top(),
            sourceRect.width(),
            sourceRect.height()
        );
    } else {
        atlasSourceRect = atlasRegion;
    }
    return true;
}

void PreviewCanvas::flushTapAtlasBatch(QPainter& painter)
{
    if (tapAtlasBatch_.isEmpty()) {
        return;
    }

    const bool hadNative = nativePaintingActive_;
    if (!hadNative && glRenderer_.isInitialized()) {
        painter.beginNativePainting();
        nativePaintingActive_ = true;
    }

    for (const BatchedSprite& sprite : tapAtlasBatch_) {
        if (sprite.image == nullptr || sprite.image->isNull()) {
            continue;
        }

        const QRectF targetRect(
            sprite.center.x() - sprite.targetWidth / 2.0,
            sprite.center.y() - sprite.targetHeight / 2.0,
            sprite.targetWidth,
            sprite.targetHeight
        );
        bool renderedByGl = false;
        if (nativePaintingActive_ && glRenderer_.isInitialized()) {
            renderedByGl = glRenderer_.drawImageQuad(
                *sprite.image,
                targetRect,
                sprite.angleDegrees,
                sprite.opacity,
                sprite.sourceRect
            );
        }

        if (renderedByGl) {
            usedGpuRendererThisFrame_ = true;
            continue;
        }

        if (nativePaintingActive_) {
            painter.endNativePainting();
            nativePaintingActive_ = false;
        }

        painter.save();
        painter.setOpacity(sprite.opacity);
        painter.translate(sprite.center);
        painter.rotate(sprite.angleDegrees);
        painter.drawImage(
            QRectF(-sprite.targetWidth / 2.0, -sprite.targetHeight / 2.0, sprite.targetWidth, sprite.targetHeight),
            *sprite.image,
            sprite.sourceRect
        );
        painter.restore();
        ++cpuFallbackCount_;

        if (hadNative && glRenderer_.isInitialized()) {
            painter.beginNativePainting();
            nativePaintingActive_ = true;
        }
    }

    if (!hadNative && nativePaintingActive_) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }

    tapAtlasBatch_.clear();
}

const QImage* PreviewCanvas::selectSlideStarImage(const TimelineNoteMarker& marker) const
{
    const bool slideLike = marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
    const bool useHeadFlags = slideLike && !marker.slideHeadUsesTapMaterial;
    const bool isBreak = useHeadFlags ? marker.headBreak : marker.isBreak;
    const bool isEach = useHeadFlags ? marker.headEach : marker.isEach;
    const bool useDouble = useHeadFlags ? marker.sameHeadSlide : marker.tapStarDouble;
    const QImage* starImage = &starImage_;
    if (isBreak) {
        if (useDouble && !starBreakDoubleImage_.isNull()) {
            starImage = &starBreakDoubleImage_;
        } else if (!starBreakImage_.isNull()) {
            starImage = &starBreakImage_;
        }
    } else if (isEach) {
        if (useDouble && !starEachDoubleImage_.isNull()) {
            starImage = &starEachDoubleImage_;
        } else if (!starEachImage_.isNull()) {
            starImage = &starEachImage_;
        } else if (useDouble && !starDoubleImage_.isNull()) {
            starImage = &starDoubleImage_;
        }
    } else if (useDouble && !starDoubleImage_.isNull()) {
        starImage = &starDoubleImage_;
    }
    return starImage;
}

const QImage* PreviewCanvas::selectSlideMovingStarImage(const TimelineNoteMarker& marker) const
{
    const QImage* starImage = &starImage_;
    if (marker.trackBreak && !starBreakImage_.isNull()) {
        starImage = &starBreakImage_;
    } else if (marker.slideEach && !starEachImage_.isNull()) {
        starImage = &starEachImage_;
    }
    return starImage;
}

qreal PreviewCanvas::slideStartupStarInitialScale(const QImage& starImage) const
{
    if (starImage.isNull()) {
        return kStarAssetScale;
    }

    const qreal headWidth = (!tapImage_.isNull() ? tapImage_.width() * kSkinAssetScale : starImage.width() * kStarAssetScale)
        * kSlideSpawnStarRelativeScale;
    return qMax<qreal>(0.01, headWidth / qMax(1, starImage.width()));
}

const QImage* PreviewCanvas::selectSlideTrackImage(const TimelineNoteMarker& marker) const
{
    const QImage* image = &slideTrackImage_;
    if (marker.trackBreak && !slideTrackBreakImage_.isNull()) {
        image = &slideTrackBreakImage_;
    } else if (marker.slideEach && !slideTrackEachImage_.isNull()) {
        image = &slideTrackEachImage_;
    }
    return image;
}

const QImage* PreviewCanvas::selectWifiTrackImage(const TimelineNoteMarker& marker, int sampleIndex, int sampleCount) const
{
    const QVector<QImage>* images = &wifiImages_;
    if (marker.trackBreak && !wifiBreakImages_.isEmpty()) {
        images = &wifiBreakImages_;
    } else if (marker.slideEach && !wifiEachImages_.isEmpty()) {
        images = &wifiEachImages_;
    }
    if (images->isEmpty()) {
        return nullptr;
    }
    const int maxIndex = images->size() - 1;
    const int sourceIndex = sampleCount <= 0
        ? qBound(0, sampleIndex, maxIndex)
        : sampleCount <= 1
        ? 0
        : qBound(0, qRound(static_cast<qreal>(sampleIndex) * maxIndex / qMax(1, sampleCount - 1)), maxIndex);
    return &images->at(sourceIndex);
}

QImage PreviewCanvas::cachedGuideTransform(const QImage& image, int targetWidth, int targetHeight, qreal angleDegrees)
{
    if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        return QImage();
    }
    const int width = quantizeDimension(targetWidth, kGuideTransformSizeStep);
    const int height = quantizeDimension(targetHeight, kGuideTransformSizeStep);
    const int angleBucket = qRound(angleDegrees);
    const QString key = QStringLiteral("%1|%2|%3|%4")
        .arg(static_cast<qulonglong>(image.cacheKey()))
        .arg(width)
        .arg(height)
        .arg(angleBucket);
    if (kEnablePreviewCaches) {
        const auto cached = guideTransformCache_.constFind(key);
        if (cached != guideTransformCache_.cend()) {
            return cached.value();
        }
    }

    QImage scaled = image;
    if (scaled.width() != width || scaled.height() != height) {
        scaled = scaled.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QImage transformed = scaled;
    if ((angleBucket % 360) != 0) {
        transformed = scaled.transformed(QTransform().rotate(angleBucket), Qt::SmoothTransformation);
    }
    if (kEnablePreviewCaches) {
        while (guideTransformCache_.size() >= kGuideTransformCacheLimit && !guideTransformCacheOrder_.isEmpty()) {
            const QString oldestKey = guideTransformCacheOrder_.takeFirst();
            guideTransformCache_.remove(oldestKey);
        }
        guideTransformCache_.insert(key, transformed);
        guideTransformCacheOrder_.append(key);
    }
    return transformed;
}

QImage PreviewCanvas::cachedSpriteTransform(const QImage& image, int targetWidth, int targetHeight, qreal angleDegrees)
{
    if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        return QImage();
    }
    const int width = quantizeDimension(targetWidth, kSpriteTransformSizeStep);
    const int height = quantizeDimension(targetHeight, kSpriteTransformSizeStep);
    const int angleBucket = qRound(angleDegrees);
    const QString key = QStringLiteral("%1|%2|%3|%4")
        .arg(static_cast<qulonglong>(image.cacheKey()))
        .arg(width)
        .arg(height)
        .arg(angleBucket);
    if (kEnablePreviewCaches) {
        const auto cached = spriteTransformCache_.constFind(key);
        if (cached != spriteTransformCache_.cend()) {
            return cached.value();
        }
    }

    QImage scaled = image;
    if (scaled.width() != width || scaled.height() != height) {
        scaled = scaled.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QImage transformed = scaled;
    if ((angleBucket % 360) != 0) {
        transformed = scaled.transformed(QTransform().rotate(angleBucket), Qt::SmoothTransformation);
    }
    if (kEnablePreviewCaches) {
        while (spriteTransformCache_.size() >= kSpriteTransformCacheLimit && !spriteTransformCacheOrder_.isEmpty()) {
            const QString oldestKey = spriteTransformCacheOrder_.takeFirst();
            spriteTransformCache_.remove(oldestKey);
        }
        spriteTransformCache_.insert(key, transformed);
        spriteTransformCacheOrder_.append(key);
    }
    return transformed;
}

