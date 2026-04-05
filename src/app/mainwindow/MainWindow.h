#pragma once

#include <functional>
#include <utility>

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QVector>

#include "PreviewAudioSettings.h"
#include "PreviewRenderSettings.h"
#include "SimaiDocument.h"
#include "SimaiTimingMetadata.h"
#include "SimaiNativeParser.h"
#include "timeline/TimelineData.h"
#include "timeline/TimelineQuickModel.h"
#include "timeline/TimelineSlowRefresh.h"
#include "TimelineView.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "tools/video_export/VideoExportSnapshot.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"

class QAction;
class QByteArray;
class QCloseEvent;
class QDockWidget;
class QEvent;
class QFrame;
class QGridLayout;
class QHBoxLayout;
class QHideEvent;
class QLabel;
class LatencyDetectorDialog;
class QListWidget;
class QListWidgetItem;
class QJsonObject;
class QLineEdit;
class QMenu;
class QMoveEvent;
class QTabWidget;
class QToolBar;
class QPushButton;
class QShowEvent;
class QThread;
class QThreadPool;
class BracketScopeHighlighter;
class PlainCodeEditor;
class PreviewRuntime;
class PreviewMediaController;
class QPlainTextEdit;
class QProcess;
class QProgressDialog;
class QPropertyAnimation;
class QResizeEvent;
class QStackedWidget;
class QSlider;
class QSplitter;
class QSoundEffect;
class QTimer;
class QTextEdit;
class QToolButton;
class QWidget;
class QWheelEvent;
class QtPreviewSfxRuntime;
class TimelineView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    struct CliVideoExportRequest {
        QString chartPathOrDirectory;
        QString difficulty = QStringLiteral("MAS");
        QString outputPath;
        int outputWidth = 1024;
        int outputHeight = 1024;
        int fps = 60;
        double exportStartSeconds = 0.0;
        double contentDurationSeconds = -1.0;
        bool showTimestamp = true;
        bool showObjectStatsHud = false;
        bool smoothBrightness = miacode::preview_video::kSmoothBrightnessDefault;
        double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
        double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
        double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;
        PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
        double noteFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
        int skinLoadWaitMs = 2000;
    };

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    bool exportPreviewVideoFromCli(
        const CliVideoExportRequest& request,
        QString* resolvedOutputPath,
        QString* errorMessage,
        QString* details = nullptr
    );

protected:
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onValidateSimai();
    void onNewFile();
    void onOpenFile();
    bool onSaveFile();
    bool onSaveFileAs();
    void onMirrorLeftRight();
    void onMirrorUpDown();
    void onRotate180();
    void onRotate45CounterClockwise();
    void onRotate45Clockwise();
    void onNormalizeWholeChart();
    void onToggleBreakSelection();
    void onToggleExSelection();
    void onToggleFireworkSelection();
    void onRandomRotateSelection();
    void onStopPreview();
    void onTogglePreviewPause();
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    void onEditStaticTapOnSlideThreshold();
    void onExportPreviewVideo();
    void onBatchExportPreviewVideo();
    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
    void onOpenLatencyDetector();
    void onPreferences();
    void onAbout();
    void onToggleFindReplace();
    void onFindNext();
    void onFindPrevious();
    void onReplaceOne();
    void onReplaceAll();
    void onErrorItemActivated(QListWidgetItem* item);
    void onMuriItemActivated(QListWidgetItem* item);
