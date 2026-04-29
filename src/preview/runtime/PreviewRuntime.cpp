#include "preview/runtime/PreviewRuntime.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/Mmcss.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQuickWindow>
#include <QTextStream>

namespace {

constexpr int kPreviewIntervalWindowSize = 120;

double averageOrZero(double total, qint64 count)
{
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

double fpsFromAverageMs(double averageMs)
{
    return averageMs > 1e-6 ? 1000.0 / averageMs : 0.0;
}

double rollingAverageMs(const QVector<double>& samples, int count)
{
    if (count <= 0) {
        return 0.0;
    }
    double sumMs = 0.0;
    for (int index = 0; index < count; ++index) {
        sumMs += samples.at(index);
    }
    return sumMs / static_cast<double>(count);
}

double rollingMaxMs(const QVector<double>& samples, int count)
{
    double maxMs = 0.0;
    for (int index = 0; index < count; ++index) {
        maxMs = qMax(maxMs, samples.at(index));
    }
    return maxMs;
}

// Multiplier applied to the target frame interval to flag a "stutter".
// 1.5× target (≈25ms at 60Hz) is the threshold beyond which an isolated
// frame is reliably noticeable. Counted against the rolling sample window
// so the HUD shows a per-second-scale figure.
constexpr double kStutterMultiplier = 1.5;

int rollingStutterCount(const QVector<double>& samples, int count, double thresholdMs)
{
    if (thresholdMs <= 0.0 || count <= 0) {
        return 0;
    }
    int stutters = 0;
    for (int index = 0; index < count; ++index) {
        if (samples.at(index) > thresholdMs) {
            ++stutters;
        }
    }
    return stutters;
}

double targetIntervalMsForStutterDetection(double targetFps)
{
    // Fall back to 60fps if target hasn't been published yet (early frames).
    const double effectiveTarget = targetFps > 1.0 ? targetFps : 60.0;
    return 1000.0 / effectiveTarget;
}

struct PlaybackScopeAccumulator {
    bool active = false;
    double* sumMs = nullptr;
    double* maxMs = nullptr;
    qint64* sampleCount = nullptr;
};

void recordIntervalSample(
    QElapsedTimer& timer,
    qint64& lastSampleNs,
    QVector<double>& samplesMs,
    int& writeIndex,
    int& sampleCount,
    double& sessionSumMs,
    double& sessionMaxMs,
    qint64& sessionSampleCount,
    double& fpsDisplay,
    const PlaybackScopeAccumulator& playbackScope = PlaybackScopeAccumulator{})
{
    if (!timer.isValid()) {
        timer.start();
        lastSampleNs = 0;
        fpsDisplay = 0.0;
        return;
    }

    const qint64 nowNs = timer.nsecsElapsed();
    if (lastSampleNs >= 0) {
        const qint64 intervalNs = nowNs - lastSampleNs;
        if (intervalNs > 0) {
            const double intervalMs = static_cast<double>(intervalNs) / 1000000.0;
            if (!samplesMs.isEmpty()) {
                samplesMs[writeIndex] = intervalMs;
                writeIndex = (writeIndex + 1) % samplesMs.size();
                sampleCount = qMin(sampleCount + 1, samplesMs.size());
                fpsDisplay = fpsFromAverageMs(rollingAverageMs(samplesMs, sampleCount));
            }
            sessionSumMs += intervalMs;
            sessionMaxMs = qMax(sessionMaxMs, intervalMs);
            sessionSampleCount += 1;
            if (playbackScope.active
                && playbackScope.sumMs != nullptr
                && playbackScope.maxMs != nullptr
                && playbackScope.sampleCount != nullptr) {
                *playbackScope.sumMs += intervalMs;
                *playbackScope.maxMs = qMax(*playbackScope.maxMs, intervalMs);
                *playbackScope.sampleCount += 1;
            }
        }
    }
    lastSampleNs = nowNs;
}

PreviewRuntimeLayerProfileAggregate& ensureLayerProfileAggregate(
    QVector<PreviewRuntimeLayerProfileAggregate>* aggregates,
    const QString& name)
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

}  // namespace

PreviewRuntime::PreviewRuntime(QObject* parent)
    : QObject(parent)
{
    assets_ = new miacode::preview::runtime::PreviewSceneAssetRepository(this);
    refreshAssetStateFromRepository();
    presentedFrameIntervalsMs_.resize(kPreviewIntervalWindowSize);
    presentedFrameIntervalsMs_.fill(0.0);
    tickIntervalsMs_.resize(kPreviewIntervalWindowSize);
    tickIntervalsMs_.fill(0.0);
    updateRequestIntervalsMs_.resize(kPreviewIntervalWindowSize);
    updateRequestIntervalsMs_.fill(0.0);
    connect(assets_, &miacode::preview::runtime::PreviewSceneAssetRepository::assetsChanged, this, [this]() {
        refreshAssetStateFromRepository();
        update();
    });
}

PreviewRuntime::~PreviewRuntime()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    qint64 profilingWriteElapsedMs = 0;
    const bool profilingWasDirty = profilingSummaryDirty_;
    if (profilingSummaryDirty_) {
        QElapsedTimer profilingWriteTimer;
        profilingWriteTimer.start();
        writeProfilingSummaryToFile();
        profilingWriteElapsedMs = profilingWriteTimer.elapsed();
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/preview_runtime"),
        QStringLiteral("destructor"),
        totalTimer.elapsed(),
        QStringLiteral("profiling_summary_dirty=%1 profiling_write_ms=%2")
            .arg(profilingWasDirty ? 1 : 0)
            .arg(profilingWriteElapsedMs)
    );
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

QQuickWindow* PreviewRuntime::visibleHostWindow() const
{
    return visibleHostWindow_.data();
}

void PreviewRuntime::requestActivate()
{
    if (visibleHostWindow_ != nullptr) {
        visibleHostWindow_->requestActivate();
    }
}

void PreviewRuntime::update()
{
    updateRequestCountTotal_ += 1;
    frameState_.updateRequestCount = updateRequestCountTotal_;
    PlaybackScopeAccumulator playbackScope;
    playbackScope.active = activePlaybackProfiling_;
    playbackScope.sumMs = &playbackUpdateRequestIntervalSumMs_;
    playbackScope.maxMs = &playbackUpdateRequestIntervalMaxMs_;
    playbackScope.sampleCount = &playbackUpdateRequestIntervalSampleCount_;
    recordIntervalSample(
        updateRequestTimer_,
        lastUpdateRequestNs_,
        updateRequestIntervalsMs_,
        updateRequestIntervalWriteIndex_,
        updateRequestIntervalCount_,
        updateRequestIntervalSumMs_,
        updateRequestIntervalMaxMs_,
        updateRequestIntervalSampleCount_,
        frameState_.updateRequestFpsDisplay,
        playbackScope
    );
    {
        const double thresholdMs = kStutterMultiplier
            * targetIntervalMsForStutterDetection(frameState_.framePacingTargetFps);
        frameState_.updateRequestMaxMsDisplay =
            rollingMaxMs(updateRequestIntervalsMs_, updateRequestIntervalCount_);
        frameState_.updateRequestStutterCountDisplay = rollingStutterCount(
            updateRequestIntervalsMs_, updateRequestIntervalCount_, thresholdMs);
    }
    pendingPresentedStatsRefresh_ = true;
    emit frameStateChanged();
    if (visibleHostWindow_ != nullptr) {
        visibleHostWindow_->requestUpdate();
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
    bool videoFrameStalled,
    bool requestUpdate)
{
    frameState_.media.externalMediaType = mediaType;
    frameState_.media.externalVideoPlaybackActive = videoPlaybackActive;
    frameState_.media.externalPlaybackSecond = qMax(0.0, playbackSecond);
    frameState_.media.externalClockDeltaSeconds = clockDeltaSeconds;
    frameState_.media.externalVideoFrameAgeMs = videoFrameAgeMs;
    frameState_.media.externalVideoFrameStalled = videoFrameStalled;
    if (requestUpdate) {
        update();
    }
}

void PreviewRuntime::setExternalStageMediaProfileSummary(
    bool separateSurfaceActive,
    bool hasResolvedMedia,
    bool hasVideoMedia,
    const QString& mediaTypeName,
    qint64 videoFrameCountTotal,
    double videoFrameRate,
    double videoFrameIntervalAvgMs,
    double videoFrameIntervalMaxMs,
    qint64 videoFrameStallCount)
{
    externalStageMediaSeparateSurfaceActive_ = separateSurfaceActive;
    externalStageMediaHasResolvedMedia_ = hasResolvedMedia;
    externalStageMediaHasVideoMedia_ = hasVideoMedia;
    externalStageMediaMediaTypeName_ = mediaTypeName.trimmed().isEmpty()
        ? QStringLiteral("none")
        : mediaTypeName.trimmed();
    frameState_.media.externalVideoFrameCountTotal = qMax<qint64>(0, videoFrameCountTotal);
    frameState_.media.externalVideoFrameRate = qMax(0.0, videoFrameRate);
    frameState_.media.externalVideoFrameIntervalAvgMs = qMax(0.0, videoFrameIntervalAvgMs);
    frameState_.media.externalVideoFrameIntervalMaxMs = qMax(0.0, videoFrameIntervalMaxMs);
    frameState_.media.externalVideoFrameStallCount = qMax<qint64>(0, videoFrameStallCount);
}

void PreviewRuntime::setFramePacingDebugState(
    bool usesDisplayRefreshPacing,
    double targetFps,
    double displayRefreshRate)
{
    const double normalizedTargetFps = qMax(0.0, targetFps);
    const double normalizedDisplayRefreshRate = qMax(0.0, displayRefreshRate);
    const bool changed =
        frameState_.framePacingUsesDisplayRefresh != usesDisplayRefreshPacing
        || qAbs(frameState_.framePacingTargetFps - normalizedTargetFps) > 0.01
        || qAbs(frameState_.displayRefreshRate - normalizedDisplayRefreshRate) > 0.01;
    if (!changed) {
        return;
    }
    frameState_.framePacingUsesDisplayRefresh = usesDisplayRefreshPacing;
    frameState_.framePacingTargetFps = normalizedTargetFps;
    frameState_.displayRefreshRate = normalizedDisplayRefreshRate;
    update();
}

void PreviewRuntime::setPlayheadSeconds(double seconds, bool requestUpdate)
{
    frameState_.playheadSeconds = seconds;
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

void PreviewRuntime::setTapFlowSpeed(double flowSpeed)
{
    frameState_.render.tapFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    update();
}

void PreviewRuntime::setTouchFlowSpeed(double flowSpeed)
{
    frameState_.render.touchFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    update();
}

void PreviewRuntime::setNoteFlowSpeed(double flowSpeed)
{
    const double normalizedFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    frameState_.render.tapFlowSpeed = normalizedFlowSpeed;
    frameState_.render.touchFlowSpeed = normalizedFlowSpeed;
    update();
}

void PreviewRuntime::setSlideEarlierSecondAndTextOnTop(bool enabled)
{
    frameState_.render.slideEarlierSecondAndTextOnTop = enabled;
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
    requestedShowObjectStatsHud_ = show;
    frameState_.render.showObjectStatsHud = requestedShowObjectStatsHud_ && !suppressObjectStatsHud_;
    update();
}

void PreviewRuntime::setSuppressObjectStatsHud(bool suppress)
{
    if (suppressObjectStatsHud_ == suppress) {
        return;
    }
    suppressObjectStatsHud_ = suppress;
    frameState_.render.showObjectStatsHud = requestedShowObjectStatsHud_ && !suppressObjectStatsHud_;
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
    frameState_.tickFpsDisplay = 0.0;
    frameState_.updateRequestFpsDisplay = 0.0;
    frameState_.presentMaxMsDisplay = 0.0;
    frameState_.tickMaxMsDisplay = 0.0;
    frameState_.updateRequestMaxMsDisplay = 0.0;
    frameState_.presentStutterCountDisplay = 0;
    frameState_.tickStutterCountDisplay = 0;
    frameState_.updateRequestStutterCountDisplay = 0;
    frameState_.tickCount = 0;
    frameState_.updateRequestCount = 0;
    frameState_.presentedFrameCount = 0;
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
    tickCountTotal_ += 1;
    frameState_.tickCount = tickCountTotal_;
    PlaybackScopeAccumulator playbackScope;
    playbackScope.active = activePlaybackProfiling_;
    playbackScope.sumMs = &playbackTickIntervalSumMs_;
    playbackScope.maxMs = &playbackTickIntervalMaxMs_;
    playbackScope.sampleCount = &playbackTickIntervalSampleCount_;
    recordIntervalSample(
        tickTimer_,
        lastTickNs_,
        tickIntervalsMs_,
        tickIntervalWriteIndex_,
        tickIntervalCount_,
        tickIntervalSumMs_,
        tickIntervalMaxMs_,
        tickIntervalSampleCount_,
        frameState_.tickFpsDisplay,
        playbackScope
    );
    {
        const double thresholdMs = kStutterMultiplier
            * targetIntervalMsForStutterDetection(frameState_.framePacingTargetFps);
        frameState_.tickMaxMsDisplay =
            rollingMaxMs(tickIntervalsMs_, tickIntervalCount_);
        frameState_.tickStutterCountDisplay = rollingStutterCount(
            tickIntervalsMs_, tickIntervalCount_, thresholdMs);
    }
}

void PreviewRuntime::notePresentedTextureStats(const PreviewTextureStats& stats)
{
    if (!miacode::debug_options::previewProfileOutputEnabled()) {
        return;
    }
    profilingSummaryDirty_ = true;
    profiledTextureFrameCount_ += 1;
    cachedTextureHitTotal_ += stats.cachedHitCount;
    cachedTextureCreateTotal_ += stats.cachedCreateCount;
    transientTextureHitTotal_ += stats.transientHitCount;
    transientTextureCreateTotal_ += stats.transientCreateCount;
    spriteCountTotal_ += stats.spriteCount;
    spriteBatchCountTotal_ += stats.spriteBatchCount;
    spriteCountMax_ = qMax(spriteCountMax_, stats.spriteCount);
    spriteBatchCountMax_ = qMax(spriteBatchCountMax_, stats.spriteBatchCount);
    if (stats.spriteCount > 0 || stats.spriteBatchCount > 0) {
        profiledActiveSpriteFrameCount_ += 1;
    }

    double frameLayerBuildTotalMs = 0.0;
    for (const PreviewTextureLayerStats& layerStat : stats.layerStats) {
        PreviewRuntimeLayerProfileAggregate& aggregate =
            ensureLayerProfileAggregate(&layerProfileAggregates_, layerStat.name);
        aggregate.spriteCountSum += layerStat.spriteCount;
        aggregate.spriteBatchCountSum += layerStat.spriteBatchCount;
        aggregate.spriteRunCountSum += layerStat.spriteRunCount;
        aggregate.spriteGeometryReallocCountSum += layerStat.spriteGeometryReallocCount;
        aggregate.spriteVerticesRequiredSum += layerStat.spriteVerticesRequired;
        aggregate.spriteVerticesCapacitySum += layerStat.spriteVerticesCapacity;
        aggregate.spriteScratchReserveGrowCountSum += layerStat.spriteScratchReserveGrowCount;
        aggregate.candidateCountSum += layerStat.candidateCount;
        aggregate.activeCountSum += layerStat.activeCount;
        aggregate.buildMsSum += layerStat.buildMs;
        aggregate.spriteCountMax = qMax(aggregate.spriteCountMax, layerStat.spriteCount);
        aggregate.spriteBatchCountMax = qMax(aggregate.spriteBatchCountMax, layerStat.spriteBatchCount);
        aggregate.spriteRunCountMax = qMax(aggregate.spriteRunCountMax, layerStat.spriteRunCount);
        aggregate.spriteGeometryReallocCountMax =
            qMax(aggregate.spriteGeometryReallocCountMax, layerStat.spriteGeometryReallocCount);
        aggregate.spriteVerticesRequiredMax = qMax(aggregate.spriteVerticesRequiredMax, layerStat.spriteVerticesRequired);
        aggregate.spriteVerticesCapacityMax = qMax(aggregate.spriteVerticesCapacityMax, layerStat.spriteVerticesCapacity);
        aggregate.spriteScratchReserveGrowCountMax =
            qMax(aggregate.spriteScratchReserveGrowCountMax, layerStat.spriteScratchReserveGrowCount);
        aggregate.candidateCountMax = qMax(aggregate.candidateCountMax, layerStat.candidateCount);
        aggregate.activeCountMax = qMax(aggregate.activeCountMax, layerStat.activeCount);
        aggregate.buildMsMax = qMax(aggregate.buildMsMax, layerStat.buildMs);
        if (layerStat.spriteCount > 0 || layerStat.spriteBatchCount > 0 || layerStat.activeCount > 0) {
            aggregate.spriteActiveFrameCount += 1;
        }
        frameLayerBuildTotalMs += layerStat.buildMs;
    }

    layerBuildMsTotal_ += frameLayerBuildTotalMs;
    layerBuildMsMax_ = qMax(layerBuildMsMax_, frameLayerBuildTotalMs);
    if (frameLayerBuildTotalMs >= peakFrameLayerBuildMs_) {
        peakFrameLayerBuildMs_ = frameLayerBuildTotalMs;
        peakFrameSpriteCount_ = stats.spriteCount;
        peakFrameSpriteBatchCount_ = stats.spriteBatchCount;
    }

    const PreviewStageBackgroundFrameProfile& stageBackground = stats.stageBackground;
    stageBackgroundProfile_.mediaFrameCount += stageBackground.mediaFrameCount;
    stageBackgroundProfile_.dimFrameCount += stageBackground.dimFrameCount;
    stageBackgroundProfile_.videoFrameCount += stageBackground.videoFrameCount;
    stageBackgroundProfile_.staticImageFrameCount += stageBackground.staticImageFrameCount;
    stageBackgroundProfile_.dimUniformUpdateCount += stageBackground.dimUniformUpdateCount;

    if (stageBackground.mediaToImageMs > 0.0 || stageBackground.videoFrameCount > 0) {
        stageBackgroundProfile_.mediaToImageMsSum += stageBackground.mediaToImageMs;
        stageBackgroundProfile_.mediaToImageMsMax =
            qMax(stageBackgroundProfile_.mediaToImageMsMax, stageBackground.mediaToImageMs);
        stageBackgroundProfile_.mediaToImageSampleCount += 1;
    }
    if (stageBackground.mediaTextureMs > 0.0 || stageBackground.mediaFrameCount > 0) {
        stageBackgroundProfile_.mediaTextureMsSum += stageBackground.mediaTextureMs;
        stageBackgroundProfile_.mediaTextureMsMax =
            qMax(stageBackgroundProfile_.mediaTextureMsMax, stageBackground.mediaTextureMs);
        stageBackgroundProfile_.mediaTextureSampleCount += 1;
    }
    if (stageBackground.dimUniformUpdateMs > 0.0 || stageBackground.dimUniformUpdateCount > 0) {
        stageBackgroundProfile_.dimUniformUpdateMsSum += stageBackground.dimUniformUpdateMs;
        stageBackgroundProfile_.dimUniformUpdateMsMax =
            qMax(stageBackgroundProfile_.dimUniformUpdateMsMax, stageBackground.dimUniformUpdateMs);
        stageBackgroundProfile_.dimUniformUpdateSampleCount += 1;
    }
    if (stageBackground.nodeUpdateMs > 0.0
        || stageBackground.mediaFrameCount > 0
        || stageBackground.dimFrameCount > 0) {
        stageBackgroundProfile_.nodeUpdateMsSum += stageBackground.nodeUpdateMs;
        stageBackgroundProfile_.nodeUpdateMsMax =
            qMax(stageBackgroundProfile_.nodeUpdateMsMax, stageBackground.nodeUpdateMs);
        stageBackgroundProfile_.nodeUpdateSampleCount += 1;
    }
}

void PreviewRuntime::noteFixedTimerPacingMetrics(
    qint64 latenessNs,
    bool softLate,
    bool hardResync,
    qint64 presentMissedSlots)
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    if (latenessNs > 0) {
        const double latenessMs = static_cast<double>(latenessNs) / 1000000.0;
        fixedTimerLateWakeupCount_ += 1;
        fixedTimerLatenessSumMs_ += latenessMs;
        fixedTimerLatenessMaxMs_ = qMax(fixedTimerLatenessMaxMs_, latenessMs);
        fixedTimerLatenessSampleCount_ += 1;
    }
    if (softLate) {
        fixedTimerSoftLateCount_ += 1;
    }
    if (hardResync) {
        fixedTimerHardResyncCount_ += 1;
    }
    if (presentMissedSlots > 0) {
        fixedTimerPresentMissedSlotCount_ += presentMissedSlots;
    }
}

void PreviewRuntime::notePreviewClockMetrics(
    double audioDeltaSeconds,
    double visualDeltaSeconds,
    double audioMinusFallbackSeconds,
    bool hasAudioClock,
    bool audioLargeStep,
    bool visualLargeStep)
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    if (hasAudioClock) {
        if (qIsFinite(audioDeltaSeconds) && audioDeltaSeconds > 0.0) {
            audioClockDeltaMaxMs_ = qMax(audioClockDeltaMaxMs_, audioDeltaSeconds * 1000.0);
        }
        if (qIsFinite(audioMinusFallbackSeconds)) {
            const double deltaMs = audioMinusFallbackSeconds * 1000.0;
            audioVsFallbackSumMs_ += deltaMs;
            audioVsFallbackMaxAbsMs_ = qMax(audioVsFallbackMaxAbsMs_, qAbs(deltaMs));
            audioVsFallbackSampleCount_ += 1;
        }
        if (audioLargeStep) {
            audioClockLargeStepCount_ += 1;
        }
    }
    if (visualLargeStep) {
        visualTimeLargeStepCount_ += 1;
    }
    Q_UNUSED(visualDeltaSeconds);
}

void PreviewRuntime::noteFixedTimerHighResolutionRequest(bool requested)
{
    if (!requested || fixedTimerHighResRequested_) {
        return;
    }
    fixedTimerHighResRequested_ = true;
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
}

void PreviewRuntime::noteFixedTimerHighResolutionBeginResult(bool ok)
{
    if (!ok || fixedTimerHighResBeginOk_) {
        return;
    }
    fixedTimerHighResBeginOk_ = true;
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
}

void PreviewRuntime::noteFixedTimerHighResolutionStopState(bool activeAtStop)
{
    if (!activeAtStop || fixedTimerHighResActiveAtStop_) {
        return;
    }
    fixedTimerHighResActiveAtStop_ = true;
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
}

void PreviewRuntime::notePreviewPacingTick(qint64 wallDeltaNs, double playheadDeltaSeconds, double speedRatio)
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    if (qIsFinite(speedRatio) && speedRatio > 0.0) {
        tickSpeedRatioMax_ = qMax(tickSpeedRatioMax_, speedRatio);
        if (speedRatio > 1.5 && wallDeltaNs > 0 && playheadDeltaSeconds > 0.0) {
            tickLargeStepCount_ += 1;
        }
    }
}

