#include "PreviewCanvas.h"
#include "common/AssetPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPointer>
#include <QRectF>
#include <QStringList>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QTransform>
#include <QtMath>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif

#include <algorithm>
#include <numeric>

namespace {
enum class SlideTrackTrimMode {
    AreaImmediate,
    UniformTime,
};

constexpr int kMargin = 0;
constexpr qreal kLogicalCanvasSize = 540.0;
constexpr qreal kLogicalCanvasCenter = kLogicalCanvasSize / 2.0;
constexpr qreal kLogicalDistanceTap = kLogicalCanvasSize * 122.5 / 1080.0;
constexpr qreal kLogicalDistanceEdge = kLogicalCanvasSize * 480.0 / 1080.0;
constexpr qreal kTapUnitsPerSecond = 540.0;
constexpr qreal kLaneUnitVectorBaseDegrees = -67.5;
constexpr qreal kLaneRotationBaseDegrees = 22.5;
constexpr qreal kLaneAngleStepDegrees = 45.0;
constexpr qreal kDistanceToScaleSlope = 0.008;
constexpr qreal kDistanceToScaleOffset = 0.51;
constexpr qreal kSlideStarFadeBaseScale = 0.45;
constexpr qreal kSlideStarFadeScaleDelta = 0.55;
constexpr qreal kSkinAssetScale = 0.5;
constexpr qreal kStarAssetScale = 90.0 / 126.0;
constexpr qreal kSlideSpawnStarRelativeScale = 1.08;
constexpr qreal kLogicalOutlineInset = 25.0;
constexpr qreal kNoteGuideSourceRadius = 240.75;
constexpr qreal kEachLine1SourceRadius = 240.02;
constexpr qreal kEachLine2SourceRadius = 239.89;
constexpr qreal kEachLine3SourceRadius = 239.65;
constexpr qreal kEachLine4SourceRadius = 239.92;
constexpr int kHoldTargetWidth = 60;
constexpr int kFpsSampleWindowMs = 250;
constexpr int kFrameStatsWindowSize = 120;
constexpr qreal kSlideTrackScale = 0.5;
constexpr qreal kSlideTrackFadeInSeconds = 0.2;
constexpr qreal kTouchDurationSeconds = 0.5;
constexpr qreal kTouchAppearPhase = 0.25;
constexpr qreal kTouchStartOffset = 30.0;
constexpr qreal kTouchClosedOffset = 12.0;
constexpr qreal kTouchHoldStartOffset = 24.0;
constexpr qreal kTouchHoldClosedOffset = 8.0;
constexpr qreal kTouchAssetScale = 0.5;
constexpr SlideTrackTrimMode kSlideTrackTrimMode = SlideTrackTrimMode::UniformTime;
constexpr qreal kAngleWrapOffset = 540.0;
constexpr qreal kAngleWrapCycle = 360.0;
constexpr qreal kAngleWrapCenter = 180.0;
constexpr bool kEnablePreviewCaches = true;
constexpr int kGuideTransformCacheLimit = 256;
constexpr int kSpriteTransformCacheLimit = 512;
constexpr int kGuideTransformSizeStep = 2;
constexpr int kSpriteTransformSizeStep = 4;
constexpr int kAtlasPadding = 2;
constexpr int kAtlasMaxWidth = 2048;

struct SampleStats {
    bool hasValue = false;
    double avgMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
};

SampleStats computeSampleStats(const QVector<double>& samples)
{
    SampleStats stats;
    if (samples.isEmpty()) {
        return stats;
    }

    stats.hasValue = true;
    const double sum = std::accumulate(samples.cbegin(), samples.cend(), 0.0);
    stats.avgMs = sum / static_cast<double>(samples.size());

    QVector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    stats.maxMs = sorted.constLast();
    const int p95Index = qBound(0, static_cast<int>(qCeil(sorted.size() * 0.95)) - 1, sorted.size() - 1);
    stats.p95Ms = sorted.at(p95Index);
    return stats;
}

QString formatHudTimeLabel(double seconds)
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(seconds * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 sec = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    return QString("%1:%2:%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

QFont hudMonoFont(int pointSize, QFont::Weight weight = QFont::Medium)
{
    QFont font;
    for (const QString& family : QStringList{"Cascadia Mono", "JetBrains Mono", "Cascadia Code", "Consolas"}) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

int quantizeDimension(int value, int step)
{
    const int clampedValue = qMax(1, value);
    if (step <= 1) {
        return clampedValue;
    }
    return qMax(step, ((clampedValue + step / 2) / step) * step);
}

QPointF interpolatePoint(const QVector<QPointF>& points, qreal proportion)
{
    if (points.isEmpty()) {
        return QPointF();
    }
    if (points.size() == 1) {
        return points.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (points.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), points.size() - 2);
    const qreal t = scaled - index;
    const QPointF& a = points[index];
    const QPointF& b = points[index + 1];
    return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
}

qreal interpolateAngle(const QVector<double>& angles, qreal proportion)
{
    if (angles.isEmpty()) {
        return 0.0;
    }
    if (angles.size() == 1) {
        return angles.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (angles.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), angles.size() - 2);
    const qreal t = scaled - index;
    const qreal a = angles[index];
    const qreal b = angles[index + 1];
    qreal delta = std::fmod(b - a + kAngleWrapOffset, kAngleWrapCycle) - kAngleWrapCenter;
    return a + delta * t;
}

int currentAreaIndexForProportion(const QVector<double>& thresholds, qreal proportion, int areaCount)
{
    if (areaCount <= 1) {
        return 0;
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    if (!thresholds.isEmpty()) {
        const int limit = qMin(areaCount, thresholds.size());
        int currentArea = 0;
        for (int nextArea = 1; nextArea < limit; ++nextArea) {
            if (clamped >= thresholds[nextArea]) {
                currentArea = nextArea;
            } else {
                break;
            }
        }
        return qBound(0, currentArea, areaCount - 1);
    }
    return qBound(0, static_cast<int>(qFloor(clamped * (areaCount - 1))), areaCount - 1);
}

QPointF laneUnitVector(int lane)
{
    if (lane < 1 || lane > 8) {
        return QPointF(0.0, 0.0);
    }
    const qreal angleDeg = kLaneUnitVectorBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
    const qreal angleRad = qDegreesToRadians(angleDeg);
    return QPointF(
        qCos(angleRad),
        qSin(angleRad)
    );
}

QPointF mapLogicalPointToRect(const QPointF& logicalPoint, const QRectF& targetRect)
{
    const qreal scale = targetRect.width() / kLogicalCanvasSize;
    return QPointF(
        targetRect.left() + logicalPoint.x() * scale,
        targetRect.top() + logicalPoint.y() * scale
    );
}

qreal mapLogicalLengthToRect(qreal logicalLength, const QRectF& targetRect)
{
    return logicalLength * (targetRect.width() / kLogicalCanvasSize);
}

qreal tapScaleForDistance(qreal distance)
{
    return distance * kDistanceToScaleSlope + kDistanceToScaleOffset;
}

qreal laneRotationDegrees(int lane)
{
    if (lane < 1 || lane > 8) {
        return 0.0;
    }
    return kLaneRotationBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
}

qreal laneRotationDegreesForIndex(qreal laneIndex)
{
    return (kLaneRotationBaseDegrees - kLaneAngleStepDegrees) + laneIndex * kLaneAngleStepDegrees;
}

QColor tapColorForMarker(const TimelineNoteMarker& marker)
{
    if (marker.headBreak || marker.isBreak) {
        return QColor("#F39C12");
    }
    // Slide/wifi head stars use `headEach`; the trace body uses `slideEach`.
    // `isEach` is intentionally ignored for slide-like notes to avoid overlap.
    const bool each = (marker.type == "slide" || marker.type == "wifi") ? marker.headEach : marker.isEach;
    if (each) {
        return QColor("#3FD7FF");
    }
    return QColor("#F7E45C");
}

QColor exTintColor(bool isBreak, bool isEach)
{
    if (isBreak) {
        return QColor("#F59E0B");
    }
    if (isEach) {
        return QColor("#FFF05C");
    }
    return QColor("#FF9FD6");
}

QColor exStarTintColor(bool isBreak, bool isEach)
{
    if (isBreak) {
        return QColor("#F59E0B");
    }
    if (isEach) {
        return QColor("#FFF05C");
    }
    return QColor("#6FB6FF");
}

QString defaultOutlinePath()
{
    return miacode::assets::assetPath("background/outline.png");
}

QString defaultNoteGuideDir()
{
    return miacode::assets::assetPath("noteguide");
}

bool previewStartupTimingEnabled()
{
    static const bool enabled = []() {
        const QString raw = qEnvironmentVariable(
            "MIACODE_ENABLE_STARTUP_TIMING",
            qEnvironmentVariable("MAIMURI_ENABLE_STARTUP_TIMING")
        ).trimmed();
        return raw == "1" || raw.compare("true", Qt::CaseInsensitive) == 0;
    }();
    return enabled;
}

QString startupTimingLogPath()
{
    return QDir::temp().filePath("miacode_startup_timing.log");
}

void appendPreviewStartupTiming(const QString& stage, qint64 deltaMs)
{
    if (!previewStartupTimingEnabled()) {
        return;
    }
    static QElapsedTimer timer;
    static qint64 lastMs = 0;
    if (!timer.isValid()) {
        timer.start();
        lastMs = 0;
    }
    const qint64 elapsedMs = timer.elapsed();
    const qint64 resolvedDeltaMs = deltaMs >= 0 ? deltaMs : (elapsedMs - lastMs);
    lastMs = elapsedMs;

    QFile logFile(startupTimingLogPath());
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&logFile);
    out << "stage=" << stage
        << ", elapsed_ms=" << elapsedMs
        << ", delta_ms=" << resolvedDeltaMs
        << "\n";
}
}

PreviewCanvas::PreviewCanvas(QWindow* parent)
    : QOpenGLWindow(NoPartialUpdate, parent)
{
    const QString outlinePath = defaultOutlinePath();
    if (QFileInfo::exists(outlinePath)) {
        outlineImage_ = QImage(outlinePath);
    }
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
    touchPointImage_ = std::move(result.touchPointImage);
    touchPointEachImage_ = std::move(result.touchPointEachImage);
    touchHold0Image_ = std::move(result.touchHold0Image);
    touchHold1Image_ = std::move(result.touchHold1Image);
    touchHold2Image_ = std::move(result.touchHold2Image);
    touchHold3Image_ = std::move(result.touchHold3Image);
    touchHoldBorderImage_ = std::move(result.touchHoldBorderImage);
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
    const double clamped = qBound(0.0, brightness, 1.0);
    if (qFuzzyCompare(backgroundBrightness_ + 1.0, clamped + 1.0)) {
        return;
    }
    backgroundBrightness_ = clamped;
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
    const QImage* tapImage = &tapImage_;
    if ((marker.headBreak || marker.isBreak) && !tapBreakImage_.isNull()) {
        tapImage = &tapBreakImage_;
    } else if (marker.isEach && !tapEachImage_.isNull()) {
        tapImage = &tapEachImage_;
    }
    return tapImage;
}

const QImage* PreviewCanvas::selectHoldImage(const TimelineNoteMarker& marker) const
{
    const QImage* holdImage = &holdImage_;
    if ((marker.headBreak || marker.isBreak) && !holdBreakImage_.isNull()) {
        holdImage = &holdBreakImage_;
    } else if (marker.isEach && !holdEachImage_.isNull()) {
        holdImage = &holdEachImage_;
    }
    return holdImage;
}

const QImage* PreviewCanvas::selectTapNoteGuideImage(const TimelineNoteMarker& marker) const
{
    const bool slideHeadStar = marker.type == "slide" || marker.type == "wifi";
    if (slideHeadStar) {
        if (!marker.hasHeadStar) {
            return nullptr;
        }
        if (marker.headBreak && !noteGuideBreakImage_.isNull()) {
            return &noteGuideBreakImage_;
        }
        if (marker.headEach && !noteGuideEachImage_.isNull()) {
            return &noteGuideEachImage_;
        }
        return noteGuideSlideImage_.isNull() ? &noteGuideNormalImage_ : &noteGuideSlideImage_;
    }
    if (marker.isBreak && !noteGuideBreakImage_.isNull()) {
        return &noteGuideBreakImage_;
    }
    if (marker.isEach && !noteGuideEachImage_.isNull()) {
        return &noteGuideEachImage_;
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
            &touchPointImage_,
            &touchPointEachImage_,
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
    const QImage* starImage = &starImage_;
    if (marker.headBreak) {
        if (marker.sameHeadSlide && !starBreakDoubleImage_.isNull()) {
            starImage = &starBreakDoubleImage_;
        } else if (!starBreakImage_.isNull()) {
            starImage = &starBreakImage_;
        }
    } else if (marker.headEach) {
        if (marker.sameHeadSlide && !starEachDoubleImage_.isNull()) {
            starImage = &starEachDoubleImage_;
        } else if (!starEachImage_.isNull()) {
            starImage = &starEachImage_;
        } else if (marker.sameHeadSlide && !starDoubleImage_.isNull()) {
            starImage = &starDoubleImage_;
        }
    } else if (marker.sameHeadSlide && !starDoubleImage_.isNull()) {
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

QRectF PreviewCanvas::currentStageRect() const
{
    return QRectF(
        kMargin,
        kMargin,
        qMax<qreal>(1.0, width() - kMargin * 2),
        qMax<qreal>(1.0, height() - kMargin * 2)
    );
}

QRectF PreviewCanvas::stagePlayfieldRect(const QRectF& stageRect) const
{
    const QRectF innerRect = stageRect.adjusted(18.0, 18.0, -18.0, -18.0);
    const qreal playfieldSide = qMin<qreal>(qMin(innerRect.width(), innerRect.height()), kLogicalCanvasSize);
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
    painter.fillRect(stageRect, QColor("#1F2833"));

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

    if (!mediaSize.isEmpty()) {
        QSize fittedSize = mediaSize;
        const QSize mediaBounds(
            qMax(1, qRound(stageRect.width())),
            qMax(1, qRound(stageRect.height()))
        );
        fittedSize.scale(mediaBounds, Qt::KeepAspectRatioByExpanding);
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

    const int darkAlpha = qBound(0, qRound((1.0 - backgroundBrightness_) * 255.0), 255);
    if (darkAlpha > 0) {
        painter.fillRect(stageRect, QColor(0, 0, 0, darkAlpha));
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

void PreviewCanvas::drawTouchLayer(QPainter& painter, const QRectF& playfieldRect)
{
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "touch") {
            drawTouchMarker(painter, marker, playfieldRect);
        } else if (marker.type == "touch_hold") {
            drawTouchHoldMarker(painter, marker, playfieldRect);
        }
    }
}

void PreviewCanvas::drawTrackLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "slide") {
            drawSlideTrack(painter, marker, playfieldRect);
        } else if (marker.type == "wifi") {
            drawWifiTrack(painter, marker, playfieldRect);
        }
    }
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawGuideLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    drawNoteGuides(painter, playfieldRect);
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawHoldLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "hold") {
            drawHoldMarker(painter, marker, playfieldRect);
        }
    }
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawTapLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    tapAtlasBatchingActive_ = glRenderer_.isInitialized();
    tapAtlasBatch_.clear();
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "slide") {
            drawSlideMarker(painter, marker, playfieldRect);
        } else if (marker.type == "wifi") {
            drawWifiMarker(painter, marker, playfieldRect);
        }
    }
    flushTapAtlasBatch(painter);
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "tap" || marker.type == "slide" || marker.type == "wifi") {
            drawTapMarker(painter, marker, playfieldRect);
        }
    }
    flushTapAtlasBatch(painter);
    tapAtlasBatchingActive_ = false;
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawHud(QPainter& painter, const QRectF& stageRect)
{
    painter.setPen(QColor("#D9E2EC"));
    const qreal hudPadding = qBound<qreal>(10.0, stageRect.width() * 0.028, 18.0);
    const int timeFontPointSize = qBound(12, qRound(stageRect.width() * 0.03), 20);
    const int debugFontPointSize = qBound(8, qRound(stageRect.width() * 0.019), 13);
    QFont timeFont = hudMonoFont(timeFontPointSize, QFont::DemiBold);
    if (!showDebugInfo_) {
        painter.setFont(timeFont);
        painter.drawText(
            QPointF(stageRect.left() + hudPadding, stageRect.bottom() - hudPadding),
            formatHudTimeLabel(playheadSeconds_)
        );
        return;
    }
    QFont fpsFont = hudMonoFont(debugFontPointSize, QFont::Medium);
    painter.setFont(fpsFont);
    const QFontMetrics metrics(fpsFont);
    const qreal leftX = stageRect.left() + hudPadding;
    const qreal baseline0 = stageRect.top() + hudPadding + metrics.ascent();
    const QString rendererLabel = usedGpuRendererThisFrame_ ? "Renderer: GPU" : "Renderer: CPU";
    painter.drawText(
        QPointF(leftX, baseline0),
        rendererLabel
    );
    painter.drawText(
        QPointF(leftX, baseline0 + metrics.height()),
        QString::number(fpsDisplay_, 'f', 1) + " FPS"
    );
    painter.drawText(
        QPointF(leftX, baseline0 + metrics.height() * 2),
        QString("Fallback: %1")
            .arg(cpuFallbackCount_)
    );

    painter.setFont(timeFont);
    painter.drawText(
        QPointF(stageRect.left() + hudPadding, stageRect.bottom() - hudPadding),
        formatHudTimeLabel(playheadSeconds_)
    );
}

