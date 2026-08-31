#pragma once

#include <functional>

#include <QVariantList>
#include <memory>
#include <utility>

#include <QChronoTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <functional>
#include <QPointer>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QTextEdit>
#include <QVariantMap>
#include <QVector>

#include "PreviewAudioSettings.h"
#include "common/PreviewTimingSettings.h"
#include "PreviewRenderSettings.h"
#include "SimaiDocument.h"
#include "SimaiTimingMetadata.h"
#include "SimaiNativeParser.h"
#include "timeline/TimelineData.h"
#include "timeline/TimelineQuickModel.h"
#include "timeline/TimelineSlowRefresh.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "tools/video_export/VideoExportSnapshot.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "app/qml_ui/QmlDocumentProjection.h"
#include "app/qml_ui/QmlAnalysisProjection.h"
#include "app/v2/ApplicationServices.h"
#include "app/v2/EditorPageRouter.h"
#include "app/v2/EditorSyncController.h"
#include "app/v2/ChartDropImportService.h"
#include "core/chart/transform/ChartNormalization.h"

class QAction;
class QByteArray;
class QCloseEvent;
class QChronoTimer;
class QDialog;
class QDockWidget;
class QEvent;
class PreviewStageMediaHost;
class QmlEditorPageHost;
class QmlExportSession;
namespace miacode::v2 {
class UiRequestService;
class JobProgressService;
}
namespace miacode::qml_ui {
class QmlPreviewSettingsModel;
}
class QmlUiBootstrap;
class QFrame;
class QGraphicsOpacityEffect;
class QGridLayout;
class QHBoxLayout;
class QHideEvent;
class QLabel;
namespace miacode::latency {
class LatencySandboxController;
}
namespace miacode::video_export {
}
namespace miacode::ui {
class BusySpinner;
}
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
class PreviewRuntime;
class PreviewStageMediaHost;
struct IntroBannerSpec;
class QPlainTextEdit;
class QProcess;
class QPropertyAnimation;
class QResizeEvent;
class QShortcut;
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
class PreviewAudioDeviceWatcher;
class QtPreviewSfxRuntime;
class TimelineQuickStateBridge;
class QuickShellPreviewCompositeSurface;

namespace miacode::waveform {
class WaveformCacheService;
struct WaveformData;
}

namespace miacode::preview::scene {
class PreviewProgressStatsCache;
}

// The three QuickShell* abstract bases are gone with the polling controller
// that needed them: the QML sessions call these methods on MainWindow directly.
// Implements miacode::v2::EditorPageRouter so the QML page host can switch
// pages without naming this class. The router half is the part that survives
// stage 4; the hidden QStackedWidget the switches also drive is not.
class MainWindow : public QMainWindow, public miacode::v2::EditorPageRouter
{
    Q_OBJECT

    // The latency-detection sandbox controller drives the timeline +
    // SFX runtime directly during BPM/offset audition; granting it
    // friend access is cleaner than punching a dozen narrow accessors
    // through MainWindow's API surface for a single feature. The page
    // widget also needs friend access to call the private
    // applyLatencyDetectorBpm/Offset writers and to read the document
    // state for refresh.
    friend class miacode::latency::LatencySandboxController;
    // The Export hub page launches the existing ExportSection entry slots
    // with an explicitly selected difficulty and reads document/difficulty
    // state for its badge row — same narrow-feature rationale as the
    // latency page above.

public:
    // Phase 4c — non-owning accessor for the preview stage-media host
    // (QMediaPlayer + QVideoSink). The host is created lazily inside
    // MainWindow.PreviewStageMediaRoute on first chart-load.
    PreviewStageMediaHost* previewStageMediaHost() const;

