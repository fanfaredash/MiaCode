#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QSize>
#include <QVector>

#include <memory>

#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "core/scene/PreviewFrameState.h"
#include "preview/runtime/PreviewSceneAssetRepository.h"

class QQuickWindow;

struct PreviewRuntimeLayerProfileAggregate {
    QString name;
    qint64 spriteCountSum = 0;
    qint64 spriteBatchCountSum = 0;
    qint64 spriteRunCountSum = 0;
    qint64 spriteGeometryReallocCountSum = 0;
    qint64 spriteVerticesRequiredSum = 0;
    qint64 spriteVerticesCapacitySum = 0;
    qint64 spriteScratchReserveGrowCountSum = 0;
    qint64 candidateCountSum = 0;
    qint64 activeCountSum = 0;
    double buildMsSum = 0.0;
    qint64 spriteCountMax = 0;
    qint64 spriteBatchCountMax = 0;
    qint64 spriteRunCountMax = 0;
    qint64 spriteGeometryReallocCountMax = 0;
    qint64 spriteVerticesRequiredMax = 0;
    qint64 spriteVerticesCapacityMax = 0;
    qint64 spriteScratchReserveGrowCountMax = 0;
    qint64 candidateCountMax = 0;
    qint64 activeCountMax = 0;
    double buildMsMax = 0.0;
    qint64 spriteActiveFrameCount = 0;
};

struct PreviewRuntimeStageBackgroundAggregate {
    double mediaToImageMsSum = 0.0;
    double mediaToImageMsMax = 0.0;
    qint64 mediaToImageSampleCount = 0;
    double mediaTextureMsSum = 0.0;
    double mediaTextureMsMax = 0.0;
    qint64 mediaTextureSampleCount = 0;
    double dimUniformUpdateMsSum = 0.0;
    double dimUniformUpdateMsMax = 0.0;
    qint64 dimUniformUpdateSampleCount = 0;
    double nodeUpdateMsSum = 0.0;
    double nodeUpdateMsMax = 0.0;
    qint64 nodeUpdateSampleCount = 0;
    qint64 mediaFrameCount = 0;
    qint64 dimFrameCount = 0;
    qint64 videoFrameCount = 0;
    qint64 staticImageFrameCount = 0;
    qint64 dimUniformUpdateCount = 0;
};

class PreviewRuntime : public QObject
{
    Q_OBJECT

public:
    explicit PreviewRuntime(QObject* parent = nullptr);
    ~PreviewRuntime() override;

    void setVisibleHostWindow(QQuickWindow* window);
    void clearVisibleHostWindow(QQuickWindow* window);
    void notifyVisibleFramePresented();
    QQuickWindow* visibleHostWindow() const;
    void requestActivate();
    void update();

    void setStageMediaAvailable(bool hasMedia);
    void setStageMediaPresentationMode(
        miacode::preview::scene::PreviewStageMediaPresentationMode mode,
        bool requestUpdate = true);
    void setExternalStageMediaDebugState(
        miacode::preview::scene::PreviewExternalStageMediaType mediaType,
        bool videoPlaybackActive,
        double playbackSecond,
        double clockDeltaSeconds,
        qint64 videoFrameAgeMs,
        bool videoFrameStalled,
        bool requestUpdate = true);
    void setExternalStageMediaProfileSummary(
        bool separateSurfaceActive,
        bool hasResolvedMedia,
        bool hasVideoMedia,
        const QString& mediaTypeName,
        qint64 videoFrameCountTotal,
        double videoFrameRate,
        double videoFrameIntervalAvgMs,
        double videoFrameIntervalMaxMs,
        qint64 videoFrameStallCount);
    void setFramePacingDebugState(
        bool usesDisplayRefreshPacing,
        double targetFps,
        double displayRefreshRate);
    void setPlayheadSeconds(double seconds, bool requestUpdate = true);
    void setMediaFrame(const QImage& frame);
    void setVideoFrame(const QVideoFrame& frame);
    void setResolvedStageVideoFrame(
        const QVideoFrame& frame,
        const QImage& resolvedImage,
        bool cacheable,
        quint64 serial,
        double toImageMs
    );
    void setRetainedVideoFallbackFrame(const QImage& frame);
    void setNoteMarkers(const QVector<TimelineNoteMarker>& notes);
    void setProgressStatsCache(std::shared_ptr<const miacode::preview::scene::PreviewProgressStatsCache> cache);
    void setMuriAnalysisReport(const MuriAnalysisReport& report);
    void setMuriRenderOptions(const MuriRenderOptions& options);
    void setSkinDirectory(const QString& skinDir);
    void setOutlineVariant(PreviewOutlineVariant variant);
    void setBackgroundBrightness(double brightness);
    void setBackgroundBrightnessOuter(double brightness);
    void setBackgroundBrightnessInner(double brightness);
    void setLayoutSquareScale(double scale);
    void setSmoothBrightness(bool smooth);
    void setBackgroundScaleMode(PreviewBackgroundScaleMode mode);
    void setTapFlowSpeed(double flowSpeed);
    void setTouchFlowSpeed(double flowSpeed);
    void setNoteFlowSpeed(double flowSpeed);
    void setSlideEarlierSecondAndTextOnTop(bool enabled);
    void setShowDebugInfo(bool show);
    void setShowTimestamp(bool show);
    void setShowObjectStatsHud(bool show);
    void setSuppressObjectStatsHud(bool suppress);
    bool showTimestamp() const;
    bool showObjectStatsHud() const;