bool PreviewCanvas::drawSpriteImage(
    QPainter& painter,
    const QImage& image,
    const QPointF& center,
    int targetWidth,
    int targetHeight,
    qreal angleDegrees,
    qreal opacity,
    const QRectF& sourceRect
)
{
    if (opacity <= 0.0) {
        return true;
    }
    if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    const QImage* renderImage = &image;
    QRectF resolvedSourceRect = sourceRect;
    resolveAtlasImage(image, sourceRect, renderImage, resolvedSourceRect);
    if (renderImage == nullptr || renderImage->isNull()) {
        return false;
    }
    if (tapAtlasBatchingActive_ && !tapAtlasBatch_.isEmpty() && renderImage != &tapAtlasImage_) {
        flushTapAtlasBatch(painter);
    }
    if (tapAtlasBatchingActive_ && renderImage == &tapAtlasImage_) {
        BatchedSprite sprite;
        sprite.image = renderImage;
        sprite.center = center;
        sprite.targetWidth = targetWidth;
        sprite.targetHeight = targetHeight;
        sprite.angleDegrees = angleDegrees;
        sprite.opacity = opacity;
        sprite.sourceRect = resolvedSourceRect;
        tapAtlasBatch_.append(sprite);
        return true;
    }

    const QRectF targetRect(
        center.x() - targetWidth / 2.0,
        center.y() - targetHeight / 2.0,
        targetWidth,
        targetHeight
    );
    bool renderedByGl = false;
    if (glRenderer_.isInitialized()) {
        if (nativePaintingActive_) {
            renderedByGl = glRenderer_.drawImageQuad(*renderImage, targetRect, angleDegrees, opacity, resolvedSourceRect);
        } else {
            painter.beginNativePainting();
            renderedByGl = glRenderer_.drawImageQuad(*renderImage, targetRect, angleDegrees, opacity, resolvedSourceRect);
            painter.endNativePainting();
        }
    }
    if (renderedByGl) {
        usedGpuRendererThisFrame_ = true;
        return true;
    }
    if (nativePaintingActive_) {
        return false;
    }

    if (resolvedSourceRect.isValid() && !resolvedSourceRect.isEmpty()) {
        ++cpuFallbackCount_;
        painter.save();
        painter.setOpacity(opacity);
        painter.translate(center);
        painter.rotate(angleDegrees);
        painter.drawImage(
            QRectF(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight),
            *renderImage,
            resolvedSourceRect
        );
        painter.restore();
        return true;
    }

    const QImage transformed = cachedSpriteTransform(*renderImage, targetWidth, targetHeight, angleDegrees);
    if (transformed.isNull()) {
        return false;
    }
    ++cpuFallbackCount_;
    painter.save();
    painter.drawImage(
        QPointF(center.x() - transformed.width() / 2.0, center.y() - transformed.height() / 2.0),
        transformed
    );
    painter.restore();
    return true;
}