private:
    using BatchTransform = std::function<QString(const QString&, int*)>;
    enum class ChartTransformOp {
        MirrorLeftRight,
        MirrorUpDown,
        Rotate180,
        Rotate45CounterClockwise,
        Rotate45Clockwise,
    };
    enum class PreviewCanvasFrameRateMode {
        Fps60,
        Fps120,
        DisplayRefresh,
    };
    enum class PreviewSkinVariant {
        Standard,
        Dx,
    };
    enum class TextEncoding {
        Utf8,
        System,
    };
    struct TimelineCursorNote;
    struct PreparedStartupRestoreDocument {
        quint64 generation = 0;
        QString normalizedPath;
        SimaiDocument document;
        TextEncoding encodingUsed = TextEncoding::Utf8;
        QString resolvedTrackPath;
        double trackDurationSeconds = 0.0;
        bool hasTrackDuration = false;
        qint64 readElapsedMs = 0;
        qint64 decodeElapsedMs = 0;
        qint64 parseElapsedMs = 0;
        qint64 trackProbeElapsedMs = 0;
        qint64 totalElapsedMs = 0;
    };
    struct WaveformCacheEntry {
        QString trackPath;
        qint64 fileSize = -1;
        qint64 lastModifiedMs = -1;
        double durationSeconds = 0.0;
        QVector<float> peaks;
    };

    bool maybeSaveBeforeContinue();
    void configureRuntimeDebugOutput();
    void ensurePreviewMediaControllerInitialized();
    void shutdownPreviewMediaController();
    void dispatchPreviewMediaControllerCall(
        std::function<void(PreviewMediaController*)> call,
        Qt::ConnectionType connectionType = Qt::QueuedConnection
    ) const;
    bool queryPreviewMediaControllerHasVideoMedia() const;
    double queryPreviewMediaControllerCurrentPlaybackSecond() const;
    QString queryPreviewMediaControllerProfilingSummaryLines() const;
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
    void setupInitialWindowGeometry();
    void setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu);
    void setOutlineDockCollapsed(bool collapsed);
    void updateOutlineDockCollapseButton();
    void updateLatencyDetectorAvailability();
    QString resolveLatencyDetectorTrackPath() const;
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument();
    bool saveToPath(const QString& path);
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
    QTextEdit* activeFindTarget() const;
    bool runFindInEditor(bool backward);
    void updateEditorFindBarGeometry();
    void applyFindOverlayInset();
    void hideFindReplaceBar();
    void updateDirtyState();
    void markCurrentFieldDirty();
    bool undoDeletedDifficultyField();
    void clearDeletedDifficultyUndoState();
    void rebuildFieldSidebar();
    void updateEditorHeader();
    void updateEditorHeaderLayoutMode();
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
    void schedulePreviewSeek(double second, bool centerView);
    bool stepPreviewSliderBySeconds(double deltaSeconds, bool centerView);
    bool handlePreviewSliderWheel(QWheelEvent* event);
    void beginPreviewHeldSeek(int direction, int key);
    void stopPreviewHeldSeek(int key = 0);
    void applyPreviewHeldSeekTick();
    void updatePreviewSliderRange();
    void updatePreviewSliderPosition(double second);
    QString formatPreviewTimestamp(double second) const;
    void showPreviewSliderTimeHint(int sliderValue);
    void refreshEmbeddedPreviewSurface(bool force = false);
    void scheduleEmbeddedPreviewSurfaceRefresh(int delayMs = 0);
    void noteEmbeddedPreviewResizeActivity(const char* source = nullptr);
    void suspendEmbeddedPreviewForNativeDialog(const char* source = nullptr);
    void resumeEmbeddedPreviewForNativeDialog(const char* source = nullptr);
    void refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers);
    void updatePreviewObjectStats(double second);
    void clearPreviewObjectStats();
    int updatePreviewStatsLayoutMode(int hostWidth = -1);
    void updatePreviewWorkspaceLayout();
    void updatePreviewPanelLayout();
    void setWorkspacePanelsSwapped(bool swapped, bool persistState);
    void applyWorkspacePanelArrangement();
    void cacheWorkspaceLayoutSizes();
    void restoreWorkspaceLayoutSizes();
    void refreshLayoutAfterPageSwitch();
    void openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title);
    void applySharedExportTaskSettings(const VideoExportTask& task);
    double previewDurationSeconds() const;
    double previewPlaybackEndSeconds() const;
    void applyPreviewPlaybackRate(double rate);
    void togglePreviewFullscreen();
    void enterPreviewFullscreen();
    void exitPreviewFullscreen();
    void updatePreviewFullscreenButtonAppearance();
    void updatePreviewFullscreenOverlayGeometry();
    void showPreviewFullscreenControls(bool animate = true);
    void hidePreviewFullscreenControls(bool animate = true);
    void schedulePreviewFullscreenControlsAutoHide();
    void pollPreviewFullscreenCursor();
    bool shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const;
    QRect previewFullscreenControlCardRect(bool visible) const;
    void setPreviewCanvasAspectRatio(double ratio, bool persistState);
    double normalizedPreviewCanvasAspectRatio(double ratio) const;
    void setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    PreviewCanvasFrameRateMode previewCanvasFrameRateModeFromStorageValue(const QString& value) const;
    QString previewCanvasFrameRateModeStorageValue() const;
    PreviewSkinVariant previewSkinVariantFromStorageValue(const QString& value) const;
    QString previewSkinVariantStorageValue() const;
    double currentPreviewCanvasRefreshRate() const;
    void refreshPreviewFrameRateTimers();
    int computeBottomTabsDeviceHeight() const;
    void updateBottomTabsDeviceHeight();
    double timelineSecondForCursor(int line, int col) const;
    void jumpToLocation(int line, int col);
    QString transformChartText(const QString& input, ChartTransformOp op, int* changedCount = nullptr) const;
    QString editorText() const;
    QString resolveDefaultTrackPath() const;
    QString resolvePreviewSkinDir() const;
    bool buildVideoExportSnapshot(
        const VideoExportTask& requestedTask,
        VideoExportSnapshot* snapshot,
        QString* errorMessage
    );
    bool buildVideoExportSnapshotForChartDirectory(
        const QString& chartDirectory,
        int difficultyId,
        const QString& difficultyToken,
        const VideoExportTask& requestedTask,
        const QString& outputDirectory,
        VideoExportSnapshot* snapshot,
        QString* errorMessage
    );
    bool startVideoExportWorkerProcess(QProcess* process, const VideoExportSnapshot& snapshot, QString* errorMessage);
    bool runVideoExportWorkerSync(
        const VideoExportSnapshot& snapshot,
        QProgressDialog* progressDialog,
        bool* canceledByUser,
        QString* errorMessage,
        const std::function<void(int percent, const QString& rawMessage)>& progressCallback = {}
    );
    bool launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage);
    void showExportToolbarMenu();
    void handleVideoExportWorkerStdout();
    void handleVideoExportWorkerStderr();
    void handleVideoExportWorkerEvent(const QJsonObject& eventObject);
    void handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus);
    void cancelVideoExportWorker();
    void clearVideoExportWorkerState();
    QString resolveProjectRenderStateFilePath() const;
    QString resolveInitialOpenDirectory() const;
    void resetPortablePreviewSettingsToDefaults();
    void applyPortablePreviewSettings(const QJsonObject& preview);
    void loadPortableState();
    void savePortableState() const;
    void applyEditorTextFontSize(int pointSize, bool persistPreference);
    void applyEditorLineSpacingFactor(double factor, bool persistPreference);
    void applyUiTheme();
    void applySystemWindowBackdrop(QWidget* target = nullptr) const;
    void setInvalidStarPreviewEasterEggEnabled(bool enabled);
    void ensureInvalidStarPreviewEasterEggSounds();
    void playInvalidStarPreviewEasterEggSound(bool enabled);
    void persistEditorTextFontPreference() const;
    void loadProjectRenderState();
    void saveProjectRenderState() const;
    void removeProjectRenderState() const;
    void applyPreviewAudioSettingsToRuntime();
    void setLastOpenDirectory(const QString& pathOrDir);
    bool runValidateSimai();
    bool runValidateSimaiSilently(bool focusFirstIssue = false);
    bool preparePreviewStartState();
    void appendOutput(const QString& title, const QString& payload);
    void logWindowGeometryDebug(const QString& tag, const QString& detail = QString());
    QString formatWindowStateFlags(Qt::WindowStates states) const;
    void logTopLevelWindowSnapshot(const QString& tag);
    void logNativeWindowDebug(const QString& tag, WId dialogWId = 0);
    void logOwnedNativeWindowSnapshot(const QString& tag, int maxWindows = 12);
    void refreshEditorExtraSelections();
    void setPreviewFollowDecoration(int line, int col);
    void clearPreviewFollowDecoration();
    void clearValidationErrors();
    void clearValidationDecorations();
    void addValidationError(
        int line,
        int col,
        const QString& message,
        const QString& issueTypeKey = QString(),
        const QString& issueTypeLabel = QString(),
        bool ignoredInHeader = false
    );
    void addValidationDecoration(int line, int col, const QString& message, int endCol = -1);
    void clearMuriDiagnostics();
    QListWidgetItem* addWrappedListEntry(
        QListWidget* list,
        const QString& html,
        const QString& plainText,
        int line = 1,
        int col = 1,
        double second = -1.0,
        bool enabled = true
    );
    void relayoutWrappedListRows(QListWidget* list);
    void scheduleWrappedListRelayout(QListWidget* list);
    void refreshMuriDiagnosticsPanel();
    QString currentValidationIgnoreScopeKey() const;
    bool isIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey) const;
    void setIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey, bool ignored);
    void showIssueListContextMenu(QListWidget* list, const QPoint& pos, bool muriList);
    void rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers);
    void clearValidationCache();
    void refreshValidationPanelForActiveField();
    void applyDeferredAnalysisUiUpdates();
    void setValidationTabVisible(bool visible);
    void applyMuriRenderOptions();
    void setMuriRenderMode(RenderMode mode, bool persistState = true);
    struct ValidationCachedIssue {
        int line = 1;
        int col = 1;
        int endCol = 1;
        QString rawMessage;
        QString displayMessage;
        QString issueTypeKey;
        QString issueTypeLabel;
    };

    struct ValidationDecoration {
        int line = 1;
        int col = 1;
        int endCol = 1;
        QString message;
        bool warning = false;
    };

    struct ValidationCacheEntry {
        QString chartText;
        bool chineseUi = false;
        miacode::simai::SimaiTimingMetadata timingMetadata;
        bool ok = true;
        int errorCount = 0;
        int warningCount = 0;
        int lenientNoteCount = 0;
        int lenientErrorCount = 0;
        int strictNoteCount = 0;
        int strictErrorCount = 0;
        QVector<ValidationCachedIssue> issues;
    };

    struct DeletedDifficultyUndoState {
        bool valid = false;
        bool wasActive = false;
        int difficultyId = 0;
        SimaiDifficultyData difficultyData;
    };

    QWidget* editorWidget_ = nullptr;
    PreviewRuntime* previewCanvas_ = nullptr;
    PreviewMediaController* previewMediaController_ = nullptr;
    QThread* previewMediaControllerThread_ = nullptr;
    QtPreviewSfxRuntime* previewSfxRuntime_ = nullptr;
    QThreadPool* previewWarmupPool_ = nullptr;
    QThreadPool* timelineSlowRefreshPool_ = nullptr;
    QThreadPool* timelineAnalysisPool_ = nullptr;
    TimelineView* timelineView_ = nullptr;
    QPlainTextEdit* outputView_ = nullptr;
    QListWidget* errorList_ = nullptr;
    QListWidget* muriList_ = nullptr;
    QAction* validateAction_ = nullptr;
    QAction* newAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* settingsPlaceholderAction_ = nullptr;
    QAction* preferencesAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* transformMirrorLeftRightAction_ = nullptr;
    QAction* transformMirrorUpDownAction_ = nullptr;
    QAction* transformRotate180Action_ = nullptr;
    QAction* transformRotate45CounterClockwiseAction_ = nullptr;
    QAction* transformRotate45ClockwiseAction_ = nullptr;
    QAction* normalizeWholeChartAction_ = nullptr;
    QAction* transformToggleBreakAction_ = nullptr;
    QAction* transformToggleExAction_ = nullptr;
    QAction* transformToggleFireworkAction_ = nullptr;
    QAction* transformRandomRotateAction_ = nullptr;
    QAction* findReplaceAction_ = nullptr;
    QAction* stopPreviewAction_ = nullptr;
    QAction* pausePreviewAction_ = nullptr;
    QAction* exportVideoAction_ = nullptr;
    QAction* latencyDetectorAction_ = nullptr;
    QAction* toggleJudgeMarkersAction_ = nullptr;
    QAction* toggleTouchTrailAction_ = nullptr;
    QAction* renderModeNativeAction_ = nullptr;
    QAction* renderModeMaimuriDxAction_ = nullptr;
    QAction* editStaticTapOnSlideThresholdAction_ = nullptr;
    QAction* previewAudioSettingsAction_ = nullptr;
    QAction* previewVideoSettingsAction_ = nullptr;
    QAction* swapWorkspaceSidesAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    QLabel* currentFileLabel_ = nullptr;
    QTimer* qtPreviewTimer_ = nullptr;
    QTimer* qtPreviewTimelineTimer_ = nullptr;
    QTimer* previewSeekDebounceTimer_ = nullptr;
    QTimer* timelineAnalysisIdleTimer_ = nullptr;
    QTimer* exportVideoHoverMenuTimer_ = nullptr;
    QTimer* embeddedPreviewRefreshTimer_ = nullptr;
    QTimer* embeddedPreviewResizeSettleTimer_ = nullptr;
    QObject* editorViewport_ = nullptr;
    QProcess* videoExportWorkerProcess_ = nullptr;
    QProgressDialog* videoExportProgressDialog_ = nullptr;
    QPointer<QLabel> aboutIconLabel_;
    QSoundEffect* invalidStarPreviewEnableSound_ = nullptr;
    QSoundEffect* invalidStarPreviewDisableSound_ = nullptr;
    QByteArray videoExportWorkerStdoutBuffer_;
    QByteArray videoExportWorkerStderrBuffer_;
    QString videoExportWorkerJobId_;
    QString videoExportWorkerOutputPath_;
    QString videoExportWorkerResultMessage_;
    QString videoExportWorkerResultDetails_;
    QElapsedTimer videoExportWorkerElapsed_;
    QString lastSessionFilePath_;
    QString currentFilePath_;
    QString lastOpenDir_;
    QString lastTrackPath_;
    int lastTimelineParseDifficultyId_ = 0;
    QString lastTimelineParseChartText_;
    miacode::simai::SimaiTimingMetadata lastTimelineParseTimingMetadata_;
    SimaiNativeParseResult lastTimelineParseResult_;
    QVector<TimelineNoteMarker> latestTimelineNoteMarkers_;
    QByteArray latestTimelineNoteMarkerSignature_;
    quint64 latestTimelinePreviewRevision_ = 0;
    bool latestTimelinePreviewSnapshotReady_ = false;
    TimelineQuickModel timelineQuickModel_;
    TimelineSlowRefreshRequest pendingTimelineSlowRefresh_;
    TimelineAnalysisRefreshRequest pendingTimelineAnalysisRefresh_;
    quint64 timelineRevision_ = 0;
    quint64 timelineSlowRequestedRevision_ = 0;
    quint64 timelineSlowRunningRevision_ = 0;
    quint64 timelineAnalysisRequestedRevision_ = 0;
    quint64 timelineAnalysisRunningRevision_ = 0;
    bool timelineSlowWorkerRunning_ = false;
    bool timelineAnalysisWorkerRunning_ = false;
    QByteArray lastPreviewNoteMarkerSignature_;
    QByteArray muriAnalysisReportNoteMarkerSignature_;
    bool outlineDockCollapsed_ = false;
    int outlineDockExpandedWidth_ = 190;
    TextEncoding currentEncoding_ = TextEncoding::Utf8;
    bool runtimeDebugOutputEnabled_ = false;
    quint64 windowEventDebugSequence_ = 0;
    bool previewSfxRuntimePrepared_ = false;
    quint64 previewWarmupGeneration_ = 0;
    quint64 previewMediaWarmupAppliedGeneration_ = 0;
    quint64 previewSfxWarmupAppliedGeneration_ = 0;
    quint64 startupRestoreGeneration_ = 0;
    bool startupRestorePending_ = false;
    quint64 waveformRefreshGeneration_ = 0;
    bool pendingDeferredValidationUiRefresh_ = false;
    bool pendingDeferredMuriUiRefresh_ = false;
    QString previewMediaWarmupChartPath_;
    QString previewMediaWarmupResolvedPath_;
    QString previewMediaWarmupTrackPath_;
    QString previewSfxWarmupChartPath_;
    QString previewSfxWarmupTrackPath_;
    QString previewSfxWarmupSfxDir_;
    bool videoExportWorkerSuccess_ = false;
    bool videoExportWorkerCompletionReceived_ = false;
    bool videoExportWorkerCancelRequested_ = false;
    bool restoreSquareAfterVideoExport_ = false;
    int videoExportWorkerLastProgressPercent_ = 0;
    qint64 videoExportWorkerLastEtaSeconds_ = -1;
    int projectLastOpenedDifficultyId_ = 0;
    bool documentDirty_ = false;
    bool currentFieldDirty_ = false;
    bool qtPreviewPlaying_ = false;
    bool qtPreviewTimelineDirty_ = false;
    bool qtPreviewAwaitingFrameSwap_ = false;
    qint64 qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    bool previewSliderDragging_ = false;
    bool embeddedPreviewRefreshPending_ = false;
    bool embeddedPreviewResizeActive_ = false;
    bool embeddedPreviewRefreshSuspended_ = false;
    bool suppressTimelineCursorSync_ = false;
    bool suppressTextDirtyTracking_ = false;
    bool autoRestoreLastSessionFile_ = true;
    bool editorCtrlLeftJumpPending_ = false;
    bool editorCtrlLeftJumpDragged_ = false;
    bool invalidStarPreviewEasterEggEnabled_ = false;
    int invalidStarPreviewAboutClickCount_ = 0;
    QElapsedTimer invalidStarPreviewAboutClickElapsed_;
    QPoint editorCtrlLeftJumpPressPos_;
    int previewHeldSeekDirection_ = 0;
    int previewSeekHeldArrowKey_ = 0;
    int previewSeekHeldArrowLastElapsedMs_ = 0;
    QElapsedTimer previewSeekHeldArrowElapsed_;
    QElapsedTimer previewScrubRenderElapsed_;
    double qtPreviewStartSecond_ = 0.0;
    double qtPreviewPauseSecond_ = 0.0;
    double qtPreviewPlaybackReturnSecond_ = 0.0;
    double qtPreviewPlaybackEndSecond_ = 0.0;
    double qtPreviewLastTimelineSecond_ = -1.0;
    double qtPreviewPendingTimelineSecond_ = 0.0;
    bool qtPreviewPendingTimelineCenterView_ = true;
    bool pendingPreviewPlaybackStart_ = false;
    bool pendingPreviewPlaybackResumeFromPause_ = false;
    quint64 pendingPreviewPlaybackRevision_ = 0;
    int pendingPreviewPlaybackDifficultyId_ = 0;
    double pendingPreviewPlaybackSecond_ = 0.0;
    QElapsedTimer qtPreviewElapsed_;
    QElapsedTimer qtPreviewWatchdogElapsed_;
    qint64 qtPreviewNextFixedTickDueNs_ = -1;
    double qtPreviewTimelineStartSecond_ = 0.0;
    QElapsedTimer qtPreviewTimelineElapsed_;
    double previewPendingSeekSecond_ = 0.0;
    bool previewPendingSeekCenterView_ = true;
    bool pausedPreviewMediaSeekPending_ = false;
    double pausedPreviewMediaSeekSecond_ = 0.0;
    double previewPlaybackRate_ = 1.0;
    double previewTrackDurationSeconds_ = 0.0;
    WaveformCacheEntry waveformCacheEntry_;
    bool showSlideTracks_ = true;
    bool showJudgeMarkers_ = false;
    bool showTouchTrail_ = false;
    MuriAnalysisReport muriAnalysisReport_;
    QVector<MuriStaticReference> muriStaticReferences_;
    MuriRenderOptions muriRenderOptions_;
    int staticTapOnSlideThresholdMs_ = 200;
    double previewBackgroundBrightnessOuter_ = miacode::preview_video::kBackgroundBrightnessDefault;
    double previewBackgroundBrightnessInner_ = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double previewLayoutSquareScale_ = miacode::preview_video::kLayoutSquareScaleDefault;
    bool previewSmoothBrightness_ = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewBackgroundScaleMode previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    double previewNoteFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    PreviewSkinVariant previewSkinVariant_ = PreviewSkinVariant::Standard;
    PreviewCanvasFrameRateMode previewCanvasFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    double previewCanvasAspectRatio_ = 1.0;
    bool previewAutoRestoreSquareAfterExport_ = false;
    bool previewShowDebugInfo_ = false;
    bool previewShowTimestamp_ = true;
    bool previewShowObjectStatsHud_ = false;
    bool exportShowObjectStatsHud_ = false;
    bool previewShowValidationSummary_ = true;
    bool workspacePanelsSwapped_ = false;
    PreviewAudioSettings softwarePreviewAudioSettings_;
    PreviewAudioSettings previewAudioSettings_;
    int editorTextFontPointSize_ = 0;
    double editorLineSpacingFactor_ = 1.5;
    SimaiDocument document_;
    QWidget* welcomePage_ = nullptr;
    QLabel* welcomeEmptyHintLabel_ = nullptr;
    QWidget* metadataPage_ = nullptr;
    QWidget* metadataCard_ = nullptr;
    QLabel* metadataEmptyHintLabel_ = nullptr;
    QWidget* chartPage_ = nullptr;
    QStackedWidget* editorStack_ = nullptr;
    QDockWidget* outlineDock_ = nullptr;
    QListWidget* outlineList_ = nullptr;
    QToolButton* outlineCollapseButton_ = nullptr;
    QLineEdit* titleEdit_ = nullptr;
    QLineEdit* artistEdit_ = nullptr;
    QLineEdit* firstEdit_ = nullptr;
    QLineEdit* designerEdit_ = nullptr;
    QTextEdit* metadataExtraEdit_ = nullptr;
    QWidget* editorDifficultyControls_ = nullptr;
    QLabel* difficultyLevelLabel_ = nullptr;
    QLineEdit* difficultyLevelEdit_ = nullptr;
    QLabel* difficultyDesignerLabel_ = nullptr;
    QLineEdit* difficultyDesignerEdit_ = nullptr;
    QWidget* editorHeaderWidget_ = nullptr;
    QWidget* editorBatchTransformControls_ = nullptr;
    QToolButton* transformMirrorLeftRightButton_ = nullptr;
    QToolButton* transformMirrorUpDownButton_ = nullptr;
    QToolButton* transformRotate180Button_ = nullptr;
    QToolButton* transformRotate45CounterClockwiseButton_ = nullptr;
    QToolButton* transformRotate45ClockwiseButton_ = nullptr;
    QLabel* editorContextLabel_ = nullptr;
    QWidget* editorValidationSummaryWidget_ = nullptr;
    QLabel* editorValidationErrorIconLabel_ = nullptr;
    QLabel* editorValidationErrorCountLabel_ = nullptr;
    QLabel* editorValidationWarningIconLabel_ = nullptr;
    QLabel* editorValidationWarningCountLabel_ = nullptr;
    QLabel* editorValidationMuriIconLabel_ = nullptr;
    QLabel* editorValidationMuriCountLabel_ = nullptr;
    QLabel* editorCursorLabel_ = nullptr;
    QWidget* editorFindBar_ = nullptr;
    QLineEdit* editorFindEdit_ = nullptr;
    QLineEdit* editorReplaceEdit_ = nullptr;
    QToolButton* editorFindPrevButton_ = nullptr;
    QToolButton* editorFindNextButton_ = nullptr;
    QToolButton* editorFindCloseButton_ = nullptr;
    QPushButton* editorReplaceButton_ = nullptr;
    QPushButton* editorReplaceAllButton_ = nullptr;
    QLabel* editorEmptyStateLabel_ = nullptr;
    QTabWidget* bottomTabs_ = nullptr;
    QToolButton* deleteDifficultyButton_ = nullptr;
    QSplitter* workspaceSplitter_ = nullptr;
    QWidget* previewPanel_ = nullptr;
    QWidget* previewLeftColumn_ = nullptr;
    QWidget* previewCanvasContainer_ = nullptr;
    QFrame* previewCanvasFrame_ = nullptr;
    QFrame* previewControlCard_ = nullptr;
    QHBoxLayout* previewControlsLayout_ = nullptr;
    QToolButton* stopPreviewButton_ = nullptr;
    QToolButton* pausePreviewButton_ = nullptr;
    QToolButton* syntaxCheckButton_ = nullptr;
    QToolButton* exportVideoButton_ = nullptr;
    QMenu* exportVideoMenu_ = nullptr;
    QMenu* toolboxMenu_ = nullptr;
    QToolButton* previewAudioSettingsButton_ = nullptr;
    QToolButton* previewVideoSettingsButton_ = nullptr;
    QToolButton* latencyDetectorButton_ = nullptr;
    QSlider* previewSlider_ = nullptr;
    QToolButton* previewSpeedButton_ = nullptr;
    QToolButton* previewFullscreenButton_ = nullptr;
    QTimer* previewHeldSeekTimer_ = nullptr;
    QLabel* previewTapStatsLabel_ = nullptr;
    QLabel* previewHoldStatsLabel_ = nullptr;
    QLabel* previewSlideStatsLabel_ = nullptr;
    QLabel* previewTouchStatsLabel_ = nullptr;
    QLabel* previewBreakStatsLabel_ = nullptr;
    QLabel* previewTotalStatsLabel_ = nullptr;
    QWidget* previewStatsCard_ = nullptr;
    QGridLayout* previewStatsGridLayout_ = nullptr;
    QVector<QLabel*> previewStatsChips_;
    int previewStatsLayoutRows_ = 0;
    int previewStatsLayoutCols_ = 0;
    QWidget* previewFullscreenWindow_ = nullptr;
    QWidget* previewFullscreenHost_ = nullptr;
    QWidget* previewFullscreenControlsWindow_ = nullptr;
    QWidget* previewFullscreenHintWindow_ = nullptr;
    QLabel* previewFullscreenHintLabel_ = nullptr;
    QTimer* previewFullscreenHintTimer_ = nullptr;
    QTimer* previewFullscreenControlsTimer_ = nullptr;
    QTimer* previewFullscreenCursorPollTimer_ = nullptr;
    QPropertyAnimation* previewFullscreenControlsAnimation_ = nullptr;
    QPropertyAnimation* previewFullscreenControlsOpacityAnimation_ = nullptr;
    bool previewFullscreenActive_ = false;
    bool previewFullscreenControlsVisible_ = false;
    bool previewFullscreenCursorTrackingInitialized_ = false;
    QPoint previewFullscreenLastCursorPos_;
    bool previewLayoutInitialized_ = false;
    int workspaceCachedLeftWidth_ = 0;
    int workspaceCachedRightWidth_ = 0;
    QVector<TimelineNoteMarker> previewStatsNoteMarkers_;
    QPointer<LatencyDetectorDialog> latencyDetectorDialog_;
    BracketScopeHighlighter* chartBracketHighlighter_ = nullptr;
    BracketScopeHighlighter* metadataBracketHighlighter_ = nullptr;
    QHash<int, ValidationCacheEntry> validationCacheByDifficulty_;
    QHash<QString, QSet<QString>> ignoredHeaderIssueTypesByFile_;
    QVector<ValidationDecoration> validationDecorations_;
    DeletedDifficultyUndoState deletedDifficultyUndoState_;
    bool previewFollowDecorationActive_ = false;
    int previewFollowDecorationLine_ = 1;
    int previewFollowDecorationCol_ = 1;
    QString activeOutlineKey_ = "metadata";
    int activeDifficultyId_ = 0;
};
