#include "preview/runtime/PreviewRuntime.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/runtime/PreviewQuickRuntimeSurface.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQuickWindow>
#include <QTextStream>
#include <QWindow>

namespace {

PreviewRuntimeLayerProfileAggregate& ensureLayerAggregate(
    QVector<PreviewRuntimeLayerProfileAggregate>* aggregates,
    const QString& name
)
{
    Q_ASSERT(aggregates != nullptr);

    for (PreviewRuntimeLayerProfileAggregate& aggregate : *aggregates) {
        if (aggregate.name == name) {
            return aggregate;
        }
    }

    aggregates->append(PreviewRuntimeLayerProfileAggregate{name});
    return aggregates->last();
}

double averageOrZero(double total, qint64 count)
{
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

double frameLayerBuildMs(const PreviewTextureStats& frameStats)
{
    double total = 0.0;
    for (const PreviewTextureLayerStats& layerStat : frameStats.layerStats) {
        total += layerStat.buildMs;
    }
    return total;
}

}  // namespace

PreviewRuntime::PreviewRuntime(bool enableLegacySurface, QObject* parent)
    : QObject(parent)
    , legacySurfaceEnabled_(enableLegacySurface)
{
    assets_ = new miacode::preview::runtime::PreviewSceneAssetRepository(this);
    refreshAssetStateFromRepository();
    presentedFrameIntervalsMs_.resize(120);
    presentedFrameIntervalsMs_.fill(0.0);
    if (legacySurfaceEnabled_) {
        surface_ = new PreviewQuickRuntimeSurface(this);
        surface_->setRuntime(this);

        connect(surface_, &PreviewQuickRuntimeSurface::framePresented, this, [this]() {
            PreviewTextureStats stats;
            if (surface_ != nullptr) {
                stats = surface_->textureStats();
            }
            handlePresentedFrame(surface_ != nullptr ? &stats : nullptr);
        });
    }
    connect(assets_, &miacode::preview::runtime::PreviewSceneAssetRepository::assetsChanged, this, [this]() {
        refreshAssetStateFromRepository();
        update();
    });
}

PreviewRuntime::~PreviewRuntime()
{
    if (profilingSummaryDirty_) {
        writeProfilingSummaryToFile();
    }
}

QWindow* PreviewRuntime::hostWindow() const
{
    return surface_ != nullptr ? surface_->hostWindow() : nullptr;
}

void PreviewRuntime::setVisibleHostWindow(QQuickWindow* window)
{
    if (visibleHostWindow_ == window) {
        return;
    }
    visibleHostWindow_ = window;
}

void PreviewRuntime::clearVisibleHostWindow(QQuickWindow* window)
{
    if (visibleHostWindow_ != window) {
        return;
    }
    visibleHostWindow_.clear();
}

void PreviewRuntime::notifyVisibleFramePresented()
{
    handlePresentedFrame();
}

void PreviewRuntime::requestActivate()
{
    if (visibleHostWindow_ != nullptr
        && (surface_ == nullptr || visibleHostWindow_ != surface_->hostWindow())) {
        visibleHostWindow_->requestActivate();
        return;
    }
    if (surface_ != nullptr) {
        surface_->requestActivate();
    }
}

void PreviewRuntime::update()
{
    pendingPresentedStatsRefresh_ = true;
    emit frameStateChanged();
    if (visibleHostWindow_ != nullptr
        && (surface_ == nullptr || visibleHostWindow_ != surface_->hostWindow())) {
        visibleHostWindow_->requestUpdate();
    }
    if (surface_ != nullptr) {
        surface_->requestFrame();
    }
}

void PreviewRuntime::setStageMediaAvailable(bool hasMedia)
{
    if (assets_ != nullptr) {
        assets_->setStageMediaAvailable(hasMedia);
    }
    frameState_.media.stageMediaAvailable = hasMedia;
    update();
}

void PreviewRuntime::setStageMediaPresentationMode(
    miacode::preview::scene::PreviewStageMediaPresentationMode mode,
    bool requestUpdate)
{
    if (frameState_.media.presentationMode == mode) {
        return;
    }
    frameState_.media.presentationMode = mode;
    if (requestUpdate) {
        update();
    }
}

void PreviewRuntime::setExternalStageMediaDebugState(
    miacode::preview::scene::PreviewExternalStageMediaType mediaType,
    bool videoPlaybackActive,
    double playbackSecond,
    double clockDeltaSeconds,
    qint64 videoFrameAgeMs,
    bool requestUpdate)
{
    frameState_.media.externalMediaType = mediaType;
    frameState_.media.externalVideoPlaybackActive = videoPlaybackActive;
    frameState_.media.externalPlaybackSecond = qMax(0.0, playbackSecond);
    frameState_.media.externalClockDeltaSeconds = clockDeltaSeconds;
    frameState_.media.externalVideoFrameAgeMs = videoFrameAgeMs;
    if (requestUpdate) {
        update();
    }
}

void PreviewRuntime::setExternalStageMediaProfileSummary(
    bool separateSurfaceActive,
    bool hasResolvedMedia,
    bool hasVideoMedia,
    const QString& mediaTypeName,
    qint64 videoFrameCountTotal)
{
    externalStageMediaSeparateSurfaceActive_ = separateSurfaceActive;
    externalStageMediaHasResolvedMedia_ = hasResolvedMedia;
    externalStageMediaHasVideoMedia_ = hasVideoMedia;
    externalStageMediaMediaTypeName_ = mediaTypeName.trimmed().isEmpty()
        ? QStringLiteral("none")
        : mediaTypeName.trimmed();
    externalStageMediaVideoFrameCountTotal_ = qMax<qint64>(0, videoFrameCountTotal);
}

void PreviewRuntime::setPlayheadSeconds(double seconds, bool requestUpdate)
{
    frameState_.playheadSeconds = qMax(0.0, seconds);
    if (requestUpdate) {
        update();
    }
}

void PreviewRuntime::setMediaFrame(const QImage& frame)
{
    frameState_.media.mediaFrame = frame;
    frameState_.media.resolvedStageImage = QImage();
    frameState_.media.stageMediaSerial = 0;
    frameState_.media.resolvedStageImageCacheable = false;
    frameState_.media.resolvedStageImageToImageMs = 0.0;
#ifdef HAVE_QT_MULTIMEDIA
    frameState_.media.videoFrame = QVideoFrame();
#endif
    frameState_.media.retainedVideoFallbackFrame = QImage();
    setStageMediaAvailable(!frame.isNull());
}

void PreviewRuntime::setVideoFrame(const QVideoFrame& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    frameState_.media.videoFrame = frame;
    if (!frame.isValid()) {
        frameState_.media.resolvedStageImage = QImage();
        frameState_.media.resolvedStageImageCacheable = false;
        frameState_.media.resolvedStageImageToImageMs = 0.0;
    }
#else
    Q_UNUSED(frame);
#endif
}

void PreviewRuntime::setResolvedStageVideoFrame(
    const QVideoFrame& frame,
    const QImage& resolvedImage,
    bool cacheable,
    quint64 serial,
    double toImageMs
)
{
#ifdef HAVE_QT_MULTIMEDIA
    frameState_.media.videoFrame = frame;
#else
    Q_UNUSED(frame);
#endif
    frameState_.media.resolvedStageImage = resolvedImage;
    frameState_.media.resolvedStageImageCacheable = !resolvedImage.isNull() && cacheable;
    frameState_.media.stageMediaSerial = serial;
    frameState_.media.resolvedStageImageToImageMs = !resolvedImage.isNull() ? qMax(0.0, toImageMs) : 0.0;
    update();
}

void PreviewRuntime::setRetainedVideoFallbackFrame(const QImage& frame)
{
    frameState_.media.retainedVideoFallbackFrame = frame;
    update();
}

void PreviewRuntime::setNoteMarkers(const QVector<TimelineNoteMarker>& notes)
{
    frameState_.noteMarkers = notes;
    frameState_.sceneContentRevision += 1;
    update();
}

void PreviewRuntime::setProgressStatsCache(
    std::shared_ptr<const miacode::preview::scene::PreviewProgressStatsCache> cache
)
{
    frameState_.progressStatsCache = std::move(cache);
    update();
}

void PreviewRuntime::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    frameState_.muriAnalysisReport = report;
    frameState_.sceneContentRevision += 1;
    update();
}