void PreviewCanvas::warmTransformCachesForCurrentSize()
{
    if (!kEnablePreviewCaches) {
        return;
    }

    guideTransformCache_.clear();
    guideTransformCacheOrder_.clear();
    spriteTransformCache_.clear();
    spriteTransformCacheOrder_.clear();

    const QRectF playfieldRect = currentPlayfieldRect();
    const int side = qMax(1, qRound(playfieldRect.width()));
    if (side <= 0) {
        return;
    }
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const qreal tapBaseScale = canvasScale * kSkinAssetScale;

    const int laneAngles[] = {23, 68, 113, 158, 203, 248, 293, 338};

    const auto warmSprite = [this](const QImage& image, int targetWidth, int targetHeight, int angleDegrees) {
        if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
            return;
        }
        cachedSpriteTransform(image, targetWidth, targetHeight, angleDegrees);
    };
    const auto warmGuide = [this](const QImage& image, int targetWidth, int targetHeight, int angleDegrees) {
        if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
            return;
        }
        cachedGuideTransform(image, targetWidth, targetHeight, angleDegrees);
    };

    const QImage* tapSprites[] = {
        &tapImage_, &tapEachImage_, &tapBreakImage_
    };
    for (const QImage* image : tapSprites) {
        if (image == nullptr || image->isNull()) {
            continue;
        }
        const int minWidth = quantizeDimension(qRound(image->width() * tapBaseScale * tapScaleForDistance(0.0)), kSpriteTransformSizeStep);
        const int maxWidth = quantizeDimension(qRound(image->width() * tapBaseScale), kSpriteTransformSizeStep);
        const qreal aspect = static_cast<qreal>(image->height()) / qMax(1, image->width());
        for (int targetWidth = minWidth; targetWidth <= maxWidth; targetWidth += kSpriteTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * aspect));
            for (int angle : laneAngles) {
                warmSprite(*image, targetWidth, targetHeight, angle);
            }
        }
    }

    const QImage* starSprites[] = {
        &starImage_, &starEachImage_, &starBreakImage_
    };
    const qreal tapSizedStarWidth = (!tapImage_.isNull() ? tapImage_.width() * kSkinAssetScale : 90.0) * kSlideSpawnStarRelativeScale;
    const qreal tapSizedStarHeight = (!tapImage_.isNull() ? tapImage_.height() * kSkinAssetScale : 90.0) * kSlideSpawnStarRelativeScale;
    for (const QImage* image : starSprites) {
        if (image == nullptr || image->isNull()) {
            continue;
        }
        const int spawnMinWidth = quantizeDimension(qRound(tapSizedStarWidth * canvasScale * tapScaleForDistance(0.0)), kSpriteTransformSizeStep);
        const int spawnMaxWidth = quantizeDimension(qRound(tapSizedStarWidth * canvasScale), kSpriteTransformSizeStep);
        const qreal spawnAspect = tapSizedStarHeight / qMax<qreal>(1.0, tapSizedStarWidth);
        for (int targetWidth = spawnMinWidth; targetWidth <= spawnMaxWidth; targetWidth += kSpriteTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * spawnAspect));
            for (int angle : laneAngles) {
                warmSprite(*image, targetWidth, targetHeight, angle);
            }
        }
        const int waitMinWidth = quantizeDimension(qRound(image->width() * canvasScale * slideStartupStarInitialScale(*image)), kSpriteTransformSizeStep);
        const int waitMaxWidth = quantizeDimension(qRound(image->width() * canvasScale * kStarAssetScale), kSpriteTransformSizeStep);
        const qreal waitAspect = static_cast<qreal>(image->height()) / qMax(1, image->width());
        for (int targetWidth = waitMinWidth; targetWidth <= waitMaxWidth; targetWidth += kSpriteTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * waitAspect));
            for (int angle = 0; angle < 360; angle += 15) {
                warmSprite(*image, targetWidth, targetHeight, angle);
            }
        }
    }

    const struct {
        const QImage* image;
        qreal sourceRadius;
    } guideSprites[] = {
        {&noteGuideNormalImage_, kNoteGuideSourceRadius},
        {&noteGuideBreakImage_, kNoteGuideSourceRadius},
        {&noteGuideEachImage_, kNoteGuideSourceRadius},
        {&noteGuideSlideImage_, kNoteGuideSourceRadius},
        {&noteGuideEachLine1Image_, kEachLine1SourceRadius},
        {&noteGuideEachLine2Image_, kEachLine2SourceRadius},
        {&noteGuideEachLine3Image_, kEachLine3SourceRadius},
        {&noteGuideEachLine4Image_, kEachLine4SourceRadius},
    };
    for (const auto& guide : guideSprites) {
        if (guide.image == nullptr || guide.image->isNull() || guide.sourceRadius <= 0.0) {
            continue;
        }
        const qreal minScale = kLogicalDistanceTap / guide.sourceRadius;
        const qreal maxScale = kLogicalDistanceEdge / guide.sourceRadius;
        const int minWidth = quantizeDimension(qRound(guide.image->width() * canvasScale * minScale), kGuideTransformSizeStep);
        const int maxWidth = quantizeDimension(qRound(guide.image->width() * canvasScale * maxScale), kGuideTransformSizeStep);
        const qreal aspect = static_cast<qreal>(guide.image->height()) / qMax(1, guide.image->width());
        for (int targetWidth = minWidth; targetWidth <= maxWidth; targetWidth += kGuideTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * aspect));
            for (int angle = 0; angle < 360; angle += 15) {
                warmGuide(*guide.image, targetWidth, targetHeight, angle);
            }
        }
    }
}

void PreviewCanvas::prebuildTrackAreaCachesForCurrentState()
{
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
    if (!kEnablePreviewCaches || noteMarkers_.isEmpty()) {
        return;
    }

    const QRectF playfieldRect = currentPlayfieldRect();
    if (playfieldRect.width() <= 0.0 || playfieldRect.height() <= 0.0) {
        return;
    }

    QImage dummy(1, 1, QImage::Format_ARGB32_Premultiplied);
    dummy.fill(Qt::transparent);
    QPainter painter(&dummy);

    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "slide") {
            const QImage* image = selectSlideTrackImage(marker);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            for (int segmentIndex = 0; segmentIndex < marker.slideTrackAreaPoints.size(); ++segmentIndex) {
                const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                const QVector<QVector<int>>& areaCuts = marker.slideTrackAreaCutIndices.value(segmentIndex);
                for (int areaIndex = 0; areaIndex < areas.size(); ++areaIndex) {
                    drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image);
                    const QVector<int>& cuts = areaCuts.value(areaIndex);
                    for (int cut : cuts) {
                        drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, cut, playfieldRect, image);
                    }
                }
            }
        } else if (marker.type == "wifi") {
            for (int areaIndex = 0; areaIndex < marker.wifiTrackAreaPoints.size(); ++areaIndex) {
                drawCachedWifiArea(painter, marker, areaIndex, 0, playfieldRect);
                const int areaSize = marker.wifiTrackAreaPoints[areaIndex].size();
                const QVector<double>& checkpoints = marker.wifiTrackAreaCheckpoints.value(areaIndex);
                for (int passed = 1; passed <= checkpoints.size(); ++passed) {
                    const int localCut = qBound(
                        0,
                        qFloor(static_cast<qreal>(passed) * areaSize / qMax(1, checkpoints.size())),
                        areaSize
                    );
                    drawCachedWifiArea(painter, marker, areaIndex, localCut, playfieldRect);
                }
            }
        }
    }
}

