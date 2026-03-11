#pragma once

#include <functional>
#include <utility>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

#include "PreviewAudioSettings.h"
#include "SimaiDocument.h"
#include "TimelineView.h"

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
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

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
    void onStopPreview();
    void onPreviewFromStart();
    void onPreviewFromCursor();
    void onTogglePreviewPause();
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    void onPreviewAudioSettings();
    void onPreviewDisplaySettings();
    void onPreviewRenderSettings();
    void onOpenLatencyDetector();
    void onPreferences();
    void onAbout();
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
    enum class TextEncoding {
        Utf8,
        System,
    };

    bool maybeSaveBeforeContinue();
    void configureRuntimeDebugOutput();
    void ensurePreviewMediaControllerInitialized();
    void ensurePreviewSfxRuntimePrepared();
    void schedulePreviewSubsystemWarmup();
    void tryFinalizePreviewSubsystemWarmup();
    void setupInitialWindowGeometry();
    void setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* previewMenu, QMenu* helpMenu);
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
    void setCurrentFilePath(const QString& path);
    void updateWindowTitle();
    void updateCurrentFileLabel();
    void updatePauseButtonAppearance();
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
    void flushQtPreviewTimelinePosition();
    void onQtPreviewTick();
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
    double previewDurationSeconds() const;
    void applyPreviewPlaybackRate(double rate);
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
    void persistEditorTextFontPreference() const;
    void loadProjectRenderState();
    void saveProjectRenderState() const;
    void removeProjectRenderState() const;
    void applyPreviewAudioSettingsToRuntime();
    void setLastOpenDirectory(const QString& pathOrDir);
    bool runValidateSimai();
    bool saveBeforePreviewStart();
    void appendOutput(const QString& title, const QString& payload);
    void logWindowGeometryDebug(const QString& tag, const QString& detail = QString());
    QString formatWindowStateFlags(Qt::WindowStates states) const;
    void logTopLevelWindowSnapshot(const QString& tag);
    void logNativeWindowDebug(const QString& tag, WId dialogWId = 0);
    void clearValidationErrors();
    void clearValidationDecorations();
    void addValidationError(int line, int col, const QString& message);
    void addValidationDecoration(int line, int col, const QString& message);

    struct TimelineCursorNote {
        int line = 1;
        int col = 1;
        int lane = -1;
        double second = 0.0;
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
    QAction* stopPreviewAction_ = nullptr;
    QAction* previewFromStartAction_ = nullptr;
    QAction* previewFromCursorAction_ = nullptr;
    QAction* pausePreviewAction_ = nullptr;
    QAction* latencyDetectorAction_ = nullptr;
    QAction* toggleJudgeMarkersAction_ = nullptr;
    QAction* toggleTouchTrailAction_ = nullptr;
    QAction* previewRenderSettingsAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    QLabel* currentFileLabel_ = nullptr;
    QTimer* metadataRefreshTimer_ = nullptr;
    QTimer* qtPreviewTimer_ = nullptr;
    QTimer* qtPreviewTimelineTimer_ = nullptr;
    QTimer* previewSeekDebounceTimer_ = nullptr;
    QObject* editorViewport_ = nullptr;
    QProcess* previewProcess_ = nullptr;
    QString previewStdoutBuffer_;
    QString previewStderrBuffer_;
    QString currentFilePath_;
    QString lastOpenDir_;
    QString lastTrackPath_;
    QVector<TimelineCursorNote> timelineCursorNotes_;
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
    int previewSeekHeldArrowKey_ = 0;
    QElapsedTimer previewSeekHeldArrowElapsed_;
    double qtPreviewStartSecond_ = 0.0;
    double qtPreviewPauseSecond_ = 0.0;
    double qtPreviewLastTimelineSecond_ = -1.0;
    double qtPreviewPendingTimelineSecond_ = 0.0;
    bool qtPreviewPendingTimelineCenterView_ = true;
    QElapsedTimer qtPreviewElapsed_;
    QElapsedTimer qtPreviewWatchdogElapsed_;
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
    double previewBackgroundBrightness_ = 0.2;
    bool previewShowDebugInfo_ = false;
    PreviewAudioSettings softwarePreviewAudioSettings_;
    PreviewAudioSettings previewAudioSettings_;
    int editorTextFontPointSize_ = 0;
    double editorLineSpacingFactor_ = 1.5;
    SimaiDocument document_;
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
    QVector<TimelineNoteMarker> previewStatsNoteMarkers_;
    QPointer<LatencyDetectorDialog> latencyDetectorDialog_;
    BracketScopeHighlighter* chartBracketHighlighter_ = nullptr;
    BracketScopeHighlighter* metadataBracketHighlighter_ = nullptr;
    QString activeOutlineKey_ = "metadata";
    int activeDifficultyId_ = 0;
};
