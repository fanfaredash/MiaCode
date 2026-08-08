#pragma once

#include <QString>
#include <QSurfaceFormat>

#include <limits>

#include "PreviewRenderSettings.h"
#include "VideoExportController.h"
#include "preview/runtime/PreviewQuickD3D11ExportSession.h"
#include "preview/runtime/PreviewQuickExportSession.h"
#include "preview/runtime/PreviewSceneAssetRepository.h"

class QOpenGLContext;

// P5.3 — which offscreen render session the backend drives. OpenGL is the
// stable default; D3D11Qrhi is the plan-P5 QRhi/D3D11 session, selected only
// via the hidden MIACODE_EXPORT_RENDER_BACKEND switch (resolved by the caller
// in VideoExportPreparedTask, which also owns the OpenGL fallback on init
// failure). Must be set BEFORE initializeOffscreenRenderer.
enum class ExportQuickRenderSessionBackend {
    OpenGl,
    D3D11Qrhi,
};

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
    void setTapJudgeTextDistance(PreviewTapJudgeTextDistance distance);
    void setShowDebugInfo(bool show);
    void setShowTimestamp(bool show);
    void setShowObjectStatsHud(bool show);
    void setNoteMarkers(const QVector<TimelineNoteMarker>& notes);
    void setMuriAnalysisReport(const MuriAnalysisReport& report);
    void setMuriRenderOptions(const MuriRenderOptions& options);

    bool hasCoreSkinAssetsLoadedForDebug() const;
    // P5.3 backend selection. Call before initializeOffscreenRenderer; when
    // falling back D3D11→OpenGL, shutdownOffscreenRenderer first, then switch
    // and re-initialize (both sessions carry the same bootstrapped state).
    void setRenderSessionBackend(ExportQuickRenderSessionBackend backend);
    ExportQuickRenderSessionBackend renderSessionBackend() const { return sessionBackend_; }
    bool initializeOffscreenRenderer(
        const QSurfaceFormat& requestedFormat = QSurfaceFormat(),
        QOpenGLContext* shareContext = nullptr,
        QString* errorMessage = nullptr
    );
    void shutdownOffscreenRenderer();
    void setPreservePremultipliedReadback(bool preserve);
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
    qint64 stateUpdateNsLastFrameForDebug() const;
    qint64 polishNsLastFrameForDebug() const;
    qint64 syncNsLastFrameForDebug() const;
    qint64 renderSubmitNsLastFrameForDebug() const;
    // P1 — actual GL renderer string for the offscreen export session (empty
    // until the offscreen renderer is initialized).
    QString lastGlRendererForDebug() const { return session_.lastGlRenderer(); }
    // P5.3 — backend-appropriate "which GPU" string for the render_backend
    // summary: GL renderer string on OpenGL, DXGI adapter description on D3D11.
    QString adapterOrRendererForDebug() const;
    QString d3d11AdapterLuidForDebug() const { return d3d11Session_.adapterLuidForDebug(); }
    QString d3d11RenderTargetFormatForDebug() const
    {
        return d3d11Session_.renderTargetFormatNameForDebug();
    }
    qsizetype d3d11ReadbackRowPitchForDebug() const
    {
        return d3d11Session_.lastReadbackRowPitchForDebug();
    }
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
    // P5 — the D3D11/QRhi session runs in parallel to the OpenGL one; state
    // setters mirror into both (cheap while uninitialized) so a D3D11→OpenGL
    // fallback re-init needs no state replay. Only one is ever initialized.
    PreviewQuickD3D11ExportSession d3d11Session_;
    ExportQuickRenderSessionBackend sessionBackend_ = ExportQuickRenderSessionBackend::OpenGl;
    miacode::preview::scene::PreviewFrameState frameState_;
    PreviewQuickExportRenderStats lastRenderStats_;
    QSurfaceFormat requestedFormat_;
    QOpenGLContext* shareContext_ = nullptr;
};