void PreviewRuntime::noteDisplayRefreshFrameRequest()
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    displayRefreshRequestCount_ += 1;
}

void PreviewRuntime::noteDisplayRefreshFramePresentation(qint64 waitNs, bool matchedRequest)
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    if (!matchedRequest) {
        return;
    }
    displayRefreshPresentedAfterRequestCount_ += 1;
    if (waitNs <= 0) {
        return;
    }
    const double waitMs = static_cast<double>(waitNs) / 1000000.0;
    displayRefreshPresentWaitSumMs_ += waitMs;
    displayRefreshPresentWaitMaxMs_ = qMax(displayRefreshPresentWaitMaxMs_, waitMs);
    displayRefreshPresentWaitSampleCount_ += 1;
}

void PreviewRuntime::noteDisplayRefreshWatchdogTimeout()
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    displayRefreshWatchdogTimeoutCount_ += 1;
}

void PreviewRuntime::noteDisplayRefreshQueuedTick()
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    displayRefreshQueuedTickCount_ += 1;
}

void PreviewRuntime::noteDisplayRefreshTimerFallbackTick()
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    displayRefreshTimerFallbackTickCount_ += 1;
}

void PreviewRuntime::noteFixedGateVisualTick(qint64 gateWaitNs)
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    fixedGateVisualTickCount_ += 1;
    if (gateWaitNs > 0) {
        const double waitMs = static_cast<double>(gateWaitNs) / 1000000.0;
        fixedGatePresentGateWaitSumMs_ += waitMs;
        fixedGatePresentGateWaitMaxMs_ = qMax(fixedGatePresentGateWaitMaxMs_, waitMs);
        fixedGatePresentGateWaitSampleCount_ += 1;
    }
}

