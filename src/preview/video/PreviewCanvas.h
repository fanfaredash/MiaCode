#pragma once

#include "common/LayoutRingConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "PreviewRenderSettings.h"
#include "PreviewGLRenderer.h"
#include "TimelineView.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QOpenGLExtraFunctions>
#include <QOpenGLWindow>
#include <QRectF>
#include <QString>
#include <QStringList>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif

class QPainter;
class QRectF;
class QTimer;
class QOffscreenSurface;
class QOpenGLFramebufferObject;
class QSurfaceFormat;

class PreviewCanvas : public QOpenGLWindow
{
    Q_OBJECT

public:
    struct SkinLoadResult;

    explicit PreviewCanvas(QWindow* parent = nullptr);
    ~PreviewCanvas() override;

    void setStageMediaAvailable(bool hasMedia);
    void setPlayheadSeconds(double seconds);
    void setMediaFrame(const QImage& frame);
    void setVideoFrame(const QVideoFrame& frame);
    void setNoteMarkers(const QVector<TimelineNoteMarker>& notes);
    void setSkinDirectory(const QString& skinDir);
    void setBackgroundBrightness(double brightness);
    void setBackgroundBrightnessOuter(double brightness);
    void setBackgroundBrightnessInner(double brightness);
    void setLayoutSquareScale(double scale);
    void setSmoothBrightness(bool smooth);
    void setBackgroundScaleMode(PreviewBackgroundScaleMode mode);
    void setNoteFlowSpeed(double flowSpeed);
    double noteFlowSpeed() const;
    void setCpuTrackAreaCachingEnabled(bool enabled);
    void setShowDebugInfo(bool show);
    void setShowTimestamp(bool show);
    void setShowObjectStatsHud(bool show);
    bool showTimestamp() const;
    void copyRenderStateFrom(const PreviewCanvas& source);
    QImage renderOverlayFrame(
        const QSize& outputSize,
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud = false
    );
    bool initializeOffscreenRenderer(
        const QSurfaceFormat& requestedFormat,
        QOpenGLContext* shareContext,
        QString* errorMessage = nullptr
    );
    void shutdownOffscreenRenderer();
    QImage renderOverlayFrameOffscreen(
        const QSize& outputSize,
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud = false
    );
    bool supportsOffscreenPboReadback(QString* errorMessage = nullptr);
    void resetOffscreenPboReadback();
    bool renderOverlayFrameOffscreenPboStep(
        const QSize& outputSize,
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud,
        QImage* completedFrame,
        bool* completedFrameReady,
        bool drainOnly = false,
        QString* errorMessage = nullptr
    );
    bool isGpuRendererReadyForDebug() const { return glRenderer_.isInitialized(); }
    bool usedGpuRendererLastFrameForDebug() const { return usedGpuRendererThisFrame_; }
    int cpuFallbackCountLastFrameForDebug() const { return cpuFallbackCount_; }
    qint64 offscreenDrawNsLastFrameForDebug() const { return offscreenDrawNsLastFrame_; }
    qint64 offscreenReadbackNsLastFrameForDebug() const { return offscreenReadbackNsLastFrame_; }
    bool offscreenPboReadbackActiveForDebug() const { return offscreenReadbackPendingIndex_ >= 0; }
    bool hasCoreSkinAssetsLoadedForDebug() const
    {
        return !tapImage_.isNull() && !holdImage_.isNull() && !starImage_.isNull();
    }
    void reset();
    void noteTickForProfiling();
    void resetProfilingSession();
    QString writeProfilingSummaryToFile();
    QSize preferredSize() const;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

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
    struct TapApproachSample {
        qreal distance = 0.0;
        qreal scale = 0.0;
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
    void beginNativeBatch(QPainter& painter);
    void endNativeBatch(QPainter& painter);
    void applySkinLoadResult(SkinLoadResult&& result);
    void scheduleTexturePrewarm();
    void processTexturePrewarmQueue();
    void refreshOutlineAsset();
    QRectF currentStageRect() const;
    QRectF stageRectForSize(const QSize& renderSize) const;
    QRectF stagePlayfieldRect(const QRectF& stageRect) const;
    QRectF currentPlayfieldRect() const;
    void warmTransformCachesForCurrentSize();
    void prebuildTrackAreaCachesForCurrentState();
    void drawStageBackground(QPainter& painter, const QSize& canvasSize, const QRectF& stageRect);
    void drawPlayfieldBackdrop(QPainter& painter, const QRectF& playfieldRect);
    void drawTouchLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawTouchHoldLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawTrackLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawSlideMotionLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawGuideLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawHoldAndTapHeadLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawJudgeEffectLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawJudgeEffectTouchLayer(QPainter& painter, const QRectF& playfieldRect);
    void drawJudgeEffectFireworkLayer(QPainter& painter, const QRectF& playfieldRect);
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
    void drawTouchMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect, int overlapCount);
    void drawTapMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    void drawHoldMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect);
    TapApproachSample sampleTapApproach(qreal deltaSeconds) const;
    qreal sampleSlideTrackPreTraceOpacity(qreal markerSecond, qreal playheadSecond) const;
    void refreshTimingFromFlowSpeed();
    void renderCanvas(QPainter& painter);
    void renderCanvas(
        QPainter& painter,
        const QSize& canvasSize,
        bool drawStageBackground,
        bool clearToStageColor,
        bool highQualityRender
    );
    bool ensureOffscreenFramebuffer(const QSize& framebufferSize, QString* errorMessage = nullptr);
    bool ensureOffscreenReadbackPbos(const QSize& framebufferSize, QString* errorMessage = nullptr);
    bool mapOffscreenReadbackPbo(int pboIndex, const QSize& imageSize, QImage* frame, QString* errorMessage = nullptr);
    void destroyOffscreenReadbackPbos();
    bool supportsOffscreenPboReadback(QOpenGLContext* context) const;
    void updateFpsSample();
    void collectGpuProfilingResults(bool waitForAll);
    QString profilingSummaryPath() const;

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
    QImage touchCornerBreakImage_;
    QImage touchBorder2Image_;
    QImage touchBorder2EachImage_;
    QImage touchBorder2BreakImage_;
    QImage touchBorder3Image_;
    QImage touchBorder3EachImage_;
    QImage touchBorder3BreakImage_;
    QImage touchPointImage_;
    QImage touchPointEachImage_;
    QImage touchPointBreakImage_;
    QImage touchHold0Image_;
    QImage touchHold1Image_;
    QImage touchHold2Image_;
    QImage touchHold3Image_;
    QImage touchHoldBorderImage_;
    QImage judgeEffectTapImage_;
    QRectF judgeEffectTapSourceRect_;
    QImage judgeEffectTapBreakImage_;
    QRectF judgeEffectTapBreakSourceRect_;
    QImage judgeEffectHoldSustainCircleImage_;
    QImage judgeEffectTouchCircleImage_;
    QImage judgeEffectTouchPart01Image_;
    QImage judgeEffectTouchPart02Image_;
    QImage judgeEffectFireworkImage_;
    QRectF judgeEffectFireworkSourceRect_;
    QImage judgeEffectFireworkColorBallImage_;
    QRectF judgeEffectFireworkColorBallSourceRect_;
    QImage outlineImage_;
    QImage tapAtlasImage_;
    QImage trackAtlasImage_;
    QImage touchAtlasImage_;
    QImage guideAtlasImage_;
    QHash<quint64, QImage> overlayCache_;
    QImage brightnessMaskCache_;
    QSize brightnessMaskCacheSize_;
    double brightnessMaskCacheOuter_ = -1.0;
    double brightnessMaskCacheInner_ = -1.0;
    double brightnessMaskCacheLayoutScale_ = -1.0;
    double brightnessMaskCacheRingRatio_ = -1.0;
    bool brightnessMaskCacheSmooth_ = false;
    QHash<quint64, AtlasRegionRef> atlasRegions_;
    QHash<QString, QImage> guideTransformCache_;
    QStringList guideTransformCacheOrder_;
    QHash<QString, QImage> spriteTransformCache_;
    QStringList spriteTransformCacheOrder_;
    QHash<QString, CachedTrackArea> slideTrackAreaCache_;
    QHash<QString, CachedTrackArea> wifiTrackAreaCache_;
    bool cpuTrackAreaCachingEnabled_ = true;
    PreviewGLRenderer glRenderer_;
    QVector<TimelineNoteMarker> noteMarkers_;
    QImage mediaFrame_;