void PreviewCanvas::drawNoteGuides(QPainter& painter, const QRectF& playfieldRect)
{
    struct ActiveEachCandidate {
        const TimelineNoteMarker* marker = nullptr;
    };

    auto renderGuideImage = [this, &painter, &playfieldRect](
        const QImage* image,
        qreal scale,
        qreal angleDegrees,
        const QPointF& logicalPos,
        bool preferGpu,
        qreal gpuAngleOffset
    ) {
        if (image == nullptr || image->isNull() || scale <= 0.0) {
            return;
        }
        const QImage* renderImage = image;
        QRectF renderSourceRect;
        const bool atlasResolved = resolveAtlasImage(*image, QRectF(), renderImage, renderSourceRect);
        if (renderImage == nullptr || renderImage->isNull()) {
            return;
        }
        const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
        const QPointF point = mapLogicalPointToRect(logicalPos, playfieldRect);
        const int targetWidth = qMax(1, qRound(image->width() * canvasScale * scale));
        const int targetHeight = qMax(1, qRound(image->height() * canvasScale * scale));
        const QRectF targetRect(
            point.x() - targetWidth / 2.0,
            point.y() - targetHeight / 2.0,
            targetWidth,
            targetHeight
        );
        bool renderedByGl = false;
        if (preferGpu && glRenderer_.isInitialized()) {
            if (nativePaintingActive_) {
                renderedByGl = glRenderer_.drawImageQuad(
                    *renderImage,
                    targetRect,
                    angleDegrees + gpuAngleOffset,
                    1.0,
                    renderSourceRect
                );
            } else {
                painter.beginNativePainting();
                renderedByGl = glRenderer_.drawImageQuad(*renderImage, targetRect, angleDegrees + gpuAngleOffset, 1.0, renderSourceRect);
                painter.endNativePainting();
            }
        }
        if (renderedByGl) {
            usedGpuRendererThisFrame_ = true;
            return;
        }
        if (nativePaintingActive_) {
            return;
        }

        if (atlasResolved && renderSourceRect.isValid() && !renderSourceRect.isEmpty()) {
            ++cpuFallbackCount_;
            painter.save();
            painter.translate(point);
            painter.rotate(angleDegrees);
            painter.drawImage(
                QRectF(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight),
                *renderImage,
                renderSourceRect
            );
            painter.restore();
            return;
        }

        const QImage transformed = cachedGuideTransform(*image, targetWidth, targetHeight, angleDegrees);
        if (transformed.isNull()) {
            return;
        }
        ++cpuFallbackCount_;
        painter.drawImage(
            QPointF(
                point.x() - transformed.width() / 2.0,
                point.y() - transformed.height() / 2.0
            ),
            transformed
        );
    };

    auto renderConcentricGuide = [&renderGuideImage](
        const QImage* image,
        qreal distance,
        qreal sourceRadius,
        qreal angleDegrees,
        bool preferGpu = true,
        qreal gpuAngleOffset = 0.0
    ) {
        if (distance <= 0.0 || sourceRadius <= 0.0) {
            return;
        }
        const qreal guideRadius = distance < kLogicalDistanceTap
            ? kLogicalDistanceTap
            : qMin(distance, kLogicalDistanceEdge);
        const qreal scale = guideRadius / sourceRadius;
        renderGuideImage(
            image,
            scale,
            angleDegrees,
            QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter),
            preferGpu,
            gpuAngleOffset
        );
    };

    auto renderTailGuide = [&renderGuideImage](const QImage* image, int lane, qreal distance) {
        const qreal scale = qMax<qreal>(0.0, tapScaleForDistance(distance));
        if (scale <= 0.0) {
            return;
        }
        const QPointF unit = laneUnitVector(lane);
        const qreal renderedDistance = distance < kLogicalDistanceTap
            ? kLogicalDistanceTap
            : qMin(distance, kLogicalDistanceEdge);
        const QPointF logicalPos(
            kLogicalCanvasCenter + unit.x() * renderedDistance,
            kLogicalCanvasCenter + unit.y() * renderedDistance
        );
        renderGuideImage(image, scale, laneRotationDegrees(lane), logicalPos, true, 0.0);
    };

    QHash<qint64, QVector<ActiveEachCandidate>> eachGroups;
    const auto addEachCandidate = [&eachGroups](const TimelineNoteMarker& marker) {
        const qint64 key = qRound64(marker.second * 1000000.0);
        eachGroups[key].append(ActiveEachCandidate{&marker});
    };

    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "tap" || marker.type == "slide" || marker.type == "wifi") {
            const bool slideHeadStar = marker.type == "slide" || marker.type == "wifi";
            if (slideHeadStar && !marker.hasHeadStar) {
                continue;
            }
            if (playheadSeconds_ > marker.second) {
                continue;
            }
            const qreal distance = static_cast<qreal>(playheadSeconds_ - marker.second) * kTapUnitsPerSecond + kLogicalDistanceEdge;
            renderConcentricGuide(
                selectTapNoteGuideImage(marker),
                distance,
                kNoteGuideSourceRadius,
                laneRotationDegrees(marker.lane),
                true,
                0.0
            );
            const bool inEachGroup = slideHeadStar ? marker.headEach : marker.isEach;
            if (inEachGroup) {
                addEachCandidate(marker);
            }
        } else if (marker.type == "hold") {
            if (marker.endSecond < marker.second || playheadSeconds_ > marker.endSecond) {
                continue;
            }
            const qreal distance = static_cast<qreal>(playheadSeconds_ - marker.second) * kTapUnitsPerSecond + kLogicalDistanceEdge;
            const QImage* headImage = marker.isBreak ? &noteGuideBreakImage_
                : marker.isEach ? &noteGuideEachImage_
                : &noteGuideNormalImage_;
            renderConcentricGuide(headImage, distance, kNoteGuideSourceRadius, laneRotationDegrees(marker.lane), true, 0.0);

            const qreal distanceEnd = static_cast<qreal>(playheadSeconds_ - marker.endSecond) * kTapUnitsPerSecond + kLogicalDistanceEdge;
            if (distanceEnd >= kLogicalDistanceTap) {
                renderTailGuide(selectHoldEndNoteGuideImage(marker), marker.lane, distanceEnd);
            }
            if (marker.isEach) {
                addEachCandidate(marker);
            }
        }
    }

    for (auto it = eachGroups.cbegin(); it != eachGroups.cend(); ++it) {
        const QVector<ActiveEachCandidate>& notes = it.value();
        if (notes.size() < 2) {
            continue;
        }

        const int groupSize = notes.size();
        int laneDistance = 4;
        if (groupSize == 2) {
            const int a = notes[0].marker->lane;
            const int b = notes[1].marker->lane;
            const int delta = qAbs(a - b);
            laneDistance = qMin(delta, 8 - delta);
        }

        const QImage* lineImage = nullptr;
        qreal sourceRadius = kEachLine4SourceRadius;
        if (groupSize >= 3 || laneDistance == 4) {
            lineImage = &noteGuideEachLine4Image_;
            sourceRadius = kEachLine4SourceRadius;
        } else if (laneDistance == 3) {
            lineImage = &noteGuideEachLine3Image_;
            sourceRadius = kEachLine3SourceRadius;
        } else if (laneDistance == 2) {
            lineImage = &noteGuideEachLine2Image_;
            sourceRadius = kEachLine2SourceRadius;
        } else if (laneDistance == 1) {
            lineImage = &noteGuideEachLine1Image_;
            sourceRadius = kEachLine1SourceRadius;
        }
        if (lineImage == nullptr || lineImage->isNull()) {
            continue;
        }

        const qreal distance = static_cast<qreal>(playheadSeconds_ - notes[0].marker->second) * kTapUnitsPerSecond + kLogicalDistanceEdge;
        if (distance <= 0.0) {
            continue;
        }

        qreal angleDegrees = 0.0;
        if (!(groupSize >= 3 || laneDistance == 4)) {
            const int a = notes[0].marker->lane;
            const int b = notes[1].marker->lane;
            int diff = (b - a) % 8;
            if (diff > 4) {
                diff -= 8;
            } else if (diff < -4) {
                diff += 8;
            }
            qreal midpoint = a + diff / 2.0;
            while (midpoint <= 0.0) {
                midpoint += 8.0;
            }
            while (midpoint > 8.0) {
                midpoint -= 8.0;
            }
            const qreal sourceMidpoint = 1.0 + laneDistance / 2.0;
            angleDegrees = laneRotationDegreesForIndex(midpoint) - laneRotationDegreesForIndex(sourceMidpoint);
        }

        renderConcentricGuide(lineImage, distance, sourceRadius, angleDegrees, true, 0.0);
    }
}

void PreviewCanvas::drawTapMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    const bool slideHeadStar = marker.type == "slide" || marker.type == "wifi";
    if (slideHeadStar && !marker.hasHeadStar) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    if (slideHeadStar ? deltaSeconds >= 0.0 : deltaSeconds > 0.0) {
        return;
    }

    const qreal distance = deltaSeconds * kTapUnitsPerSecond + kLogicalDistanceEdge;
    const qreal spawnScale = tapScaleForDistance(distance);
    if (spawnScale < 0.0) {
        return;
    }

    const QPointF unit = laneUnitVector(marker.lane);
    const bool parked = distance < kLogicalDistanceTap;
    const qreal renderedDistance = parked ? kLogicalDistanceTap : distance;
    const qreal effectiveScale = parked ? spawnScale : 1.0;
    const QPointF logicalPoint(
        kLogicalCanvasCenter + unit.x() * renderedDistance,
        kLogicalCanvasCenter + unit.y() * renderedDistance
    );
    const QPointF point = mapLogicalPointToRect(logicalPoint, playfieldRect);
    const QImage* tapImage = slideHeadStar ? selectSlideStarImage(marker) : selectTapImage(marker);
    QImage renderImage = tapImage != nullptr ? *tapImage : QImage();
    if (slideHeadStar && marker.headEx && !renderImage.isNull()) {
        const QImage& overlay = (marker.sameHeadSlide && !starExDoubleImage_.isNull()) ? starExDoubleImage_ : starExImage_;
        if (!overlay.isNull()) {
            const QColor tint = exStarTintColor(marker.headBreak, marker.headEach);
            renderImage = composeOverlay(renderImage, overlay, 0.82, 0.18, nullptr, &tint);
        }
    } else if (!slideHeadStar && marker.isEx && !renderImage.isNull() && !tapExImage_.isNull()) {
        const QColor tint = exTintColor(marker.isBreak, marker.isEach);
        renderImage = composeOverlay(renderImage, tapExImage_, 0.82, 0.18, nullptr, &tint);
    }

    if (!renderImage.isNull()) {
        const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
        int targetWidth = 0;
        int targetHeight = 0;
        if (slideHeadStar) {
            const qreal baseWidth = (!tapImage_.isNull() ? tapImage_.width() * kSkinAssetScale : renderImage.width() * kStarAssetScale)
                * kSlideSpawnStarRelativeScale;
            const qreal baseHeight = (!tapImage_.isNull() ? tapImage_.height() * kSkinAssetScale : renderImage.height() * kStarAssetScale)
                * kSlideSpawnStarRelativeScale;
            targetWidth = qMax(1, qRound(baseWidth * canvasScale * effectiveScale));
            targetHeight = qMax(1, qRound(baseHeight * canvasScale * effectiveScale));
        } else {
            const qreal imageScale = canvasScale * effectiveScale * kSkinAssetScale;
            targetWidth = qMax(1, qRound(renderImage.width() * imageScale));
            targetHeight = qMax(1, qRound(renderImage.height() * imageScale));
        }

        if (!drawSpriteImage(
                painter,
                renderImage,
                point,
                targetWidth,
                targetHeight,
                laneRotationDegrees(marker.lane))) {
            return;
        }
        return;
    }

    const qreal radius = mapLogicalLengthToRect(16.0 * effectiveScale, playfieldRect);
    const QColor fillColor = tapColorForMarker(marker);
    painter.setPen(QPen(QColor("#0F1720"), qMax<qreal>(1.5, radius * 0.14)));
    painter.setBrush(fillColor);
    painter.drawEllipse(point, radius, radius);

    painter.setPen(QPen(QColor("#FFFDF4"), qMax<qreal>(1.0, radius * 0.10)));
    painter.drawEllipse(point, radius * 0.58, radius * 0.58);
}