void PreviewRuntime::setMuriRenderOptions(const MuriRenderOptions& options)
{
    frameState_.muriRenderOptions = options;
    frameState_.sceneContentRevision += 1;
    update();
}

void PreviewRuntime::setSkinDirectory(const QString& skinDir)
{
    if (assets_ != nullptr) {
        assets_->setSkinDirectory(skinDir);
    }
}

void PreviewRuntime::setOutlineVariant(PreviewOutlineVariant variant)
{
    if (assets_ != nullptr) {
        assets_->setOutlineVariant(variant);
    }
}

void PreviewRuntime::setBackgroundBrightness(double brightness)
{
    frameState_.render.backgroundBrightnessOuter = qBound(0.0, brightness, 1.0);
    frameState_.render.backgroundBrightnessInner = qBound(0.0, brightness, 1.0);
    update();
}

void PreviewRuntime::setBackgroundBrightnessOuter(double brightness)
{
    frameState_.render.backgroundBrightnessOuter = qBound(0.0, brightness, 1.0);
    update();
}

void PreviewRuntime::setBackgroundBrightnessInner(double brightness)
{
    frameState_.render.backgroundBrightnessInner = qBound(0.0, brightness, 1.0);
    update();
}

void PreviewRuntime::setLayoutSquareScale(double scale)
{
    frameState_.render.layoutSquareScale = miacode::preview_video::normalizedLayoutSquareScale(scale);
    update();
}

