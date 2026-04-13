#include "preview/runtime/PreviewRuntime.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
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

void recordIntervalSample(
    QElapsedTimer& timer,
    qint64& lastSampleNs,
    QVector<double>& samplesMs,
    int& writeIndex,
    int& sampleCount,
    double& sessionSumMs,
    double& sessionMaxMs,
    qint64& sessionSampleCount,
    double& fpsDisplay)
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
    if (profilingSummaryDirty_) {
        writeProfilingSummaryToFile();
    }
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
    if (visibleHostWindow_ != nullptr) {
        visibleHostWindow_->requestActivate();
    }
}

void PreviewRuntime::update()
{
    updateRequestCountTotal_ += 1;
    frameState_.updateRequestCount = updateRequestCountTotal_;
    recordIntervalSample(
        updateRequestTimer_,
        lastUpdateRequestNs_,
        updateRequestIntervalsMs_,
        updateRequestIntervalWriteIndex_,
        updateRequestIntervalCount_,
        updateRequestIntervalSumMs_,
        updateRequestIntervalMaxMs_,
        updateRequestIntervalSampleCount_,
        frameState_.updateRequestFpsDisplay
    );
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
    frameState_.tickFpsDisplay = 0.0;
    frameState_.updateRequestFpsDisplay = 0.0;
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
    recordIntervalSample(
        tickTimer_,
        lastTickNs_,
        tickIntervalsMs_,
        tickIntervalWriteIndex_,
        tickIntervalCount_,
        tickIntervalSumMs_,
        tickIntervalMaxMs_,
        tickIntervalSampleCount_,
        frameState_.tickFpsDisplay
    );
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
        aggregate.candidateCountSum += layerStat.candidateCount;
        aggregate.activeCountSum += layerStat.activeCount;
        aggregate.buildMsSum += layerStat.buildMs;
        aggregate.spriteCountMax = qMax(aggregate.spriteCountMax, layerStat.spriteCount);
        aggregate.spriteBatchCountMax = qMax(aggregate.spriteBatchCountMax, layerStat.spriteBatchCount);
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

void PreviewRuntime::noteFixedTimerDeadlineMetrics(qint64 latenessNs, int catchupTicks, qint64 skippedIntervals)
{
    if (!miacode::debug_options::previewProfileOutputEnabled()) {
        return;
    }
    profilingSummaryDirty_ = true;
    if (latenessNs > 0) {
        const double latenessMs = static_cast<double>(latenessNs) / 1000000.0;
        fixedTimerLateWakeupCount_ += 1;
        fixedTimerLatenessSumMs_ += latenessMs;
        fixedTimerLatenessMaxMs_ = qMax(fixedTimerLatenessMaxMs_, latenessMs);
        fixedTimerLatenessSampleCount_ += 1;
    }
    if (catchupTicks > 0) {
        fixedTimerCatchupTickCount_ += catchupTicks;
    }
    if (skippedIntervals > 0) {
        fixedTimerSkippedIntervalCount_ += skippedIntervals;
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
    fixedTimerLatenessSumMs_ = 0.0;
    fixedTimerLatenessMaxMs_ = 0.0;
    fixedTimerLatenessSampleCount_ = 0;
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
    stream << "frame_pacing.mode="
           << (frameState_.framePacingUsesDisplayRefresh ? "display_refresh" : "fixed_interval") << '\n';
    stream << "frame_pacing.target_fps=" << QString::number(frameState_.framePacingTargetFps, 'f', 4) << '\n';
    stream << "frame_pacing.display_refresh_rate=" << QString::number(frameState_.displayRefreshRate, 'f', 4)
           << '\n';
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
    recordIntervalSample(
        presentTimer_,
        lastPresentedNs_,
        presentedFrameIntervalsMs_,
        presentedFrameIntervalWriteIndex_,
        presentedFrameIntervalCount_,
        presentedFrameIntervalSumMs_,
        presentedFrameIntervalMaxMs_,
        presentedFrameIntervalSampleCount_,
        frameState_.fpsDisplay
    );
    frameState_.usedGpuRendererThisFrame = true;
    frameState_.cpuFallbackCount = 0;
}
