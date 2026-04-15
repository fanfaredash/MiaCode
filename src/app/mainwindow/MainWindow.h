#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <QChronoTimer>
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
class QChronoTimer;
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

namespace miacode::waveform {
class WaveformCacheService;
struct WaveformData;
}

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
    void beginShellPreviewScrub() override;
    void updateShellPreviewScrub(double second, bool centerView) override;
    void endShellPreviewScrub(double second, bool centerView) override;
    void setShellPreviewRate(double rate) override;
    bool stepShellPreviewBySeconds(double deltaSeconds, bool centerView) override;
    void beginShellPreviewHeldSeek(int direction, int key) override;
    void stopShellPreviewHeldSeek(int key = 0) override;
    void setShellPreviewFullscreen(bool fullscreen) override;
    bool shellHasShortcut(const QKeySequence& sequence) const override;
    bool shellTriggerShortcut(const QKeySequence& sequence) override;
    QString shellWindowTitle() const override;
    bool shellWorkspacePanelsSwapped() const override;
    QString shellPreviewSpeedLabel() const override;
    bool shellPreviewPlaying() const override;
    double shellPreviewPositionSeconds() const override;
    double shellPreviewDurationSeconds() const override;
    QStringList shellPreviewStatsTexts() const override;
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

    #include "MainWindowPrivateMethodsA.inc"
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
    PreviewOutlineVariant effectivePreviewOutlineVariant() const;
    void applyEffectivePreviewOutlineVariantToCanvas();
    void applyPreviewOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection, bool persistState);
    PreviewSkinVariant previewSkinVariantFromStorageValue(const QString& value) const;
    QString previewSkinVariantStorageValue() const;
    double currentPreviewCanvasRefreshRate() const;
    void refreshPreviewFrameRateTimers();
    double timelineSecondForCursor(int line, int col) const;
    void jumpToLocation(int line, int col);
    QString transformChartText(const QString& input, ChartTransformOp op, int* changedCount = nullptr) const;
    QString editorText() const;
    QString resolveDefaultTrackPath() const;
    QString resolvePreviewSkinDir() const;
    QString resolveProjectRenderStateFilePath() const;
    QString resolveInitialOpenDirectory() const;
    void resetPortablePreviewSettingsToDefaults();
    void applyPortablePreviewSettings(const QJsonObject& preview);
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
    bool runValidateSimaiSilently(bool focusFirstIssue = false);
    bool preparePreviewStartState();
    void refreshEditorExtraSelections();
    void setPreviewFollowDecoration(int line, int col, int endCol = -1, bool ensureVisible = false);
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

    struct SelectionTransformUndoEntry {
        int undoStepAfterApply = 0;
        int originalAnchor = -1;
        int originalPosition = -1;
        int transformedAnchor = -1;
        int transformedPosition = -1;
    };

    class EditorSection;
    class PreferencesSection;
    class PreviewSection;
    class WindowSection;
    class DialogsSection;
    class ExportSection;
    class ValidationSection;
    class DocumentSection;
    class FrameSection;
    class TimelineSection;
    std::unique_ptr<EditorSection> editorSection_;
    std::unique_ptr<PreferencesSection> preferencesSection_;
    std::unique_ptr<PreviewSection> previewSection_;
    std::unique_ptr<WindowSection> windowSection_;
    std::unique_ptr<DialogsSection> dialogsSection_;
    std::unique_ptr<ExportSection> exportSection_;
    std::unique_ptr<ValidationSection> validationSection_;
    std::unique_ptr<DocumentSection> documentSection_;
    std::unique_ptr<FrameSection> frameSection_;
    std::unique_ptr<TimelineSection> timelineSection_;

    #include "MainWindowMemberStorage.inc"
};