void PreviewRuntime::setSmoothBrightness(bool smooth)
{
    frameState_.render.smoothBrightness = smooth;
    update();
}

void PreviewRuntime::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    frameState_.render.backgroundScaleMode = mode;
    update();
}

void PreviewRuntime::setNoteFlowSpeed(double flowSpeed)
{
    frameState_.render.noteFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    update();
}

void PreviewRuntime::setShowDebugInfo(bool show)
{
    frameState_.render.showDebugInfo = show;
    update();
}

void PreviewRuntime::setShowTimestamp(bool show)
{
    frameState_.render.showTimestamp = show;
    update();
}

void PreviewRuntime::setShowObjectStatsHud(bool show)
{
    frameState_.render.showObjectStatsHud = show;
    update();
}

bool PreviewRuntime::showTimestamp() const
{
    return frameState_.render.showTimestamp;
}

bool PreviewRuntime::showObjectStatsHud() const
{
    return frameState_.render.showObjectStatsHud;
}

void PreviewRuntime::reset()
{
    const auto presentationMode = frameState_.media.presentationMode;
    frameState_.noteMarkers.clear();
    frameState_.progressStatsCache.reset();
    frameState_.muriAnalysisReport = MuriAnalysisReport();
    frameState_.playheadSeconds = 0.0;
    frameState_.sceneContentRevision = 0;
    frameState_.media = miacode::preview::scene::PreviewMediaFrameState();
    frameState_.media.presentationMode = presentationMode;
    frameState_.fpsDisplay = 0.0;
    frameState_.cpuFallbackCount = 0;
    frameState_.usedGpuRendererThisFrame = false;
    if (assets_ != nullptr) {
        assets_->setStageMediaAvailable(false);
    }
    refreshAssetStateFromRepository();
    resetProfilingSession();
    pendingPresentedStatsRefresh_ = true;
    update();
}

void PreviewRuntime::noteTickForProfiling()
{
}

void PreviewRuntime::resetProfilingSession()
{
    presentTimer_.invalidate();
    lastPresentedNs_ = -1;
    presentedFrameIntervalsMs_.fill(0.0);
    presentedFrameIntervalWriteIndex_ = 0;
    presentedFrameIntervalCount_ = 0;
    frameState_.fpsDisplay = 0.0;
    profilingSummaryDirty_ = false;
    profiledTextureFrameCount_ = 0;
    profiledActiveSpriteFrameCount_ = 0;
    cachedTextureHitTotal_ = 0;
    cachedTextureCreateTotal_ = 0;
    transientTextureHitTotal_ = 0;
    transientTextureCreateTotal_ = 0;
    spriteCountTotal_ = 0;
    spriteBatchCountTotal_ = 0;
    spriteCountMax_ = 0;
    spriteBatchCountMax_ = 0;
    layerBuildMsTotal_ = 0.0;
    layerBuildMsMax_ = 0.0;
    peakFrameSpriteCount_ = 0;
    peakFrameSpriteBatchCount_ = 0;
    peakFrameLayerBuildMs_ = 0.0;
    layerProfileAggregates_.clear();
    stageBackgroundProfile_ = PreviewRuntimeStageBackgroundAggregate();
    externalStageMediaVideoFrameCountTotal_ = 0;
    presentedFrameCountTotal_ = 0;
}

