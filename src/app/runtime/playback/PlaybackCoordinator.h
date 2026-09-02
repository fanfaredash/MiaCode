#pragma once

#include "runtime/RuntimeContext.h"

#include "app/v2/AudioClockSource.h"
#include "app/v2/PlaybackControl.h"
#include "app/v2/PreviewPlaybackPort.h"
#include "audio/PreviewAudioDeviceChangePolicy.h"
#include "audio/PreviewAudioDeviceCutoff.h"
#include "runtime/playback/PlaybackIdentityGate.h"

class Session;

namespace miacode::preview_audio {
struct PreviewAudioCompletion;
}

namespace miacode::runtime {

class PlaybackCoordinator final : public miacode::v2::PlaybackControl,
                                  public miacode::v2::PreviewPlaybackPort,
                                  public miacode::v2::AudioClockSource {
public:
    PlaybackCoordinator(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state,
                        quint64 sessionGeneration = 0);

    void setDocumentRevision(quint64 revision);
    void invalidateSession();

    miacode::v2::PlaybackSnapshot playbackSnapshot() const override;
    bool acceptsPlaybackCallback(
        const miacode::v2::PlaybackCallbackStamp& stamp) const override;
    double currentAudioClockSecond() const override;

    void resetPreviewTrackTimelineOffsets();
    void applyWaveformData(const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData);
    void refreshWaveformCache();
    void refreshWaveformCache(double knownDurationSeconds);
    bool hasActiveDifficulty() const;
    // True when there is a chart the preview transport can play: either a real
    // active difficulty, or the latency page's synthesized test chart. Playback
    // gates use this so the latency audition reuses the exact same transport.
    bool hasPreviewableChart() const;
    int activeDifficultyId() const;
    QString activeChartText() const;
    miacode::simai::SimaiTimingMetadata currentTimingMetadata() const;
    double parsedRawFirstSeconds(bool* ok = nullptr) const;
    double parsedFirstSeconds(bool* ok = nullptr) const;
    double parsedWholeBpm(bool* ok = nullptr) const;
    int parsedClockCount() const;
    QString parsedLatencyMeterId() const;
    void applyLatencyDetectorOffset(double seconds);
    void applyLatencyDetectorBpm(double bpm);
    void applyLatencyDetectorClockCount(int clockCount);
    void setCurrentFilePath(const QString& path, bool suppressImmediateRefresh = false);
    void updateWindowTitle();
    void updateCurrentFileLabel();
    QString editorText() const;
    void scheduleTimelineRefresh();
    void refreshTimelineMetadata();
    void refreshTimelineQuickModelFromCurrentText();
    bool timelineTabIsForeground() const;
    bool quickTimelineBridgeReady() const;
    void flushDeferredTimelineBridgeState();
    void onTimelineHeaderNavigateRequested(double second);
    void onTimelineUserInteractionStarted();
    void onTimelineDragStarted();
    void onTimelineWheelNavigateRequested(double second);
    void onTimelineCenterNavigateRequested(double second);
    void onTimelineDragFinished(double second);
    void onTimelineFollowPreviewToggled(bool enabled);
    void onTimelineViewportLockToggled(bool enabled);
    void onTimelineFollowProgressToggled(bool enabled);
    void onTimelineSyncToggled(bool enabled);
    void applyLatestTimelinePreviewStateToPausedPreview();
    void requestTimelineSlowRefresh();
    void dispatchTimelineSlowRefresh();
    void scheduleTimelineAnalysisRefresh(
        const TimelineSlowRefreshRequest& request,
        const SimaiNativeParseResult& parseResult,
        const TimelinePreviewRefreshState& previewState
    );
    bool scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs = -1);
    void requestTimelineAnalysisDispatch(int delayMs = -1);
    void dispatchTimelineAnalysisRefresh();
    void rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers);
    double timelineSecondForCursor(int line, int col) const;
    bool resolveTimelineSecondForCursor(int line, int col, double* second) const;
    void updateTimelineCursorFromEditorLocation(int line, int col, bool centerView);
    void navigateTimelineToSecond(double second, bool focusEditor = true);
    void deferTimelineCursorBridgeUpdate(double second, bool centerView);
    bool resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const;
    bool moveEditorCursorToTimelineLocation(
        int line,
        int col,
        bool selectToken,
        bool focusEditor,
        bool centerView,
        bool suppressSignals,
        qint64* cursorMoveElapsedNs = nullptr,
        qint64* followOverlayElapsedNs = nullptr
    );
    void updatePreviewFollowDecorationForTimelineBlueLine(
        double second,
        bool ensureVisible = false,
        qint64* resolveElapsedNs = nullptr,
        qint64* followOverlayElapsedNs = nullptr,
        TimelineQuickModel::PreviewFollowSpan* spanOut = nullptr);
    void syncEditorCursorToPreviewSecond(
        double second,
        bool centerView = true,
        bool ensureVisibleWhenPaused = false);
    // See MainWindowMemberStorage.inc `touchPadAuthoringAnchor*`: maps a playhead
    // parked by a touch-authoring seek back to the token that click wrote to.
    double touchPadAuthoringAnchoredSecond(double previewSecond) const;
    void setTouchPadAuthoringAnchor(double seekSecond, double tokenSecond);
    double previewDurationSeconds() const;
    double previewPlaybackEndSeconds() const;
    void updatePreviewSliderRange();
    void updatePreviewSliderPosition(double second);
    void refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers);
    void clearPreviewObjectStats();
    void emitChartSwitchResourceGauge();
    int updatePreviewStatsLayoutMode(int hostWidth = -1);
    int previewStatsMinimumHeightForPanelWidth(int panelWidth) const;
    double normalizedPreviewCanvasAspectRatio(double ratio) const;
    QString previewFrameRateModeStorageValue(PreviewCanvasFrameRateMode mode) const;
    QString previewCanvasFrameRateModeStorageValue() const;
    QString previewStageMediaFrameRateModeStorageValue() const;
    QString timelineFrameRateModeStorageValue() const;
    double currentPreviewCanvasRefreshRate() const;
    PreviewCanvasFrameRateMode currentPreviewStageMediaFrameRateMode() const;
    bool currentVideoDecodePrefersSoftware() const;
    PreviewCanvasFrameRateMode currentTimelineFrameRateMode() const;
    bool previewCanvasUsesFrameSwappedPacing() const;
    double targetRefreshRateForFrameRateMode(PreviewCanvasFrameRateMode mode) const;
    qint64 previewCanvasTargetFrameIntervalNs() const;
    qint64 targetFrameIntervalNsForFrameRateMode(PreviewCanvasFrameRateMode mode) const;
    qint64 timelineTargetFrameIntervalNs() const;
    void refreshTimelineWaveformPhaseCompensation();
    void applyPreviewStageMediaFrameRateMode();
    void resetQtPreviewFixedFramePacing();
    void scheduleNextQtPreviewTick();
    void requestNextDisplayRefreshPreviewFrame();
    void requestNextFixedIntervalPreviewFrame();
    void advanceFixedIntervalGateAfterPresent();
    void requestNextPreviewCanvasFrame();
    void refreshPreviewFrameRateTimers();
    void setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    void setPreviewStageMediaFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    void setVideoDecodePrefersSoftware(bool preferSoftware, bool persistState);
    void setTimelineFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    void setPreviewCanvasAspectRatio(double ratio, bool persistState);
    void togglePreviewFullscreen();
    void enterPreviewFullscreen();
    void exitPreviewFullscreen();
    void updatePreviewFullscreenButtonAppearance();
    bool shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const;
    QRect previewFullscreenControlCardRect(bool visible) const;
    void showPreviewFullscreenControls(bool animate = true);
    void hidePreviewFullscreenControls(bool animate = true);
    void schedulePreviewFullscreenControlsAutoHide();
    void pollPreviewFullscreenCursor();
    void updatePreviewFullscreenOverlayGeometry();
    void updatePreviewWorkspaceLayout();
    void cacheWorkspaceLayoutSizes();
    void restoreWorkspaceLayoutSizes();
    void setWorkspacePanelsSwapped(bool swapped, bool persistState);
    void applyWorkspacePanelArrangement();
    void refreshLayoutAfterPageSwitch();
    void updatePreviewPanelLayout(int panelWidthOverride = -1, int panelHeightOverride = -1);
    void updatePreviewObjectStats(double second);
    QString formatPreviewTimestamp(double second) const;
    void showPreviewSliderTimeHint(int sliderValue);
    void requestPausedPreviewSeek(
        double second,
        bool centerView,
        bool submitMediaImmediately = true,
        bool logHotPath = true);
    void applyPausedPreviewVisualSecond(double second, bool centerView);
    void submitPausedMediaSeek(double second, quint64 generation);
    void maybeSubmitLatestPausedMediaSeek();
    void handlePausedPreviewMediaSeekCompleted(double second, quint64 generation);
    void schedulePreviewSeek(double second, bool centerView);
    bool stepPreviewBySeconds(double deltaSeconds, bool centerView);
    bool handlePreviewSeekWheel(QWheelEvent* event);
    void beginPreviewHeldSeek(int direction, int key);
    void stopPreviewHeldSeek(int key = 0);
    void applyPreviewHeldSeekTick();
    void seekPreviewToSecond(double second, bool centerView);
    void seekPreviewDiscreteToSecond(double second, bool centerView);
    void applyPreviewPlaybackRate(double rate);
    bool preparePreviewStartState();
    void onStopPreview();
    void onTogglePreviewPause();
    // Export-page negative-time intro region: the 片头 occupies [-duration, 0)
    // on the preview timeline (添加片头 on). Scrub/pause shows a static intro
    // frame; play advances through it (overlay + opening sfx) and crosses 0 into
    // the chart audition. handleExportIntroSliderSeek routes a slider seek into
    // the region (negative) vs the chart (>=0); refreshExportIntroState reacts to
    // the 添加片头 toggle / install (resizes the slider, enters/leaves the region).
    bool exportIntroEnabled() const;
    double exportIntroLowerBoundSeconds() const;
    void setupExportIntroOverlayData();
    void renderExportIntroFrame(double positionSeconds);
    void enterExportIntroRegion(double positionSeconds);
    void exitExportIntroRegion();
    bool exportIntroLeadInPlaying() const;
    void pauseExportIntroAdvance();
    void startExportIntroAdvance(double fromPositionSeconds);
    void tickExportIntroLeadIn();
    void cancelExportIntroLeadIn();
    bool handleExportIntroSliderSeek(double second);
    void refreshExportIntroState();
    // clock_count count-in for the export-page audition (see MemberStorage). Setup
    // is seeded when the audition scene installs; the cursor resets to skip elapsed
    // ticks at each playback start; due ticks fire one-shot from the playback tick.
    void setExportAuditionClockSchedule(int clockCount, double clockBpm);
    void clearExportAuditionClockSchedule();
    void resetExportAuditionClockCursor(double startSecond);
    void maybeFireExportAuditionClockTicks(double second);
    bool startQtPreviewPlayback(double second, bool resumeFromPause = false);
    // The wall clock names the pause second in both cases; this only selects whether the
    // audio position is sampled alongside it and recorded. AudioPosition is the
    // audio-device auto-pause, the one path where a process stall makes the two diverge
    // measurably — the divergence is logged as pause_audio_stall_observed and acted on by
    // nobody, because at a device switch the stalled audio is lost rather than deferred.
    enum class PauseSecondSource { WallClock, AudioPosition, NativeDeviceCutoff };
    void pauseQtPreviewPlaybackExact(PauseSecondSource pauseSecondSource = PauseSecondSource::WallClock);
    void pausePreviewForAudioDeviceChange(miacode::preview_audio::device_change::Change change);
    void applyPreviewAudioDeviceCutoff(
        const miacode::preview_audio::PreviewAudioDeviceCutoff& cutoff);
    void handlePreviewStartupCanvasPresented();
    void handlePreviewStartupVideoPrepared(double second, quint64 transactionId);
    void handlePreviewAudioPrepared(const miacode::preview_audio::PreviewAudioCompletion& completion);
    void handlePreviewRetainedPlaybackCompleted(
        const miacode::preview_audio::PreviewAudioCompletion& completion);
    void finishQtPreviewPlaybackAndReturnToEntry();
    void stopQtPreviewPlayback(bool keepPosition = true);
    void applyQtPreviewPosition(double second, bool centerView);
    void syncPausedPreviewMediaTimestamps(double second);
    void flushQtPreviewTimelinePosition();
    // Phase-locked sampling entry point: driven by TimelineQuickItem's afterAnimating hook
    // (once per timeline frame, GUI thread, just before that frame's scene-graph sync).
    void onTimelineRenderCadenceTick();
    // Fallback entry point for qtPreviewTimelineTimer_, which is now a watchdog: it flushes
    // only when the render cadence above has gone silent (window hidden, scene graph torn
    // down, render loop stalled), so a dead cadence can never freeze the playhead.
    void onTimelineCadenceWatchdogTick();
    qint64 timelineCadenceWatchdogThresholdMs() const;
    void onQtPreviewTickAtSecond(double second, double fallbackSecond, bool hasAudioClock);
    void onQtPreviewTick();
    double applyVisualClockSmoothing(double audioSecond, double fallbackSecond, bool hasAudioClock);
    void resetVisualClockSmoothing();
    void jumpToNearestTimelineNote(double second, int lane);

    bool playing() const;
    miacode::v2::PlaybackTransportState playbackTransportState() const;
    double positionSeconds() const;
    double durationSeconds() const;
    double lowerBoundSeconds() const;
    void togglePlayback() override;
    void stop() override;
    void seek(double second) override;
    void beginScrub() override;
    void updateScrub(double second) override;
    void updateScrub(double second, bool centerView);
    void endScrub(double second) override;
    void endScrub(double second, bool centerView);
    void setPlaybackRate(double rate) override;
    void nudgePlaybackRate(int direction) override;
    double playbackRate() const;
    QString playbackRateLabel() const;
    QObject* previewRuntimeObject() const;
    QObject* stageMediaHostObject() const;
    double canvasAspectRatio() const;
    QStringList statsTexts() const;
    RenderMode muriRenderMode() const;
    void setMuriRenderMode(RenderMode mode);
    void toggleMuriRenderMode();
    QStringList availableSkinDirectoryNames() const;
    QString skinDisplayName(const QString& directoryName) const;
    QString resolveSkinDir() const;
    QString resolveSkinRootDir() const;
    QString resolveCustomOutlineDir() const;
    void applyOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                             bool persistState);
    QVariantMap renderSettings() const;
    void setRenderSetting(const QString& key, const QVariant& value);
    void refreshSurfaces();
    void applySfxLevels();
    void prepareForShutdown();
    PreviewAudioSettings audioSettings() const;
    void applyAudioSettings(const PreviewAudioSettings& settings);
    void saveAudioSettingsAsSoftwareDefault();
    void restoreAudioSettingsFromSoftwareDefault();

    QObject* timelineStateBridge() const;
    void noteTimelineSurfaceReady();
    void navigateToSecond(double second);
    void centerOnSecond(double second);
    void wheelNavigateToSecond(double second);
    void timelineDragStarted();
    void timelineDragFinished(double second);
    void timelineUserInteractionStarted();
    void setFollowPreviewEnabled(bool enabled);
    QString bottomTabsCurrentTabId() const;
    void setBottomTabsCurrentTabId(const QString& tabId);
    bool bottomTabsVisible() const;
    bool timelineTabVisible() const;
    bool muriTabVisible() const;
    bool validationTabVisible() const;
    bool ignoreMuriIssuePrompts() const;

    // Live wall-clock extrapolation of the playhead. Public because
    // Session::currentPreviewAuthoritativeAudioClockSecond() forwards here for
    // the four domains outside playback/ that still read the clock through
    // Session, matching how every other forwarding shell reaches this class.
    // NOT the same value as currentAudioClockSecond() — see the definition site
    // in Playback.cpp for why the two must stay separate.
    double authoritativeAudioClockSecond() const;