    // Accessor for the latency-detection sandbox controller. The
    // The QML latency page binds its controls to the controller
    // (BPM/offset/subdivision/SFX-volume + audition lifecycle).
    // Always non-null after construction.
    miacode::latency::LatencySandboxController* latencySandboxController() const;

signals:
    void videoExportWorkerRunningChanged(bool running);
    void normalizeWholeChartRequested();
    void mediaToolsRequested();
    void preferencesRequested();
    // Routed by QmlEditorPageHost to the v2 cover page.
    void coverExportRequested(int difficultyId);
    void documentValidationChanged();
    void previewSkinDirectoryChanged();
    void editorPreferencesChanged();
    void muriPromptPreferenceChanged();
    // Emitted after the authoritative document replacement has installed its
    // final file identity and active difficulty.  UIv2 uses this to reset
    // every derived editor presentation (text, bookmarks and tabs) together.
    void documentReplaced();
    // The shell's presentation changed: which bottom tab is showing, which tabs
    // exist, whether the panel is up, the preview canvas aspect, or which
    // workspace page is active. Emitted where those change rather than sampled
    // — QuickShellController used to poll all of it on a timer.
    void shellPresentationChanged();
    // The preview playhead moved. Emitted from applyQtPreviewPosition — the one
    // function that moves it, on a tick or on a seek — so the shell's transport
    // is told rather than left to sample. It is separate from
    // shellPresentationChanged because it fires per frame during playback and
    // the bottom panel has no reason to wake for it.
    void shellPreviewPlayheadChanged();

public:
    enum class DocumentField {
        Title,
        Artist,
        First,
        Designer,
        VideoPath,
        ExtraText,
    };

    enum class DifficultyField {
        Level,
        Designer,
    };

    using DocumentValidationSnapshot = miacode::qml_ui::DocumentValidationProjection;
    using QmlAnalysisSnapshot = miacode::qml_ui::AnalysisProjection;

    // Result of the all-or-nothing QML metadata-source replacement.  The
    // candidate is parsed and strictly validated before the live document is
    // touched, so callers can keep their editor text and navigate a rejection.
    struct DocumentSourceReplaceResult {
        bool accepted = false;
        quint64 revision = 0;
        QVector<miacode::qml_ui::DocumentValidationProjectionIssue> issues;
    };