void PreviewRuntime::updateTextureProfilingStats(const PreviewTextureStats& frameStats)
{
    profilingSummaryDirty_ = true;
    profiledTextureFrameCount_ += 1;
    cachedTextureHitTotal_ += frameStats.cachedHitCount;
    cachedTextureCreateTotal_ += frameStats.cachedCreateCount;
    transientTextureHitTotal_ += frameStats.transientHitCount;
    transientTextureCreateTotal_ += frameStats.transientCreateCount;
    spriteCountTotal_ += frameStats.spriteCount;
    spriteBatchCountTotal_ += frameStats.spriteBatchCount;
    spriteCountMax_ = qMax(spriteCountMax_, frameStats.spriteCount);
    spriteBatchCountMax_ = qMax(spriteBatchCountMax_, frameStats.spriteBatchCount);

    const double totalBuildMs = frameLayerBuildMs(frameStats);
    layerBuildMsTotal_ += totalBuildMs;
    layerBuildMsMax_ = qMax(layerBuildMsMax_, totalBuildMs);

    if (frameStats.spriteCount > 0) {
        profiledActiveSpriteFrameCount_ += 1;
    }
    if (frameStats.spriteCount > peakFrameSpriteCount_
        || (frameStats.spriteCount == peakFrameSpriteCount_ && totalBuildMs > peakFrameLayerBuildMs_)) {
        peakFrameSpriteCount_ = frameStats.spriteCount;
        peakFrameSpriteBatchCount_ = frameStats.spriteBatchCount;
        peakFrameLayerBuildMs_ = totalBuildMs;
    }

    const PreviewStageBackgroundFrameProfile& stage = frameStats.stageBackground;
    stageBackgroundProfile_.mediaFrameCount += stage.mediaFrameCount;
    stageBackgroundProfile_.dimFrameCount += stage.dimFrameCount;
    stageBackgroundProfile_.videoFrameCount += stage.videoFrameCount;
    stageBackgroundProfile_.staticImageFrameCount += stage.staticImageFrameCount;
    stageBackgroundProfile_.dimUniformUpdateCount += stage.dimUniformUpdateCount;

    if (stage.videoFrameCount > 0 && stage.mediaToImageMs > 0.0) {
        stageBackgroundProfile_.mediaToImageMsSum += stage.mediaToImageMs;
        stageBackgroundProfile_.mediaToImageMsMax =
            qMax(stageBackgroundProfile_.mediaToImageMsMax, stage.mediaToImageMs);
        stageBackgroundProfile_.mediaToImageSampleCount += 1;
    }
    if (stage.mediaFrameCount > 0) {
        stageBackgroundProfile_.mediaTextureMsSum += stage.mediaTextureMs;
        stageBackgroundProfile_.mediaTextureMsMax =
            qMax(stageBackgroundProfile_.mediaTextureMsMax, stage.mediaTextureMs);
        stageBackgroundProfile_.mediaTextureSampleCount += 1;
    }
    if (stage.dimFrameCount > 0) {
        stageBackgroundProfile_.dimUniformUpdateMsSum += stage.dimUniformUpdateMs;
        stageBackgroundProfile_.dimUniformUpdateMsMax =
            qMax(stageBackgroundProfile_.dimUniformUpdateMsMax, stage.dimUniformUpdateMs);
        stageBackgroundProfile_.dimUniformUpdateSampleCount += 1;
    }
    stageBackgroundProfile_.nodeUpdateMsSum += stage.nodeUpdateMs;
    stageBackgroundProfile_.nodeUpdateMsMax =
        qMax(stageBackgroundProfile_.nodeUpdateMsMax, stage.nodeUpdateMs);
    stageBackgroundProfile_.nodeUpdateSampleCount += 1;

    for (const PreviewTextureLayerStats& layerStat : frameStats.layerStats) {
        PreviewRuntimeLayerProfileAggregate& aggregate =
            ensureLayerAggregate(&layerProfileAggregates_, layerStat.name);
        aggregate.spriteCountSum += layerStat.spriteCount;
        aggregate.spriteBatchCountSum += layerStat.spriteBatchCount;
        aggregate.candidateCountSum += layerStat.candidateCount;
        aggregate.activeCountSum += layerStat.activeCount;
        aggregate.buildMsSum += layerStat.buildMs;
        aggregate.spriteCountMax = qMax(aggregate.spriteCountMax, layerStat.spriteCount);
        aggregate.spriteBatchCountMax = qMax(aggregate.spriteBatchCountMax, layerStat.spriteBatchCount);
        aggregate.candidateCountMax = qMax(aggregate.candidateCountMax, layerStat.candidateCount);
        aggregate.activeCountMax = qMax(aggregate.activeCountMax, layerStat.activeCount);
        aggregate.buildMsMax = qMax(aggregate.buildMsMax, layerStat.buildMs);
        if (layerStat.spriteCount > 0) {
            aggregate.spriteActiveFrameCount += 1;
        }
    }
}