void PreviewCanvas::drawHoldMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.endSecond < marker.second) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    const qreal deltaEndSeconds = static_cast<qreal>(playheadSeconds_ - marker.endSecond);
    if (deltaEndSeconds > 0.0) {
        return;
    }

    qreal distance = deltaSeconds * kTapUnitsPerSecond + kLogicalDistanceEdge;
    const qreal spawnScale = tapScaleForDistance(distance);
    if (spawnScale < 0.0) {
        return;
    }

    const QPointF unit = laneUnitVector(marker.lane);
    const QImage* holdImage = selectHoldImage(marker);
    QImage holdCapImage = holdImage != nullptr ? *holdImage : QImage();
    if (marker.isEx && !holdCapImage.isNull() && !holdExImage_.isNull()) {
        const QColor tint = exTintColor(marker.isBreak, marker.isEach);
        holdCapImage = composeOverlay(holdCapImage, holdExImage_, 0.85, 0.20, nullptr, &tint);
    }
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const auto drawHoldStripSlices = [&](const QPointF& center, int bodyLogicalLength, qreal scale) -> bool {
        if (holdCapImage.isNull()) {
            return false;
        }

        const int srcW = qMax(1, holdCapImage.width());
        const int srcH = qMax(1, holdCapImage.height());
        const int capRaw = qMax(1, qMin(srcH / 2, qRound(srcH * 67.0 / 200.0)));
        const int midY = qBound(0, srcH / 2, srcH - 1);
        const int targetWidth = qMax(1, qRound(kHoldTargetWidth * canvasScale * scale));
        const int targetCapHeight = qMax(
            1,
            qRound(static_cast<qreal>(qRound(static_cast<qreal>(capRaw) * kHoldTargetWidth / srcW)) * canvasScale * scale)
        );
        const int targetBodyHeight = qMax(0, qRound(bodyLogicalLength * canvasScale * scale));
        const qreal capOffset = (targetBodyHeight + targetCapHeight) / 2.0;
        const qreal angle = laneRotationDegrees(marker.lane);
        const QPointF headCenter = center + unit * capOffset;
        const QPointF tailCenter = center - unit * capOffset;

        if (targetBodyHeight > 0) {
            drawSpriteImage(
                painter,
                holdCapImage,
                center,
                targetWidth,
                targetBodyHeight,
                angle,
                1.0,
                QRectF(0.0, midY, srcW, 1.0)
            );
        }
        drawSpriteImage(
            painter,
            holdCapImage,
            headCenter,
            targetWidth,
            targetCapHeight,
            angle,
            1.0,
            QRectF(0.0, 0.0, srcW, capRaw)
        );
        drawSpriteImage(
            painter,
            holdCapImage,
            tailCenter,
            targetWidth,
            targetCapHeight,
            angle,
            1.0,
            QRectF(0.0, srcH - capRaw, srcW, capRaw)
        );
        return true;
    };

    if (distance < kLogicalDistanceTap) {
        const QPointF logicalPoint(
            kLogicalCanvasCenter + unit.x() * kLogicalDistanceTap,
            kLogicalCanvasCenter + unit.y() * kLogicalDistanceTap
        );
        const QPointF point = mapLogicalPointToRect(logicalPoint, playfieldRect);
        if (drawHoldStripSlices(point, 0, spawnScale)) {
            return;
        }

        const qreal radius = mapLogicalLengthToRect(18.0 * spawnScale, playfieldRect);
        painter.setPen(QPen(QColor("#0F1720"), qMax<qreal>(1.5, radius * 0.14)));
        painter.setBrush(tapColorForMarker(marker));
        painter.drawEllipse(point, radius, radius);
        return;
    }

    distance = qMin(distance, kLogicalDistanceEdge);
    qreal distanceEnd = deltaEndSeconds * kTapUnitsPerSecond + kLogicalDistanceEdge;
    if (distanceEnd < kLogicalDistanceTap) {
        distanceEnd = kLogicalDistanceTap;
    } else if (distanceEnd > kLogicalDistanceEdge) {
        distanceEnd = kLogicalDistanceEdge;
    }

    const QPointF logicalHead(
        kLogicalCanvasCenter + unit.x() * distance,
        kLogicalCanvasCenter + unit.y() * distance
    );
    const QPointF logicalTail(
        kLogicalCanvasCenter + unit.x() * distanceEnd,
        kLogicalCanvasCenter + unit.y() * distanceEnd
    );
    const QPointF logicalCenter = (logicalHead + logicalTail) * 0.5;
    const QPointF centerPoint = mapLogicalPointToRect(logicalCenter, playfieldRect);
    const QPointF headPoint = mapLogicalPointToRect(logicalHead, playfieldRect);
    const QPointF tailPoint = mapLogicalPointToRect(logicalTail, playfieldRect);

    const int lineLength = qMax(0, qRound(distance - distanceEnd));
    if (drawHoldStripSlices(centerPoint, lineLength, 1.0)) {
        return;
    }

    const qreal bodyWidth = mapLogicalLengthToRect(30.0, playfieldRect);
    painter.setPen(QPen(tapColorForMarker(marker), qMax<qreal>(4.0, bodyWidth), Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(tailPoint, headPoint);
}

void PreviewCanvas::drawCachedSlideArea(
    QPainter& painter,
    const TimelineNoteMarker& marker,
    int segmentIndex,
    int areaIndex,
    int localCut,
    const QRectF& playfieldRect,
    const QImage* image,
    qreal opacity,
    bool trimFromTail
)
{
    if (image == nullptr || image->isNull()) {
        return;
    }
    if (opacity <= 0.0) {
        return;
    }
    if (segmentIndex < 0 || segmentIndex >= marker.slideTrackAreaPoints.size()) {
        return;
    }
    const QVector<QVector<QPointF>>& segmentAreas = marker.slideTrackAreaPoints[segmentIndex];
    if (areaIndex < 0 || areaIndex >= segmentAreas.size()) {
        return;
    }
    const QVector<QPointF>& points = segmentAreas[areaIndex];
    if (points.isEmpty()) {
        return;
    }
    const QVector<double>& rotations = marker.slideTrackAreaRotations.value(segmentIndex).value(areaIndex);
    const int clampedCut = qBound(0, localCut, points.size());
    if (clampedCut >= points.size()) {
        return;
    }
    const int startPointIndex = trimFromTail ? 0 : clampedCut;
    const int endPointIndex = trimFromTail ? points.size() - clampedCut : points.size();
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const int targetWidth = qMax(1, qRound(image->width() * canvasScale * kSlideTrackScale));
    const int targetHeight = qMax(1, qRound(image->height() * canvasScale * kSlideTrackScale));

    if (glRenderer_.isInitialized()) {
        const QImage* renderImage = image;
        QRectF resolvedSourceRect;
        resolveAtlasImage(*image, QRectF(), renderImage, resolvedSourceRect);
        if (renderImage != nullptr && !renderImage->isNull()) {
            QVector<QPointF> centers;
            QVector<qreal> angles;
            centers.reserve(qMax(0, endPointIndex - startPointIndex));
            angles.reserve(qMax(0, endPointIndex - startPointIndex));
            for (int pointIndex = startPointIndex; pointIndex < endPointIndex; ++pointIndex) {
                centers.append(mapLogicalPointToRect(
                    QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                    playfieldRect
                ));
                angles.append(-rotations.value(pointIndex));
            }

            bool renderedByGl = false;
            if (nativePaintingActive_) {
                renderedByGl = glRenderer_.drawImageQuadBatch(
                    *renderImage,
                    centers,
                    targetWidth,
                    targetHeight,
                    angles,
                    opacity,
                    resolvedSourceRect
                );
            } else {
                painter.beginNativePainting();
                renderedByGl = glRenderer_.drawImageQuadBatch(
                    *renderImage,
                    centers,
                    targetWidth,
                    targetHeight,
                    angles,
                    opacity,
                    resolvedSourceRect
                );
                painter.endNativePainting();
            }
            if (renderedByGl) {
                usedGpuRendererThisFrame_ = true;
                return;
            }
            if (nativePaintingActive_) {
                return;
            }
        }
    }

    const int playfieldWidth = qMax(1, qRound(playfieldRect.width()));
    const QString cacheKey = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(marker.slideSegmentKeys.value(segmentIndex, marker.slideTrackKey))
        .arg(static_cast<qulonglong>(image->cacheKey()))
        .arg(segmentIndex)
        .arg(areaIndex)
        .arg(clampedCut)
        .arg(trimFromTail ? 1 : 0)
        .arg(playfieldWidth)
        .arg(qRound(playfieldRect.left()))
        .arg(qRound(playfieldRect.top()));

    CachedTrackArea uncached;
    CachedTrackArea* cachedArea = nullptr;
    auto cacheIt = slideTrackAreaCache_.end();
    if (kEnablePreviewCaches) {
        cacheIt = slideTrackAreaCache_.find(cacheKey);
        if (cacheIt != slideTrackAreaCache_.end()) {
            cachedArea = &cacheIt.value();
        }
    }
    if (cachedArea == nullptr) {
        const qreal targetWidth = image->width() * canvasScale * kSlideTrackScale;
        const qreal targetHeight = image->height() * canvasScale * kSlideTrackScale;

        QRectF bounds;
        for (int pointIndex = startPointIndex; pointIndex < endPointIndex; ++pointIndex) {
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            );
            bounds = bounds.isNull()
                ? QRectF(
                    point.x() - targetWidth / 2.0,
                    point.y() - targetHeight / 2.0,
                    targetWidth,
                    targetHeight
                )
                : bounds.united(QRectF(
                    point.x() - targetWidth / 2.0,
                    point.y() - targetHeight / 2.0,
                    targetWidth,
                    targetHeight
                ));
        }

        if (bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
            return;
        }

        const QRect pixelBounds = bounds.adjusted(-1.0, -1.0, 1.0, 1.0).toAlignedRect();
        CachedTrackArea built;
        built.image = QImage(pixelBounds.size(), QImage::Format_ARGB32_Premultiplied);
        built.image.fill(Qt::transparent);
        built.offset = pixelBounds.topLeft();

        QPainter cachePainter(&built.image);
        cachePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (int pointIndex = startPointIndex; pointIndex < endPointIndex; ++pointIndex) {
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            ) - built.offset;
            const qreal angle = rotations.value(pointIndex);
            cachePainter.save();
            cachePainter.translate(point);
            cachePainter.rotate(-angle);
            cachePainter.drawImage(
                QRectF(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight),
                *image
            );
            cachePainter.restore();
        }
        if (kEnablePreviewCaches) {
            cacheIt = slideTrackAreaCache_.insert(cacheKey, built);
            cachedArea = &cacheIt.value();
        } else {
            uncached = built;
            cachedArea = &uncached;
        }
    }

    if (cachedArea != nullptr) {
        painter.save();
        painter.setOpacity(opacity);
        painter.drawImage(cachedArea->offset, cachedArea->image);
        painter.restore();
    }
}