#ifdef HAVE_QT_MULTIMEDIA
    QVideoFrame videoFrame_;
#endif
    bool stageMediaAvailable_ = false;
    double playheadSeconds_ = 0.0;
    double backgroundBrightnessOuter_ = miacode::preview_video::kBackgroundBrightnessDefault;
    double backgroundBrightnessInner_ = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double layoutSquareScale_ = miacode::preview_video::kLayoutSquareScaleDefault;
    bool smoothBrightness_ = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewBackgroundScaleMode backgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    double noteFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    double tapLifecycleDurationSeconds_ = miacode::preview_gameplay::kTapLifecycleDurationSeconds;
    double tapSpawnDurationSeconds_ = miacode::preview_gameplay::kTapSpawnDurationSeconds;
    double tapFlyDurationSeconds_ = miacode::preview_gameplay::kTapFlyDurationSeconds;
    double tapUnitsPerSecond_ = miacode::preview_gameplay::kTapUnitsPerSecond;
    double slideTrackAppearLeadInSeconds_ = miacode::preview_gameplay::kSlideTrackAppearLeadInSeconds;
    double slideTrackFullBrightLeadInSeconds_ = miacode::preview_gameplay::kSlideTrackFullBrightLeadInSeconds;
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
    int cpuFallbackCount_ = 0;
    bool usedGpuRendererThisFrame_ = false;
    bool showDebugInfo_ = false;
    bool showTimestamp_ = true;
    bool showObjectStatsHud_ = false;
    double layoutRingDiameterRatio_ = miacode::layout_ring::kFallbackPlayfieldDiameterRatio;
    bool highQualityRender_ = false;
    bool nativePaintingActive_ = false;
    bool tapAtlasBatchingActive_ = false;
    QVector<BatchedSprite> tapAtlasBatch_;
    QElapsedTimer profileSessionClock_;
    qint64 lastProfileFrameStartNs_ = -1;
    qint64 lastProfileCpuFrameNs_ = 0;
    double profileCpuPrepTotalMs_ = 0.0;
    double profileCpuUploadTotalMs_ = 0.0;
    double profileGpuDrawTotalMs_ = 0.0;
    quint64 profileFrameCount_ = 0;
    quint64 profileGpuSampleCount_ = 0;
    QVector<double> profileCpuPrepSamplesMs_;
    QVector<double> profileCpuUploadSamplesMs_;
    QVector<double> profileGpuDrawSamplesMs_;
    QVector<double> profilePresentApproxSamplesMs_;
    QVector<double> profileTickToPaintSamplesMs_;
    QVector<double> profileVideoMapSamplesMs_;
    QVector<double> profileVideoUploadSamplesMs_;
    qint64 pendingTickToPaintStartNs_ = -1;
    bool gpuTimerQueriesSupported_ = false;
    GLuint gpuTimeQueries_[4] = {0, 0, 0, 0};
    bool gpuTimeQueryPending_[4] = {false, false, false, false};
    int gpuTimeQueryCursor_ = 0;
    quint64 skinLoadGeneration_ = 0;
    QTimer* texturePrewarmTimer_ = nullptr;
    QVector<QImage> pendingTexturePrewarmImages_;
    qint64 lastSkinLoadDispatchMs_ = -1;
    qint64 texturePrewarmStartMs_ = -1;
    QOffscreenSurface* offscreenSurface_ = nullptr;
    QOpenGLContext* offscreenContext_ = nullptr;
    QOpenGLFramebufferObject* offscreenFramebuffer_ = nullptr;
    QSize offscreenFramebufferSize_;
    GLuint offscreenReadbackPbos_[2] = {0, 0};
    QSize offscreenReadbackPboSize_;
    qsizetype offscreenReadbackPboBytes_ = 0;
    int offscreenReadbackPboWriteIndex_ = 0;
    int offscreenReadbackPendingIndex_ = -1;
    qint64 offscreenDrawNsLastFrame_ = 0;
    qint64 offscreenReadbackNsLastFrame_ = 0;
};
