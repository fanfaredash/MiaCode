#pragma once

#include <functional>
#include <utility>

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QPoint>
#include <QVector>

#include "PreviewAudioSettings.h"
#include "PreviewRenderSettings.h"
#include "SimaiDocument.h"
#include "TimelineView.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"

class QAction;
class QByteArray;
class QCloseEvent;
class QEvent;
class QFrame;
class QGridLayout;
class QHideEvent;
class QLabel;
class LatencyDetectorDialog;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QMenu;
class QMoveEvent;
class QTabWidget;
class QToolBar;
class QPushButton;
class QShowEvent;
class BracketScopeHighlighter;
class PlainCodeEditor;
class PreviewCanvas;
class PreviewMediaController;
class QPlainTextEdit;
class QProcess;
class QResizeEvent;
class QStackedWidget;
class QSlider;
class QSplitter;
class QTimer;
class QTextEdit;
class QToolButton;
class QWidget;
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
        int skinLoadWaitMs = 2000;
    };

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;
    bool exportPreviewVideoFromCli(
        const CliVideoExportRequest& request,
        QString* resolvedOutputPath,
        QString* errorMessage,
        QString* details = nullptr
    );

protected:
    void closeEvent(QCloseEvent* event) override;
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
    void onToggleBreakSelection();
    void onToggleExSelection();
    void onToggleFireworkSelection();
    void onRandomRotateSelection();
    void onStopPreview();
    void onTogglePreviewPause();
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    void onExportPreviewVideo();
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
    void onPreviewProcessFinished(int exitCode);

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
    enum class PreviewFollowMode {
        EveryComma,
        NonEmptyComma,
        LineOnly,
    };
    enum class TextEncoding {
        Utf8,
        System,
    };
    struct TimelineCursorNote;

    bool maybeSaveBeforeContinue();
    void configureRuntimeDebugOutput();
    void ensurePreviewMediaControllerInitialized();
    void ensurePreviewSfxRuntimePrepared();
    void schedulePreviewSubsystemWarmup();
    void tryFinalizePreviewSubsystemWarmup();
    void setupInitialWindowGeometry();
    void setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu);
    void updateLatencyDetectorAvailability();
    QString resolveLatencyDetectorTrackPath() const;
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument();
    bool saveToPath(const QString& path);
    bool applyBatchTransform(const QString& opName, const BatchTransform& transform);
    bool applySelectionBatchTransform(const QString& opName, const BatchTransform& transform);
    bool ensurePreviewSessionStarted();
    void stopPreviewSession();
    bool sendPreviewCommand(const QString& mode, int cursorLine, int cursorCol, const QString& trackPath);
    bool sendPreviewPrepareCommand();
    bool sendPreviewConfigCommand(const QString& audition = QString());
    void startPreviewProcess(const QString& mode, int cursorLine, int cursorCol);
    bool handlePreviewSessionLine(const QString& line);
    void bootstrapPreviewWindow();
    void arrangeWithPreviewWindow();
    void schedulePreviewArrange(int delayMs);
    std::pair<int, int> currentCursorLineCol() const;
    std::pair<int, int> currentSelectionOrCursorLineCol() const;
    bool currentSelectionRange(int* startPos, int* endPos) const;
    void setEditorText(const QString& text);
    void setMetadataExtraText(const QString& text);
    bool openFileAtPath(const QString& path, bool showStatusMessage = true, bool showErrors = true);
    bool restoreLastSessionFile();
    void setCurrentFilePath(const QString& path);
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
    void rebuildFieldSidebar();
    void updateEditorHeader();
    void updateEditorHeaderLayoutMode();
    void updateEditorStatus();
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
    double parsedFirstSeconds(bool* ok = nullptr) const;
    double parsedWholeBpm(bool* ok = nullptr) const;
    QString parsedLatencyMeterId() const;
    void applyLatencyDetectorOffset(double seconds);
    void applyLatencyDetectorBpm(double bpm);
    void applyLatencyDetectorMeter(const QString& meterId);
    void refreshWaveformCache();
    void scheduleTimelineRefresh();
    void refreshTimelineMetadata();
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
    void startQtPreviewPlayback(double second, bool resumeFromPause = false);
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
    void updatePreviewSliderRange();
    void updatePreviewSliderPosition(double second);
    QString formatPreviewTimestamp(double second) const;
    void showPreviewSliderTimeHint(int sliderValue);
    void refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers);
    void updatePreviewObjectStats(double second);
    void clearPreviewObjectStats();
    int updatePreviewStatsLayoutMode(int hostWidth = -1);
    void updatePreviewWorkspaceLayout();
    void updatePreviewPanelLayout();
    void cacheWorkspaceLayoutSizes();
    void restoreWorkspaceLayoutSizes();
    void refreshLayoutAfterPageSwitch();
    void openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title);
    double previewDurationSeconds() const;
    void applyPreviewPlaybackRate(double rate);
    void setPreviewCanvasAspectRatio(double ratio, bool persistState);
    double normalizedPreviewCanvasAspectRatio(double ratio) const;
    void setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    PreviewCanvasFrameRateMode previewCanvasFrameRateModeFromStorageValue(const QString& value) const;
    QString previewCanvasFrameRateModeStorageValue() const;
    void setPreviewFollowMode(PreviewFollowMode mode, bool persistState);
    PreviewFollowMode previewFollowModeFromStorageValue(const QString& value) const;
    QString previewFollowModeStorageValue() const;
    double currentPreviewCanvasRefreshRate() const;
    void refreshPreviewFrameRateTimers();
    int computeBottomTabsDeviceHeight() const;
    void updateBottomTabsDeviceHeight();
    bool findTimelineCursorNoteForTextPosition(int line, int col, int* indexOut) const;
    bool resolveTimelineNoteFromCursorAnchor(
        double second,
        int anchorLine,
        int anchorCol,
        int lane,
        int* line,
        int* col,
        double* noteSecond
    ) const;
    double timelineSecondForCursor(int line, int col) const;
    void jumpToLocation(int line, int col);
    QString transformChartText(const QString& input, ChartTransformOp op, int* changedCount = nullptr) const;
    QString editorText() const;
    QString resolvePreviewSessionScriptPath() const;
    QString resolveDefaultTrackPath() const;
    QString resolvePreviewSkinDir() const;
    QString resolveProjectRenderStateFilePath() const;
    QString resolveInitialOpenDirectory() const;
    void loadPortableState();
    void savePortableState() const;
    void applyEditorTextFontSize(int pointSize, bool persistPreference);
    void applyEditorLineSpacingFactor(double factor, bool persistPreference);
    void applyUiTheme();
    void applySystemWindowBackdrop(QWidget* target = nullptr) const;
    void persistEditorTextFontPreference() const;
    void loadProjectRenderState();
    void saveProjectRenderState() const;
    void removeProjectRenderState() const;
    void applyPreviewAudioSettingsToRuntime();
    void setLastOpenDirectory(const QString& pathOrDir);
    bool runValidateSimai();
    bool runValidateSimaiSilently(bool focusFirstIssue = false);
    void scheduleAutoValidation();
    bool saveBeforePreviewStart();
    void appendOutput(const QString& title, const QString& payload);
    void logWindowGeometryDebug(const QString& tag, const QString& detail = QString());
    QString formatWindowStateFlags(Qt::WindowStates states) const;
    void logTopLevelWindowSnapshot(const QString& tag);
    void logNativeWindowDebug(const QString& tag, WId dialogWId = 0);
    void refreshEditorExtraSelections();
    void setPreviewFollowDecoration(int line, int col);
    void clearPreviewFollowDecoration();
    void clearValidationErrors();
    void clearValidationDecorations();
    void addValidationError(int line, int col, const QString& message);
    void addValidationDecoration(int line, int col, const QString& message, int endCol = -1);
    void clearValidationCache();
    void refreshValidationPanelForActiveField();
    void setValidationTabVisible(bool visible);
    bool findCursorNoteForTextPosition(
        const QVector<TimelineCursorNote>& notes,
        int line,
        int col,
        int* indexOut
    ) const;
    bool resolveNearestCursorNote(
        const QVector<TimelineCursorNote>& notes,
        double second,
        int lane,
        int* line,
        int* col,
        double* noteSecond
    ) const;
    bool resolveCursorNoteFromAnchor(
        const QVector<TimelineCursorNote>& notes,
        double second,
        int anchorLine,
        int anchorCol,
        int lane,
        int* line,
        int* col,
        double* noteSecond
    ) const;

    struct TimelineCursorNote {
        int line = 1;
        int col = 1;
        int lane = -1;
        double second = 0.0;
    };

    struct ValidationCachedIssue {
        int line = 1;
        int col = 1;
        int endCol = 1;
        QString displayMessage;
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
        bool ok = true;
        int errorCount = 0;
        int warningCount = 0;
        int lenientNoteCount = 0;
        int lenientErrorCount = 0;
        int strictNoteCount = 0;
        int strictErrorCount = 0;
        QVector<ValidationCachedIssue> issues;
    };

    QWidget* editorWidget_ = nullptr;
    PreviewCanvas* previewCanvas_ = nullptr;
    PreviewMediaController* previewMediaController_ = nullptr;
    QtPreviewSfxRuntime* previewSfxRuntime_ = nullptr;
    TimelineView* timelineView_ = nullptr;
    QPlainTextEdit* outputView_ = nullptr;
    QListWidget* errorList_ = nullptr;
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
    QAction* previewAudioSettingsAction_ = nullptr;
    QAction* previewVideoSettingsAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    QLabel* currentFileLabel_ = nullptr;
    QTimer* metadataRefreshTimer_ = nullptr;
    QTimer* validationRefreshTimer_ = nullptr;
    QTimer* qtPreviewTimer_ = nullptr;
    QTimer* qtPreviewTimelineTimer_ = nullptr;
    QTimer* previewSeekDebounceTimer_ = nullptr;
    QObject* editorViewport_ = nullptr;
    QProcess* previewProcess_ = nullptr;
    QString previewStdoutBuffer_;
    QString previewStderrBuffer_;
    QString lastSessionFilePath_;
    QString currentFilePath_;
    QString lastOpenDir_;
    QString lastTrackPath_;
    QVector<TimelineCursorNote> timelineCursorNotes_;
    QVector<TimelineCursorNote> previewFollowCursorNotes_;
    QByteArray lastPreviewNoteMarkerSignature_;
    TextEncoding currentEncoding_ = TextEncoding::Utf8;
    int previewArrangeRetryCount_ = 0;
    quint64 previewArrangeGeneration_ = 0;
    bool legacyPygamePreviewEnabled_ = false;
    bool runtimeDebugOutputEnabled_ = false;
    quint64 windowEventDebugSequence_ = 0;
    bool previewSfxRuntimePrepared_ = false;
    bool previewSubsystemWarmupScheduled_ = false;
    bool previewSubsystemWarmupFinalized_ = false;
    int previewSubsystemWarmupPendingTasks_ = 0;
    int projectLastOpenedDifficultyId_ = 0;
    bool documentDirty_ = false;
    bool currentFieldDirty_ = false;
    bool qtPreviewPlaying_ = false;
    bool qtPreviewPendingAudioCalibration_ = false;
    bool qtPreviewTimelineDirty_ = false;
    bool qtPreviewAwaitingFrameSwap_ = false;
    qint64 qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    bool previewSliderDragging_ = false;
    bool suppressTimelineCursorSync_ = false;
    bool suppressTextDirtyTracking_ = false;
    bool autoRestoreLastSessionFile_ = true;
    bool editorCtrlLeftJumpPending_ = false;
    bool editorCtrlLeftJumpDragged_ = false;
    QPoint editorCtrlLeftJumpPressPos_;
    int previewSeekHeldArrowKey_ = 0;
    QElapsedTimer previewSeekHeldArrowElapsed_;
    QElapsedTimer previewScrubRenderElapsed_;
    double qtPreviewStartSecond_ = 0.0;
    double qtPreviewPauseSecond_ = 0.0;
    double qtPreviewPlaybackReturnSecond_ = 0.0;
    double qtPreviewLastTimelineSecond_ = -1.0;
    double qtPreviewPendingTimelineSecond_ = 0.0;
    bool qtPreviewPendingTimelineCenterView_ = true;
    QElapsedTimer qtPreviewElapsed_;
    QElapsedTimer qtPreviewWatchdogElapsed_;
    qint64 qtPreviewNextFixedTickDueNs_ = -1;
    double qtPreviewTimelineStartSecond_ = 0.0;
    bool qtPreviewTimelineCenterNextTick_ = true;
    QElapsedTimer qtPreviewTimelineElapsed_;
    double previewPendingSeekSecond_ = 0.0;
    bool previewPendingSeekCenterView_ = true;
    double previewPlaybackRate_ = 1.0;
    double previewTrackDurationSeconds_ = 0.0;
    bool showSlideTracks_ = true;
    bool showJudgeMarkers_ = false;
    bool showTouchTrail_ = false;
    double previewBackgroundBrightnessOuter_ = miacode::preview_video::kBackgroundBrightnessDefault;
    double previewBackgroundBrightnessInner_ = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double previewLayoutSquareScale_ = miacode::preview_video::kLayoutSquareScaleDefault;
    bool previewSmoothBrightness_ = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewBackgroundScaleMode previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    double previewNoteFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    PreviewCanvasFrameRateMode previewCanvasFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    PreviewFollowMode previewFollowMode_ = PreviewFollowMode::NonEmptyComma;
    double previewCanvasAspectRatio_ = 1.0;
    bool previewAutoRestoreSquareAfterExport_ = true;
    bool previewShowDebugInfo_ = false;
    bool previewShowTimestamp_ = true;
    bool previewShowObjectStatsHud_ = false;
    bool exportShowObjectStatsHud_ = false;
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
    QListWidget* outlineList_ = nullptr;
    QLineEdit* titleEdit_ = nullptr;
    QLineEdit* artistEdit_ = nullptr;
    QLineEdit* firstEdit_ = nullptr;
    QLineEdit* designerEdit_ = nullptr;
    QTextEdit* metadataExtraEdit_ = nullptr;
    QWidget* editorDifficultyControls_ = nullptr;
    QLineEdit* difficultyLevelEdit_ = nullptr;
    QLineEdit* difficultyDesignerEdit_ = nullptr;
    QWidget* editorHeaderWidget_ = nullptr;
    QWidget* editorBatchTransformControls_ = nullptr;
    QToolButton* transformMirrorLeftRightButton_ = nullptr;
    QToolButton* transformMirrorUpDownButton_ = nullptr;
    QToolButton* transformRotate180Button_ = nullptr;
    QToolButton* transformRotate45CounterClockwiseButton_ = nullptr;
    QToolButton* transformRotate45ClockwiseButton_ = nullptr;
    QLabel* editorContextLabel_ = nullptr;
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
    QToolButton* stopPreviewButton_ = nullptr;
    QToolButton* pausePreviewButton_ = nullptr;
    QToolButton* syntaxCheckButton_ = nullptr;
    QToolButton* exportVideoButton_ = nullptr;
    QToolButton* previewAudioSettingsButton_ = nullptr;
    QToolButton* previewVideoSettingsButton_ = nullptr;
    QToolButton* latencyDetectorButton_ = nullptr;
    QSlider* previewSlider_ = nullptr;
    QToolButton* previewSpeedButton_ = nullptr;
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
    bool previewLayoutInitialized_ = false;
    int workspaceCachedLeftWidth_ = 0;
    int workspaceCachedRightWidth_ = 0;
    QVector<TimelineNoteMarker> previewStatsNoteMarkers_;
    QPointer<LatencyDetectorDialog> latencyDetectorDialog_;
    BracketScopeHighlighter* chartBracketHighlighter_ = nullptr;
    BracketScopeHighlighter* metadataBracketHighlighter_ = nullptr;
    QHash<int, ValidationCacheEntry> validationCacheByDifficulty_;
    QVector<ValidationDecoration> validationDecorations_;
    bool previewFollowDecorationActive_ = false;
    int previewFollowDecorationLine_ = 1;
    int previewFollowDecorationCol_ = 1;
    QString activeOutlineKey_ = "metadata";
    int activeDifficultyId_ = 0;
};