void PreviewCanvas::drawCachedWifiArea(
    QPainter& painter,
    const TimelineNoteMarker& marker,
    int areaIndex,
    int localCut,
    const QRectF& playfieldRect,
    qreal opacity
)
{
    if (opacity <= 0.0) {
        return;
    }
    if (areaIndex < 0 || areaIndex >= marker.wifiTrackAreaPoints.size()) {
        return;
    }
    const QVector<QPointF>& points = marker.wifiTrackAreaPoints[areaIndex];
    if (points.isEmpty()) {
        return;
    }
    const QVector<double>& rotations = marker.wifiTrackAreaRotations.value(areaIndex);
    const QVector<int>& imageIndices = marker.wifiTrackAreaImageIndices.value(areaIndex);
    const int clampedCut = qBound(0, localCut, points.size());
    const int visibleCount = points.size() - clampedCut;
    if (visibleCount <= 0) {
        return;
    }

    if (glRenderer_.isInitialized()) {
        const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
        for (int pointIndex = clampedCut; pointIndex < points.size(); ++pointIndex) {
            const int imageIndex = imageIndices.value(pointIndex, pointIndex);
            const QImage* image = selectWifiTrackImage(marker, imageIndex, 0);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            const int targetWidth = qMax(1, qRound(image->width() * canvasScale * kSlideTrackScale));
            const int targetHeight = qMax(1, qRound(image->height() * canvasScale * kSlideTrackScale));
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            );
            const qreal angle = -rotations.value(pointIndex);
            drawSpriteImage(painter, *image, point, targetWidth, targetHeight, angle, opacity);
        }
        return;
    }

    int variantTag = 0;
    if (marker.trackBreak && !wifiBreakImages_.isEmpty()) {
        variantTag = 2;
    } else if (marker.slideEach && !wifiEachImages_.isEmpty()) {
        variantTag = 1;
    }

    const int playfieldWidth = qMax(1, qRound(playfieldRect.width()));
    const QString cacheKey = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(marker.slideTrackKey)
        .arg(variantTag)
        .arg(areaIndex)
        .arg(clampedCut)
        .arg(playfieldWidth)
        .arg(qRound(playfieldRect.left()))
        .arg(qRound(playfieldRect.top()));

    CachedTrackArea uncached;
    CachedTrackArea* cachedArea = nullptr;
    auto cacheIt = wifiTrackAreaCache_.end();
    if (kEnablePreviewCaches) {
        cacheIt = wifiTrackAreaCache_.find(cacheKey);
        if (cacheIt != wifiTrackAreaCache_.end()) {
            cachedArea = &cacheIt.value();
        }
    }
    if (cachedArea == nullptr) {
        QRectF bounds;

        for (int pointIndex = clampedCut; pointIndex < points.size(); ++pointIndex) {
            const int imageIndex = imageIndices.value(pointIndex, pointIndex);
            const QImage* image = selectWifiTrackImage(marker, imageIndex, 0);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
            const qreal targetWidth = image->width() * canvasScale * kSlideTrackScale;
            const qreal targetHeight = image->height() * canvasScale * kSlideTrackScale;
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            );
            bounds = bounds.isNull()
                ? QRectF(point.x() - targetWidth / 2.0, point.y() - targetHeight / 2.0, targetWidth, targetHeight)
                : bounds.united(QRectF(point.x() - targetWidth / 2.0, point.y() - targetHeight / 2.0, targetWidth, targetHeight));
        }

        if (bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
            return;
        }

        const QRect pixelBounds = bounds.adjusted(-1.0, -1.0, 1.0, 1.0).toAlignedRect();
        CachedTrackArea built;
        built.image = QImage(pixelBounds.size(), QImage::Format_ARGB32_Premultiplied);
        built.image.fill(Qt::transparent);
        built.offset = pixelBounds.topLeft();

        QPainter cachePainter(&built.image);
        cachePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (int pointIndex = clampedCut; pointIndex < points.size(); ++pointIndex) {
            const int imageIndex = imageIndices.value(pointIndex, pointIndex);
            const QImage* image = selectWifiTrackImage(marker, imageIndex, 0);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
            const qreal targetWidth = image->width() * canvasScale * kSlideTrackScale;
            const qreal targetHeight = image->height() * canvasScale * kSlideTrackScale;
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            ) - built.offset;
            const qreal angle = rotations.value(pointIndex);
            cachePainter.save();
            cachePainter.translate(point);
            cachePainter.rotate(-angle);
            cachePainter.drawImage(
                QRectF(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight),
                *image
            );
            cachePainter.restore();
        }
        if (kEnablePreviewCaches) {
            cacheIt = wifiTrackAreaCache_.insert(cacheKey, built);
            cachedArea = &cacheIt.value();
        } else {
            uncached = built;
            cachedArea = &uncached;
        }
    }

    if (cachedArea != nullptr) {
        painter.save();
        painter.setOpacity(opacity);
        painter.drawImage(cachedArea->offset, cachedArea->image);
        painter.restore();
    }
}