private:
    bool bottomTabsTabVisibleFromState(RuntimeContext::BottomTabsTabId tabId) const;
    static QString bottomTabsTabIdToString(RuntimeContext::BottomTabsTabId tabId);
    void queueTimelineCursorBridgeUpdate(double second, bool centerView);
    void scheduleDeferredTimelineBridgeFlush();
    void invalidatePreviewFollowBindingCache();
    bool cachedPreviewFollowBindingContainsSecond(double second) const;
    void cachePreviewFollowBinding(const TimelineQuickModel::PreviewFollowBinding& binding);
    void cancelPreviewStartupSync(const char* cause);
    void clearPreviewPlayingRetainedSeek();
    void tryCommitPreviewStartupSync();
    void handlePreviewAudioStartupCompletion(
        const miacode::preview_audio::PreviewAudioCompletion& completion);
    void handlePreviewPlayingRetainedSeekCompletion(
        const miacode::preview_audio::PreviewAudioCompletion& completion);
    void scheduleDeferredPreviewUiTail(
        bool applyPreviewVisualSettings,
        bool applyDeferredAnalysis,
        bool dispatchTimelineAnalysis,
        bool writeProfilingSummary,
        bool updatePauseButton,
        bool updateObjectStats,
        double objectStatsSecond,
        bool refreshStageMediaDebugState,
        bool updatePausedPreviewFollowDecoration);
    quint64 requestPausedPreviewVisualSeek(
        double second,
        bool centerView,
        int submitNowLogValue,
        bool logHotPath = true);
    void pauseQtPreviewPlaybackForReanchor();
    void stopQtPreviewTimers();
    void finalizeQtPreviewPlaybackStart(double effectiveStartSecond);
    void softStopQtPreviewPlaybackToSecond(double second, bool centerView);
    void anchorQtPreviewPlaybackToSecond(double second, bool centerView);
    // Stage 4.9d-3 (D-class): function bodies moved in verbatim from their
    // former Session/ShellHost/DocumentSessionHost homes — see LayoutUi.cpp,
    // PlaybackState.cpp, TimelineFlow.cpp and Playback.cpp for the definitions.
    void updatePauseButtonAppearance();
    void setMetadataExtraText(const QString& text);
    void clearValidationErrors();
    void showPreviewPlaybackRateToast(double rate);
    void updatePreviewPlaybackRateToastGeometry();
    void updateEditorFindBarGeometry();
    void applyFindOverlayInset();
    // Stage 4.9d-4a (T-class): thin StageMediaHost forwards whose bodies only touch
    // state_/ui_ (including via the state_.previewStageMediaHost_ pointer the
    // coordinator already holds) — moved in verbatim, see SurfaceContract.cpp.
    bool previewStageMediaRouteHasVideo() const;
    void pausePreviewStageMediaRoutePlayback();
    void syncPreviewStageMediaRoutePlayback(double second);
    void setPreviewStageMediaRouteObservedPlayheadSecond(double second);
    void resetPreviewStageMediaRouteTimelineOffset();
    void submitPreviewStageMediaRoutePausedSeek(double second, quint64 generation);
    void seekPreviewStageMediaRouteWhilePaused(double second);
    // Stage 4.9d-4a: Session-own preview methods with no other caller outside
    // playback/ — moved in verbatim, see IntroRegion.cpp, PlaybackGlue.cpp,
    // TimelineFlow.cpp and Playback.cpp for the definitions.
    bool currentExportIntroLeadInSpec(IntroBannerSpec* outSpec) const;
    bool ensureAuditionSceneReady();
    miacode::waveform::WaveformCacheService* ensureWaveformCacheService();
    QString formatPreviewPlaybackRateToastText(double rate) const;
    Session& session_;
    RuntimeContext::Ui& ui_;
    RuntimeContext::State& state_;
    PlaybackIdentityGate identity_;

    bool beginPlaybackCommand();
};

}  // namespace miacode::runtime