    enum class QmlDocumentCommitKind {
        Incremental,
        DifficultySelection,
        Structure,
        SourceReplacement,
        Open,
        SavePoint,
    };

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
        bool showChartInfoHud = false;
        bool addIntro = false;
        double previewMaxOutputSeconds = 0.0;  // 0 = full; >0 caps output (intro preview)
        miacode::preview_gameplay::CenterDisplayMode centerDisplayMode =
            miacode::preview_gameplay::kDefaultCenterDisplayMode;
        bool smoothBrightness = miacode::preview_video::kSmoothBrightnessDefault;
        double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
        double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
        double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;
        PreviewOutlineVariant outlineVariant = PreviewOutlineVariant::Line;
        PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
        double noteFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
        double touchFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
        PreviewTapJudgeTextDistance tapJudgeTextDistance = PreviewTapJudgeTextDistance::Inner;
        PreviewJudgeEffectStyle judgeEffectStyle = PreviewJudgeEffectStyle::Standard;
        int skinLoadWaitMs = 2000;
    };

    // The application services are constructed before the window and outlive
    // it: the document domain, the UI-request boundary and the job-progress
    // surface belong to miacode::v2::ApplicationServices, not to a QWidget.
    // MainWindow borrows them (stage 3.5 item 1) — it must not own or
    // re-create any of them.
    explicit MainWindow(miacode::v2::ApplicationServices& services,
                        QWidget* parent = nullptr);
    ~MainWindow() override;
    bool exportPreviewVideoFromCli(
        const CliVideoExportRequest& request,
        QString* resolvedOutputPath,
        QString* errorMessage,
        QString* details = nullptr
    );
    bool openStartupTarget(const QString& path);
    QString documentField(DocumentField field) const;
    QString difficultyField(int difficultyId, DifficultyField field) const;
    QVector<int> documentDifficultyIds() const;
    // Which difficulty this project was last opened on. Persisted per project
    // and read by the export page to pick its default; an accessor rather than
    // a private member reach so the export page's coupling is bounded.
    int projectLastOpenedDifficultyId() const;
    // The Muri overlay render options the export task is seeded from. Owned by
    // the window for now — the Muri render settings domain has not moved yet.
    const MuriRenderOptions& muriRenderOptions() const;
    QString documentSourceText() const;
    QString activeDocumentChartText() const;
    QString documentDifficultyChartText(int difficultyId) const;
    QString documentFilePath() const;
    int documentActiveDifficultyId() const;
    void publishEditorCaret(int difficultyId, int line, int column);
    void handleEditorPointerInteraction(int difficultyId);
    miacode::v2::EditorSyncController& editorSyncController();
    const miacode::v2::EditorSyncController& editorSyncController() const;
    bool requestEditorNavigation(int line, int column, int endLine, int endColumn,
                                 bool selectToken, bool focusEditor, bool centerView);
    bool editorAuthoringContextActive() const;
    void refreshEditorAuthoringContext();
    void setTouchPadAuthoringCtrlHold(bool active);
    bool applyTouchPadAuthoringPreviewAnchor(int difficultyId, int line, int column);
    bool seekPreviewToEditorLocation(int difficultyId, int line, int column);
    // v2 has no QAction/QMenu layer, and the v1 chart-transform actions carry
    // Qt::WindowShortcut context on a MainWindow that is hidden and therefore
    // never active — so none of them fire in v2. QML binds the same
    // ShortcutRegistry ids and dispatches through here, which keeps one command
    // table instead of nineteen new public methods. Returns false for an id
    // this window does not own.
    bool documentUnifiedDesignerEnabled() const;
    bool updateDocumentField(DocumentField field, const QString& value);
    bool updateDifficultyField(int difficultyId, DifficultyField field, const QString& value);
    bool updateActiveChartText(const QString& value);
    DocumentSourceReplaceResult replaceDocumentSourceText(const QString& value);
    bool saveDocument();
    bool saveDocumentAs(const QString& path);
    bool discardDocumentChanges();
    bool selectDocumentDifficulty(int difficultyId);
    bool addDocumentDifficulty(int difficultyId);
    bool removeDocumentDifficulty(int difficultyId);
    void enableUnifiedDocumentDesigner(const QString& canonicalName);
    void disableUnifiedDocumentDesigner();
    // Transitional one-way adapter: ChartWorkspace commits first, then the
    // hidden MainWindow consumes this immutable value for timeline/preview and
    // legacy-page compatibility. It never supplies UIv2 dirty/revision truth.
    bool applyCommittedQmlDocument(
        const QString& sourceText, const QString& filePath, int activeDifficultyId,
        bool dirty, quint64 revision, QmlDocumentCommitKind kind,
        bool usedSystemEncoding = false);
    // An audition scene — the export page's or the latency page's — is
    // installed and playable. Called by whoever installed it, with the two
    // things only that installer can answer: whether the scene still matches
    // its source, and how to build it again.
    void setAuditionSceneReady(std::function<bool()> stillCurrent,
                               std::function<void()> reinstall);
    void clearAuditionSceneReady();
    // True when an audition scene can be played right now. Rebuilds it once if
    // it has gone stale — the readiness gate for these pages had no recovery at
    // all, so a Play that was refused stayed refused until the page happened to
    // reinstall for some other reason.
    bool ensureAuditionSceneReady();

    void setQmlDocumentSaveHandler(std::function<bool(const QString&)> handler);
    // The v2 shell answers the unsaved-changes question itself, because only it
    // can see which difficulties changed — MainWindow mirrors one flag for the
    // whole file. Without a handler installed the Widgets prompt still runs,
    // which is what the hidden window's own File menu still needs.
    void setQmlLeaveDocumentHandler(std::function<void(std::function<void(bool)>)> handler);
    // Write-back into the workspace for the few callers that still edit the
    // active chart from the MainWindow side. Without it they would be writing
    // into a document copy the QML editor overwrites on its next commit.
    // 音频设置 page. The mixer is a value type, so the page reads a copy and
    // hands one back; every write lands on the same runtime-apply and persist
    // path the Widgets dialog used, rather than the page poking members.
    PreviewAudioSettings currentPreviewAudioSettings() const;
    void applyPreviewAudioSettingsFromUi(const PreviewAudioSettings& settings);
    void savePreviewAudioSettingsAsSoftwareDefault();
    void restorePreviewAudioSettingsFromSoftwareDefault();

    // 预览设置 page. Read as one map, written one key at a time: each of these
    // has its own way of reaching the running preview — a canvas setter, a
    // stage-media reroute, an outline recompute, a muri re-apply — and that
    // wiring belongs here beside the members rather than in the QML layer. The
    // map also carries the ranges (square scale, flow speed) so the page does
    // not restate limits the preview owns.
    QVariantMap previewRenderSettings() const;
    void setPreviewRenderSetting(const QString& key, const QVariant& value);

    void setQmlChartTextHandler(std::function<bool(const QString&)> handler);
    bool applyChartTextThroughWorkspace(const QString& text);
    DocumentValidationSnapshot documentValidationSnapshot() const;
    QmlAnalysisSnapshot qmlAnalysisSnapshot() const;
    bool ignoreMuriIssuePrompts() const;
    void invalidateDocumentValidationRevision();
    bool validateActiveDocument();
    void setQuickShellRootWindow(QWindow* window);
    void releaseChartDropImportService();
    void handleAudioDrop(const QStringList& audioPaths,
                         quint64 requestId,
                         quint64 generation,
                         miacode::v2::ChartDropImportService::Completion completion);
    // Shows the first-run welcome / initial-config dialog (preview side +
    // theme). Called from QmlUiBootstrap after the UI is ready.
    void showWelcomeDialog();
    bool quickShellRootWindowFrameGeometryAvailable() const;
    QRect quickShellRootWindowFrameGeometry() const;
    void setQuickShellBackendActive(bool active);
    // Narrow public handles on the shared Widgets-free UI boundary. No new
    // friend declarations: the QML layer reaches these through the context.
    // Whole-chart normalize options. MainWindow stays the single owner so
    // savePortableState() remains the only writer of the stored copy.
    miacode::chart_transform::ChartNormalizationOptions chartNormalizeOptions() const;
    void setChartNormalizeOptions(const miacode::chart_transform::ChartNormalizationOptions& options);
    // Read-only hand-off to the single export-session owner. QML page services
    // may compose on top of this session, but never construct another one.
    QmlExportSession* qmlExportSession() const { return qmlExportSession_; }
    miacode::v2::UiRequestService* uiRequestService() const;
    miacode::v2::JobProgressService* jobProgressService() const;
    void preparePreviewForShutdown();
    bool shellTimelineSurfaceReady() const;
    void noteQuickTimelineSurfaceReady();
    // Asks whether the window may close, answering through the continuation.
    // It cannot be a return value: the unsaved-changes prompt is a QML dialog
    // now, and the only way to get an answer out of one synchronously would be
    // a nested event loop inside a window's close handler.
    void requestShellClose(std::function<void(bool)> onDecided);
    // The same question about the document alone, for flows that replace it.
    void requestLeaveDocument(std::function<void(bool)> onDecided);
    // 打开最近. The list has always been kept (and persisted) here; until now
    // its only reader was the hidden MainWindow's own File menu, so under v2
    // nothing could see it. Entries that no longer exist on disk are dropped
    // as they are read, which is what the Widgets menu did when it rebuilt.
    // Each entry is { path, label }. The label is the containing folder's name,
    // which for a chart is the song — v1's rule, and the reason its menu was
    // readable: a full path is both too wide for a menu and mostly the same
    // prefix repeated. The path rides along for the tooltip.
    QVariantList recentDocumentEntries();
    // Autosave snapshots for the open chart, newest first, as { path, label }.
    // Same shape and the same reason.
    QVariantList backupDocumentEntries();
    // Restore one of them. Confirms through the shell before overwriting.
    void restoreBackupDocument(const QString& path);
    // Remember a chart the shell just created or opened.
    void noteRecentDocument(const QString& path);
    void toggleShellPreviewPlayback();
    void stopShellPreview();
    void seekShellPreview(double second);
    void beginShellPreviewScrub();
    void updateShellPreviewScrub(double second, bool centerView);
    void endShellPreviewScrub(double second, bool centerView);
    void setShellPreviewRate(double rate);
    void toggleShellMuriRenderMode();
    RenderMode muriRenderMode() const;
    void nudgeShellPreviewRate(int direction);
    bool stepShellPreviewBySeconds(double deltaSeconds, bool centerView);
    void beginShellPreviewHeldSeek(int direction, int key);
    void stopShellPreviewHeldSeek(int key = 0);
    void setShellPreviewFullscreen(bool fullscreen);
    void setShellPreviewPaneWidthRatio(double ratio);
    void setShellBottomTabsHeight(int height);
    void setShellBottomTabsCurrentTab(const QString& tabId);
    void navigateShellTimelineToSecond(double second);
    void wheelShellTimelineNavigate(double second);
    void centerShellTimelineNavigate(double second);
    void shellTimelineDragStarted();
    void shellTimelineDragFinished(double second);
    void shellTimelineUserInteractionStarted();
    void shellTimelineSurfaceReady();
    void shellTimelineFollowPreviewToggled(bool enabled);
    void shellTimelineViewportLockToggled(bool enabled);
    void shellTimelineFollowProgressToggled(bool enabled);
    void shellTimelineSyncToggled(bool enabled);
    bool shellHasShortcut(const QKeySequence& sequence) const;
    bool shellTriggerShortcut(const QKeySequence& sequence);
    QString shellWindowTitle() const;
    bool shellWorkspacePanelsSwapped() const;
    QString shellPreviewSpeedLabel() const;
    bool shellMuriCheckRenderMode() const;
    bool shellPreviewPlaying() const;
    double shellPreviewPositionSeconds() const;
    double shellPreviewDurationSeconds() const;
    double shellPreviewLowerBoundSeconds() const;
    QStringList shellPreviewStatsTexts() const;
    double shellPreviewCanvasAspectRatio() const;
    quint64 shellPreviewPaneRestoreGeneration() const;
    double shellPreviewPaneWidthRatio() const;
    bool shellPreviewFullscreen() const;
    QObject* shellPreviewRuntimeObject() const;
    QObject* shellPreviewStageMediaHostObject() const;
    bool shellPreviewUsesSeparateSurface() const;
    QWindow* shellPreviewCompositeWindow() const;
    QObject* shellTimelineStateBridgeObject() const;
    QString shellBottomTabsCurrentTabId() const;
    bool shellBottomTabsVisible() const;
    bool shellTimelineTabVisible() const;
    bool shellValidationTabVisible() const;
    bool shellMuriTabVisible() const;
    bool shellExportPageActive() const;
    QWidget* shellWindowWidget() const;
    QDockWidget* shellOutlineDockWidget() const;
    bool shellOutlineDockCollapsed() const;
    int shellOutlineDockExpandedWidth() const;
    QWidget* shellWorkspaceWidget() const;
    QWidget* shellBottomTabsWidget() const;
    int shellBottomTabsHeight() const;
    double shellBottomTabsHeaderScale() const;
    QWidget* shellPreviewPanelWidget() const;
    double shellNormalizedPreviewCanvasAspectRatio() const;
    void shellRefreshLayoutAfterResize();
    void shellSetRootWindowFrameGeometry(const QRect& geometry);
    void shellNoteQuickUiReady();