void PreviewCanvas::drawSlideTrack(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.availableSecond < 0.0
        || marker.slideTrackAreaPoints.isEmpty()
        || playheadSeconds_ < marker.availableSecond - kSlideTrackFadeInSeconds
        || (marker.endSecond > marker.slideTraceSecond && playheadSeconds_ >= marker.endSecond)) {
        return;
    }
    const QImage* image = selectSlideTrackImage(marker);
    if (image->isNull()) {
        return;
    }

    int startSegment = 0;
    int startAreaIndex = 0;
    int removedArrowCount = 0;
    qreal startProportion = 0.0;
    qreal opacity = 1.0;
    if (playheadSeconds_ < marker.slideTraceSecond) {
        const qreal fadeStartSecond = marker.availableSecond - kSlideTrackFadeInSeconds;
        if (playheadSeconds_ < fadeStartSecond) {
            return;
        }
        opacity = qBound<qreal>(
            0.0,
            (playheadSeconds_ - fadeStartSecond) / qMax(0.001, kSlideTrackFadeInSeconds),
            1.0
        );
    } else if (!marker.slideSegmentShootSeconds.isEmpty()
        && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()
        && marker.slideTrackAreaPoints.size() == marker.slideSegmentDurations.size()) {
        for (int i = marker.slideSegmentShootSeconds.size() - 1; i >= 0; --i) {
            if (playheadSeconds_ >= marker.slideSegmentShootSeconds[i]) {
                startSegment = i;
                break;
            }
        }
        startSegment = qBound(0, startSegment, marker.slideTrackAreaPoints.size() - 1);
        const qreal duration = qMax(0.001, marker.slideSegmentDurations.value(startSegment));
        startProportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideSegmentShootSeconds[startSegment]) / duration, 1.0);
        const int areaCount = marker.slideTrackAreaPoints[startSegment].size();
        startAreaIndex = currentAreaIndexForProportion(marker.slideTrackAreaThresholds.value(startSegment), startProportion, areaCount);

        qreal totalDuration = marker.endSecond - marker.slideTraceSecond;
        if (!marker.slideTrackAreaThresholds.isEmpty()
            && !marker.slideTrackAreaThresholds.constLast().isEmpty()
            && !marker.slideSegmentShootSeconds.isEmpty()
            && !marker.slideSegmentDurations.isEmpty()) {
            const int lastSegmentIndex = qMin(
                marker.slideTrackAreaThresholds.size(),
                qMin(marker.slideSegmentShootSeconds.size(), marker.slideSegmentDurations.size())
            ) - 1;
            if (lastSegmentIndex >= 0) {
                const QVector<double>& lastThresholds = marker.slideTrackAreaThresholds[lastSegmentIndex];
                const qreal lastAreaEntryProportion = qBound<qreal>(0.0, lastThresholds.constLast(), 1.0);
                const qreal trimEndSecond =
                    marker.slideSegmentShootSeconds[lastSegmentIndex]
                    + lastAreaEntryProportion * marker.slideSegmentDurations[lastSegmentIndex];
                totalDuration = trimEndSecond - marker.slideTraceSecond;
            }
        }
        if (totalDuration <= 0.0) {
            totalDuration = 0.0;
            for (double segmentDuration : marker.slideSegmentDurations) {
                totalDuration += segmentDuration;
            }
        }
        if (totalDuration > 0.0) {
            int totalArrowCount = 0;
            for (const auto& segmentAreas : marker.slideTrackAreaPoints) {
                for (const auto& areaPoints : segmentAreas) {
                    totalArrowCount += areaPoints.size();
                }
            }
            const qreal totalProportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / totalDuration, 1.0);
            removedArrowCount = qBound(0, qFloor(totalProportion * totalArrowCount), totalArrowCount);
        }
    }

    painter.save();
    painter.setOpacity(opacity);

    if (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate) {
        for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex > startSegment; --segmentIndex) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
            for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image, opacity);
            }
        }
        if (startSegment >= 0 && startSegment < marker.slideTrackAreaPoints.size()) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[startSegment];
            const QVector<QVector<double>>& checkpoints = marker.slideTrackAreaCheckpoints.value(startSegment);
            const QVector<QVector<int>>& cutIndices = marker.slideTrackAreaCutIndices.value(startSegment);
            const int clampedStartArea = qBound(0, startAreaIndex, areas.size());
            int partialTrimCount = 0;
            if (clampedStartArea >= 0 && clampedStartArea < areas.size()) {
                const QVector<double>& areaCheckpoints = checkpoints.value(clampedStartArea);
                if (!areaCheckpoints.isEmpty()) {
                    int passedCheckpoints = 0;
                    for (double checkpoint : areaCheckpoints) {
                        if (startProportion >= checkpoint) {
                            ++passedCheckpoints;
                        } else {
                            break;
                        }
                    }
                    if (passedCheckpoints > 0) {
                        const QVector<int>& areaCutIndices = cutIndices.value(clampedStartArea);
                        if (!areaCutIndices.isEmpty()) {
                            const int cutIndex = areaCutIndices.value(qMin(passedCheckpoints - 1, areaCutIndices.size() - 1));
                            partialTrimCount = qBound(0, cutIndex, areas[clampedStartArea].size());
                        } else {
                            partialTrimCount = qBound(
                                0,
                                qFloor(static_cast<qreal>(passedCheckpoints) * areas[clampedStartArea].size() / areaCheckpoints.size()),
                                areas[clampedStartArea].size()
                            );
                        }
                    }
                }
            }
            for (int areaIndex = areas.size() - 1; areaIndex >= clampedStartArea; --areaIndex) {
                const int localCut = areaIndex == clampedStartArea ? partialTrimCount : 0;
                drawCachedSlideArea(painter, marker, startSegment, areaIndex, localCut, playfieldRect, image, opacity);
            }
        }
    } else {
        int trimSegment = marker.slideTrackAreaPoints.size();
        int trimArea = 0;
        int trimLocalCut = 0;
        int remainingToRemove = removedArrowCount;

        for (int segmentIndex = 0; segmentIndex < marker.slideTrackAreaPoints.size(); ++segmentIndex) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
            const int segmentArrowCount = std::accumulate(
                areas.cbegin(),
                areas.cend(),
                0,
                [](int total, const QVector<QPointF>& areaPoints) { return total + areaPoints.size(); }
            );
            if (remainingToRemove >= segmentArrowCount) {
                remainingToRemove -= segmentArrowCount;
                continue;
            }

            trimSegment = segmentIndex;
            for (int areaIndex = 0; areaIndex < areas.size(); ++areaIndex) {
                const int areaArrowCount = areas[areaIndex].size();
                if (remainingToRemove < areaArrowCount) {
                    trimArea = areaIndex;
                    trimLocalCut = qBound(0, remainingToRemove, areaArrowCount);
                    remainingToRemove = 0;
                    break;
                }
                remainingToRemove -= areaArrowCount;
            }
            break;
        }

        for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex > trimSegment; --segmentIndex) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
            for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image, opacity);
            }
        }
        if (trimSegment >= 0 && trimSegment < marker.slideTrackAreaPoints.size()) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[trimSegment];
            for (int areaIndex = areas.size() - 1; areaIndex >= trimArea; --areaIndex) {
                const int localCut = areaIndex == trimArea ? trimLocalCut : 0;
                drawCachedSlideArea(
                    painter,
                    marker,
                    trimSegment,
                    areaIndex,
                    localCut,
                    playfieldRect,
                    image,
                    opacity,
                    areaIndex == trimArea
                );
            }
        }
    }
    painter.restore();
}

void PreviewCanvas::drawWifiTrack(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.availableSecond < 0.0
        || marker.wifiTrackAreaPoints.isEmpty()
        || playheadSeconds_ < marker.availableSecond - kSlideTrackFadeInSeconds
        || (marker.endSecond > marker.slideTraceSecond && playheadSeconds_ >= marker.endSecond)) {
        return;
    }
    qreal opacity = 1.0;
    int startAreaIndex = 0;
    qreal startProportion = 0.0;
    if (playheadSeconds_ < marker.availableSecond) {
        const qreal fadeStartSecond = marker.availableSecond - kSlideTrackFadeInSeconds;
        if (playheadSeconds_ < fadeStartSecond) {
            return;
        }
        opacity = qBound<qreal>(0.0, (playheadSeconds_ - fadeStartSecond) / kSlideTrackFadeInSeconds, 1.0);
    } else if (playheadSeconds_ >= marker.slideTraceSecond) {
        const qreal totalDuration = !marker.slideSegmentDurations.isEmpty()
            ? qMax(0.001, marker.slideSegmentDurations.constFirst())
            : qMax(0.001, marker.endSecond - marker.slideTraceSecond);
        startProportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / totalDuration, 1.0);
        startAreaIndex = currentAreaIndexForProportion(marker.wifiTrackAreaThresholds, startProportion, marker.wifiTrackAreaPoints.size());
    }

    painter.save();
    const int clampedStartArea = qBound(0, startAreaIndex, marker.wifiTrackAreaPoints.size());
    int partialTrimCount = 0;
    if (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate
        && clampedStartArea >= 0
        && clampedStartArea < marker.wifiTrackAreaPoints.size()) {
        const QVector<double>& areaCheckpoints = marker.wifiTrackAreaCheckpoints.value(clampedStartArea);
        if (!areaCheckpoints.isEmpty()) {
            int passedCheckpoints = 0;
            for (double checkpoint : areaCheckpoints) {
                if (startProportion >= checkpoint) {
                    ++passedCheckpoints;
                } else {
                    break;
                }
            }
            partialTrimCount = qBound(
                0,
                qFloor(static_cast<qreal>(passedCheckpoints) * marker.wifiTrackAreaPoints[clampedStartArea].size() / areaCheckpoints.size()),
                marker.wifiTrackAreaPoints[clampedStartArea].size()
            );
        }
    }
    for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= clampedStartArea; --areaIndex) {
        const int localCut = (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate && areaIndex == clampedStartArea)
            ? partialTrimCount
            : 0;
        drawCachedWifiArea(painter, marker, areaIndex, localCut, playfieldRect, opacity);
    }
    painter.restore();
}

void PreviewCanvas::drawSlideMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.slideTraceSecond <= marker.second || marker.slideSegmentPoints.isEmpty()) {
        return;
    }

    qreal angle = 0.0;
    QPointF logicalPoint;
    qreal imageScale = kStarAssetScale;
    qreal imageOpacity = 1.0;
    const QImage* starImage = nullptr;

    if (playheadSeconds_ < marker.second || playheadSeconds_ > marker.endSecond) {
        return;
    }

    if (playheadSeconds_ < marker.slideTraceSecond) {
        if (!marker.hasHeadStar) {
            return;
        }
        starImage = selectSlideMovingStarImage(marker);
        if (starImage->isNull()) {
            return;
        }
        const QVector<QPointF>& points = marker.slideSegmentPoints.constFirst();
        const QVector<double>& angles = marker.slideSegmentAngles.constFirst();
        if (points.isEmpty()) {
            return;
        }
        logicalPoint = points.constFirst();
        angle = angles.isEmpty() ? 0.0 : angles.constFirst();
        const qreal waitDuration = qMax(0.001, marker.slideTraceSecond - marker.second);
        const qreal waitT = qBound<qreal>(0.0, (playheadSeconds_ - marker.second) / waitDuration, 1.0);
        const qreal startScale = slideStartupStarInitialScale(*starImage);
        imageScale = startScale + (kStarAssetScale - startScale) * waitT;
        imageOpacity = 0.5 + 0.5 * waitT;
    } else {
        starImage = selectSlideMovingStarImage(marker);
        if (starImage->isNull()) {
            return;
        }
        int segmentIndex = 0;
        if (!marker.slideSegmentShootSeconds.isEmpty() && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()) {
            for (int i = marker.slideSegmentShootSeconds.size() - 1; i >= 0; --i) {
                if (playheadSeconds_ >= marker.slideSegmentShootSeconds[i]) {
                    segmentIndex = i;
                    break;
                }
            }
        }
        segmentIndex = qBound(0, segmentIndex, marker.slideSegmentPoints.size() - 1);
        const QVector<QPointF>& points = marker.slideSegmentPoints[segmentIndex];
        const QVector<double>& angles = marker.slideSegmentAngles.value(segmentIndex);
        if (points.isEmpty()) {
            return;
        }
        qreal proportion = 1.0;
        if (segmentIndex < marker.slideSegmentShootSeconds.size() && segmentIndex < marker.slideSegmentDurations.size()) {
            const qreal duration = qMax(0.001, marker.slideSegmentDurations[segmentIndex]);
            proportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideSegmentShootSeconds[segmentIndex]) / duration, 1.0);
        }
        logicalPoint = interpolatePoint(points, proportion);
        angle = interpolateAngle(angles, proportion);
    }

    const QPointF point = mapLogicalPointToRect(
        QPointF(kLogicalCanvasCenter + logicalPoint.x(), kLogicalCanvasCenter + logicalPoint.y()),
        playfieldRect
    );
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const int targetWidth = qMax(1, qRound(starImage->width() * canvasScale * imageScale));
    const int targetHeight = qMax(1, qRound(starImage->height() * canvasScale * imageScale));
    if (!drawSpriteImage(
            painter,
            *starImage,
            point,
            targetWidth,
            targetHeight,
            angle,
            imageOpacity)) {
        return;
    }
}

