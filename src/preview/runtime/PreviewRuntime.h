#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSize>
#include <QUrl>
#include <QVector>
#include <QVariantMap>

#include <atomic>
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
    Q_PROPERTY(bool introOverlayActive READ introOverlayActive NOTIFY introOverlayStateChanged)
    Q_PROPERTY(int introOverlayFrame READ introOverlayFrame NOTIFY introOverlayStateChanged)
    Q_PROPERTY(QVariantMap introBannerTrack READ introBannerTrack NOTIFY introOverlayDataChanged)
    Q_PROPERTY(QVariantMap introBannerTemplate READ introBannerTemplate NOTIFY introOverlayDataChanged)
    Q_PROPERTY(QUrl introBackgroundImage READ introBackgroundImage NOTIFY introOverlayDataChanged)
    Q_PROPERTY(QUrl introLogoImage READ introLogoImage NOTIFY introOverlayDataChanged)
    // "片头" tab styling (introBannerStyleMap: backdropImage / backdropBlurEnabled /
    // cardShadowEnabled) — applied onto IntroOverlay.qml key-by-key in QML, mirroring
    // the export session's setProperty loop so preview and export can't diverge.
    Q_PROPERTY(QVariantMap introBannerStyle READ introBannerStyle NOTIFY introOverlayDataChanged)

public:
    explicit PreviewRuntime(QObject* parent = nullptr);
    ~PreviewRuntime() override;

    void setVisibleHostWindow(QQuickWindow* window);
    void clearVisibleHostWindow(QQuickWindow* window);
    void notifyVisibleFramePresented();
    // Called from the QSG render thread when the firework layer emits a node;
    // drives the firework warm-up completion check (atomic — thread-safe).
    void notifyFireworkLayerPresentedNode();
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
    void setHudPlayheadSecondsOverride(double seconds, bool requestUpdate = true);
    void clearHudPlayheadSecondsOverride(bool requestUpdate = true);
    void setIntroOverlayData(
        const QVariantMap& bannerTrack,
        const QVariantMap& bannerTemplate,
        const QUrl& backgroundImage,
        const QUrl& logoImage,
        const QVariantMap& bannerStyle = {});
    void setIntroOverlayFrame(int authoringFrame, bool active, bool requestUpdate = true);
    void clearIntroOverlay(bool requestUpdate = true);
    bool introOverlayActive() const { return introOverlayActive_; }
    int introOverlayFrame() const { return introOverlayFrame_; }
    QVariantMap introBannerTrack() const { return introBannerTrack_; }
    QVariantMap introBannerTemplate() const { return introBannerTemplate_; }
    QUrl introBackgroundImage() const { return introBackgroundImage_; }
    QUrl introLogoImage() const { return introLogoImage_; }
    QVariantMap introBannerStyle() const { return introBannerStyle_; }
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
    // Read-only accessor for the bound skin directory. Returns the empty
    // string when no skin has been bound yet.
    QString skinDirectory() const;
    void setOutlineVariant(PreviewOutlineVariant variant);
    void setOutlineImagePath(const QString& path);
    void setOutlineSelection(
        PreviewOutlineVariant variant,
        const QString& path,
        miacode::preview::runtime::PreviewOutlineImageMode imageMode =
            miacode::preview::runtime::PreviewOutlineImageMode::Direct);
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
    void setTapJudgeTextDistance(PreviewTapJudgeTextDistance distance);
    void setJudgeEffectStyle(PreviewJudgeEffectStyle style);
    void setHoveredTouchPad(const QString& pad);
    bool beginTouchPadAuthoringPress(const QString& pad);
    bool finishTouchPadAuthoringPress(const QString& pad, bool backtickSeparator);
    void cancelTouchPadAuthoringPress();
    bool touchPadAuthoringEnabled() const { return frameState_.touchPadAuthoringEnabled; }
    bool touchPadAuthoringPressActive() const { return !frameState_.pressedTouchPad.isEmpty(); }
    void setTouchPadAuthoringEnabled(bool enabled);
    void setShowDebugInfo(bool show);
    void setSuppressDebugInfo(bool suppress);
    void setShowTimestamp(bool show);
    void setShowObjectStatsHud(bool show);
    void setFixHudTextLayout(bool enabled);
    void setCenterDisplayMode(miacode::preview_gameplay::CenterDisplayMode mode);
    void setSuppressObjectStatsHud(bool suppress);
    void setShowChartInfoHud(bool show);
    void setUseMineSkin(bool enabled);
    void setChartInfo(const QString& title,
                      const QString& artist,
                      const QString& difficultyLabel,
                      const QString& designer);
    bool showTimestamp() const;
    bool showObjectStatsHud() const;
    bool showChartInfoHud() const;
    bool useMineSkin() const;
    miacode::preview_gameplay::CenterDisplayMode centerDisplayMode() const;

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
    // --debug-only v2 UI probe. PreviewQuickSceneRoot reports its live binding
    // and each frame-state fan-out here; payload is emitted only in the
    // existing pause resource gauge, never per frame.
    void noteV2UiSceneRootBound(quint64 instanceId, bool visible);
    void noteV2UiSceneRootUnbound(quint64 instanceId);
    void noteV2UiSceneRootVisibility(quint64 instanceId, bool visible);
    void noteV2UiSceneRootFrameStateDispatch(bool visible);
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
    std::shared_ptr<const miacode::preview::scene::PreviewFrameState> frameStateSnapshot() const;
    // GUI-thread builder state. QSG consumers must use
    // frameStateSnapshot() so a frame never observes partially-mutated state.
    const miacode::preview::scene::PreviewFrameState& frameState() const { return frameState_; }
    // Detailed render-side resource snapshot for the leak gauge ("key=val …"): scene content
    // revision (rebuild count), current cached/transient texture count + GPU bytes, cumulative
    // texture creates (churn), peak sprite count, presented-frame total. GUI-thread members,
    // refreshed from the render thread via notePresentedTextureStats.
    QString resourceGaugePayload() const;