QString PreviewRuntime::writeProfilingSummaryToFile()
{
    if (!miacode::debug_options::previewProfileOutputEnabled()
        || presentedFrameIntervalCount_ <= 0) {
        return QString();
    }

    const QString summaryPath = miacode::debug_log::previewProfileSummaryPath();
    const QFileInfo summaryInfo(summaryPath);
    if (!summaryInfo.absolutePath().isEmpty()) {
        QDir().mkpath(summaryInfo.absolutePath());
    }

    QFile file(summaryPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview_profile"),
            QStringLiteral("failed_to_open path=%1").arg(summaryPath)
        );
        return QString();
    }

    double sumMs = 0.0;
    double maxMs = 0.0;
    for (int index = 0; index < presentedFrameIntervalCount_; ++index) {
        const double sample = presentedFrameIntervalsMs_[index];
        sumMs += sample;
        maxMs = qMax(maxMs, sample);
    }
    const double avgMs = sumMs / static_cast<double>(presentedFrameIntervalCount_);
    QTextStream stream(&file);
    stream << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    stream << "profile_scope=session_accumulated" << '\n';
    stream << "frame_samples=" << presentedFrameIntervalCount_ << '\n';
    stream << "presented_total=" << presentedFrameCountTotal_ << '\n';
    stream << "present_avg_ms=" << QString::number(avgMs, 'f', 4) << '\n';
    stream << "present_max_ms=" << QString::number(maxMs, 'f', 4) << '\n';
    stream << "fps=" << QString::number(frameState_.fpsDisplay, 'f', 4) << '\n';
    stream << "external_stage_media.separate_surface_active="
           << (externalStageMediaSeparateSurfaceActive_ ? 1 : 0) << '\n';
    stream << "external_stage_media.has_resolved_media="
           << (externalStageMediaHasResolvedMedia_ ? 1 : 0) << '\n';
    stream << "external_stage_media.has_video_media="
           << (externalStageMediaHasVideoMedia_ ? 1 : 0) << '\n';
    stream << "external_stage_media.media_type=" << externalStageMediaMediaTypeName_ << '\n';
    stream << "external_stage_media.video_frames_total=" << externalStageMediaVideoFrameCountTotal_ << '\n';
    stream << "texture_profiled_frames=" << profiledTextureFrameCount_ << '\n';
    stream << "texture_active_sprite_frames=" << profiledActiveSpriteFrameCount_ << '\n';
    stream << "texture_cached_hits_total=" << cachedTextureHitTotal_ << '\n';
    stream << "texture_cached_creates_total=" << cachedTextureCreateTotal_ << '\n';
    stream << "texture_transient_hits_total=" << transientTextureHitTotal_ << '\n';
    stream << "texture_transient_creates_total=" << transientTextureCreateTotal_ << '\n';
    stream << "sprite_count_avg=" << QString::number(averageOrZero(static_cast<double>(spriteCountTotal_), profiledTextureFrameCount_), 'f', 4) << '\n';
    stream << "sprite_count_max=" << spriteCountMax_ << '\n';
    stream << "sprite_batch_count_avg=" << QString::number(averageOrZero(static_cast<double>(spriteBatchCountTotal_), profiledTextureFrameCount_), 'f', 4) << '\n';
    stream << "sprite_batch_count_max=" << spriteBatchCountMax_ << '\n';
    stream << "stage_bg.media_frames=" << stageBackgroundProfile_.mediaFrameCount << '\n';
    stream << "stage_bg.dim_frames=" << stageBackgroundProfile_.dimFrameCount << '\n';
    stream << "stage_bg.video_frames=" << stageBackgroundProfile_.videoFrameCount << '\n';
    stream << "stage_bg.static_image_frames=" << stageBackgroundProfile_.staticImageFrameCount << '\n';
    stream << "stage_bg.dim_uniform_updates=" << stageBackgroundProfile_.dimUniformUpdateCount << '\n';
    stream << "stage_bg.media_to_image_avg_ms="
           << QString::number(
                  averageOrZero(stageBackgroundProfile_.mediaToImageMsSum, stageBackgroundProfile_.mediaToImageSampleCount),
                  'f',
                  4
              )
           << '\n';
    stream << "stage_bg.media_to_image_max_ms="
           << QString::number(stageBackgroundProfile_.mediaToImageMsMax, 'f', 4) << '\n';
    stream << "stage_bg.media_texture_avg_ms="
           << QString::number(
                  averageOrZero(stageBackgroundProfile_.mediaTextureMsSum, stageBackgroundProfile_.mediaTextureSampleCount),
                  'f',
                  4
              )
           << '\n';
    stream << "stage_bg.media_texture_max_ms="
           << QString::number(stageBackgroundProfile_.mediaTextureMsMax, 'f', 4) << '\n';
    stream << "stage_bg.dim_uniform_update_avg_ms="
           << QString::number(
                  averageOrZero(
                      stageBackgroundProfile_.dimUniformUpdateMsSum,
                      stageBackgroundProfile_.dimUniformUpdateSampleCount
                  ),
                  'f',
                  4
              )
           << '\n';
    stream << "stage_bg.dim_uniform_update_max_ms="
           << QString::number(stageBackgroundProfile_.dimUniformUpdateMsMax, 'f', 4) << '\n';
    stream << "stage_bg.node_update_avg_ms="
           << QString::number(
                  averageOrZero(stageBackgroundProfile_.nodeUpdateMsSum, stageBackgroundProfile_.nodeUpdateSampleCount),
                  'f',
                  4
              )
           << '\n';
    stream << "stage_bg.node_update_max_ms="
           << QString::number(stageBackgroundProfile_.nodeUpdateMsMax, 'f', 4) << '\n';
    stream << "layer_build_total_avg_ms=" << QString::number(averageOrZero(layerBuildMsTotal_, profiledTextureFrameCount_), 'f', 4) << '\n';
    stream << "layer_build_total_max_ms=" << QString::number(layerBuildMsMax_, 'f', 4) << '\n';
    stream << "peak_frame.sprite_count=" << peakFrameSpriteCount_ << '\n';
    stream << "peak_frame.sprite_batch_count=" << peakFrameSpriteBatchCount_ << '\n';
    stream << "peak_frame.layer_build_total_ms=" << QString::number(peakFrameLayerBuildMs_, 'f', 4) << '\n';
    for (const PreviewRuntimeLayerProfileAggregate& layerStat : layerProfileAggregates_) {
        stream << "layer." << layerStat.name << ".sprite_count_avg="
               << QString::number(averageOrZero(static_cast<double>(layerStat.spriteCountSum), profiledTextureFrameCount_), 'f', 4) << '\n';
        stream << "layer." << layerStat.name << ".sprite_count_max=" << layerStat.spriteCountMax << '\n';
        stream << "layer." << layerStat.name << ".sprite_batch_count_avg="
               << QString::number(averageOrZero(static_cast<double>(layerStat.spriteBatchCountSum), profiledTextureFrameCount_), 'f', 4) << '\n';
        stream << "layer." << layerStat.name << ".sprite_batch_count_max=" << layerStat.spriteBatchCountMax << '\n';
        stream << "layer." << layerStat.name << ".candidate_count_avg="
               << QString::number(averageOrZero(static_cast<double>(layerStat.candidateCountSum), profiledTextureFrameCount_), 'f', 4) << '\n';
        stream << "layer." << layerStat.name << ".candidate_count_max=" << layerStat.candidateCountMax << '\n';
        stream << "layer." << layerStat.name << ".active_count_avg="
               << QString::number(averageOrZero(static_cast<double>(layerStat.activeCountSum), profiledTextureFrameCount_), 'f', 4) << '\n';
        stream << "layer." << layerStat.name << ".active_count_max=" << layerStat.activeCountMax << '\n';
        stream << "layer." << layerStat.name << ".build_ms_avg="
               << QString::number(averageOrZero(layerStat.buildMsSum, profiledTextureFrameCount_), 'f', 4) << '\n';
        stream << "layer." << layerStat.name << ".build_ms_max="
               << QString::number(layerStat.buildMsMax, 'f', 4) << '\n';
        stream << "layer." << layerStat.name << ".active_sprite_frames=" << layerStat.spriteActiveFrameCount << '\n';
    }
    file.close();
    profilingSummaryDirty_ = false;
    return file.fileName();
}