    void reset();
    void noteTickForProfiling();
    void notePresentedTextureStats(const PreviewTextureStats& stats);
    void noteFixedTimerPacingMetrics(
        qint64 latenessNs,
        bool softLate,
        bool hardResync,
        qint64 presentMissedSlots);
    void notePreviewPacingTick(qint64 wallDeltaNs, double playheadDeltaSeconds, double speedRatio);
    void noteDisplayRefreshFrameRequest();
    void noteDisplayRefreshFramePresentation(qint64 waitNs, bool matchedRequest);
    void noteDisplayRefreshWatchdogTimeout();
    void noteDisplayRefreshQueuedTick();
    void noteDisplayRefreshTimerFallbackTick();
    void noteFixedGateVisualTick(qint64 gateWaitNs);
    void noteFixedGatePresentWithoutTick(qint64 gateWaitNs);
    void noteFixedGateWatchdogKick();
    void noteFixedGateMissedTargetSlots(qint64 count);
    void setActivePlaybackProfilingEnabled(bool enabled);
    void notePreviewClockMetrics(
        double audioDeltaSeconds,
        double visualDeltaSeconds,
        double audioMinusFallbackSeconds,
        bool hasAudioClock,
        bool audioLargeStep,
        bool visualLargeStep);
    void noteFixedTimerHighResolutionRequest(bool requested);
    void noteFixedTimerHighResolutionBeginResult(bool ok);
    void noteFixedTimerHighResolutionStopState(bool activeAtStop);
    void resetProfilingSession();
    QString writeProfilingSummaryToFile();
    bool hasCoreSkinAssetsLoadedForDebug() const;

    void setFrameSize(const QSize& size);
    const miacode::preview::scene::PreviewFrameState& frameState() const { return frameState_; }

signals:
    void frameStateChanged();
    void framePresented();

private:
    void handlePresentedFrame();
    void refreshAssetStateFromRepository();
    void updatePresentedFrameStats();