protected:
    void closeEvent(QCloseEvent* event);
    bool event(QEvent* event);
    bool eventFilter(QObject* watched, QEvent* event);
    void resizeEvent(QResizeEvent* event);
    void moveEvent(QMoveEvent* event);
    void showEvent(QShowEvent* event);
    void hideEvent(QHideEvent* event);
    void changeEvent(QEvent* event);

private slots:
    void onNewFile();
    void onOpenFile();
    void onOpenCurrentFolder();
    void refreshRestoreBackupMenu(QMenu* restoreBackupMenu);
    void restoreBackupFilePath(const QString& path);
    bool onSaveFile();
    bool onSaveFileAs();
    void onNormalizeWholeChart();
    void onStopPreview();
    void onTogglePreviewPause();
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    void onEditStaticTapOnSlideThreshold();
    int resolveToolsMenuExportDifficultyId() const;
    void onExportCover();
    void onBatchExportPreviewVideo();
    void onPackAsZip();
    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
    void onSkinSettings();
    // Asks the QML shell to show the media tools page. Kept as a slot because
    // the latency page and the tools menu both still trigger it.
    void onMediaProcessingTools();

public:
    // 音视频处理's narrow surface for the QML page. Public rather than another
    // friend declaration: the QML layer reaches these through the context.
    void onCompressBackgroundVideo();
    void onConvertTrackTo44100Hz();
    // Prepend-blank, split around the QML dialog.
    QVariantMap prependMediaBlankContext(bool isTrack);
    QVariantMap detectMediaBlankTiming(bool isTrack);
    void restoreMediaBlankBackup(bool isTrack);
    void applyMediaBlank(bool isTrack, double beats, double bpm);

