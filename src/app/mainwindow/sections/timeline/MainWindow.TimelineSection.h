#pragma once

#include "../../MainWindow.h"

class MainWindow::TimelineSection {
public:
    TimelineSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void resetPreviewTrackTimelineOffsets();
    void applyWaveformData(const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData);
    void refreshWaveformCache();
    void refreshWaveformCache(double knownDurationSeconds);
    bool hasActiveDifficulty() const;
    int activeDifficultyId() const;
    QString activeChartText() const;
    miacode::simai::SimaiTimingMetadata currentTimingMetadata() const;
    double parsedRawFirstSeconds(bool* ok = nullptr) const;
    double parsedFirstSeconds(bool* ok = nullptr) const;
    double parsedWholeBpm(bool* ok = nullptr) const;
    QString parsedLatencyMeterId() const;
    void applyLatencyDetectorOffset(double seconds);
    void applyLatencyDetectorBpm(double bpm);
    void setCurrentFilePath(const QString& path, bool suppressImmediateRefresh = false);
    void updateWindowTitle();
    void updateCurrentFileLabel();
    QString editorText() const;
    void scheduleTimelineRefresh();
    void refreshTimelineMetadata();
    void applyTimelineQuickChange(int position, int charsRemoved, int charsAdded);
    void refreshTimelineQuickModelFromCurrentText();
    bool timelineTabIsForeground() const;
    bool quickTimelineBridgeReady() const;
    void flushDeferredTimelineBridgeState();
    void onTimelineHeaderNavigateRequested(double second);
    void onTimelineUserInteractionStarted();
    void onTimelineDragStarted();
    void onTimelineCenterNavigateRequested(double second);
    void onTimelineDragFinished(double second);
    void onTimelineFollowPreviewToggled(bool enabled);
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
    void seekTimelineToCursor(int line, int col);
    void syncTimelineToEditorCursor(bool centerView = true);
    void navigateTimelineToSecond(double second, bool focusEditor = true);
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
    double previewDurationSeconds() const;
    double previewPlaybackEndSeconds() const;
    void updatePreviewSliderRange();
    void updatePreviewSliderPosition(double second);
    void refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers);
    void clearPreviewObjectStats();
    int updatePreviewStatsLayoutMode(int hostWidth = -1);
    int previewStatsMinimumHeightForPanelWidth(int panelWidth) const;
    double normalizedPreviewCanvasAspectRatio(double ratio) const;
    QString previewCanvasFrameRateModeStorageValue() const;
    double currentPreviewCanvasRefreshRate() const;
    bool previewCanvasUsesFrameSwappedPacing() const;
    qint64 previewCanvasTargetFrameIntervalNs() const;
    qint64 timelineTargetFrameIntervalNs() const;
    double fixedIntervalPreviewSecondForDeadlineNs(qint64 deadlineNs) const;
    void resetQtPreviewFixedFramePacing();
    void scheduleNextQtPreviewTick();
    void requestNextDisplayRefreshPreviewFrame();
    void refreshPreviewFrameRateTimers();
    void setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
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
    void requestPausedPreviewSeek(double second, bool centerView, bool submitMediaImmediately = true);
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
    void applyPreviewPlaybackRate(double rate);
    bool preparePreviewStartState();
    void onStopPreview();
    void onTogglePreviewPause();
    bool startQtPreviewPlayback(double second, bool resumeFromPause = false);
    void handlePreviewStartupCanvasPresented();
    void handlePreviewStartupVideoPrepared(double second, quint64 transactionId);
    void finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage);
    void stopQtPreviewPlayback(bool keepPosition = true);
    void applyQtPreviewPosition(double second, bool centerView);
    void syncPausedPreviewMediaTimestamps(double second);
    void flushQtPreviewTimelinePosition();
    void onQtPreviewTickAtSecond(double second);
    void onQtPreviewTick();
    void jumpToNearestTimelineNote(double second, int lane);

private:
    void queueTimelineCursorBridgeUpdate(double second, bool centerView);
    void invalidatePreviewFollowBindingCache();
    bool cachedPreviewFollowBindingContainsSecond(double second) const;
    void cachePreviewFollowBinding(const TimelineQuickModel::PreviewFollowBinding& binding);
    void cancelPreviewStartupSync();
    void tryCommitPreviewStartupSync();
    void stopQtPreviewTimers();
    void finalizeQtPreviewPlaybackStart(double effectiveStartSecond);
    void pauseQtPreviewPlaybackExact();
    void anchorQtPreviewPlaybackToSecond(double second, bool centerView);
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