void PreviewRuntime::noteFixedGatePresentWithoutTick(qint64 gateWaitNs)
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    fixedGatePresentWithoutTickCount_ += 1;
    Q_UNUSED(gateWaitNs);
}

void PreviewRuntime::noteFixedGateWatchdogKick()
{
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    fixedGateWatchdogKickCount_ += 1;
}

void PreviewRuntime::noteFixedGateMissedTargetSlots(qint64 count)
{
    if (count <= 0) {
        return;
    }
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
    fixedGateMissedTargetSlotsCount_ += count;
}

void PreviewRuntime::setActivePlaybackProfilingEnabled(bool enabled)
{
    if (activePlaybackProfiling_ == enabled) {
        return;
    }
    activePlaybackProfiling_ = enabled;
    if (enabled) {
        // Reset playback-scope accumulators so pause gaps don't leak into the next run.
        playbackPresentedFrameIntervalSumMs_ = 0.0;
        playbackPresentedFrameIntervalMaxMs_ = 0.0;
        playbackPresentedFrameIntervalSampleCount_ = 0;
        playbackUpdateRequestIntervalSumMs_ = 0.0;
        playbackUpdateRequestIntervalMaxMs_ = 0.0;
        playbackUpdateRequestIntervalSampleCount_ = 0;
        playbackTickIntervalSumMs_ = 0.0;
        playbackTickIntervalMaxMs_ = 0.0;
        playbackTickIntervalSampleCount_ = 0;
    }
    if (miacode::debug_options::previewProfileOutputEnabled()) {
        profilingSummaryDirty_ = true;
    }
}