bool PreviewRuntime::hasCoreSkinAssetsLoadedForDebug() const
{
    return assets_ != nullptr && assets_->hasCoreSkinAssetsLoaded();
}

void PreviewRuntime::setFrameSize(const QSize& size)
{
    const QSize safeSize(qMax(1, size.width()), qMax(1, size.height()));
    if (frameSize_ == safeSize) {
        return;
    }
    frameSize_ = safeSize;
    pendingPresentedStatsRefresh_ = true;
    update();
}

void PreviewRuntime::handlePresentedFrame(const PreviewTextureStats* frameStats)
{
    if (frameStats != nullptr) {
        updateTextureProfilingStats(*frameStats);
    }
    presentedFrameCountTotal_ += 1;
    updatePresentedFrameStats();
    pendingPresentedStatsRefresh_ = false;
    emit framePresented();
}

void PreviewRuntime::refreshAssetStateFromRepository()
{
    if (assets_ == nullptr) {
        return;
    }
    frameState_.assets = assets_->assetState();
    frameState_.skin = assets_->skinAssets();
    frameState_.judgeOverlay = assets_->judgeOverlayAssets();
    frameState_.judgeEffect = assets_->judgeEffectAssets();
}

void PreviewRuntime::updatePresentedFrameStats()
{
    if (!presentTimer_.isValid()) {
        presentTimer_.start();
        lastPresentedNs_ = 0;
        frameState_.fpsDisplay = 0.0;
        frameState_.usedGpuRendererThisFrame = true;
        frameState_.cpuFallbackCount = 0;
        return;
    }

    const qint64 nowNs = presentTimer_.nsecsElapsed();
    if (lastPresentedNs_ >= 0) {
        const qint64 intervalNs = nowNs - lastPresentedNs_;
        const double intervalMs = static_cast<double>(intervalNs) / 1000000.0;
        if (!presentedFrameIntervalsMs_.isEmpty()) {
            presentedFrameIntervalsMs_[presentedFrameIntervalWriteIndex_] = intervalMs;
            presentedFrameIntervalWriteIndex_ =
                (presentedFrameIntervalWriteIndex_ + 1) % presentedFrameIntervalsMs_.size();
            presentedFrameIntervalCount_ = qMin(presentedFrameIntervalCount_ + 1, presentedFrameIntervalsMs_.size());
            double sumMs = 0.0;
            for (int index = 0; index < presentedFrameIntervalCount_; ++index) {
                sumMs += presentedFrameIntervalsMs_[index];
            }
            frameState_.fpsDisplay = sumMs > 1e-6
                ? 1000.0 / (sumMs / static_cast<double>(presentedFrameIntervalCount_))
                : 0.0;
        }
    }
    lastPresentedNs_ = nowNs;
    frameState_.usedGpuRendererThisFrame = true;
    frameState_.cpuFallbackCount = 0;
}