private slots:
    void onReadTitleFromTrack();
    void onReadArtistFromTrack();
    void onExtractBackgroundFromTrack();
    // Opens the "manage per-difficulty designers" dialog (rows for &des_1..7
    // plus the "all difficulties share one designer" toggle). See
    // DocumentSection::openPerDifficultyDesignerDialog() in DocumentFlow.
    void onManagePerDifficultyDesigners();
    void onAbout();
    void onToggleFindReplace();
    void onFindNext();
    void onFindPrevious();
    void onReplaceOne();
    void onReplaceAll();
    void onErrorItemActivated(QListWidgetItem* item);
    void onMuriItemActivated(QListWidgetItem* item);
public:
    // Public so callers outside MainWindow (the preferences dialog)
    // can read the cached mode. Exposing the enum doesn't widen any
    // mutation surface (the setter setPreviewCanvasFrameRateMode
    // remains internal); only the value type is visible.
    enum class PreviewCanvasFrameRateMode {
        Fps30,
        Fps60,
        Fps120,
        DisplayRefresh,
    };
private:
    std::function<bool(const QString&)> qmlDocumentSaveHandler_;
    std::function<void(std::function<void(bool)>)> qmlLeaveDocumentHandler_;
    std::function<bool(const QString&)> qmlChartTextHandler_;
    quint64 appliedQmlWorkspaceRevision_ = 0;
    // Borrowed from applicationServices_; never owned here.
    miacode::v2::ApplicationServices& applicationServices_;
    miacode::v2::EditorSyncController* editorSyncController_ = nullptr;
    miacode::v2::ChartDropImportService* chartDropImportService_ = nullptr;
    using BatchTransform = std::function<QString(const QString&, int*)>;
    using SelectionContextBatchTransform = std::function<QString(const QString&, const QString&, int*)>;
    enum class ChartTransformOp {
        MirrorLeftRight,
        MirrorUpDown,
        Rotate180,
        Rotate45CounterClockwise,
        Rotate45Clockwise,
    };
    // Moved to core/video/PreviewRenderSettings.h. The alias keeps the
    // MainWindow::PreviewSkinVariant spelling valid at existing call sites.
    using PreviewSkinVariant = ::PreviewSkinVariant;
    enum class PreviewStageMediaRoute {
        QuickShellStageHost,
    };
    enum class TextEncoding {
        Utf8,
        System,
    };
    enum class BottomTabsTabId {
        Timeline,
        Validation,
        Muri,
        Unknown,
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
    QString formatPreviewPlaybackRateToastText(double rate) const;
    void showPreviewPlaybackRateToast(double rate);
    void updatePreviewPlaybackRateToastGeometry();
    void showPreviewFullscreenControls(bool animate = true);
    void hidePreviewFullscreenControls(bool animate = true);
    void schedulePreviewFullscreenControlsAutoHide();
    void pollPreviewFullscreenCursor();
    bool shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const;
    QRect previewFullscreenControlCardRect(bool visible) const;
    void setPreviewCanvasAspectRatio(double ratio, bool persistState);
    double normalizedPreviewCanvasAspectRatio(double ratio) const;
    PreviewCanvasFrameRateMode previewFrameRateModeFromStorageValue(
        const QString& value,
        PreviewCanvasFrameRateMode fallback) const;
    PreviewCanvasFrameRateMode previewCanvasFrameRateModeFromStorageValue(const QString& value) const;
    QString previewFrameRateModeStorageValue(PreviewCanvasFrameRateMode mode) const;
    QString previewCanvasFrameRateModeStorageValue() const;
    QString previewStageMediaFrameRateModeStorageValue() const;
    QString timelineFrameRateModeStorageValue() const;
public:
    // Public getter for the current preview canvas frame-rate mode,
    // read by the preferences dialog to seed its combo box. Read-only
    // — the setter setPreviewCanvasFrameRateMode stays internal.
    PreviewCanvasFrameRateMode currentPreviewCanvasFrameRateMode() const;
    PreviewCanvasFrameRateMode currentPreviewStageMediaFrameRateMode() const;
    PreviewCanvasFrameRateMode currentTimelineFrameRateMode() const;
    // Preview video decode-mode preference (硬件渲染 / 软件渲染 toggle). false =
    // hardware (D3D11VA, the default), true = software (FFmpeg CPU). Mirrors the
    // PV-refresh-rate preference: persisted in portable state and pushed to
    // PreviewStageMediaHost::setVideoDecodePreference (which hot-switches live).
    bool currentVideoDecodePrefersSoftware() const;
private:
    PreviewOutlineVariant previewOutlineVariantFromStorageValue(const QString& value) const;
    QString previewOutlineVariantStorageValue() const;
    PreviewOutlineVariant autoPreviewOutlineVariantForChart(const QString& chartPath) const;
    PreviewOutlineVariant effectivePreviewOutlineVariant() const;
    void applyEffectivePreviewOutlineVariantToCanvas();
    QString resolvePreviewCustomOutlinePath() const;
    QStringList availablePreviewCustomOutlineFileNames() const;
    void applyPreviewCustomOutlineFileName(const QString& fileName, bool persistState);
    PreviewSkinVariant previewSkinVariantFromStorageValue(const QString& value) const;
    QString previewSkinVariantStorageValue() const;
    void refreshPreviewFrameRateTimers();
    void setTouchPadAuthoringAnchor(double seekSecond, double tokenSecond);
    double timelineSecondForCursor(int line, int col) const;
    bool resolveTimelineSecondForCursor(int line, int col, double* second) const;
    void jumpToLocation(int line, int col);
    QString editorText() const;
    QString resolveDefaultTrackPath() const;
    void applyPreviewSkinDirectoryToSurfaces();
    QString resolveProjectRenderStateFilePath() const;
    QString resolveInitialOpenDirectory() const;
    void resetPortablePreviewSettingsToDefaults();
    void applyPortablePreviewSettings(const QJsonObject& preview);
    void loadPortableState();
    void savePortableState() const;

public:
    // The QML pages' bounded reach into the window.
    //
    // These were private, which meant QmlCommandService, QmlPreviewModel and
    // QmlPreviewSettingsModel each needed `friend class` — and a friend grant is
    // unbounded: it lets any later edit reach any member, forever. Publishing
    // exactly what those pages call replaced three blanket grants with this
    // list, at no cost to the recorded surface (docs/specs/ui/
    // QML_UI_V2_BACKEND_SURFACE_ZH.md already counted every name here).
    //
    // The skin/outline entries are catalog queries — path resolution and
    // directory listing, no state of their own. The two setters already took a
    // "persist" flag, so they match the preference surface below.
    QStringList availablePreviewSkinDirectoryNames() const;
    QString previewSkinDisplayName(const QString& directoryName) const;
    QString resolvePreviewSkinDir() const;
    QString resolvePreviewSkinRootDir() const;
    QString resolvePreviewCustomOutlineDir() const;
    void applyPreviewOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                                    bool persistState);
    void setMuriRenderMode(RenderMode mode, bool persistState = true);
    void onPreferences();
    // The export page's two remaining reads: where the live playhead is (it
    // seeds the export range from the current position) and a nudge to
    // re-derive the negative-time intro region after an intro edit.
    double currentPreviewAuthoritativeAudioClockSecond() const;
    void refreshExportIntroState();

    // ---- miacode::v2::EditorPageRouter ----
    // Thin forwarders onto the existing switchTo*Field entry points, which stay
    // private: they still drive the hidden widget stack, and that is exactly
    // what must not become public API.
    bool hasActiveDifficulty() const override;
    int activeDifficultyId() const override;
    bool enterDifficultyPage(int difficultyId) override;
    bool enterMetadataPage() override;
    bool enterLatencyPage() override;
    bool enterExportPage() override;
    void packChartAsZip() override;

    // The live-surface half of the preview appearance settings. The values
    // themselves belong to miacode::v2::PreviewAppearanceState; these two push
    // them into the objects only this window holds, so the QML pages no longer
    // need `friend` access to previewCanvas_ / previewSfxRuntime_ to make an
    // appearance change take effect.
    // Pushes the current mixer levels into the live SFX runtime, reloading the
    // sound bank first when the selected intro sound changed. A no-op until the
    // audio engine is up, which is the guard every caller used to repeat.
    void applyPreviewSfxLevels(bool reloadAssets = false);
    void refreshPreviewSurfaces();

    // 偏好设置's narrow surface for the QML page. Every setter already took a
    // "persist" flag, so the QML model is a projection rather than new policy.
    void applyEditorTextFontSize(int pointSize, bool persistPreference);
    void applyEditorLineSpacingFactor(double factor, bool persistPreference);
    void applyEditorHalfWidthInputEnabled(bool enabled, bool persistPreference);
    void applyEditorOverwriteModeEnabled(bool enabled, bool persistPreference);
    void applyEditorAutoCompletionEnabled(bool enabled, bool persistPreference);
    void applyEditorImeInputDisabled(bool disabled, bool persistPreference);
    // 延迟校准's narrow surface for the QML page. Reads mirror what the
    // Widgets page derived from the document; writes are the existing
    // applyLatencyDetector* transactions.
    double latencyDocumentWholeBpm() const;
    double latencyDocumentOffsetSeconds() const;
    int latencyDocumentClockCount() const;
    QString latencyTrackPath() const;
    void applyLatencyDetectorBpm(double bpm);
    void applyLatencyDetectorOffset(double seconds);
    void applyLatencyDetectorClockCount(int clockCount);
    // Re-applies shortcut bindings to the live QActions after an edit.
    void applyConfiguredShortcuts();
    // Performance + workspace settings the QML preferences page drives.
    void setWorkspacePanelsSwapped(bool swapped, bool persistState);
    void setVideoDecodePrefersSoftware(bool preferSoftware, bool persistState);
    void setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    void setPreviewStageMediaFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    void setTimelineFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState);
    double currentPreviewCanvasRefreshRate() const;
    int currentEditorTextFontSize() const;
    double currentEditorLineSpacingFactor() const;
    bool currentEditorHalfWidthInputEnabled() const;
    bool currentEditorAutoCompletionEnabled() const;
    bool currentEditorImeInputDisabled() const;
    bool currentWorkspacePanelsSwapped() const;

