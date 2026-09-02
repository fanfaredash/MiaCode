    bool maybeSaveBeforeContinue();
    // A warning for the viewer, through the shell rather than a Widgets box.
    void postShellNotice(const QString& title, const QString& text);
    void configureRuntimeDebugOutput();
    PreviewStageMediaRoute previewStageMediaRoute() const;
    bool previewUsesStageMediaHostRoute() const;
    bool shouldDeferQuickShellStartupStageMediaLoad() const;
    void noteQuickShellStartupUiReady();
    void scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
    void updatePreviewStageMediaPresentationMode(bool requestUpdate = true);
    void ensurePreviewStageMediaRouteInitialized();
    void syncPreviewStageMediaRouteChartPath(
        const QString& chartPath,
        const QString& trackPath,
        double pausedSecond,
        const QString& chartVideoOverridePath = QString()
    );
    void clearPreviewStageMediaRoute();
    void releasePreviewStageMediaDecoderForFileOperation();
    void applyPreviewMediaWarmupToStageMediaRoute(
        const QString& chartPath,
        const QString& resolvedMediaPath,
        const QString& trackPath
    );
    void resetPreviewStageMediaRouteTimelineOffset();
    void applyPreviewStageMediaRoutePlaybackRate(double rate, const char* site = nullptr);
    void applyPreviewStageMediaRouteVisualSettings();
    bool previewStageMediaRouteHasVideo() const;
    double previewStageMediaRouteCurrentPlaybackSecond() const;
    void startPreviewStageMediaRoutePlayback(double second);
    void syncPreviewStageMediaRoutePlayback(double second);
    void pausePreviewStageMediaRoutePlayback();
    void seekPreviewStageMediaRouteWhilePaused(double second);
    void submitPreviewStageMediaRoutePausedSeek(double second, quint64 generation);
    void setPreviewStageMediaRouteObservedPlayheadSecond(double second);
    void ensurePreviewStageMediaHostInitialized();
    void ensureQuickShellPreviewCompositeSurfaceInitialized();
    void shutdownPreviewStageMediaHost();
    bool quickShellPreviewUsesSeparateSurface() const;
    QWindow* quickShellPreviewCompositeWindow() const;
    void refreshQuickShellPreviewCompositeSurfaceState();
    void refreshPreviewStageMediaRouteDebugState(bool requestUpdate = true);
    void ensurePreviewSfxRuntimePrepared();
    void schedulePreviewSubsystemWarmup();
    void schedulePreviewMediaWarmup(quint64 generation, const QString& chartPathSnapshot, const QString& trackPathSnapshot, const QString& chartVideoOverrideSnapshot = QString());
    void schedulePreviewSfxWarmup(
        quint64 generation,
        const QString& chartPathSnapshot,
        const QString& trackPathSnapshot,
        const PreviewAudioSettings& audioSettingsSnapshot,
        double playbackRateSnapshot
    );
    void applyPreviewMediaWarmupResult(
        quint64 generation,
        const QString& chartPath,
        const QString& resolvedMediaPath,
        const QString& trackPath,
        qint64 workerElapsedMs
    );
    void applyPreviewSfxWarmupResult(
        quint64 generation,
        const QString& chartPath,
        const QString& trackPath,
        const QString& sfxDir,
        qint64 workerElapsedMs
    );
    void finishFrameBootstrap(QToolBar* toolBar, const std::function<void(const QString&)>& logStartupStage);
    void setPreviewFixedTimerHighResolutionActive(bool active);
    void hidePreviewPlaybackRateToast();
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument();
    bool saveToPath(const QString& path);
    void runAutosaveCheck(bool allowHistory = true);
    QString resolveAutosaveDirectoryPath() const;
    QString currentDocumentTextForAutosave() const;
    void pruneAutosaveFiles(const QString& autosaveDirectoryPath) const;
    void resetAutosaveState(const QString& referenceText);
    void setEditorText(const QString& text);
    void setMetadataExtraText(const QString& text);
    bool openFileAtPath(const QString& path, bool showStatusMessage = true, bool showErrors = true);
    bool restoreLastSessionFile();
    void scheduleStartupRestoreLastSessionFile();
    void cancelPendingStartupRestore();
    void applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared);
    void applyOpenedDocumentState(
        const QString& normalizedPath,
        TextEncoding encodingUsed,
        const SimaiDocument& document,
        bool showStatusMessage,
        double knownTrackDurationSeconds = -1.0
    );
    void setCurrentFilePath(const QString& path, bool suppressImmediateRefresh = false);
    void updateWindowTitle();
    void updateCurrentFileLabel();
    void updatePauseButtonAppearance();
    void updateDirtyState();
    void refreshCurrentFieldDirtyState();
    void markCurrentFieldDirty();
    bool undoDeletedDifficultyField();
    void clearDeletedDifficultyUndoState();
    void rebuildFieldSidebar();
    void updateEditorHeader();
    void updateEditorHeaderLayoutMode();
    void syncEditorHeaderMinimumWidth();
    void updateEditorStatus();
    void updateEditorValidationSummary();
    void updateEditorEmptyState();
    void updateDifficultyScopedActionStates();
    // Export-page intro lead-in: returns true (and fills *outSpec) when the
    // embedded video panel has 添加片头 active, so the audition can play the
    // animated intro before the chart. cancelExportIntroLeadIn() forwards to the
    // timeline driver (used by teardown).
    bool currentExportIntroLeadInSpec(IntroBannerSpec* outSpec) const;
    bool exportIntroLeadInPlaying() const;
    void cancelExportIntroLeadIn();
    // Negative-time intro region forwarders: handleExportIntroSliderSeek routes a
    // slider seek into the intro (returns true if handled); lowerBound is the
    // slider minimum (negative when 添加片头 on); refreshExportIntroState reacts to
    // the 添加片头 toggle.
    bool handleExportIntroSliderSeek(double second);
    double exportIntroLowerBoundSeconds() const;
    // clock_count count-in for the export-page audition: seed/clear forwarders the
    // audition-scene install/teardown call (the count-in plays on the chart audition
    // after the intro hands off, mirroring the exported clock track).
    void setExportAuditionClockSchedule(int clockCount, double clockBpm);
    void clearExportAuditionClockSchedule();
    void updateMetadataPageMode();
    bool deleteDifficultyField(int difficultyId);
    void populateMetadataPage();
    void populateDifficultyPage(int difficultyId);
    bool switchToMetadataField();
    bool switchToWelcomePage();
    bool switchToDifficultyField(int difficultyId);
    bool switchToLatencyField();
    bool switchToExportField();
    void activateInitialField();
    void loadDocument();
    void clearTimelineAndPreview();
    QString activeChartText() const;
    miacode::simai::SimaiTimingMetadata currentTimingMetadata() const;
    double parsedRawFirstSeconds(bool* ok = nullptr) const;
    double parsedFirstSeconds(bool* ok = nullptr) const;
    double parsedWholeBpm(bool* ok = nullptr) const;
    int parsedClockCount() const;
    QString parsedLatencyMeterId() const;
    void resetPreviewTrackTimelineOffsets();
    miacode::waveform::WaveformCacheService* ensureWaveformCacheService();
    void applyWaveformData(const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData);
    void refreshWaveformCache();
    void refreshWaveformCache(double knownDurationSeconds);
    void refreshTimelineQuickModelFromCurrentText();
    void applyLatestTimelinePreviewStateToPausedPreview();
    void scheduleTimelineRefresh();
    void refreshTimelineMetadata();
    void requestTimelineSlowRefresh();
    void dispatchTimelineSlowRefresh();
    void scheduleTimelineAnalysisRefresh(
        const TimelineSlowRefreshRequest& request,
        const SimaiNativeParseResult& parseResult,
        const TimelinePreviewRefreshState& previewState);
    bool scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs = -1);
    void requestTimelineAnalysisDispatch(int delayMs = -1);
    void dispatchTimelineAnalysisRefresh();
    const MuriAnalysisReport& alignedMuriAnalysisReportForPreview() const;
    void applyAlignedMuriAnalysisReportToViews();
    void navigateTimelineToSecond(double second, bool focusEditor = true);
    void deferTimelineCursorBridgeUpdate(double second, bool centerView);
    void scheduleDeferredEditorUiUpdate(
        bool updateStatus = false,
        bool updateEmptyState = false,
        bool syncTimelineCursor = false,
        bool centerView = false,
        bool syncPreviewFollow = false,
        double previewFollowSecond = 0.0,
        bool ensurePreviewFollowVisible = false);
    void flushDeferredEditorUiUpdate();
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
    void syncEditorCursorToPreviewSecond(
        double second,
        bool centerView = true,
        bool ensureVisibleWhenPaused = false);
    void jumpToNearestTimelineNote(double second, int lane);
    bool startQtPreviewPlayback(double second, bool resumeFromPause = false);
    void pauseQtPreviewPlaybackExact();
    void stopQtPreviewPlayback(bool keepPosition = true);
    void applyQtPreviewPosition(double second, bool centerView);
    // Single writer for playing_. The flag has never travelled alone —
    // every site that flips it also tells EditorSyncController — and the QML
    // shell needs it too: its play/pause control reads it, and for a while
    // nothing announced the flip at all, which is how the transport ended up
    // frozen while the timeline followed along fine. Assign through here, never
    // directly.
    void setPreviewPlayingFlag(bool playing);
    void syncPausedPreviewMediaTimestamps(double second);
    void flushQtPreviewTimelinePosition();
    void onTimelineRenderCadenceTick();
    void onTimelineCadenceWatchdogTick();
    void onQtPreviewTick();
    bool previewCanvasUsesFrameSwappedPacing() const;
    qint64 previewCanvasTargetFrameIntervalNs() const;
    void resetQtPreviewFixedFramePacing();
    void scheduleNextQtPreviewTick();
    void requestNextDisplayRefreshPreviewFrame();
    void requestNextFixedIntervalPreviewFrame();
    void advanceFixedIntervalGateAfterPresent();
    void requestNextPreviewCanvasFrame();
    void seekPreviewToSecond(double second, bool centerView);
    void seekPreviewDiscreteToSecond(double second, bool centerView);
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
    void updatePreviewSliderRange();
    void updatePreviewSliderPosition(double second);
    QString formatPreviewTimestamp(double second) const;
    void showPreviewSliderTimeHint(int sliderValue);
    void refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers);
    void updatePreviewObjectStats(double second);
    void clearPreviewObjectStats();
    void emitChartSwitchResourceGauge();
    int updatePreviewStatsLayoutMode(int hostWidth = -1);
    int previewStatsMinimumHeightForPanelWidth(int panelWidth) const;
    void updatePreviewWorkspaceLayout();
    void updatePreviewPanelLayout(int panelWidthOverride = -1, int panelHeightOverride = -1);
    void applyWorkspacePanelArrangement();
    void cacheWorkspaceLayoutSizes();
    void restoreWorkspaceLayoutSizes();
    void refreshQuickShellRehostedWidgetParent(QWidget* widget);
    void refreshLayoutAfterPageSwitch();
    // Arms a short watch window during which the NEXT resize of the rehosted
    // workspace surface re-runs refreshLayoutAfterPageSwitch(). Page switches that
    // change the preview aspect (export page) or the bottom-tabs height drive the
    // workspace surface to a new size ASYNCHRONOUSLY from QML, AFTER the synchronous
    // page-build + refresh have already run; without this, the just-built page is
    // left composited at its pre-resize (scrambled) geometry until an input event
    // forces a repaint. Self-disarms after a short grace.
    void armWorkspaceSurfaceSettleRelayout();
    int bottomTabsTabIndex(BottomTabsTabId tabId) const;
    BottomTabsTabId bottomTabsTabIdFromString(const QString& tabId) const;
