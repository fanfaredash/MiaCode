PreviewCanvas::PreviewCanvas(QWindow* parent)
    : QOpenGLWindow(NoPartialUpdate, parent)
{
    const QString outlinePath = defaultOutlinePath();
    if (QFileInfo::exists(outlinePath)) {
        outlineImage_ = QImage(outlinePath);
    }
    judgeEffectTapImage_ = buildJudgeEffectTapFallbackImage();
    judgeEffectTapSourceRect_ = nonTransparentBounds(judgeEffectTapImage_);
    judgeEffectTapBreakImage_ = buildJudgeEffectTapBreakFallbackImage();
    judgeEffectTapBreakSourceRect_ = nonTransparentBounds(judgeEffectTapBreakImage_);
    judgeEffectFireworkImage_ = buildJudgeEffectFireworkFallbackImage();
    judgeEffectFireworkSourceRect_ = nonTransparentBounds(judgeEffectFireworkImage_);
    judgeEffectFireworkColorBallImage_ = buildJudgeEffectFireworkColorBallFallbackImage();
    judgeEffectFireworkColorBallSourceRect_ = nonTransparentBounds(judgeEffectFireworkColorBallImage_);
}

PreviewCanvas::~PreviewCanvas()
{
    if (context() != nullptr) {
        makeCurrent();
        if (gpuTimerQueriesSupported_) {
            QOpenGLContext* ctx = QOpenGLContext::currentContext();
            QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
            if (extra != nullptr) {
                extra->glDeleteQueries(4, gpuTimeQueries_);
            }
        }
        glRenderer_.shutdown();
        doneCurrent();
    }
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
}

void PreviewCanvas::setVideoFrame(const QVideoFrame& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    mediaFrame_ = QImage();
    videoFrame_ = frame;
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
    QImage touchPointImage;
    QImage touchPointEachImage;
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
    result.touchPointImage = loadImageIfExists(dir.filePath("touch_point.png"));
    result.touchPointEachImage = loadImageIfExists(dir.filePath("touch_point_each.png"));
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
                &result.touchPointImage,
                &result.touchPointEachImage,
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