private:
    // Transient Alt-hold override: while the preview is paused, holding Alt
    // flips the "暂停时显示判定区" pause display (judge area ⇄ PV/BG) until released.
    void setPauseDisplayAltHoldActive(bool active);
    void setTouchPadAuthoringCtrlHoldActive(bool active);
    void addRecentFilePath(const QString& path);
    void openRecentFilePath(const QString& path);
    void refreshRecentFilesMenu(QMenu* recentFilesMenu);
    void persistEditorTextFontPreference() const;
    void loadProjectRenderState();
    void saveProjectRenderState() const;
    void removeProjectRenderState() const;
    void applyPreviewAudioSettingsToRuntime();
    void loadProjectAudioPreferences();
    void saveProjectAudioPreferences() const;
    void setLastOpenDirectory(const QString& pathOrDir);
    bool runValidateSimaiSilently(bool focusFirstIssue = false);
    bool preparePreviewStartState();
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
    void loadProjectValidationPreferences();
    void saveProjectValidationPreferences(const QString& chartFilePath = QString()) const;
    void applyIgnoreMuriIssuePrompts(bool enabled, bool persistPreference);
    void showIssueListContextMenu(QListWidget* list, const QPoint& pos, bool muriList);
    void rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers);
    void clearValidationCache();
    void refreshValidationPanelForActiveField();
    void applyDeferredAnalysisUiUpdates();
    void setValidationTabVisible(bool visible);
    BottomTabsTabId currentBottomTabsTabId() const;
    QString currentBottomTabsTabIdString() const;
    void setCurrentBottomTabsTabId(BottomTabsTabId tabId);
    void setCurrentBottomTabsTabId(const QString& tabId);
    void setBottomTabsTabVisible(BottomTabsTabId tabId, bool visible);
    bool bottomTabsTabVisible(BottomTabsTabId tabId) const;
    void restoreBottomTabsCurrentTabAfterRefresh(BottomTabsTabId preferredTabId);
    void applyMuriRenderOptions();
    struct ValidationCachedIssue {
        int line = 1;
        int col = 1;
        int endCol = 1;
        SimaiNativeValidationSeverity severity = SimaiNativeValidationSeverity::Error;
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
        SimaiNativeValidationLocale validationLocale = SimaiNativeValidationLocale::English;
        miacode::simai::SimaiTimingMetadata timingMetadata;
        quint64 validationRevision = 0;
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
        double previewSecond = -1.0;
    };

public:
    // Derived sidebar bookmark for a non-control `||` chart comment. This is a
    // transient view cache rebuilt from chart text, never a persisted object.
    struct EditorBookmark {
        QString title;
        QString text;
        int line = 1;
        QString source;
        QString commentText;
        QString commentFingerprint;
        QString contextBefore;
        QString contextAfter;
        int difficultyId = 0;
        // True when the name comes from an explicit `[label]` comment prefix.
        bool nameLocked = false;
    };

private:
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

    bool quickShellBottomTabsProxyActive() const;
    QString bottomTabsFallbackLabel(BottomTabsTabId tabId) const;
    QWidget* bottomTabsPageForTab(BottomTabsTabId tabId) const;
    QTabWidget* bottomTabsContainerForTab(BottomTabsTabId tabId) const;
    void syncBottomTabsCurrentTabToContainers();
    void syncQuickShellBottomTabsProxyRoute();

    // Synchronously advance the outline busy spinner one frame (no-op unless it
    // is active). Called from inside the slow export-page build so the spinner
    // visibly rotates while the GUI thread is blocked. See DocumentUi.cpp.
    void tickOutlineBusySpinner();

    #include "MainWindowMemberStorage.inc"
};