signals:
    void frameStateChanged();
    void framePresented();
    void introOverlayDataChanged();
    void introOverlayStateChanged();
    void touchPadAuthoringClicked(const QString& pad, bool backtickSeparator);

private:
    void publishFrameStateSnapshot();
    void handlePresentedFrame();
    void refreshAssetStateFromRepository();
    void updatePresentedFrameStats();
    // Firework PSO/texture warm-up (in-process QML preview path). See the
    // member-variable block below for the full rationale.
    void armFireworkPsoWarmupIfReady();
    void appendFireworkWarmupMarker();
    void removeFireworkWarmupMarkers();
    // Re-center the synthetic warm-up marker on the live playhead (no-op
    // unless armed-but-not-done) so a playhead move can't strand it outside
    // its lifecycle window before the firework layer draws it.
    void refreshFireworkWarmupForPlayheadChange();

    miacode::preview::runtime::PreviewSceneAssetRepository* assets_ = nullptr;
    QPointer<QQuickWindow> visibleHostWindow_;
    QSize frameSize_;
    miacode::preview::scene::PreviewFrameState frameState_;
    std::shared_ptr<const miacode::preview::scene::PreviewFrameState> publishedFrameState_;
    bool introOverlayActive_ = false;
    int introOverlayFrame_ = 0;
    QVariantMap introBannerTrack_;
    QVariantMap introBannerTemplate_;
    QUrl introBackgroundImage_;
    QUrl introLogoImage_;
    QVariantMap introBannerStyle_;
    // --- Firework PSO/texture warm-up (in-process QML path) ----------------
    // Qt RHI compiles the custom PreviewQuickJudgeFireworkMaterial pipeline
    // and uploads the firework colour-ball texture lazily on the FIRST
    // firework draw. In a fresh session that first draw lands mid-playback
    // when a real firework note fires, stalling the QSG render thread for
    // ~50-300 ms. We warm this up here: once the skin assets (incl. the
    // firework colour ball) are loaded, append a synthetic off-screen
    // firework marker so the layer issues exactly one real textured draw
    // at chart load — compiling the PSO + uploading the texture at a
    // benign moment.
    //
    // `armed_` flips true when assets are ready. COMPLETION IS GATED ON A
    // CONFIRMED PRESENT, not a blind present count and not a bare node emission:
    // the scene root bumps `fireworkLayerDrawSignal_` (render thread) from a direct
    // frameSwapped hook for each swapped frame that actually contained a firework
    // node, and `done_` only flips once that signal has advanced past the value
    // captured at arm (`fireworkWarmupArmDrawSignal_`) — i.e. once the synthetic
    // has genuinely compiled the PSO, uploaded the texture AND reached the screen. To guarantee the
    // synthetic is drawable wherever the playhead happens to be, it is
    // RE-CENTERED on the live playhead whenever the playhead has travelled far
    // enough to be about to leave the synthetic's lifecycle window, and on every
    // marker refresh / reset while armed-but-not-done (its lifecycle window is
    // fixed relative to its trigger second, so a seek / negative pre-roll /
    // play-start would otherwise move the playhead out of the window and the
    // layer would never draw it — the historical "probabilistic first-firework
    // hitch"). The travel test is core/scene/PreviewFireworkWarmupPolicy.h,
    // calibrated against the layer's real window by
    // preview_firework_warmup_policy_spec; it replaced an unconditional
    // per-playhead-change re-centre that cost a full PreviewPreparedSceneCache
    // rebuild on EVERY preview frame for as long as the warm-up stayed armed.
    // INVARIANT: while armed-but-not-done, exactly one synthetic is present in
    // frameState_.noteMarkers and fireworkWarmupCenterSecond_ is its centre —
    // any site that clears noteMarkers must re-append (setNoteMarkers, reset).
    // `fireworkWarmupArmPresentCount_` + a present-count cap is only a backstop:
    // if the firework never renders at all (layer disabled, non-rendering
    // surface) the warm-up is abandoned so the synthetic and the per-playhead
    // re-center work cannot linger for the whole session. Re-armed on
    // visible-window change (a new QQuickWindow == a fresh QRhi pipeline cache,
    // so the previously-warmed PSO/texture are gone).
    bool fireworkWarmupArmed_ = false;
    bool fireworkWarmupDone_ = false;
    qint64 fireworkWarmupArmPresentCount_ = -1;
    QElapsedTimer fireworkWarmupElapsed_;
    // Playhead the synthetic is currently centred on. The re-centre test compares
    // against this instead of firing on every playhead change — see
    // core/scene/PreviewFireworkWarmupPolicy.h and
    // refreshFireworkWarmupForPlayheadChange().
    double fireworkWarmupCenterSecond_ = 0.0;
    // Monotonic count of PRESENTED frames that contained a firework-layer node,
    // bumped on the QSG render thread (notifyFireworkLayerPresentedNode, called from
    // a direct frameSwapped hook) and read on the GUI thread (handlePresentedFrame /
    // armFireworkPsoWarmupIfReady) — std::atomic crosses that boundary.
    // `armDrawSignal_` snapshots it at arm time.
    //
    // Counting at NODE-EMISSION time was the earlier, weaker criterion: the node is
    // emitted during updatePaintNode, one whole render+swap before the pixels exist,
    // while the GUI-thread completion check runs off a QUEUED frameSwapped hop that
    // can lag a frame. That combination could retire the warm-up on a frame whose
    // pipeline build had not actually happened yet — the "warm-up done means the
    // marker was drawn, not that a render/present completed" gap called out in
    // docs/audit/PREVIEW_FIRST_PLAY_RENDER_STALL_HANDOFF_AUDIT_ZH.md §6D-2.
    std::atomic<quint64> fireworkLayerDrawSignal_{0};
    quint64 fireworkWarmupArmDrawSignal_ = 0;
    bool requestedShowObjectStatsHud_ = false;
    bool suppressObjectStatsHud_ = false;
    bool requestedShowDebugInfo_ = false;
    bool suppressDebugInfo_ = false;
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
    // Latest CURRENT cache occupancy (leak gauge), refreshed each presented frame.
    qint64 latestCachedTextureCount_ = 0;
    qint64 latestCachedTextureBytes_ = 0;
    qint64 latestTransientTextureCount_ = 0;
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
    // v2 UI probe state. All access is on the GUI thread. The map is retained
    // across play transactions so the pause gauge can report current roots;
    // dispatch counters reset on each explicit Play.
    QHash<quint64, bool> v2UiSceneRootVisibility_;
    qint64 v2UiSceneRootDispatchCount_ = 0;
    qint64 v2UiSceneRootVisibleDispatchCount_ = 0;
    qint64 v2UiSceneRootHiddenDispatchCount_ = 0;
    // Fixed-gate metrics (doc section 5.1): emitted by the present-driven fixed FPS gate path.
    qint64 fixedGateVisualTickCount_ = 0;
    qint64 fixedGatePresentWithoutTickCount_ = 0;
    double fixedGatePresentGateWaitSumMs_ = 0.0;
    double fixedGatePresentGateWaitMaxMs_ = 0.0;
    qint64 fixedGatePresentGateWaitSampleCount_ = 0;
    qint64 fixedGateMissedTargetSlotsCount_ = 0;
    qint64 fixedGateWatchdogKickCount_ = 0;
};
