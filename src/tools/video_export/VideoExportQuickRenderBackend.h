#pragma once

#include <QString>
#include <QSurfaceFormat>

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
    void setBackgroundScaleMode(PreviewBackgroundScaleMode mode);
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

    QImage renderOverlayFrame(const QSize& outputSize, double playheadSeconds, bool showTimestamp, bool showObjectStatsHud);
    QImage renderOverlayFrameOffscreen(
        const QSize& outputSize,
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud
    );

    bool isGpuRendererReadyForDebug() const;
    bool usedGpuRendererLastFrameForDebug() const;
    int cpuFallbackCountLastFrameForDebug() const;
    qint64 offscreenDrawNsLastFrameForDebug() const;
    qint64 offscreenReadbackNsLastFrameForDebug() const;
    double layoutRingDiameterRatio() const;

private:
    void refreshAssetState();
    void updateFrameStateForRender(double playheadSeconds, bool showTimestamp, bool showObjectStatsHud);

    miacode::preview::runtime::PreviewSceneAssetRepository assets_;
    PreviewQuickExportSession session_;
    miacode::preview::scene::PreviewFrameState frameState_;
    PreviewQuickExportRenderStats lastRenderStats_;
    QSurfaceFormat requestedFormat_;
    QOpenGLContext* shareContext_ = nullptr;
};