void PreviewRuntime::resetProfilingSession()
{
    presentTimer_.invalidate();
    lastPresentedNs_ = -1;
    presentedFrameIntervalsMs_.fill(0.0);
    presentedFrameIntervalWriteIndex_ = 0;
    presentedFrameIntervalCount_ = 0;
    presentedFrameIntervalSumMs_ = 0.0;
    presentedFrameIntervalMaxMs_ = 0.0;
    presentedFrameIntervalSampleCount_ = 0;
    tickTimer_.invalidate();
    lastTickNs_ = -1;
    tickIntervalsMs_.fill(0.0);
    tickIntervalWriteIndex_ = 0;
    tickIntervalCount_ = 0;
    tickIntervalSumMs_ = 0.0;
    tickIntervalMaxMs_ = 0.0;
    tickIntervalSampleCount_ = 0;
    updateRequestTimer_.invalidate();
    lastUpdateRequestNs_ = -1;
    updateRequestIntervalsMs_.fill(0.0);
    updateRequestIntervalWriteIndex_ = 0;
    updateRequestIntervalCount_ = 0;
    updateRequestIntervalSumMs_ = 0.0;
    updateRequestIntervalMaxMs_ = 0.0;
    updateRequestIntervalSampleCount_ = 0;
    frameState_.fpsDisplay = 0.0;
    frameState_.tickFpsDisplay = 0.0;
    frameState_.updateRequestFpsDisplay = 0.0;
    frameState_.presentMaxMsDisplay = 0.0;
    frameState_.tickMaxMsDisplay = 0.0;
    frameState_.updateRequestMaxMsDisplay = 0.0;
    frameState_.presentStutterCountDisplay = 0;
    frameState_.tickStutterCountDisplay = 0;
    frameState_.updateRequestStutterCountDisplay = 0;
    frameState_.tickCount = 0;
    frameState_.updateRequestCount = 0;
    frameState_.presentedFrameCount = 0;
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
    tickCountTotal_ = 0;
    updateRequestCountTotal_ = 0;
    presentedFrameCountTotal_ = 0;
    fixedTimerLateWakeupCount_ = 0;
    fixedTimerCatchupTickCount_ = 0;
    fixedTimerSkippedIntervalCount_ = 0;
    fixedTimerSoftLateCount_ = 0;
    fixedTimerHardResyncCount_ = 0;
    fixedTimerPresentMissedSlotCount_ = 0;
    fixedTimerHighResRequested_ = false;
    fixedTimerHighResBeginOk_ = false;
    fixedTimerHighResActiveAtStop_ = false;
    fixedTimerLatenessSumMs_ = 0.0;
    fixedTimerLatenessMaxMs_ = 0.0;
    fixedTimerLatenessSampleCount_ = 0;
    displayRefreshRequestCount_ = 0;
    displayRefreshPresentedAfterRequestCount_ = 0;
    displayRefreshWatchdogTimeoutCount_ = 0;
    displayRefreshQueuedTickCount_ = 0;
    displayRefreshTimerFallbackTickCount_ = 0;
    displayRefreshPresentWaitSumMs_ = 0.0;
    displayRefreshPresentWaitMaxMs_ = 0.0;
    displayRefreshPresentWaitSampleCount_ = 0;
    tickSpeedRatioMax_ = 0.0;
    tickLargeStepCount_ = 0;
    audioClockDeltaMaxMs_ = 0.0;
    audioClockLargeStepCount_ = 0;
    audioVsFallbackSumMs_ = 0.0;
    audioVsFallbackMaxAbsMs_ = 0.0;
    audioVsFallbackSampleCount_ = 0;
    visualTimeLargeStepCount_ = 0;
    playbackPresentedFrameIntervalSumMs_ = 0.0;
    playbackPresentedFrameIntervalMaxMs_ = 0.0;
    playbackPresentedFrameIntervalSampleCount_ = 0;
    playbackUpdateRequestIntervalSumMs_ = 0.0;
    playbackUpdateRequestIntervalMaxMs_ = 0.0;
    playbackUpdateRequestIntervalSampleCount_ = 0;
    playbackTickIntervalSumMs_ = 0.0;
    playbackTickIntervalMaxMs_ = 0.0;
    playbackTickIntervalSampleCount_ = 0;
    fixedGateVisualTickCount_ = 0;
    fixedGatePresentWithoutTickCount_ = 0;
    fixedGatePresentGateWaitSumMs_ = 0.0;
    fixedGatePresentGateWaitMaxMs_ = 0.0;
    fixedGatePresentGateWaitSampleCount_ = 0;
    fixedGateMissedTargetSlotsCount_ = 0;
    fixedGateWatchdogKickCount_ = 0;
}

