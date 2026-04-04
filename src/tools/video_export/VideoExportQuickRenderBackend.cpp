#include "tools/video_export/VideoExportQuickRenderBackend.h"

#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"

namespace {

double normalizedFlowSpeed(double flowSpeed)
{
    return miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
}

double normalizedLayoutScale(double scale)
{
    return miacode::preview_video::normalizedLayoutSquareScale(scale);
}

}  // namespace

bool VideoExportQuickRenderBackend::bootstrap(
    const VideoExportTask& task,
    bool stageMediaAvailable,
    const QVector<TimelineNoteMarker>& noteMarkers,
    const MuriAnalysisReport& muriAnalysisReport,
    const QSize& frameSize,
    QString* errorMessage)
{
    assets_.setStageMediaAvailable(stageMediaAvailable);
    if (!assets_.loadSkinDirectorySync(task.skinDirectory)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to load Quick export skin assets");
        }
        return false;
    }

    frameState_ = miacode::preview::scene::PreviewFrameState();
    frameState_.noteMarkers = noteMarkers;
    frameState_.muriAnalysisReport = muriAnalysisReport;
    frameState_.muriRenderOptions = task.muriRenderOptions;
    frameState_.media.stageMediaAvailable = stageMediaAvailable;
    frameState_.render.backgroundBrightnessOuter = task.backgroundBrightnessOuter;
    frameState_.render.backgroundBrightnessInner = task.backgroundBrightnessInner;
    frameState_.render.layoutSquareScale = normalizedLayoutScale(task.layoutSquareScale);
    frameState_.render.smoothBrightness = task.smoothBrightness;
    frameState_.render.backgroundScaleMode = task.backgroundScaleMode;
    frameState_.render.noteFlowSpeed = normalizedFlowSpeed(task.noteFlowSpeed);
    frameState_.render.showDebugInfo = false;
    frameState_.render.showTimestamp = task.showTimestamp;
    frameState_.render.showObjectStatsHud = task.showObjectStatsHud;
    refreshAssetState();

    session_.setLayerFlags(miacode::preview::scene::kPreviewExportOverlayRenderLayers);
    session_.setFrameSize(frameSize);
    session_.setFrameState(frameState_);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

void VideoExportQuickRenderBackend::copyRenderStateFrom(const VideoExportQuickRenderBackend& source)
{
    frameState_ = source.frameState_;
    lastRenderStats_ = source.lastRenderStats_;
    requestedFormat_ = source.requestedFormat_;
    shareContext_ = source.shareContext_;
    session_.setLayerFlags(source.session_.layerFlags());
}

void VideoExportQuickRenderBackend::setStageMediaAvailable(bool hasMedia)
{
    assets_.setStageMediaAvailable(hasMedia);
    frameState_.media.stageMediaAvailable = hasMedia;
    refreshAssetState();
}

void VideoExportQuickRenderBackend::setBackgroundBrightnessOuter(double brightness)
{
    frameState_.render.backgroundBrightnessOuter = qBound(0.0, brightness, 1.0);
}

void VideoExportQuickRenderBackend::setBackgroundBrightnessInner(double brightness)
{
    frameState_.render.backgroundBrightnessInner = qBound(0.0, brightness, 1.0);
}

void VideoExportQuickRenderBackend::setLayoutSquareScale(double scale)
{
    frameState_.render.layoutSquareScale = normalizedLayoutScale(scale);
}

void VideoExportQuickRenderBackend::setSmoothBrightness(bool smooth)
{
    frameState_.render.smoothBrightness = smooth;
}

void VideoExportQuickRenderBackend::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    frameState_.render.backgroundScaleMode = mode;
}

void VideoExportQuickRenderBackend::setNoteFlowSpeed(double flowSpeed)
{
    frameState_.render.noteFlowSpeed = normalizedFlowSpeed(flowSpeed);
}

void VideoExportQuickRenderBackend::setShowDebugInfo(bool show)
{
    frameState_.render.showDebugInfo = show;
}

void VideoExportQuickRenderBackend::setShowTimestamp(bool show)
{
    frameState_.render.showTimestamp = show;
}

void VideoExportQuickRenderBackend::setShowObjectStatsHud(bool show)
{
    frameState_.render.showObjectStatsHud = show;
}

