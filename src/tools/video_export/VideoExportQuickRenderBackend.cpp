#include "tools/video_export/VideoExportQuickRenderBackend.h"

#include "common/IntroConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "core/scene/PreviewProgressStatsCache.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace {

double normalizedFlowSpeed(double flowSpeed)
{
    return miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
}

double normalizedLayoutScale(double scale)
{
    return miacode::preview_video::normalizedLayoutSquareScale(scale);
}

std::shared_ptr<const miacode::preview::scene::PreviewProgressStatsCache> buildProgressStatsCache(
    const QVector<TimelineNoteMarker>& noteMarkers
)
{
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(noteMarkers);
    return cache;
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
    assets_.setOutlineVariant(task.outlineVariant);
    assets_.setOutlineImagePath(task.outlineImagePath);
    assets_.setStageMediaAvailable(stageMediaAvailable);
    if (!assets_.loadSkinDirectorySync(task.skinDirectory)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to load Quick export skin assets");
        }
        return false;
    }

    frameState_ = miacode::preview::scene::PreviewFrameState();
    frameState_.noteMarkers = noteMarkers;
    frameState_.progressStatsCache = buildProgressStatsCache(noteMarkers);
    frameState_.muriAnalysisReport = muriAnalysisReport;
    frameState_.muriRenderOptions = task.muriRenderOptions;
    frameState_.sceneContentRevision = 1;
    frameState_.media.stageMediaAvailable = stageMediaAvailable;
    frameState_.render.backgroundBrightnessOuter = task.backgroundBrightnessOuter;
    frameState_.render.backgroundBrightnessInner = task.backgroundBrightnessInner;
    frameState_.render.layoutSquareScale = normalizedLayoutScale(task.layoutSquareScale);
    frameState_.render.smoothBrightness = task.smoothBrightness;
    frameState_.render.backgroundScaleMode = task.backgroundScaleMode;
    frameState_.render.tapFlowSpeed = normalizedFlowSpeed(task.tapFlowSpeed);
    frameState_.render.touchFlowSpeed = normalizedFlowSpeed(task.touchFlowSpeed);
    frameState_.render.slideEarlierSecondAndTextOnTop = task.slideEarlierSecondAndTextOnTop;
    frameState_.render.showDebugInfo = false;
    frameState_.render.showTimestamp = task.showTimestamp;
    frameState_.render.showObjectStatsHud = task.showObjectStatsHud;
    frameState_.render.showChartInfoHud = task.showChartInfoHud;
    frameState_.chartTitle = task.chartTitle;
    frameState_.chartArtist = task.chartArtist;
    frameState_.chartDifficultyLabel = task.chartDifficultyLabel;
    frameState_.chartDesigner = task.chartDesigner;
    frameState_.render.centerDisplayMode = task.centerDisplayMode;
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
    assets_.setOutlineVariant(source.assets_.outlineVariant());
    assets_.setOutlineImagePath(source.assets_.outlineImagePath());
    assets_.setStageMediaAvailable(source.assets_.stageMediaAvailable());
    frameState_ = source.frameState_;
    lastRenderStats_ = source.lastRenderStats_;
    requestedFormat_ = source.requestedFormat_;
    shareContext_ = source.shareContext_;
    session_.setLayerFlags(source.session_.layerFlags());
    refreshAssetState();
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setStageMediaAvailable(bool hasMedia)
{
    assets_.setStageMediaAvailable(hasMedia);
    frameState_.media.stageMediaAvailable = hasMedia;
    refreshAssetState();
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setBackgroundBrightnessOuter(double brightness)
{
    frameState_.render.backgroundBrightnessOuter = qBound(0.0, brightness, 1.0);
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setBackgroundBrightnessInner(double brightness)
{
    frameState_.render.backgroundBrightnessInner = qBound(0.0, brightness, 1.0);
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setLayoutSquareScale(double scale)
{
    frameState_.render.layoutSquareScale = normalizedLayoutScale(scale);
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setSmoothBrightness(bool smooth)
{
    frameState_.render.smoothBrightness = smooth;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setOutlineVariant(PreviewOutlineVariant variant)
{
    assets_.setOutlineVariant(variant);
    refreshAssetState();
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    frameState_.render.backgroundScaleMode = mode;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setTapFlowSpeed(double flowSpeed)
{
    frameState_.render.tapFlowSpeed = normalizedFlowSpeed(flowSpeed);
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setTouchFlowSpeed(double flowSpeed)
{
    frameState_.render.touchFlowSpeed = normalizedFlowSpeed(flowSpeed);
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setNoteFlowSpeed(double flowSpeed)
{
    const double normalizedSpeed = normalizedFlowSpeed(flowSpeed);
    frameState_.render.tapFlowSpeed = normalizedSpeed;
    frameState_.render.touchFlowSpeed = normalizedSpeed;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setShowDebugInfo(bool show)
{
    frameState_.render.showDebugInfo = show;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setShowTimestamp(bool show)
{
    frameState_.render.showTimestamp = show;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setShowObjectStatsHud(bool show)
{
    frameState_.render.showObjectStatsHud = show;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setNoteMarkers(const QVector<TimelineNoteMarker>& notes)
{
    frameState_.noteMarkers = notes;
    frameState_.progressStatsCache = buildProgressStatsCache(notes);
    frameState_.sceneContentRevision += 1;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    frameState_.muriAnalysisReport = report;
    frameState_.sceneContentRevision += 1;
    syncSessionStateIfInitialized();
}

void VideoExportQuickRenderBackend::setMuriRenderOptions(const MuriRenderOptions& options)
{
    frameState_.muriRenderOptions = options;
    frameState_.sceneContentRevision += 1;
    syncSessionStateIfInitialized();
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
    if (!session_.initialize(requestedFormat_, shareContext_, errorMessage)) {
        return false;
    }
    session_.setFrameState(frameState_);
    return true;
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

bool VideoExportQuickRenderBackend::setupIntro(const IntroBannerSpec& intro, QString* errorMessage)
{
    if (!session_.setupIntroOverlay(
            QUrl(QString::fromLatin1(miacode::intro::kOverlayQmlUrl)),
            errorMessage)) {
        return false;
    }
    const QVariantMap track = introBannerTrackMap(intro);
    const QUrl jacketUrl = intro.jacketPath.isEmpty()
        ? QUrl()
        : QUrl::fromLocalFile(intro.jacketPath);

    // Load + parse the banner template here (C++), not via the QML's async
    // XMLHttpRequest: the headless export render loop never pumps the event
    // loop, so the XHR callback would never fire and the card would render on
    // its empty defaultTemplate(). The qrc alias prefix "/intro" maps the
    // bundled template to ":/intro/templates/maimai_banner.json".
    QVariantMap templateMap;
    QFile templateFile(QStringLiteral(":/intro/templates/maimai_banner.json"));
    if (templateFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(templateFile.readAll());
        if (doc.isObject()) {
            templateMap = doc.object().toVariantMap();
        }
    }
    if (templateMap.isEmpty() && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("intro banner template could not be loaded from qrc");
    }

    session_.setIntroBannerData(
        track,
        templateMap,
        jacketUrl,
        QUrl(QString::fromLatin1(miacode::intro::kLogoFallbackUrl)),
        introBannerStyleMap(intro));
    return true;
}

void VideoExportQuickRenderBackend::setIntroFrame(int authoringFrame, bool active)
{
    session_.setIntroFrame(authoringFrame, active);
}

bool VideoExportQuickRenderBackend::renderOverlayFrameOffscreenPboStep(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud,
    QImage* completedFrame,
    bool* completedFrameReady,
    bool drainOnly,
    QString* errorMessage,
    double hudPlayheadSecondsOverride)
{
    session_.setFrameSize(outputSize);
    updateFrameStateForRender(
        playheadSeconds,
        showTimestamp,
        showObjectStatsHud,
        hudPlayheadSecondsOverride);
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
    bool showObjectStatsHud,
    double hudPlayheadSecondsOverride)
{
    return renderOverlayFrameOffscreen(
        outputSize,
        playheadSeconds,
        showTimestamp,
        showObjectStatsHud,
        hudPlayheadSecondsOverride);
}

QImage VideoExportQuickRenderBackend::renderOverlayFrameOffscreen(
    const QSize& outputSize,
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud,
    double hudPlayheadSecondsOverride)
{
    session_.setFrameSize(outputSize);
    updateFrameStateForRender(
        playheadSeconds,
        showTimestamp,
        showObjectStatsHud,
        hudPlayheadSecondsOverride);
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

void VideoExportQuickRenderBackend::syncSessionStateIfInitialized()
{
    if (!session_.isInitialized()) {
        return;
    }
    session_.setFrameState(frameState_);
}

void VideoExportQuickRenderBackend::updateFrameStateForRender(
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud,
    double hudPlayheadSecondsOverride)
{
    session_.applyExportFrameTick(
        playheadSeconds,
        showTimestamp,
        showObjectStatsHud,
        true,
        0,
        0.0,
        hudPlayheadSecondsOverride
    );
}
