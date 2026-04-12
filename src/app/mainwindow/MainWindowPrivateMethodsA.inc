    bool maybeSaveBeforeContinue();
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
        double pausedSecond
    );
    void clearPreviewStageMediaRoute();
    void applyPreviewMediaWarmupToStageMediaRoute(
        const QString& chartPath,
        const QString& resolvedMediaPath,
        const QString& trackPath
    );
    void resetPreviewStageMediaRouteTimelineOffset();
    void applyPreviewStageMediaRoutePlaybackRate(double rate);
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
    void schedulePreviewMediaWarmup(quint64 generation, const QString& chartPathSnapshot, const QString& trackPathSnapshot);
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
    void setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu);
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument();
    bool saveToPath(const QString& path);
    void runAutosaveCheck();
    QString resolveAutosaveDirectoryPath() const;
    QString currentDocumentTextForAutosave() const;
    void pruneAutosaveFiles(const QString& autosaveDirectoryPath) const;
    void resetAutosaveState(const QString& referenceText);
    bool applyBatchTransform(const QString& opName, const BatchTransform& transform);
    bool applySelectionBatchTransform(const QString& opName, const BatchTransform& transform);
    std::pair<int, int> currentCursorLineCol() const;
    std::pair<int, int> currentSelectionOrCursorLineCol() const;
    bool currentSelectionRange(int* startPos, int* endPos) const;
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
    bool currentFieldHasUndoChanges() const;
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
    void updateMetadataPageMode();
    bool deleteDifficultyField(int difficultyId);
    void updateDifficultyDeleteButton(bool visible);
    void populateMetadataPage();
    void populateDifficultyPage(int difficultyId);
    bool switchToMetadataField();
    bool switchToWelcomePage();
    bool switchToDifficultyField(int difficultyId);
    void activateInitialField();
    void loadDocument(const SimaiDocument& document);
    void clearTimelineAndPreview();
    bool hasActiveDifficulty() const;
    int activeDifficultyId() const;
    QString activeChartText() const;
    miacode::simai::SimaiTimingMetadata currentTimingMetadata() const;
    double parsedFirstSeconds(bool* ok = nullptr) const;
    double parsedWholeBpm(bool* ok = nullptr) const;
    QString parsedLatencyMeterId() const;
    void applyLatencyDetectorOffset(double seconds);
    void applyLatencyDetectorBpm(double bpm);
    void resetPreviewTrackTimelineOffsets();
    void applyWaveformData(const QVector<float>& peaks, double durationSeconds);
    void refreshWaveformCache();
    void refreshWaveformCache(double knownDurationSeconds);
    void applyWaveformCacheEntry(
        quint64 generation,
        const QString& trackPath,
        qint64 fileSize,
        qint64 lastModifiedMs,
        double durationSeconds,
        const QVector<float>& peaks,
        qint64 buildElapsedMs
    );
    void applyTimelineQuickChange(int position, int charsRemoved, int charsAdded);
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
        bool suppressSignals
    );
    void syncEditorCursorToPreviewSecond(double second, bool centerView = true);
    void jumpToNearestTimelineNote(double second, int lane);
    bool startQtPreviewPlayback(double second, bool resumeFromPause = false);
    void stopQtPreviewPlayback(bool keepPosition = true);
    void applyQtPreviewPosition(double second, bool centerView);
    void syncPausedPreviewMediaTimestamps(double second);
    void flushQtPreviewTimelinePosition();
    void onQtPreviewTick();
    void finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage);
    bool previewCanvasUsesFrameSwappedPacing() const;
    qint64 previewCanvasTargetFrameIntervalNs() const;
    void resetQtPreviewFixedFramePacing();
    void scheduleNextQtPreviewTick();
    void requestNextDisplayRefreshPreviewFrame();
    void seekPreviewToSecond(double second, bool centerView);
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
    void updatePreviewSliderRange();
    void updatePreviewSliderPosition(double second);
    QString formatPreviewTimestamp(double second) const;
    void showPreviewSliderTimeHint(int sliderValue);
    void refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers);
    void updatePreviewObjectStats(double second);
    void clearPreviewObjectStats();
    int updatePreviewStatsLayoutMode(int hostWidth = -1);
    int previewStatsMinimumHeightForPanelWidth(int panelWidth) const;
    void updatePreviewWorkspaceLayout();
    void updatePreviewPanelLayout(int panelWidthOverride = -1, int panelHeightOverride = -1);
    void setWorkspacePanelsSwapped(bool swapped, bool persistState);
    void applyWorkspacePanelArrangement();
    void cacheWorkspaceLayoutSizes();
    void restoreWorkspaceLayoutSizes();
    void refreshQuickShellRehostedWidgetParent(QWidget* widget);
    void refreshLayoutAfterPageSwitch();