QString PreviewRuntime::writeProfilingSummaryToFile()
{
    if (!miacode::debug_options::previewProfileOutputEnabled()
        || presentedFrameIntervalSampleCount_ <= 0) {
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

    const double presentWindowAvgMs = rollingAverageMs(presentedFrameIntervalsMs_, presentedFrameIntervalCount_);
    const double presentWindowMaxMs = rollingMaxMs(presentedFrameIntervalsMs_, presentedFrameIntervalCount_);
    const double presentSessionAvgMs =
        averageOrZero(presentedFrameIntervalSumMs_, presentedFrameIntervalSampleCount_);
    const double tickSessionAvgMs = averageOrZero(tickIntervalSumMs_, tickIntervalSampleCount_);
    const double updateRequestSessionAvgMs =
        averageOrZero(updateRequestIntervalSumMs_, updateRequestIntervalSampleCount_);
    QTextStream stream(&file);
    stream << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    stream << "profile_scope=session_accumulated" << '\n';
    stream << "frame_samples=" << presentedFrameIntervalSampleCount_ << '\n';
    stream << "present_window_samples=" << presentedFrameIntervalCount_ << '\n';
    stream << "presented_total=" << presentedFrameCountTotal_ << '\n';
    stream << "present_avg_ms=" << QString::number(presentSessionAvgMs, 'f', 4) << '\n';
    stream << "present_max_ms=" << QString::number(presentedFrameIntervalMaxMs_, 'f', 4) << '\n';
    stream << "present_window_avg_ms=" << QString::number(presentWindowAvgMs, 'f', 4) << '\n';
    stream << "present_window_max_ms=" << QString::number(presentWindowMaxMs, 'f', 4) << '\n';
    stream << "fps=" << QString::number(frameState_.fpsDisplay, 'f', 4) << '\n';
    stream << "present_session_fps=" << QString::number(fpsFromAverageMs(presentSessionAvgMs), 'f', 4) << '\n';
    stream << "tick_total=" << tickCountTotal_ << '\n';
    stream << "tick_avg_ms=" << QString::number(tickSessionAvgMs, 'f', 4) << '\n';
    stream << "tick_max_ms=" << QString::number(tickIntervalMaxMs_, 'f', 4) << '\n';
    stream << "tick_fps=" << QString::number(frameState_.tickFpsDisplay, 'f', 4) << '\n';
    stream << "tick_session_fps=" << QString::number(fpsFromAverageMs(tickSessionAvgMs), 'f', 4) << '\n';
    stream << "update_requests_total=" << updateRequestCountTotal_ << '\n';
    stream << "update_request_avg_ms=" << QString::number(updateRequestSessionAvgMs, 'f', 4) << '\n';
    stream << "update_request_max_ms=" << QString::number(updateRequestIntervalMaxMs_, 'f', 4) << '\n';
    stream << "update_request_fps=" << QString::number(frameState_.updateRequestFpsDisplay, 'f', 4) << '\n';
    stream << "update_request_session_fps="
           << QString::number(fpsFromAverageMs(updateRequestSessionAvgMs), 'f', 4) << '\n';
    stream << "fixed_timer.late_wakeups=" << fixedTimerLateWakeupCount_ << '\n';
    stream << "fixed_timer.lateness_avg_ms="
           << QString::number(averageOrZero(fixedTimerLatenessSumMs_, fixedTimerLatenessSampleCount_), 'f', 4)
           << '\n';
    stream << "fixed_timer.lateness_max_ms=" << QString::number(fixedTimerLatenessMaxMs_, 'f', 4) << '\n';
    stream << "fixed_timer.catchup_ticks_total=" << fixedTimerCatchupTickCount_ << '\n';
    stream << "fixed_timer.skipped_intervals_total=" << fixedTimerSkippedIntervalCount_ << '\n';
    stream << "fixed_timer.soft_late_total=" << fixedTimerSoftLateCount_ << '\n';
    stream << "fixed_timer.hard_resync_total=" << fixedTimerHardResyncCount_ << '\n';
    stream << "fixed_timer.present_missed_slots_total=" << fixedTimerPresentMissedSlotCount_ << '\n';
    stream << "fixed_timer.high_res_resolution_enabled="
           << (fixedTimerHighResBeginOk_ ? 1 : 0) << '\n';
    stream << "fixed_timer.high_res_requested=" << (fixedTimerHighResRequested_ ? 1 : 0) << '\n';
    stream << "fixed_timer.high_res_begin_ok=" << (fixedTimerHighResBeginOk_ ? 1 : 0) << '\n';
    stream << "fixed_timer.high_res_active_at_stop=" << (fixedTimerHighResActiveAtStop_ ? 1 : 0) << '\n';
    stream << "logic.skipped_ticks_total=0" << '\n';
    stream << "frame_pacing.mode="
           << (frameState_.framePacingUsesDisplayRefresh ? "display_refresh" : "fixed_interval") << '\n';
    stream << "frame_pacing.target_fps=" << QString::number(frameState_.framePacingTargetFps, 'f', 4) << '\n';
    stream << "frame_pacing.display_refresh_rate=" << QString::number(frameState_.displayRefreshRate, 'f', 4)
           << '\n';
    stream << "display_refresh.requests_total=" << displayRefreshRequestCount_ << '\n';
    stream << "display_refresh.presented_after_request_total="
           << displayRefreshPresentedAfterRequestCount_ << '\n';
    stream << "display_refresh.watchdog_timeouts_total=" << displayRefreshWatchdogTimeoutCount_ << '\n';
    stream << "display_refresh.queued_ticks_total=" << displayRefreshQueuedTickCount_ << '\n';
    stream << "display_refresh.timer_fallback_ticks_total="
           << displayRefreshTimerFallbackTickCount_ << '\n';
    stream << "display_refresh.present_wait_avg_ms="
           << QString::number(
                  averageOrZero(displayRefreshPresentWaitSumMs_, displayRefreshPresentWaitSampleCount_),
                  'f',
                  4
              )
           << '\n';
    stream << "display_refresh.present_wait_max_ms="
           << QString::number(displayRefreshPresentWaitMaxMs_, 'f', 4) << '\n';
    stream << "tick.speed_ratio_max=" << QString::number(tickSpeedRatioMax_, 'f', 4) << '\n';
    stream << "tick.large_step_count=" << tickLargeStepCount_ << '\n';
    stream << "audio_clock.delta_max_ms=" << QString::number(audioClockDeltaMaxMs_, 'f', 4) << '\n';
    stream << "audio_clock.large_step_total=" << audioClockLargeStepCount_ << '\n';
    stream << "audio_clock.audio_vs_fallback_avg_ms="
           << QString::number(averageOrZero(audioVsFallbackSumMs_, audioVsFallbackSampleCount_), 'f', 4)
           << '\n';
    stream << "audio_clock.audio_vs_fallback_max_abs_ms="
           << QString::number(audioVsFallbackMaxAbsMs_, 'f', 4) << '\n';
    stream << "visual_time.large_step_total=" << visualTimeLargeStepCount_ << '\n';
    const double playbackPresentAvgMs =
        averageOrZero(playbackPresentedFrameIntervalSumMs_, playbackPresentedFrameIntervalSampleCount_);
    const double playbackUpdateRequestAvgMs =
        averageOrZero(playbackUpdateRequestIntervalSumMs_, playbackUpdateRequestIntervalSampleCount_);
    const double playbackTickAvgMs =
        averageOrZero(playbackTickIntervalSumMs_, playbackTickIntervalSampleCount_);
    stream << "playback_present_samples=" << playbackPresentedFrameIntervalSampleCount_ << '\n';
    stream << "playback_present_avg_ms=" << QString::number(playbackPresentAvgMs, 'f', 4) << '\n';
    stream << "playback_present_max_ms=" << QString::number(playbackPresentedFrameIntervalMaxMs_, 'f', 4)
           << '\n';
    stream << "playback_present_fps=" << QString::number(fpsFromAverageMs(playbackPresentAvgMs), 'f', 4)
           << '\n';
    stream << "playback_update_request_samples=" << playbackUpdateRequestIntervalSampleCount_ << '\n';
    stream << "playback_update_request_avg_ms=" << QString::number(playbackUpdateRequestAvgMs, 'f', 4)
           << '\n';
    stream << "playback_update_request_max_ms="
           << QString::number(playbackUpdateRequestIntervalMaxMs_, 'f', 4) << '\n';
    stream << "playback_update_request_fps="
           << QString::number(fpsFromAverageMs(playbackUpdateRequestAvgMs), 'f', 4) << '\n';
    stream << "playback_tick_samples=" << playbackTickIntervalSampleCount_ << '\n';
    stream << "playback_tick_avg_ms=" << QString::number(playbackTickAvgMs, 'f', 4) << '\n';
    stream << "playback_tick_max_ms=" << QString::number(playbackTickIntervalMaxMs_, 'f', 4) << '\n';
    stream << "playback_tick_fps=" << QString::number(fpsFromAverageMs(playbackTickAvgMs), 'f', 4)
           << '\n';
    stream << "fixed_gate.visual_ticks_total=" << fixedGateVisualTickCount_ << '\n';
    stream << "fixed_gate.present_without_tick_total=" << fixedGatePresentWithoutTickCount_ << '\n';
    stream << "fixed_gate.present_gate_wait_avg_ms="
           << QString::number(
                  averageOrZero(fixedGatePresentGateWaitSumMs_, fixedGatePresentGateWaitSampleCount_),
                  'f',
                  4
              )
           << '\n';
    stream << "fixed_gate.present_gate_wait_max_ms="
           << QString::number(fixedGatePresentGateWaitMaxMs_, 'f', 4) << '\n';
    stream << "fixed_gate.missed_target_slots_total=" << fixedGateMissedTargetSlotsCount_ << '\n';
    stream << "fixed_gate.watchdog_kick_total=" << fixedGateWatchdogKickCount_ << '\n';
    {
        // Async log writer overhead. Caller-thread cost is `log_writer.max_enqueue_ms` —
        // anything well under 1ms means logging is not on the hot path. The `total_io_ms`
        // and `worker_max_batch_ms` figures live on the worker thread and reflect the
        // actual file I/O cost, which doesn't block GUI/render frames.
        const miacode::debug_log::LogWriterStats logStats =
            miacode::debug_log::logWriterStatsSnapshot();
        const auto nsToMs = [](quint64 ns) {
            return static_cast<double>(ns) / 1000000.0;
        };
        stream << "log_writer.async_enabled=" << (logStats.asyncEnabled ? 1 : 0) << '\n';
        stream << "log_writer.worker_running=" << (logStats.workerRunning ? 1 : 0) << '\n';
        stream << "log_writer.enqueued_total=" << logStats.enqueuedCount << '\n';
        stream << "log_writer.written_total=" << logStats.writtenCount << '\n';
        stream << "log_writer.dropped_total=" << logStats.droppedCount << '\n';
        stream << "log_writer.queue_size_current=" << logStats.currentQueueSize << '\n';
        stream << "log_writer.queue_size_peak=" << logStats.peakQueueSize << '\n';
        stream << "log_writer.max_enqueue_ms="
               << QString::number(nsToMs(logStats.maxEnqueueTimeNs), 'f', 4) << '\n';
        stream << "log_writer.worker_max_batch_ms="
               << QString::number(nsToMs(logStats.maxBatchTimeNs), 'f', 4) << '\n';
        stream << "log_writer.worker_total_io_ms="
               << QString::number(nsToMs(logStats.totalIoTimeNs), 'f', 4) << '\n';
    }
    {
        // MMCSS (Windows multimedia scheduler) registration status. Surfaced here because
        // the original mmcss_register log line lives in the runtime debug log, which gets
        // trimmed mid-session — but the summary file is rewritten in full and never
        // truncated, so this is the reliable place to confirm MMCSS actually took.
        const miacode::mmcss::LastRegistrationStatus mmStatus =
            miacode::mmcss::lastRegistrationStatus();
        stream << "mmcss.ever_attempted=" << (mmStatus.everAttempted ? 1 : 0) << '\n';
        stream << "mmcss.ever_registered=" << (mmStatus.everRegistered ? 1 : 0) << '\n';
        stream << "mmcss.last_task_class="
               << (mmStatus.lastTaskClass.isEmpty() ? QStringLiteral("(none)") : mmStatus.lastTaskClass)
               << '\n';
        stream << "mmcss.last_skip_reason="
               << (mmStatus.lastSkipReason.isEmpty() ? QStringLiteral("(ok)") : mmStatus.lastSkipReason)
               << '\n';
        stream << "mmcss.last_errno=" << mmStatus.lastErrorCode << '\n';
    }
    stream << "ratio.present_per_tick="
           << QString::number(
                  tickCountTotal_ > 0
                      ? static_cast<double>(presentedFrameCountTotal_) / static_cast<double>(tickCountTotal_)
                      : 0.0,
                  'f',
                  4
              )
           << '\n';
    stream << "ratio.update_requests_per_tick="
           << QString::number(
                  tickCountTotal_ > 0
                      ? static_cast<double>(updateRequestCountTotal_) / static_cast<double>(tickCountTotal_)
                      : 0.0,
                  'f',
                  4
              )
           << '\n';
    stream << "external_stage_media.separate_surface_active="
           << (externalStageMediaSeparateSurfaceActive_ ? 1 : 0) << '\n';
    stream << "external_stage_media.has_resolved_media="
           << (externalStageMediaHasResolvedMedia_ ? 1 : 0) << '\n';
    stream << "external_stage_media.has_video_media="
           << (externalStageMediaHasVideoMedia_ ? 1 : 0) << '\n';
    stream << "external_stage_media.media_type=" << externalStageMediaMediaTypeName_ << '\n';
    stream << "external_stage_media.video_frames_total=" << frameState_.media.externalVideoFrameCountTotal << '\n';
    stream << "external_stage_media.video_frame_rate="
           << QString::number(frameState_.media.externalVideoFrameRate, 'f', 4) << '\n';
    stream << "external_stage_media.video_frame_interval_avg_ms="
           << QString::number(frameState_.media.externalVideoFrameIntervalAvgMs, 'f', 4) << '\n';
    stream << "external_stage_media.video_frame_interval_max_ms="
           << QString::number(frameState_.media.externalVideoFrameIntervalMaxMs, 'f', 4) << '\n';
    stream << "external_stage_media.video_frame_stalls_total="
           << frameState_.media.externalVideoFrameStallCount << '\n';
    stream << "ratio.present_per_video_frame="
           << QString::number(
                  frameState_.media.externalVideoFrameCountTotal > 0
                      ? static_cast<double>(presentedFrameCountTotal_)
                            / static_cast<double>(frameState_.media.externalVideoFrameCountTotal)
                      : 0.0,
                  'f',
                  4
              )
           << '\n';
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
        stream << "layer." << layerStat.name << ".sprite_run_count_avg="
               << QString::number(averageOrZero(static_cast<double>(layerStat.spriteRunCountSum), profiledTextureFrameCount_), 'f', 4) << '\n';
        stream << "layer." << layerStat.name << ".sprite_run_count_max=" << layerStat.spriteRunCountMax << '\n';
        stream << "layer." << layerStat.name << ".sprite_geometry_realloc_count_avg="
               << QString::number(
                      averageOrZero(static_cast<double>(layerStat.spriteGeometryReallocCountSum), profiledTextureFrameCount_),
                      'f',
                      4
                  )
               << '\n';
        stream << "layer." << layerStat.name << ".sprite_geometry_realloc_count_max="
               << layerStat.spriteGeometryReallocCountMax << '\n';
        stream << "layer." << layerStat.name << ".sprite_vertices_required_avg="
               << QString::number(
                      averageOrZero(static_cast<double>(layerStat.spriteVerticesRequiredSum), profiledTextureFrameCount_),
                      'f',
                      4
                  )
               << '\n';
        stream << "layer." << layerStat.name << ".sprite_vertices_required_max="
               << layerStat.spriteVerticesRequiredMax << '\n';
        stream << "layer." << layerStat.name << ".sprite_vertices_capacity_avg="
               << QString::number(
                      averageOrZero(static_cast<double>(layerStat.spriteVerticesCapacitySum), profiledTextureFrameCount_),
                      'f',
                      4
                  )
               << '\n';
        stream << "layer." << layerStat.name << ".sprite_vertices_capacity_max="
               << layerStat.spriteVerticesCapacityMax << '\n';
        stream << "layer." << layerStat.name << ".sprite_scratch_reserve_grow_count_avg="
               << QString::number(
                      averageOrZero(
                          static_cast<double>(layerStat.spriteScratchReserveGrowCountSum),
                          profiledTextureFrameCount_
                      ),
                      'f',
                      4
                  )
               << '\n';
        stream << "layer." << layerStat.name << ".sprite_scratch_reserve_grow_count_max="
               << layerStat.spriteScratchReserveGrowCountMax << '\n';
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

void PreviewRuntime::setTargetFrameIntervalNs(qint64 ns)
{
    // Clamp to a sane range — 1 ms (1000 Hz) lower bound to avoid
    // pathological small values; 100 ms (10 Hz) upper bound for the
    // same reason. The DComp surface uses this as the per-frame
    // playhead increment; absurd values would make motion look broken
    // immediately, this just keeps the value in a self-consistent
    // regime.
    const qint64 clamped = qBound<qint64>(1'000'000LL, ns, 100'000'000LL);
    if (clamped == targetFrameIntervalNs_) {
        return;
    }
    targetFrameIntervalNs_ = clamped;
    emit targetFrameIntervalChanged(targetFrameIntervalNs_);
}

void PreviewRuntime::handlePresentedFrame()
{
    presentedFrameCountTotal_ += 1;
    frameState_.presentedFrameCount = presentedFrameCountTotal_;
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
    PlaybackScopeAccumulator playbackScope;
    playbackScope.active = activePlaybackProfiling_;
    playbackScope.sumMs = &playbackPresentedFrameIntervalSumMs_;
    playbackScope.maxMs = &playbackPresentedFrameIntervalMaxMs_;
    playbackScope.sampleCount = &playbackPresentedFrameIntervalSampleCount_;
    recordIntervalSample(
        presentTimer_,
        lastPresentedNs_,
        presentedFrameIntervalsMs_,
        presentedFrameIntervalWriteIndex_,
        presentedFrameIntervalCount_,
        presentedFrameIntervalSumMs_,
        presentedFrameIntervalMaxMs_,
        presentedFrameIntervalSampleCount_,
        frameState_.fpsDisplay,
        playbackScope
    );
    {
        const double thresholdMs = kStutterMultiplier
            * targetIntervalMsForStutterDetection(frameState_.framePacingTargetFps);
        frameState_.presentMaxMsDisplay =
            rollingMaxMs(presentedFrameIntervalsMs_, presentedFrameIntervalCount_);
        frameState_.presentStutterCountDisplay = rollingStutterCount(
            presentedFrameIntervalsMs_, presentedFrameIntervalCount_, thresholdMs);
    }
    frameState_.usedGpuRendererThisFrame = true;
    frameState_.cpuFallbackCount = 0;
}
