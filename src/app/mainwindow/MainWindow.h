#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QVector>

#include "app/quick_shell/QuickShellContracts.h"
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
class QThreadPool;
class BracketScopeHighlighter;
class PlainCodeEditor;
class PreviewRuntime;
class PreviewStageMediaHost;
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
class QWindow;
class QtPreviewSfxRuntime;
class TimelineView;
class QuickShellPreviewCompositeSurface;

namespace miacode::preview::scene {
class PreviewProgressStatsCache;
}

class MainWindow : public QMainWindow,
                   public QuickShellCommandSink,
                   public QuickShellStateSource,
                   public QuickShellNativeContentProvider
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
        PreviewOutlineVariant outlineVariant = PreviewOutlineVariant::Line;
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
    bool quickShellRootWindowFrameGeometryAvailable() const;
    QRect quickShellRootWindowFrameGeometry() const;
    bool confirmShellClose() override;
    void toggleShellPreviewPlayback() override;
    void stopShellPreview() override;
    void seekShellPreview(double second) override;
    void setShellPreviewRate(double rate) override;
    void setShellPreviewFullscreen(bool fullscreen) override;
    bool shellHasShortcut(const QKeySequence& sequence) const override;
    bool shellTriggerShortcut(const QKeySequence& sequence) override;
    QString shellWindowTitle() const override;
    bool shellWorkspacePanelsSwapped() const override;
    QString shellPreviewSpeedLabel() const override;
    bool shellPreviewPlaying() const override;
    double shellPreviewPositionSeconds() const override;
    double shellPreviewDurationSeconds() const override;
    bool shellPreviewFullscreen() const override;
    QObject* shellPreviewRuntimeObject() const override;
    QObject* shellPreviewStageMediaHostObject() const override;
    bool shellPreviewUsesSeparateSurface() const override;
    QWindow* shellPreviewCompositeWindow() const override;
    QWidget* shellWindowWidget() const override;
    QDockWidget* shellOutlineDockWidget() const override;
    bool shellOutlineDockCollapsed() const override;
    int shellOutlineDockExpandedWidth() const override;
    QWidget* shellWorkspaceWidget() const override;
    QWidget* shellPreviewPanelWidget() const override;
    QString shellPreviewPanelStyleSheet() const override;
    QWidget* shellPreviewControlCardWidget() const override;
    QWidget* shellPreviewStatsCardWidget() const override;
    QLabel* shellPreviewTotalStatsLabel() const override;
    QGridLayout* shellPreviewStatsGridLayout() const override;
    int shellPreviewStatsMinimumPanelHeight(int panelWidth) const override;
    int shellUpdatePreviewStatsLayout(int hostWidth = -1) override;
    double shellNormalizedPreviewCanvasAspectRatio() const override;
    void shellRefreshLayoutAfterResize() override;
    void shellSetRootWindowFrameGeometry(const QRect& geometry) override;
    void shellNoteQuickUiReady() override;

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
    enum class PreviewStageMediaRoute {
        QuickShellStageHost,
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
    void setupInitialWindowGeometry();
    void setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu);
    void setOutlineDockCollapsed(bool collapsed);
    void updateOutlineDockCollapseButton();
    void updateLatencyDetectorAvailability();
    QString resolveLatencyDetectorTrackPath() const;
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
    QTextEdit* activeFindTarget() const;
    bool runFindInEditor(bool backward);
    void updateEditorFindBarGeometry();
    void applyFindOverlayInset();
    void hideFindReplaceBar();
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
    PreviewOutlineVariant previewOutlineVariantFromStorageValue(const QString& value) const;
    QString previewOutlineVariantStorageValue() const;
    PreviewOutlineVariant autoPreviewOutlineVariantForChart(const QString& chartPath) const;
    void applyPreviewOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection, bool persistState);
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
    QList<QAction*> quickShellShortcutActions() const;
    void appendOutput(const QString& title, const QString& payload);
    void logWindowGeometryDebug(const QString& tag, const QString& detail = QString());
    QString formatWindowStateFlags(Qt::WindowStates states) const;
    void logTopLevelWindowSnapshot(const QString& tag);
    void refreshEditorExtraSelections();
    void setPreviewFollowDecoration(int line, int col);
    void clearPreviewFollowDecoration();
    void clearValidationErrors();
    void clearValidationDecorations();
    void addValidationError(
        int line,
        int col,
        const QString& rawMessage,
        const QString& displayMessage,
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

    class EditorSection;
    class PreferencesSection;
    class PreviewSection;
    class ValidationSection;
    class DocumentSection;
    class FrameSection;
    class TimelineSection;
    std::unique_ptr<EditorSection> editorSection_;
    std::unique_ptr<PreferencesSection> preferencesSection_;
    std::unique_ptr<PreviewSection> previewSection_;
    std::unique_ptr<ValidationSection> validationSection_;
    std::unique_ptr<DocumentSection> documentSection_;
    std::unique_ptr<FrameSection> frameSection_;
    std::unique_ptr<TimelineSection> timelineSection_;

    #include "MainWindowMemberStorage.inc"
};