void PreviewCanvas::drawWifiMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.slideTraceSecond <= marker.second || marker.wifiLanePoints.isEmpty()) {
        return;
    }
    if (playheadSeconds_ < marker.second || playheadSeconds_ > marker.endSecond) {
        return;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    qreal imageScale = kStarAssetScale;
    qreal imageOpacity = 1.0;
    qreal proportion = 0.0;
    bool waiting = playheadSeconds_ < marker.slideTraceSecond;
    const QImage* starImage = selectSlideMovingStarImage(marker);
    if (starImage->isNull()) {
        return;
    }
    if (waiting) {
        if (!marker.hasHeadStar) {
            return;
        }
        const qreal waitDuration = qMax(0.001, marker.slideTraceSecond - marker.second);
        const qreal waitT = qBound<qreal>(0.0, (playheadSeconds_ - marker.second) / waitDuration, 1.0);
        const qreal startScale = slideStartupStarInitialScale(*starImage);
        imageScale = startScale + (kStarAssetScale - startScale) * waitT;
        imageOpacity = 0.5 + 0.5 * waitT;
    } else if (!marker.slideSegmentDurations.isEmpty()) {
        const qreal duration = qMax(0.001, marker.slideSegmentDurations.constFirst());
        proportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / duration, 1.0);
    } else if (marker.endSecond > marker.slideTraceSecond) {
        const qreal duration = qMax(0.001, marker.endSecond - marker.slideTraceSecond);
        proportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / duration, 1.0);
    }

    const int targetWidth = qMax(1, qRound(starImage->width() * canvasScale * imageScale));
    const int targetHeight = qMax(1, qRound(starImage->height() * canvasScale * imageScale));
    for (int laneIndex = 0; laneIndex < marker.wifiLanePoints.size(); ++laneIndex) {
        const QVector<QPointF>& points = marker.wifiLanePoints[laneIndex];
        const QVector<double>& angles = marker.wifiLaneAngles.value(laneIndex);
        if (points.isEmpty()) {
            continue;
        }
        const QPointF logicalPoint = waiting ? points.constFirst() : interpolatePoint(points, proportion);
        const qreal angle = waiting
            ? (angles.isEmpty() ? 0.0 : angles.constFirst())
            : interpolateAngle(angles, proportion);
        const QPointF point = mapLogicalPointToRect(
            QPointF(kLogicalCanvasCenter + logicalPoint.x(), kLogicalCanvasCenter + logicalPoint.y()),
            playfieldRect
        );
        if (!drawSpriteImage(
                painter,
                *starImage,
                point,
                targetWidth,
                targetHeight,
                angle,
                imageOpacity)) {
            continue;
        }
    }
}

void PreviewCanvas::drawTouchMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    if (deltaSeconds <= -kTouchDurationSeconds || deltaSeconds >= 0.0) {
        return;
    }

    const QImage& basePointImage = (marker.isEach && !touchPointEachImage_.isNull()) ? touchPointEachImage_ : touchPointImage_;
    const QImage& baseCornerImage = (marker.isEach && !touchCornerEachImage_.isNull()) ? touchCornerEachImage_ : touchCornerImage_;
    if (basePointImage.isNull() || baseCornerImage.isNull()) {
        return;
    }

    const QPointF point = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
    const int pointWidth = qMax(1, qRound(basePointImage.width() * kTouchAssetScale));
    const int pointHeight = qMax(1, qRound(basePointImage.height() * kTouchAssetScale));
    const int cornerWidth = qMax(1, qRound(baseCornerImage.width() * kTouchAssetScale));
    const int cornerHeight = qMax(1, qRound(baseCornerImage.height() * kTouchAssetScale));
    const qreal progress = qBound<qreal>(0.0, (deltaSeconds + kTouchDurationSeconds) / kTouchDurationSeconds, 1.0);
    qreal alpha = 1.0;
    qreal closeRatio = 0.0;
    if (progress < kTouchAppearPhase) {
        alpha = qBound<qreal>(0.0, progress / kTouchAppearPhase, 1.0);
    } else {
        closeRatio = qBound<qreal>(0.0, (progress - kTouchAppearPhase) / (1.0 - kTouchAppearPhase), 1.0);
    }

    const qreal logicalOffset = kTouchClosedOffset + (kTouchStartOffset - kTouchClosedOffset) * (1.0 - closeRatio);
    const qreal offset = mapLogicalLengthToRect(logicalOffset, playfieldRect);
    const struct {
        qreal dx;
        qreal dy;
        int angle;
    } layout[] = {
        {0.0, -offset, 180},
        {offset, 0.0, -90},
        {0.0, offset, 0},
        {-offset, 0.0, 90},
    };

    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (const auto& pieceLayout : layout) {
        drawSpriteImage(
            painter,
            baseCornerImage,
            QPointF(point.x() + pieceLayout.dx, point.y() + pieceLayout.dy),
            cornerWidth,
            cornerHeight,
            pieceLayout.angle,
            alpha
        );
    }
    drawSpriteImage(painter, basePointImage, point, pointWidth, pointHeight, 0.0);
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawTouchHoldMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
        return;
    }
    if (marker.endSecond <= marker.second) {
        return;
    }
    if (touchHold0Image_.isNull() || touchHold1Image_.isNull() || touchHold2Image_.isNull()
        || touchHold3Image_.isNull() || touchHoldBorderImage_.isNull()) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    const qreal holdDuration = qMax<qreal>(0.001, marker.endSecond - marker.second);
    if (deltaSeconds <= -kTouchDurationSeconds || deltaSeconds >= holdDuration) {
        return;
    }

    const QImage pointBase = !touchPointEachImage_.isNull() ? touchPointEachImage_ : touchPointImage_;
    if (pointBase.isNull()) {
        return;
    }

    const QPointF point = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
    const int pointWidth = qMax(1, qRound(pointBase.width() * kTouchAssetScale));
    const int pointHeight = qMax(1, qRound(pointBase.height() * kTouchAssetScale));
    const int borderWidth = qMax(1, qRound(touchHoldBorderImage_.width() * kTouchAssetScale));
    const int borderHeight = qMax(1, qRound(touchHoldBorderImage_.height() * kTouchAssetScale));
    qreal alpha = 1.0;
    qreal logicalOffset = kTouchHoldClosedOffset;
    if (deltaSeconds < 0.0) {
        const qreal progress = qBound<qreal>(0.0, (deltaSeconds + kTouchDurationSeconds) / kTouchDurationSeconds, 1.0);
        qreal closeRatio = 0.0;
        if (progress < kTouchAppearPhase) {
            alpha = qBound<qreal>(0.0, progress / kTouchAppearPhase, 1.0);
        } else {
            closeRatio = qBound<qreal>(0.0, (progress - kTouchAppearPhase) / (1.0 - kTouchAppearPhase), 1.0);
        }
        logicalOffset = kTouchHoldClosedOffset + (kTouchHoldStartOffset - kTouchHoldClosedOffset) * (1.0 - closeRatio);
    }

    const qreal offset = mapLogicalLengthToRect(logicalOffset, playfieldRect);
    const struct {
        const QImage* image;
        int width;
        int height;
        int angle;
        qreal dx;
        qreal dy;
    } layout[] = {
        {&touchHold0Image_, qMax(1, qRound(touchHold0Image_.width() * kTouchAssetScale)), qMax(1, qRound(touchHold0Image_.height() * kTouchAssetScale)), -135, offset, -offset},
        {&touchHold1Image_, qMax(1, qRound(touchHold1Image_.width() * kTouchAssetScale)), qMax(1, qRound(touchHold1Image_.height() * kTouchAssetScale)), -45, offset, offset},
        {&touchHold2Image_, qMax(1, qRound(touchHold2Image_.width() * kTouchAssetScale)), qMax(1, qRound(touchHold2Image_.height() * kTouchAssetScale)), 45, -offset, offset},
        {&touchHold3Image_, qMax(1, qRound(touchHold3Image_.width() * kTouchAssetScale)), qMax(1, qRound(touchHold3Image_.height() * kTouchAssetScale)), 135, -offset, -offset},
    };

    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (int index = 3; index >= 0; --index) {
        const auto& pieceLayout = layout[index];
        drawSpriteImage(
            painter,
            *pieceLayout.image,
            QPointF(point.x() + pieceLayout.dx, point.y() + pieceLayout.dy),
            pieceLayout.width,
            pieceLayout.height,
            pieceLayout.angle,
            alpha
        );
    }

    if (deltaSeconds >= 0.0) {
        const bool resumeNativeAfterBorder = nativePaintingActive_;
        if (resumeNativeAfterBorder) {
            endNativeBatch(painter);
        }
        const QRectF borderRect(
            point.x() - borderWidth / 2.0,
            point.y() - borderHeight / 2.0,
            borderWidth,
            borderHeight
        );
        const qreal progress = qBound<qreal>(0.0, deltaSeconds / holdDuration, 1.0);
        if (progress > 0.0) {
            QPainterPath clipPath;
            clipPath.moveTo(borderRect.center());
            clipPath.arcTo(borderRect, 90.0, -progress * 360.0);
            clipPath.closeSubpath();
            painter.save();
            painter.setClipPath(clipPath);
            painter.drawImage(borderRect, touchHoldBorderImage_);
            painter.restore();
        }
        if (resumeNativeAfterBorder) {
            beginNativeBatch(painter);
        }
    }
    drawSpriteImage(painter, pointBase, point, pointWidth, pointHeight, 0.0);
    if (batchNative) {
        endNativeBatch(painter);
    }
}

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
    usedGpuRendererThisFrame_ = false;
    cpuFallbackCount_ = 0;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.fillRect(QRect(QPoint(0, 0), size()), QColor("#1F2833"));

    const QRectF stageRect = currentStageRect();
    const QRectF playfieldRect = stagePlayfieldRect(stageRect);

    drawStageBackground(painter, stageRect);
    drawPlayfieldBackdrop(painter, playfieldRect);
    const bool batchNative = glRenderer_.isInitialized();
    if (batchNative) {
        beginNativeBatch(painter);
    }
    drawTouchLayer(painter, playfieldRect);
    drawTrackLayer(painter, playfieldRect);
    drawGuideLayer(painter, playfieldRect);
    drawHoldLayer(painter, playfieldRect);
    drawTapLayer(painter, playfieldRect);
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