void VideoExportQuickRenderBackend::setNoteMarkers(const QVector<TimelineNoteMarker>& notes)
{
    frameState_.noteMarkers = notes;
}

void VideoExportQuickRenderBackend::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    frameState_.muriAnalysisReport = report;
}

void VideoExportQuickRenderBackend::setMuriRenderOptions(const MuriRenderOptions& options)
{
    frameState_.muriRenderOptions = options;
}

bool VideoExportQuickRenderBackend::hasCoreSkinAssetsLoadedForDebug() const
{
    return assets_.hasCoreSkinAssetsLoaded();
}

bool VideoExportQuickRenderBackend::initializeOffscreenRenderer(
    const QSurfaceFormat& requestedFormat,
    QOpenGLContext* shareContext,
    QString* errorMessage)
{
    requestedFormat_ = requestedFormat;
    shareContext_ = shareContext;
    session_.setFrameState(frameState_);
    return session_.initialize(requestedFormat_, shareContext_, errorMessage);
}

void VideoExportQuickRenderBackend::shutdownOffscreenRenderer()
{
    session_.invalidate();
}

bool VideoExportQuickRenderBackend::supportsOffscreenPboReadback(QString* errorMessage) const
{
    return session_.supportsOffscreenPboReadback(errorMessage);
}

void VideoExportQuickRenderBackend::resetOffscreenPboReadback()
{
    session_.resetOffscreenPboReadback();
}

bool VideoExportQuickRenderBackend::renderOverlayFrameOffscreenPboStep(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud,
    QImage* completedFrame,
    bool* completedFrameReady,
    bool drainOnly,
    QString* errorMessage)
{
    session_.setFrameSize(outputSize);
    updateFrameStateForRender(playheadSeconds, showTimestamp, showObjectStatsHud);
    session_.setFrameState(frameState_);
    const bool ok = session_.renderFramePboStep(
        completedFrame,
        completedFrameReady,
        drainOnly,
        errorMessage
    );
    lastRenderStats_ = session_.lastRenderStats();
    return ok;
}

QImage VideoExportQuickRenderBackend::renderOverlayFrame(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud)
{
    return renderOverlayFrameOffscreen(outputSize, playheadSeconds, showTimestamp, showObjectStatsHud);
}

QImage VideoExportQuickRenderBackend::renderOverlayFrameOffscreen(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud)
{
    session_.setFrameSize(outputSize);
    updateFrameStateForRender(playheadSeconds, showTimestamp, showObjectStatsHud);
    session_.setFrameState(frameState_);
    const QImage frame = session_.renderFrame();
    lastRenderStats_ = session_.lastRenderStats();
    return frame;
}

bool VideoExportQuickRenderBackend::isGpuRendererReadyForDebug() const
{
    return session_.isInitialized();
}

bool VideoExportQuickRenderBackend::usedGpuRendererLastFrameForDebug() const
{
    return true;
}

int VideoExportQuickRenderBackend::cpuFallbackCountLastFrameForDebug() const
{
    return 0;
}

qint64 VideoExportQuickRenderBackend::offscreenDrawNsLastFrameForDebug() const
{
    return lastRenderStats_.renderNs;
}

qint64 VideoExportQuickRenderBackend::offscreenReadbackNsLastFrameForDebug() const
{
    return lastRenderStats_.readbackNs;
}

double VideoExportQuickRenderBackend::layoutRingDiameterRatio() const
{
    return frameState_.assets.layoutRingDiameterRatio;
}

void VideoExportQuickRenderBackend::refreshAssetState()
{
    frameState_.assets = assets_.assetState();
    frameState_.skin = assets_.skinAssets();
    frameState_.judgeOverlay = assets_.judgeOverlayAssets();
    frameState_.judgeEffect = assets_.judgeEffectAssets();
}

void VideoExportQuickRenderBackend::updateFrameStateForRender(
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud)
{
    frameState_.playheadSeconds = qMax(0.0, playheadSeconds);
    frameState_.render.showTimestamp = showTimestamp;
    frameState_.render.showObjectStatsHud = showObjectStatsHud;
    frameState_.usedGpuRendererThisFrame = true;
    frameState_.cpuFallbackCount = 0;
    frameState_.fpsDisplay = 0.0;
}
