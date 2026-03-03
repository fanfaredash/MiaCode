#pragma once

#include "PreviewGLRenderer.h"
#include "TimelineView.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QOpenGLWidget>
#include <QString>
#include <QStringList>

class QPainter;
class QRectF;

class PreviewCanvas : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit PreviewCanvas(QWidget* parent = nullptr);
    ~PreviewCanvas() override;

    void setPlayheadSeconds(double seconds);
    void setMediaFrame(const QImage& frame);
    void setNoteMarkers(const QVector<TimelineNoteMarker>& notes);
    void setSkinDirectory(const QString& skinDir);
    void setBackgroundBrightness(double brightness);
    void setShowDebugInfo(bool show);
    void reset();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

private:
    struct CachedTrackArea {
        QImage image;
        QPointF offset;
    };
    struct AtlasRegionRef {
        const QImage* atlasImage = nullptr;
        QRect rect;
    };
    struct BatchedSprite {
        const QImage* image = nullptr;
        QPointF center;
        int targetWidth = 0;
        int targetHeight = 0;
        qreal angleDegrees = 0.0;
        qreal opacity = 1.0;
        QRectF sourceRect;
    };

    const QImage* selectTapImage(const TimelineNoteMarker& marker) const;
    const QImage* selectHoldImage(const TimelineNoteMarker& marker) const;
    const QImage* selectTapNoteGuideImage(const TimelineNoteMarker& marker) const;
    const QImage* selectHoldEndNoteGuideImage(const TimelineNoteMarker& marker) const;
    const QImage* selectSlideStarImage(const TimelineNoteMarker& marker) const;
    const QImage* selectSlideMovingStarImage(const TimelineNoteMarker& marker) const;
    const QImage* selectSlideTrackImage(const TimelineNoteMarker& marker) const;
    const QImage* selectWifiTrackImage(const TimelineNoteMarker& marker, int sampleIndex, int sampleCount) const;
    qreal slideStartupStarInitialScale(const QImage& starImage) const;
    QImage cachedGuideTransform(const QImage& image, int targetWidth, int targetHeight, qreal angleDegrees);
    QImage cachedSpriteTransform(const QImage& image, int targetWidth, int targetHeight, qreal angleDegrees);
    QImage composeOverlay(
        const QImage& base,
        const QImage& overlay,
        qreal mix,
        qreal lighten,
        const QImage* accentSource = nullptr,
        const QColor* accentOverride = nullptr
    );
    void rebuildAtlases();
    void rebuildAtlas(QImage& atlasImage, const QVector<const QImage*>& images);
    bool resolveAtlasImage(const QImage& image, const QRectF& sourceRect, const QImage*& atlasImage, QRectF& atlasSourceRect) const;
    void flushTapAtlasBatch(QPainter& painter);
    QRectF currentStageRect() const;
    QRectF stagePlayfieldRect(const QRectF& stageRect) const;
    QRectF currentPlayfieldRect() const;
    void warmTransformCachesForCurrentSize();
    void prebuildTrackAreaCachesForCurrentState();
    void drawStageBackground(QPainter& painter, const QRectF& stageRect);
    void drawPlayfieldBackdrop(QPainter& painter, const QRectF& playfieldRect);
    void drawTouchLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawTrackLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawGuideLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawHoldLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawTapLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawHud(QPainter& painter, const QRectF& stageRect);
    bool drawSpriteImage(
        QPainter& painter,
        const QImage& image,
        const QPointF& center,
        int targetWidth,
        int targetHeight,
        qreal angleDegrees,
        qreal opacity = 1.0,
        const QRectF& sourceRect = QRectF()
    );
    void drawNoteGuides(QPainter& painter, const QRectF& playfieldRect);
    void drawTouchHoldMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawCachedSlideArea(
        QPainter& painter,
        const TimelineNoteMarker& marker,
        int segmentIndex,
        int areaIndex,
        int localCut,
        const QRectF& playfieldRect,
        const QImage* image,
        qreal opacity = 1.0,
        bool trimFromTail = false
    );
    void drawCachedWifiArea(
        QPainter& painter,
        const TimelineNoteMarker& marker,
        int areaIndex,
        int localCut,
        const QRectF& playfieldRect,
        qreal opacity = 1.0
    );
    void drawSlideTrack(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawWifiTrack(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawSlideMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawWifiMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawTouchMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawTapMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawHoldMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void renderCanvas(QPainter& painter);
    void updateFpsSample();

    QImage tapImage_;
    QImage tapEachImage_;
    QImage tapBreakImage_;
    QImage tapExImage_;
    QImage slideTrackImage_;
    QImage slideTrackEachImage_;
    QImage slideTrackBreakImage_;
    QImage starImage_;
    QImage starEachImage_;
    QImage starBreakImage_;
    QImage starBreakDoubleImage_;
    QImage starDoubleImage_;
    QImage starEachDoubleImage_;
    QImage starExImage_;
    QImage starExDoubleImage_;
    QVector<QImage> wifiImages_;
    QVector<QImage> wifiEachImages_;
    QVector<QImage> wifiBreakImages_;
    QImage holdImage_;
    QImage holdEachImage_;
    QImage holdBreakImage_;
    QImage holdExImage_;
    QImage noteGuideNormalImage_;
    QImage noteGuideBreakImage_;
    QImage noteGuideEachImage_;
    QImage noteGuideEachLine1Image_;
    QImage noteGuideEachLine2Image_;
    QImage noteGuideEachLine3Image_;
    QImage noteGuideEachLine4Image_;
    QImage noteGuideHoldEndImage_;
    QImage noteGuideHoldEachEndImage_;
    QImage noteGuideHoldBreakEndImage_;
    QImage noteGuideSlideImage_;
    QImage touchCornerImage_;
    QImage touchCornerEachImage_;
    QImage touchPointImage_;
    QImage touchPointEachImage_;
    QImage touchHold0Image_;
    QImage touchHold1Image_;
    QImage touchHold2Image_;
    QImage touchHold3Image_;
    QImage touchHoldBorderImage_;
    QImage outlineImage_;
    QImage tapAtlasImage_;
    QImage trackAtlasImage_;
    QImage touchAtlasImage_;
    QImage guideAtlasImage_;
    QHash<quint64, QImage> overlayCache_;
    QHash<quint64, AtlasRegionRef> atlasRegions_;
    QHash<QString, QImage> guideTransformCache_;
    QStringList guideTransformCacheOrder_;
    QHash<QString, QImage> spriteTransformCache_;
    QStringList spriteTransformCacheOrder_;
    QHash<QString, CachedTrackArea> slideTrackAreaCache_;
    QHash<QString, CachedTrackArea> wifiTrackAreaCache_;
    PreviewGLRenderer glRenderer_;
    QVector<TimelineNoteMarker> noteMarkers_;
    QImage mediaFrame_;
    double playheadSeconds_ = 0.0;
    double backgroundBrightness_ = 0.2;
    QElapsedTimer fpsTimer_;
    int fpsFrameCounter_ = 0;
    double fpsDisplay_ = 0.0;
    qint64 lastFrameTimestampNs_ = 0;
    QVector<double> frameIntervalsMs_;
    int frameIntervalWriteIndex_ = 0;
    int frameIntervalCount_ = 0;
    double frameMsAverage_ = 0.0;
    double frameMsP95_ = 0.0;
    double frameMsMax_ = 0.0;
    bool usedGpuRendererThisFrame_ = false;
    bool showDebugInfo_ = true;
    bool nativePaintingActive_ = false;
    bool tapAtlasBatchingActive_ = false;
    QVector<BatchedSprite> tapAtlasBatch_;
};