    miacode::preview::runtime::PreviewSceneAssetRepository* assets_ = nullptr;
    QPointer<QQuickWindow> visibleHostWindow_;
    QSize frameSize_;
    miacode::preview::scene::PreviewFrameState frameState_;
    bool requestedShowObjectStatsHud_ = false;
    bool suppressObjectStatsHud_ = false;
    QElapsedTimer presentTimer_;
    qint64 lastPresentedNs_ = -1;
    QVector<double> presentedFrameIntervalsMs_;
    int presentedFrameIntervalWriteIndex_ = 0;
    int presentedFrameIntervalCount_ = 0;
    double presentedFrameIntervalSumMs_ = 0.0;
    double presentedFrameIntervalMaxMs_ = 0.0;
    qint64 presentedFrameIntervalSampleCount_ = 0;
    QElapsedTimer tickTimer_;
    qint64 lastTickNs_ = -1;
    QVector<double> tickIntervalsMs_;
    int tickIntervalWriteIndex_ = 0;
    int tickIntervalCount_ = 0;
    double tickIntervalSumMs_ = 0.0;
    double tickIntervalMaxMs_ = 0.0;
    qint64 tickIntervalSampleCount_ = 0;
    QElapsedTimer updateRequestTimer_;
    qint64 lastUpdateRequestNs_ = -1;
    QVector<double> updateRequestIntervalsMs_;
    int updateRequestIntervalWriteIndex_ = 0;
    int updateRequestIntervalCount_ = 0;
    double updateRequestIntervalSumMs_ = 0.0;
    double updateRequestIntervalMaxMs_ = 0.0;
    qint64 updateRequestIntervalSampleCount_ = 0;
    bool pendingPresentedStatsRefresh_ = true;
    bool profilingSummaryDirty_ = false;
    qint64 profiledTextureFrameCount_ = 0;
    qint64 profiledActiveSpriteFrameCount_ = 0;
    qint64 cachedTextureHitTotal_ = 0;
    qint64 cachedTextureCreateTotal_ = 0;
    qint64 transientTextureHitTotal_ = 0;
    qint64 transientTextureCreateTotal_ = 0;
    qint64 spriteCountTotal_ = 0;
    qint64 spriteBatchCountTotal_ = 0;
    qint64 spriteCountMax_ = 0;
    qint64 spriteBatchCountMax_ = 0;
    double layerBuildMsTotal_ = 0.0;
    double layerBuildMsMax_ = 0.0;
    qint64 peakFrameSpriteCount_ = 0;
    qint64 peakFrameSpriteBatchCount_ = 0;
    double peakFrameLayerBuildMs_ = 0.0;
    QVector<PreviewRuntimeLayerProfileAggregate> layerProfileAggregates_;
    PreviewRuntimeStageBackgroundAggregate stageBackgroundProfile_;
    bool externalStageMediaSeparateSurfaceActive_ = false;
    bool externalStageMediaHasResolvedMedia_ = false;
    bool externalStageMediaHasVideoMedia_ = false;
    QString externalStageMediaMediaTypeName_ = QStringLiteral("none");
    qint64 tickCountTotal_ = 0;
    qint64 updateRequestCountTotal_ = 0;
    qint64 presentedFrameCountTotal_ = 0;
    qint64 fixedTimerLateWakeupCount_ = 0;
    qint64 fixedTimerCatchupTickCount_ = 0;
    qint64 fixedTimerSkippedIntervalCount_ = 0;
    qint64 fixedTimerSoftLateCount_ = 0;
    qint64 fixedTimerHardResyncCount_ = 0;
    qint64 fixedTimerPresentMissedSlotCount_ = 0;
    bool fixedTimerHighResRequested_ = false;
    bool fixedTimerHighResBeginOk_ = false;
    bool fixedTimerHighResActiveAtStop_ = false;
    double fixedTimerLatenessSumMs_ = 0.0;
    double fixedTimerLatenessMaxMs_ = 0.0;
    qint64 fixedTimerLatenessSampleCount_ = 0;
    qint64 displayRefreshRequestCount_ = 0;
    qint64 displayRefreshPresentedAfterRequestCount_ = 0;
    qint64 displayRefreshWatchdogTimeoutCount_ = 0;
    qint64 displayRefreshQueuedTickCount_ = 0;
    qint64 displayRefreshTimerFallbackTickCount_ = 0;
    double displayRefreshPresentWaitSumMs_ = 0.0;
    double displayRefreshPresentWaitMaxMs_ = 0.0;
    qint64 displayRefreshPresentWaitSampleCount_ = 0;
    double tickSpeedRatioMax_ = 0.0;
    qint64 tickLargeStepCount_ = 0;
    double audioClockDeltaMaxMs_ = 0.0;
    qint64 audioClockLargeStepCount_ = 0;
    double audioVsFallbackSumMs_ = 0.0;
    double audioVsFallbackMaxAbsMs_ = 0.0;
    qint64 audioVsFallbackSampleCount_ = 0;
    qint64 visualTimeLargeStepCount_ = 0;
    // Active-playback profile (doc section 2.5/4.4): subset of intervals recorded only while
    // preview playback is actively running, so pause/seek/shutdown gaps don't pollute realtime FPS.
    bool activePlaybackProfiling_ = false;
    double playbackPresentedFrameIntervalSumMs_ = 0.0;
    double playbackPresentedFrameIntervalMaxMs_ = 0.0;
    qint64 playbackPresentedFrameIntervalSampleCount_ = 0;
    double playbackUpdateRequestIntervalSumMs_ = 0.0;
    double playbackUpdateRequestIntervalMaxMs_ = 0.0;
    qint64 playbackUpdateRequestIntervalSampleCount_ = 0;
    double playbackTickIntervalSumMs_ = 0.0;
    double playbackTickIntervalMaxMs_ = 0.0;
    qint64 playbackTickIntervalSampleCount_ = 0;
    // Fixed-gate metrics (doc section 5.1): emitted by the present-driven fixed FPS gate path.
    qint64 fixedGateVisualTickCount_ = 0;
    qint64 fixedGatePresentWithoutTickCount_ = 0;
    double fixedGatePresentGateWaitSumMs_ = 0.0;
    double fixedGatePresentGateWaitMaxMs_ = 0.0;
    qint64 fixedGatePresentGateWaitSampleCount_ = 0;
    qint64 fixedGateMissedTargetSlotsCount_ = 0;
    qint64 fixedGateWatchdogKickCount_ = 0;
};
