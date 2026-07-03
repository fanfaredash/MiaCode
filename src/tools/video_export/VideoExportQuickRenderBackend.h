#pragma once

#include <QString>
#include <QSurfaceFormat>

#include <limits>

#include "PreviewRenderSettings.h"
#include "VideoExportController.h"
#include "preview/runtime/PreviewQuickExportSession.h"
#include "preview/runtime/PreviewSceneAssetRepository.h"

class QOpenGLContext;

class VideoExportQuickRenderBackend
{
public:
    bool bootstrap(
        const VideoExportTask& task,
        bool stageMediaAvailable,
        const QVector<TimelineNoteMarker>& noteMarkers,
        const MuriAnalysisReport& muriAnalysisReport,
        const QSize& frameSize,
        QString* errorMessage = nullptr
    );

    void copyRenderStateFrom(const VideoExportQuickRenderBackend& source);

    void setStageMediaAvailable(bool hasMedia);
    void setBackgroundBrightnessOuter(double brightness);
    void setBackgroundBrightnessInner(double brightness);
    void setLayoutSquareScale(double scale);
    void setSmoothBrightness(bool smooth);
    void setOutlineVariant(PreviewOutlineVariant variant);
    void setBackgroundScaleMode(PreviewBackgroundScaleMode mode);
    void setTapFlowSpeed(double flowSpeed);
    void setTouchFlowSpeed(double flowSpeed);
    void setNoteFlowSpeed(double flowSpeed);
    void setShowDebugInfo(bool show);
    void setShowTimestamp(bool show);
    void setShowObjectStatsHud(bool show);
    void setNoteMarkers(const QVector<TimelineNoteMarker>& notes);
    void setMuriAnalysisReport(const MuriAnalysisReport& report);
    void setMuriRenderOptions(const MuriRenderOptions& options);

    bool hasCoreSkinAssetsLoadedForDebug() const;
    bool initializeOffscreenRenderer(
        const QSurfaceFormat& requestedFormat = QSurfaceFormat(),
        QOpenGLContext* shareContext = nullptr,
        QString* errorMessage = nullptr
    );
    void shutdownOffscreenRenderer();
    bool supportsOffscreenPboReadback(QString* errorMessage = nullptr) const;
    void resetOffscreenPboReadback();

    // Pre-roll intro overlay (full-range exports). setupIntro mounts the QML
    // scene + pushes the banner data once; setIntroFrame is called per output
    // frame to advance/hide it. Both no-op if the offscreen renderer isn't up.
    bool setupIntro(const IntroBannerSpec& intro, QString* errorMessage = nullptr);
    void setIntroFrame(int authoringFrame, bool active);
    bool renderOverlayFrameOffscreenPboStep(
        const QSize& outputSize,
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud,
        QImage* completedFrame,
        bool* completedFrameReady,
        bool drainOnly,
        QString* errorMessage = nullptr,
        double hudPlayheadSecondsOverride = std::numeric_limits<double>::quiet_NaN()
    );

    QImage renderOverlayFrame(
        const QSize& outputSize,
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud,
        double hudPlayheadSecondsOverride = std::numeric_limits<double>::quiet_NaN());
    QImage renderOverlayFrameOffscreen(
        const QSize& outputSize,
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud,
        double hudPlayheadSecondsOverride = std::numeric_limits<double>::quiet_NaN()
    );

    bool isGpuRendererReadyForDebug() const;
    bool usedGpuRendererLastFrameForDebug() const;
    int cpuFallbackCountLastFrameForDebug() const;
    qint64 offscreenDrawNsLastFrameForDebug() const;
    qint64 offscreenReadbackNsLastFrameForDebug() const;
    // P1 — actual GL renderer string for the offscreen export session (empty
    // until the offscreen renderer is initialized).
    QString lastGlRendererForDebug() const { return session_.lastGlRenderer(); }
    double layoutRingDiameterRatio() const;

private:
    void refreshAssetState();
    void syncSessionStateIfInitialized();
    void updateFrameStateForRender(
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud,
        double hudPlayheadSecondsOverride = std::numeric_limits<double>::quiet_NaN());

    miacode::preview::runtime::PreviewSceneAssetRepository assets_;
    PreviewQuickExportSession session_;
    miacode::preview::scene::PreviewFrameState frameState_;
    PreviewQuickExportRenderStats lastRenderStats_;
    QSurfaceFormat requestedFormat_;
    QOpenGLContext* shareContext_ = nullptr;
};
