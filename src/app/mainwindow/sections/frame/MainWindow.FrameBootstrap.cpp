#include "../../MainWindow.h"
#include "../../MainWindowShared.h"
#include "MainWindow.FrameSection.h"
#include "../editor/MainWindow.EditorSection.h"
#include "../dialogs/MainWindow.DialogsSection.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../export/MainWindow.ExportSection.h"
#include "../preferences/MainWindow.PreferencesSection.h"
#include "../preview/MainWindow.PreviewSection.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "../validation/MainWindow.ValidationSection.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "audio/PreviewAudioDeviceWatcher.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "UiTheme.h"
#include "WindowParityMetrics.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/AdoptedSurfaceDragAutoScroll.h"
#include "common/AdoptedWidgetCoordinates.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "app/qml_ui/export/QmlExportSession.h"
#include "app/v2/JobProgressService.h"
#include "app/v2/UiRequestService.h"
#include "tools/latency/LatencySandboxController.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <algorithm>
#include <functional>

using namespace miacode::mainwindow::shared;

namespace {

void appendPreviewFramePacingDiagLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/frame_pacing"),
        text,
        true
    );
}

}  // namespace

MainWindow::MainWindow(miacode::v2::ApplicationServices& services, QWidget* parent)
    : QMainWindow(parent)
    , applicationServices_(services)
{
    // Borrowed, not created: these services outlive the window and are owned by
    // the non-Widget assembly (stage 3.5 item 1). The window only connects to
    // them.
    editorSyncController_ = &applicationServices_.editorSync();
    chartDropImportService_ = &applicationServices_.chartDropImport();
    connect(editorSyncController_, &miacode::v2::EditorSyncController::editorContextChanged,
            this, &MainWindow::refreshEditorAuthoringContext);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::caretLocationPublished,
            this, [this](int difficultyId, qulonglong, int line, int column) {
                if (difficultyId == activeDifficultyId_) {
                    publishEditorCaret(difficultyId, line, column);
                }
            });
    connect(editorSyncController_, &miacode::v2::EditorSyncController::pointerInteractionStarted,
            this, &MainWindow::handleEditorPointerInteraction);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::touchPadControlHoldChanged,
            this, &MainWindow::setTouchPadAuthoringCtrlHold);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::touchPadPreviewAnchorPublished,
            this, &MainWindow::applyTouchPadAuthoringPreviewAnchor);
    connect(editorSyncController_, &miacode::v2::EditorSyncController::previewSeekPublished,
            this, &MainWindow::seekPreviewToEditorLocation);

    // The preview appearance values live in the application assembly; this
    // window owns the live surfaces and the settings file, so it is what reacts
    // when one of them moves. Restore paths write through
    // PreviewAppearanceState::values() instead, which stays silent — reloading
    // a document must not look like a user edit and must not rewrite settings.
    miacode::v2::PreviewAppearanceState& previewAppearance =
        applicationServices_.previewAppearance();
    connect(&previewAppearance, &miacode::v2::PreviewAppearanceState::skinChanged,
            this, [this] {
                applyPreviewSkinDirectoryToSurfaces();
                savePortableState();
            });
    connect(&previewAppearance, &miacode::v2::PreviewAppearanceState::judgeEffectStyleChanged,
            this, [this, &previewAppearance] {
                if (previewCanvas_ != nullptr) {
                    previewCanvas_->setJudgeEffectStyle(previewAppearance.judgeEffectStyle());
                }
                savePortableState();
            });
    connect(&previewAppearance, &miacode::v2::PreviewAppearanceState::introSoundChanged,
            this, [this] {
                applyPreviewSfxLevels(/*reloadAssets=*/true);
                savePortableState();
            });

    QElapsedTimer startupStageTimer;
    startupStageTimer.start();
    qint64 startupLastMs = 0;
    const auto logStartupStage = [&](const QString& stageName) {
        const qint64 nowMs = startupStageTimer.elapsed();
        const qint64 deltaMs = nowMs - startupLastMs;
        startupLastMs = nowMs;
        appendStartupTimingStage(QString("mainwindow/%1").arg(stageName), nowMs, deltaMs);
    };

    configureRuntimeDebugOutput();
    logStartupStage("configure_runtime_debug_output");
    quickShellStartupStageMediaLoadDeferred_ = true;
    setProperty("miacode.dialog_parentless", true);
    logStartupStage("dialog_parentless_property_ready");
    setAttribute(Qt::WA_DontShowOnScreen);
    logStartupStage("dont_show_on_screen_ready");
    setAttribute(Qt::WA_NativeWindow);
    logStartupStage("native_window_attribute_ready");
    winId();
    logStartupStage("native_window_ready");

    editorSection_ = std::make_unique<EditorSection>(*this, ui_, state_);
    documentSection_ = std::make_unique<DocumentSection>(*this, ui_, state_);
    dialogsSection_ = std::make_unique<DialogsSection>(*this, ui_, state_);
    exportSection_ = std::make_unique<ExportSection>(*this, ui_, state_);
    // The export page reaches the engine through the assembly's slot, never
    // through this window's member. Cleared at the top of ~MainWindow so the
    // page cannot call into a half-destroyed section.
    applicationServices_.setExportEngine(exportSection_.get());
    // Page routing takes the same door: the QML page host asks the router, not
    // this window.
    applicationServices_.setEditorPageRouter(this);
    applicationServices_.setLatencyEngine(this);
    applicationServices_.setTimelineSurface(this);
    applicationServices_.setPreviewSurface(this);
    applicationServices_.setPreferencesStore(this);
    applicationServices_.setDocumentBridge(this);

    // Relay the window's push notifications into the assembly so the QML models
    // can subscribe without holding a MainWindow&. Nothing waits on a result
    // here, so a relay changes nothing about the contract — unlike the handler
    // hooks on DocumentBridge, which need an answer back.
    {
        miacode::v2::ShellNotifications& notify = applicationServices_.shellNotifications();
        connect(this, &MainWindow::shellPresentationChanged,
                &notify, &miacode::v2::ShellNotifications::presentationChanged);
        connect(this, &MainWindow::shellPreviewPlayheadChanged,
                &notify, &miacode::v2::ShellNotifications::previewPlayheadChanged);
        connect(this, &MainWindow::previewSkinDirectoryChanged,
                &notify, &miacode::v2::ShellNotifications::previewSkinDirectoryChanged);
        connect(this, &MainWindow::documentReplaced,
                &notify, &miacode::v2::ShellNotifications::documentReplaced);
        connect(this, &MainWindow::editorPreferencesChanged,
                &notify, &miacode::v2::ShellNotifications::editorPreferencesChanged);
        connect(this, &MainWindow::muriPromptPreferenceChanged,
                &notify, &miacode::v2::ShellNotifications::muriPromptPreferenceChanged);
        connect(this, &MainWindow::videoExportWorkerRunningChanged,
                &notify, &miacode::v2::ShellNotifications::videoExportWorkerRunningChanged);
        connect(this, &MainWindow::normalizeWholeChartRequested,
                &notify, &miacode::v2::ShellNotifications::normalizeWholeChartRequested);
        connect(this, &MainWindow::mediaToolsRequested,
                &notify, &miacode::v2::ShellNotifications::mediaToolsRequested);
        connect(this, &MainWindow::preferencesRequested,
                &notify, &miacode::v2::ShellNotifications::preferencesRequested);
        connect(this, &MainWindow::coverExportRequested,
                &notify, &miacode::v2::ShellNotifications::coverExportRequested);
    }
    preferencesSection_ = std::make_unique<PreferencesSection>(*this, ui_, state_);
    previewSection_ = std::make_unique<PreviewSection>(*this, ui_, state_);
    validationSection_ = std::make_unique<ValidationSection>(*this, ui_, state_);
    windowSection_ = std::make_unique<WindowSection>(*this, ui_, state_);
    frameSection_ = std::make_unique<FrameSection>(*this, ui_, state_);
    timelineSection_ = std::make_unique<TimelineSection>(*this, ui_, state_);
    // unique_ptr owns it; pass no QObject parent to avoid double-delete.
    latencySandboxController_ = std::make_unique<miacode::latency::LatencySandboxController>(this, nullptr);
    logStartupStage("sections_ready");

    previewWarmupPool_ = new QThreadPool(this);
    previewWarmupPool_->setObjectName(QStringLiteral("PreviewWarmupPool"));
    previewWarmupPool_->setMaxThreadCount(2);
    previewWarmupPool_->setExpiryTimeout(-1);
    logStartupStage("preview_warmup_pool_ready");

    timelineSlowRefreshPool_ = new QThreadPool(this);
    timelineSlowRefreshPool_->setObjectName(QStringLiteral("TimelineSlowRefreshPool"));
    timelineSlowRefreshPool_->setMaxThreadCount(1);
    timelineSlowRefreshPool_->setExpiryTimeout(-1);

    timelineAnalysisPool_ = new QThreadPool(this);
    timelineAnalysisPool_->setObjectName(QStringLiteral("TimelineAnalysisPool"));
    timelineAnalysisPool_->setMaxThreadCount(1);
    timelineAnalysisPool_->setExpiryTimeout(-1);
    logStartupStage("timeline_analysis_pools_ready");

    setWindowModified(false);
    updateWindowTitle();
    windowSection_->setupInitialWindowGeometry();
    if (QGuiApplication* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance()); guiApp != nullptr) {
        if (QStyleHints* styleHints = guiApp->styleHints(); styleHints != nullptr) {
            connect(styleHints, &QStyleHints::colorSchemeChanged, this, [this]() {
                windowSection_->applyUiTheme();
            });
        }
    }
    auto* topMenuBar = new QMenuBar(this);
    topMenuBar->setNativeMenuBar(false);
    setMenuBar(topMenuBar);
    setStatusBar(new QStatusBar(this));

    // Beta20-fix — unified all top menus to the `Name(&L)` mnemonic
    // suffix style for both English and Chinese (was: English used the
    // bare `&L` underline form `&File`/`&Edit` while Transform used the
    // `Name(&L)` suffix form, producing visually inconsistent menu
    // labels). The trailing `(L)` parens render in both locales which
    // matches the existing Chinese convention.
    auto* fileMenu = menuBar()->addMenu(UiText::text(QStringLiteral("menu.file")));
    auto* editMenu = menuBar()->addMenu(UiText::text(QStringLiteral("metadata.edit_e")));
    auto* toolsMenu = menuBar()->addMenu(UiText::text(QStringLiteral("menu.tools")));
    // Beta20-fix — Transform menu renamed to "Modify" / "调整" in both
    // languages so the Alt-T mnemonic is unambiguous for the Tools
    // menu. Picked "Modify" (Alt+M) over "Transform(&R)" because the
    // user requested a synonym, not just a different mnemonic letter.
    // The Chinese key in UiText.cpp likewise uses 调整(&M).
    auto* transformMenu = menuBar()->addMenu(UiText::text(QStringLiteral("menu.transform")));
    auto* previewMenu = menuBar()->addMenu(UiText::text(QStringLiteral("metadata.preview_p")));
    auto* helpMenu = menuBar()->addMenu(UiText::text(QStringLiteral("menu.help")));
    styleRoundedMenu(*fileMenu);
    styleRoundedMenu(*editMenu);
    styleRoundedMenu(*toolsMenu);
    styleRoundedMenu(*transformMenu);
    styleRoundedMenu(*previewMenu);
    styleRoundedMenu(*helpMenu);

    auto* toolBar = new QToolBar(QStringLiteral("Main"), this);
    addToolBar(toolBar);
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    // NB: do NOT shrink toolBar->iconSize() to font-match the gear — the gear is
    // the toolbar's only icon, so its icon-button drives the toolbar row height,
    // and a smaller iconSize visibly shortens the whole toolbar. The gear is
    // font-matched purely inside makeSettingsGearIcon, which renders the Material
    // artwork into an inset of the icon box so the glyph reads at ~menu-text size
    // while the icon box — and thus the toolbar height — stays at the default.
    setupMenusAndActions(fileMenu, editMenu, transformMenu, previewMenu, helpMenu);
    QAction* metadataSettingsAction = toolsMenu->addAction(UiText::text(QStringLiteral("sidebar.metadata")));
    connect(metadataSettingsAction, &QAction::triggered, this, [this]() {
        if (switchToMetadataField() && titleEdit_ != nullptr) {
            titleEdit_->setFocus();
        }
    });
    if (latencyDetectorAction_ != nullptr) {
        editMenu->removeAction(latencyDetectorAction_);
        toolsMenu->addAction(latencyDetectorAction_);
    }
    if (normalizeWholeChartAction_ != nullptr) {
        toolsMenu->addSeparator();
        toolsMenu->addAction(normalizeWholeChartAction_);
    }
    if (exportVideoAction_ != nullptr) {
        previewMenu->removeAction(exportVideoAction_);
        toolsMenu->addSeparator();
        toolsMenu->addAction(exportVideoAction_);
        QAction* batchExportAction = toolsMenu->addAction(UiText::text(QStringLiteral("action.batch_export")));
        connect(batchExportAction, &QAction::triggered, this, &MainWindow::onBatchExportPreviewVideo);
        QAction* exportCoverAction = toolsMenu->addAction(UiText::text(QStringLiteral("action.export_cover")));
        connect(exportCoverAction, &QAction::triggered, this, &MainWindow::onExportCover);
    }
    const QList<QAction*> editActions = editMenu->actions();
    if (!editActions.isEmpty() && editActions.constLast()->isSeparator()) {
        editMenu->removeAction(editActions.constLast());
    }
    logStartupStage("menus_and_actions_ready");

    // The chart editor is a QML TextArea. What used to stand here — a hidden
    // PlainCodeEditor, its bracket highlighter, its context menu and its
    // shortcut forwarding — mirrored a document nobody could see and took
    // input nobody could give it, because this window carries
    // WA_DontShowOnScreen.

    auto* central = new QWidget(this);
    central->setObjectName("EditorShell");
    central->setAttribute(Qt::WA_StyledBackground, true);
    central->setStyleSheet(UiTheme::editorShellStyleSheet());
    central->setMinimumWidth(320);
    central->setProperty("baseMinimumWidth", central->minimumWidth());
    workspaceContentWidget_ = central;
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    auto* editorHeader = new QFrame(central);
    editorHeader->setObjectName("EditorHeader");
    editorHeader->setAttribute(Qt::WA_StyledBackground, true);
    editorHeaderWidget_ = editorHeader;
    auto* editorHeaderLayout = new QHBoxLayout(editorHeader);
    editorHeaderLayout->setContentsMargins(12, 8, 12, 8);
    editorHeaderLayout->setSpacing(10);
    editorContextLabel_ = new QLabel(UiText::text(QStringLiteral("editor.welcome")), editorHeader);
    editorContextLabel_->setObjectName("EditorContext");
    editorContextLabel_->setFont(uiAccentFont(15, QFont::DemiBold));
    editorContextLabel_->setMinimumWidth(0);
    editorContextLabel_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    editorHeaderLayout->addWidget(editorContextLabel_, 0);

    editorBatchTransformControls_ = nullptr;
    transformMirrorLeftRightButton_ = nullptr;
    transformMirrorUpDownButton_ = nullptr;
    transformRotate180Button_ = nullptr;
    transformRotate45CounterClockwiseButton_ = nullptr;
    transformRotate45ClockwiseButton_ = nullptr;

    editorDifficultyControls_ = new QWidget(editorHeader);
    editorDifficultyControls_->setObjectName("EditorDifficultyControls");
    editorDifficultyControls_->setAttribute(Qt::WA_StyledBackground, true);
    auto* editorDifficultyLayout = new QHBoxLayout(editorDifficultyControls_);
    editorDifficultyLayout->setContentsMargins(0, 0, 0, 0);
    editorDifficultyLayout->setSpacing(8);
    auto* difficultyLevelLabel = new QLabel("Lv", editorDifficultyControls_);
    difficultyLevelLabel_ = difficultyLevelLabel;
    difficultyLevelLabel->setFont(uiAccentFont(10));
    auto* difficultyLevelLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyLevelLineEdit->setLeftPlaceholderText("&lv_n=");
    difficultyLevelEdit_ = difficultyLevelLineEdit;
    difficultyLevelEdit_->setFixedWidth(48);
    difficultyLevelEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    difficultyLevelEdit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Chart-wide timing offset (`&first`). It shares a single source of truth
    // (document_.first) with the latency page; it used to sit on the metadata
    // page but lives here now so charters can tune it against the live
    // timeline/preview.
    auto* difficultyFirstLabel = new QLabel(UiText::text(QStringLiteral("metadata.field.first")), editorDifficultyControls_);
    difficultyFirstLabel_ = difficultyFirstLabel;
    difficultyFirstLabel->setFont(uiAccentFont(10));
    auto* difficultyFirstLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyFirstLineEdit->setLeftPlaceholderText("&first=");
    firstEdit_ = difficultyFirstLineEdit;
    firstEdit_->setFixedWidth(64);
    firstEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    firstEdit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Per-difficulty designer (`&des_N`) pair — the alternative header field.
    // Exactly one of the offset/designer pairs is visible at a time, driven by
    // the 顶部显示 preference (updateEditorHeaderLayoutMode); designer names can
    // always also be managed from the metadata page's designer dialog.
    auto* difficultyDesignerLabel = new QLabel(UiText::text(QStringLiteral("editor.des")), editorDifficultyControls_);
    difficultyDesignerLabel_ = difficultyDesignerLabel;
    difficultyDesignerLabel->setFont(uiAccentFont(10));
    auto* difficultyDesignerLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyDesignerLineEdit->setLeftPlaceholderText("&des_n=");
    difficultyDesignerEdit_ = difficultyDesignerLineEdit;
    difficultyDesignerEdit_->setFixedWidth(96);
    difficultyDesignerEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    difficultyDesignerEdit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Default preference is the offset pair; keep the designer pair hidden
    // until updateEditorHeaderLayoutMode applies the loaded preference.
    difficultyDesignerLabel_->hide();
    difficultyDesignerEdit_->hide();
    editorDifficultyLayout->addWidget(difficultyLevelLabel);
    editorDifficultyLayout->addWidget(difficultyLevelEdit_);
    editorDifficultyLayout->addWidget(difficultyFirstLabel);
    editorDifficultyLayout->addWidget(firstEdit_);
    editorDifficultyLayout->addWidget(difficultyDesignerLabel);
    editorDifficultyLayout->addWidget(difficultyDesignerEdit_);
    editorDifficultyControls_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorDifficultyControls_->hide();
    editorHeaderLayout->addWidget(editorDifficultyControls_, 0);

    editorValidationSummaryWidget_ = new QWidget(editorHeader);
    auto* editorValidationSummaryLayout = new QHBoxLayout(editorValidationSummaryWidget_);
    editorValidationSummaryLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationSummaryLayout->setSpacing(8);
    constexpr int kEditorValidationSummaryEdgeGap = 4;
    auto* editorValidationSummaryLeadingGap = new QWidget(editorValidationSummaryWidget_);
    editorValidationSummaryLeadingGap->setFixedWidth(kEditorValidationSummaryEdgeGap);
    editorValidationSummaryLeadingGap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const int summaryCountReserveWidth =
        QFontMetrics(uiMonoFont(10, QFont::DemiBold)).horizontalAdvance(QStringLiteral("999"));

    auto* editorValidationErrorGroup = new QWidget(editorValidationSummaryWidget_);
    auto* editorValidationErrorLayout = new QHBoxLayout(editorValidationErrorGroup);
    editorValidationErrorLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationErrorLayout->setSpacing(6);
    editorValidationErrorIconLabel_ = new QLabel(editorValidationErrorGroup);
    editorValidationErrorIconLabel_->setFixedSize(14, 14);
    editorValidationErrorIconLabel_->setCursor(Qt::PointingHandCursor);
    editorValidationErrorIconLabel_->installEventFilter(this);
    const QString jumpToValidationToolTip = UiText::text(QStringLiteral("metadata.click_to_open_the_syntax"));
    const QString jumpToMuriToolTip = UiText::text(QStringLiteral("metadata.click_to_open_the_muri"));
    editorValidationErrorIconLabel_->setToolTip(jumpToValidationToolTip);
    editorValidationErrorCountLabel_ = new QLabel(QStringLiteral("0"), editorValidationErrorGroup);
    editorValidationErrorCountLabel_->setFont(uiMonoFont(10, QFont::DemiBold));
    editorValidationErrorCountLabel_->setFixedWidth(summaryCountReserveWidth);
    editorValidationErrorCountLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorValidationErrorCountLabel_->setCursor(Qt::PointingHandCursor);
    editorValidationErrorCountLabel_->installEventFilter(this);
    editorValidationErrorCountLabel_->setToolTip(jumpToValidationToolTip);
    editorValidationErrorLayout->addWidget(editorValidationErrorIconLabel_, 0, Qt::AlignVCenter);
    editorValidationErrorLayout->addWidget(editorValidationErrorCountLabel_, 0, Qt::AlignVCenter);
    editorValidationErrorGroup->setFixedWidth(14 + 6 + summaryCountReserveWidth);
    editorValidationErrorGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* editorValidationWarningGroup = new QWidget(editorValidationSummaryWidget_);
    auto* editorValidationWarningLayout = new QHBoxLayout(editorValidationWarningGroup);
    editorValidationWarningLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationWarningLayout->setSpacing(3);
    editorValidationWarningIconLabel_ = new QLabel(editorValidationWarningGroup);
    editorValidationWarningIconLabel_->setFixedSize(14, 14);
    editorValidationWarningIconLabel_->setCursor(Qt::PointingHandCursor);
    editorValidationWarningIconLabel_->installEventFilter(this);
    editorValidationWarningIconLabel_->setToolTip(jumpToValidationToolTip);
    editorValidationWarningCountLabel_ = new QLabel(QStringLiteral("0"), editorValidationWarningGroup);
    editorValidationWarningCountLabel_->setFont(uiMonoFont(10, QFont::DemiBold));
    editorValidationWarningCountLabel_->setFixedWidth(summaryCountReserveWidth);
    editorValidationWarningCountLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorValidationWarningCountLabel_->setCursor(Qt::PointingHandCursor);
    editorValidationWarningCountLabel_->installEventFilter(this);
    editorValidationWarningCountLabel_->setToolTip(jumpToValidationToolTip);
    editorValidationWarningLayout->addWidget(editorValidationWarningIconLabel_, 0, Qt::AlignVCenter);
    editorValidationWarningLayout->addWidget(editorValidationWarningCountLabel_, 0, Qt::AlignVCenter);
    editorValidationWarningGroup->setFixedWidth(14 + 3 + summaryCountReserveWidth);
    editorValidationWarningGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* editorValidationMuriGroup = new QWidget(editorValidationSummaryWidget_);
    auto* editorValidationMuriLayout = new QHBoxLayout(editorValidationMuriGroup);
    editorValidationMuriLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationMuriLayout->setSpacing(4);
    editorValidationMuriIconLabel_ = new QLabel(editorValidationMuriGroup);
    editorValidationMuriIconLabel_->setFixedSize(14, 14);
    editorValidationMuriIconLabel_->setCursor(Qt::PointingHandCursor);
    editorValidationMuriIconLabel_->installEventFilter(this);
    editorValidationMuriIconLabel_->setToolTip(jumpToMuriToolTip);
    editorValidationMuriCountLabel_ = new QLabel(QStringLiteral("0"), editorValidationMuriGroup);
    editorValidationMuriCountLabel_->setFont(uiMonoFont(10, QFont::DemiBold));
    editorValidationMuriCountLabel_->setFixedWidth(summaryCountReserveWidth);
    editorValidationMuriCountLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorValidationMuriCountLabel_->setCursor(Qt::PointingHandCursor);
    editorValidationMuriCountLabel_->installEventFilter(this);
    editorValidationMuriCountLabel_->setToolTip(jumpToMuriToolTip);
    editorValidationMuriLayout->addWidget(editorValidationMuriIconLabel_, 0, Qt::AlignVCenter);
    editorValidationMuriLayout->addWidget(editorValidationMuriCountLabel_, 0, Qt::AlignVCenter);
    editorValidationMuriGroup->setFixedWidth(14 + 4 + summaryCountReserveWidth);
    editorValidationMuriGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* editorValidationSummaryTrailingGap = new QWidget(editorValidationSummaryWidget_);
    editorValidationSummaryTrailingGap->setFixedWidth(kEditorValidationSummaryEdgeGap);
    editorValidationSummaryTrailingGap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    editorValidationSummaryLayout->addWidget(editorValidationSummaryLeadingGap, 0, Qt::AlignVCenter);
    editorValidationSummaryLayout->addWidget(editorValidationMuriGroup, 0, Qt::AlignVCenter);
    editorValidationSummaryLayout->addWidget(editorValidationWarningGroup, 0, Qt::AlignVCenter);
    editorValidationSummaryLayout->addWidget(editorValidationErrorGroup, 0, Qt::AlignVCenter);
    editorValidationSummaryLayout->addWidget(editorValidationSummaryTrailingGap, 0, Qt::AlignVCenter);
    editorValidationSummaryWidget_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorValidationSummaryWidget_->hide();
    editorHeaderLayout->addWidget(editorValidationSummaryWidget_, 0, Qt::AlignLeft);

    editorHeaderLayout->addStretch(1);

    auto* editorHeaderTrailingWidget = new QWidget(editorHeader);
    auto* editorHeaderTrailingLayout = new QHBoxLayout(editorHeaderTrailingWidget);
    editorHeaderTrailingLayout->setContentsMargins(0, 0, 0, 0);
    editorHeaderTrailingLayout->setSpacing(16);

    editorCursorLabel_ = new QLabel(
        UiText::text(QStringLiteral("metadata.ln_1_col_1")),
        editorHeaderTrailingWidget);
    editorCursorLabel_->setObjectName("EditorMeta");
    editorCursorLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editorCursorLabel_->setFixedWidth(
        QFontMetrics(uiMonoFont(10)).horizontalAdvance(
            UiText::text(QStringLiteral("document.ln_9999_col_9999"))) + 10);
    editorCursorLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorHeaderTrailingLayout->addWidget(editorCursorLabel_, 0, Qt::AlignRight);
    editorHeaderLayout->addWidget(editorHeaderTrailingWidget, 0, Qt::AlignRight);
    centralLayout->addWidget(editorHeader, 0);

    editorStack_ = new QStackedWidget(central);
    editorStack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    editorStack_->setMinimumWidth(0);
    editorFindGeometryHost_ = editorStack_;

    auto* findBar = new QFrame(editorStack_);
    findBar->setObjectName("EditorFindBar");
    findBar->setStyleSheet(UiTheme::editorFindBarStyleSheet());
    auto* findBarLayout = new QVBoxLayout(findBar);
    findBarLayout->setContentsMargins(10, 6, 10, 6);
    findBarLayout->setSpacing(4);

    auto* findRow = new QHBoxLayout();
    findRow->setContentsMargins(0, 0, 0, 0);
    findRow->setSpacing(6);
    editorFindEdit_ = new QLineEdit(findBar);
    editorFindEdit_->setPlaceholderText(UiText::text(QStringLiteral("metadata.find")));
    editorFindPrevButton_ = new QToolButton(findBar);
    editorFindPrevButton_->setObjectName("EditorFindPrevButton");
    editorFindPrevButton_->setText(QStringLiteral("↑"));
    editorFindPrevButton_->setToolTip(UiText::text(QStringLiteral("metadata.find_previous")));
    editorFindPrevButton_->setFixedWidth(24);
    editorFindNextButton_ = new QToolButton(findBar);
    editorFindNextButton_->setObjectName("EditorFindNextButton");
    editorFindNextButton_->setText(QStringLiteral("↓"));
    editorFindNextButton_->setToolTip(UiText::text(QStringLiteral("metadata.find_next")));
    editorFindNextButton_->setFixedWidth(24);
    editorFindCloseButton_ = new QToolButton(findBar);
    editorFindCloseButton_->setObjectName("EditorFindCloseButton");
    // Crisp painter-drawn ✕ (the bare U+2715 glyph rendered thin/misaligned in
    // the button font). Reuses the outline list's close icon so every close
    // affordance matches; colored to the find bar's button text (textPrimary,
    // the same %7 editorFindBarStyleSheet() paints the glyph buttons with). The
    // baked icon is re-tinted on theme change in WindowSection::applyUiTheme.
    editorFindCloseButton_->setIcon(makeOutlineCloseIcon(UiTheme::colors().textPrimary));
    editorFindCloseButton_->setIconSize(QSize(12, 12));
    editorFindCloseButton_->setToolTip(UiText::text(QStringLiteral("metadata.close")));
    editorFindCloseButton_->setFixedWidth(28);
    findRow->addWidget(editorFindEdit_, 1);
    findRow->addWidget(editorFindPrevButton_, 0);
    findRow->addWidget(editorFindNextButton_, 0);
    findRow->addWidget(editorFindCloseButton_, 0);
    findBarLayout->addLayout(findRow);

    auto* replaceRow = new QHBoxLayout();
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->setSpacing(4);
    editorReplaceEdit_ = new QLineEdit(findBar);
    editorReplaceEdit_->setPlaceholderText(UiText::text(QStringLiteral("metadata.replace")));
    editorReplaceButton_ = new QPushButton(UiText::text(QStringLiteral("metadata.replace")), findBar);
    editorReplaceAllButton_ = new QPushButton(UiText::text(QStringLiteral("metadata.replace_all")), findBar);
    replaceRow->addWidget(editorReplaceEdit_, 1);
    replaceRow->addWidget(editorReplaceButton_, 0);
    replaceRow->addWidget(editorReplaceAllButton_, 0);
    findBarLayout->addLayout(replaceRow);

    findBar->hide();
    editorFindBar_ = findBar;

    welcomePage_ = new QWidget(editorStack_);
    welcomePage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    welcomePage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    auto* welcomeLayout = new QVBoxLayout(welcomePage_);
    welcomeLayout->setContentsMargins(12, 8, 12, 12);
    welcomeLayout->setSpacing(8);
    welcomeEmptyHintLabel_ = new QLabel(UiText::text(QStringLiteral("metadata.empty_hint")), welcomePage_);
    welcomeEmptyHintLabel_->setFont(uiAccentFont(11));
    welcomeEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    welcomeLayout->addWidget(welcomeEmptyHintLabel_, 0, Qt::AlignLeft | Qt::AlignTop);
    welcomeLayout->addStretch(1);

    metadataPage_ = new QWidget(editorStack_);
    metadataPage_->setObjectName("MetadataPage");
    metadataPage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    metadataPage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    auto* metadataLayout = new QVBoxLayout(metadataPage_);
    metadataLayout->setContentsMargins(12, 8, 12, 12);
    metadataLayout->setSpacing(8);

    auto* metadataCard = new QFrame(metadataPage_);
    metadataCard_ = metadataCard;
    metadataCard->setObjectName("MetadataCard");
    auto* metadataCardLayout = new QVBoxLayout(metadataCard);
    metadataCardLayout->setContentsMargins(14, 12, 14, 14);
    metadataCardLayout->setSpacing(12);

    auto* infoTitle = new QLabel(UiText::text(QStringLiteral("metadata.information")), metadataPage_);
    infoTitle->setObjectName("SectionTitle");
    infoTitle->setFont(uiAccentFont(12));
    metadataCardLayout->addWidget(infoTitle);

    auto* metadataForm = new QFormLayout();
    metadataForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    metadataForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    metadataForm->setHorizontalSpacing(8);
    metadataForm->setVerticalSpacing(10);
    titleEdit_ = new QLineEdit(metadataPage_);
    artistEdit_ = new QLineEdit(metadataPage_);
    auto* designerLineEdit = new LeftPlaceholderLineEdit(metadataPage_);
    designerLineEdit->setLeftPlaceholderText("&des=");
    designerEdit_ = designerLineEdit;
    titleEdit_->setPlaceholderText("&title=");
    artistEdit_->setPlaceholderText("&artist=");
    designerEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const auto makeMetadataFieldLabel = [this](const QString& text) {
        auto* label = new QLabel(text, metadataPage_);
        label->setObjectName("MetadataFieldLabel");
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setMinimumWidth(46);
        return label;
    };
    // Title and artist each get their own "read from MP3" button on the
    // far right of the matching row. Each button only fills its own field so
    // users who only need one of the two values aren't forced to accept
    // the other.
    auto* titleWrap = new QWidget(metadataPage_);
    auto* titleWrapLayout = new QHBoxLayout(titleWrap);
    titleWrapLayout->setContentsMargins(0, 0, 0, 0);
    titleWrapLayout->setSpacing(6);
    titleWrapLayout->addWidget(titleEdit_, 1);
    auto* readTitleButton = new QToolButton(metadataPage_);
    readTitleButton->setText(UiText::text(QStringLiteral("metadata.read_from_mp3")));
    readTitleButton->setToolTip(UiText::text(QStringLiteral("metadata.choose_an_mp3_and_pull")));
    connect(readTitleButton, &QToolButton::clicked, this, &MainWindow::onReadTitleFromTrack);
    titleWrapLayout->addWidget(readTitleButton, 0, Qt::AlignRight);

    auto* artistWrap = new QWidget(metadataPage_);
    auto* artistWrapLayout = new QHBoxLayout(artistWrap);
    artistWrapLayout->setContentsMargins(0, 0, 0, 0);
    artistWrapLayout->setSpacing(6);
    artistWrapLayout->addWidget(artistEdit_, 1);
    auto* readArtistButton = new QToolButton(metadataPage_);
    readArtistButton->setText(UiText::text(QStringLiteral("metadata.read_from_mp3")));
    readArtistButton->setToolTip(UiText::text(QStringLiteral("metadata.choose_an_mp3_and_pull_2")));
    connect(readArtistButton, &QToolButton::clicked, this, &MainWindow::onReadArtistFromTrack);
    artistWrapLayout->addWidget(readArtistButton, 0, Qt::AlignRight);

    // The top `&des` is the chart-wide / fallback designer. Per-difficulty
    // names (`&des_1..7`) and the "all difficulties share one designer" toggle
    // are managed from a dialog opened by the button on the right of this row —
    // the difficulty header no longer carries a designer field, so nothing
    // here has to stay in sync with another page.
    auto* designerWrap = new QWidget(metadataPage_);
    auto* designerWrapLayout = new QHBoxLayout(designerWrap);
    designerWrapLayout->setContentsMargins(0, 0, 0, 0);
    designerWrapLayout->setSpacing(6);
    designerWrapLayout->addWidget(designerEdit_, 1);
    auto* manageDesignersButton = new QToolButton(metadataPage_);
    manageDesignersButton->setText(UiText::text(QStringLiteral("metadata.manage_per_difficulty_designers")));
    manageDesignersButton->setToolTip(UiText::text(QStringLiteral("metadata.set_each_difficulty_designer")));
    connect(manageDesignersButton, &QToolButton::clicked, this, &MainWindow::onManagePerDifficultyDesigners);
    designerWrapLayout->addWidget(manageDesignersButton, 0, Qt::AlignRight);

    // Cover-extraction row. The label is intentionally short ("曲绘") so the
    // form column stays compact; the button text carries the action and the
    // tooltip spells out the bg.jpg destination.
    auto* coverWrap = new QWidget(metadataPage_);
    auto* coverWrapLayout = new QHBoxLayout(coverWrap);
    coverWrapLayout->setContentsMargins(0, 0, 0, 0);
    coverWrapLayout->setSpacing(6);
    auto* extractCoverButton = new QToolButton(metadataPage_);
    extractCoverButton->setText(UiText::text(QStringLiteral("metadata.read_from_mp3")));
    extractCoverButton->setToolTip(UiText::text(QStringLiteral("metadata.choose_an_mp3_and_write")));
    connect(extractCoverButton, &QToolButton::clicked, this, &MainWindow::onExtractBackgroundFromTrack);
    coverWrapLayout->addWidget(extractCoverButton, 0, Qt::AlignLeft);
    coverWrapLayout->addStretch(1);

    metadataForm->addRow(makeMetadataFieldLabel(UiText::text(QStringLiteral("metadata.field.title"))), titleWrap);
    metadataForm->addRow(makeMetadataFieldLabel(UiText::text(QStringLiteral("metadata.field.artist"))), artistWrap);
    metadataForm->addRow(makeMetadataFieldLabel(UiText::text(QStringLiteral("metadata.field.des"))), designerWrap);
    metadataForm->addRow(makeMetadataFieldLabel(UiText::text(QStringLiteral("metadata.field.cover"))), coverWrap);
    metadataCardLayout->addLayout(metadataForm);

    auto* extraMetadataLabel = new QLabel(UiText::text(QStringLiteral("metadata.other_fields")), metadataPage_);
    extraMetadataLabel->setObjectName("SectionTitle");
    extraMetadataLabel->setFont(uiAccentFont(11));
    metadataCardLayout->addWidget(extraMetadataLabel);
    const auto installCtrlEnterLineBreakShortcut = [](QTextEdit* textEdit) {
        if (textEdit == nullptr) {
            return;
        }
        const auto insertLineBreak = [textEdit]() {
            if (textEdit->isReadOnly()) {
                return;
            }
            QTextCursor cursor = textEdit->textCursor();
            cursor.beginEditBlock();
            cursor.insertBlock(cursor.blockFormat(), cursor.charFormat());
            cursor.endEditBlock();
            textEdit->setTextCursor(cursor);
        };
        for (const QKeySequence& shortcutKey : {
                 QKeySequence(Qt::CTRL | Qt::Key_Return),
                 QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
            auto* shortcut = new QShortcut(shortcutKey, textEdit);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            QObject::connect(shortcut, &QShortcut::activated, textEdit, insertLineBreak);
        }
    };
    metadataExtraEdit_ = new QTextEdit(metadataPage_);
    metadataExtraEdit_->setAcceptRichText(false);
    metadataExtraEdit_->setFont(editorFont(editorTextFontPointSize_));
    metadataExtraEdit_->setLineWrapMode(QTextEdit::WidgetWidth);
    metadataExtraEdit_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    metadataExtraEdit_->setPlaceholderText("&dummy=...");
    // Plain QTextEdit, so it does not get PlainCodeEditor's ctor install: give
    // it the same macOS drag-autoscroll takeover, or a drag-select that leaves
    // the viewport strobes the selection here too.
    miacode::ui::installAdoptedSurfaceDragAutoScroll(metadataExtraEdit_);
    installCtrlEnterLineBreakShortcut(metadataExtraEdit_);
    if (QScrollBar* vbar = metadataExtraEdit_->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    if (QScrollBar* hbar = metadataExtraEdit_->horizontalScrollBar()) {
        hbar->setStyleSheet(modernScrollBarStyle());
    }
    metadataCardLayout->addWidget(metadataExtraEdit_, 1);
    metadataBracketHighlighter_ = new BracketScopeHighlighter(metadataExtraEdit_->document());
    applyEditorTextFontSize(editorTextFontPointSize_, false);
    metadataEmptyHintLabel_ = new QLabel(UiText::text(QStringLiteral("metadata.empty_hint")), metadataPage_);
    metadataEmptyHintLabel_->setFont(uiAccentFont(11));
    metadataEmptyHintLabel_->setStyleSheet("color: #6A7890; background: transparent; padding-left: 6px;");
    metadataEmptyHintLabel_->hide();
    metadataLayout->addWidget(metadataEmptyHintLabel_, 0, Qt::AlignLeft | Qt::AlignTop);
    metadataLayout->addWidget(metadataCard, 1);

    // "延迟与偏移校准" entry card (L-A migration): the latency page lost its
    // sidebar item and is now reached from here. The card carries a live
    // BPM/offset summary (refreshed by populateMetadataPage) and a button
    // that runs the full switchToLatencyField entry semantics (bottom-tab ON,
    // playhead preserved).
    auto* latencyEntryCard = new QFrame(metadataPage_);
    latencyEntryCard->setObjectName("MetadataCard");
    auto* latencyEntryLayout = new QVBoxLayout(latencyEntryCard);
    latencyEntryLayout->setContentsMargins(14, 12, 14, 14);
    latencyEntryLayout->setSpacing(8);
    auto* latencyEntryTitle = new QLabel(
        UiText::text(QStringLiteral("metadata.latency_card.title")), metadataPage_);
    latencyEntryTitle->setObjectName("SectionTitle");
    latencyEntryTitle->setFont(uiAccentFont(12));
    latencyEntryLayout->addWidget(latencyEntryTitle);
    auto* latencyEntryRow = new QHBoxLayout();
    latencyEntryRow->setSpacing(6);
    latencyEntrySummaryLabel_ = new QLabel(metadataPage_);
    latencyEntrySummaryLabel_->setObjectName("MetadataFieldLabel");
    latencyEntrySummaryLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    latencyEntryRow->addWidget(latencyEntrySummaryLabel_, 1);
    auto* openLatencyPageButton = new QToolButton(metadataPage_);
    openLatencyPageButton->setText(UiText::text(QStringLiteral("metadata.latency_card.open")));
    openLatencyPageButton->setToolTip(UiText::text(QStringLiteral("metadata.open_the_latency_settings_page")));
    connect(openLatencyPageButton, &QToolButton::clicked, this, [this]() {
        switchToLatencyField();
    });
    latencyEntryRow->addWidget(openLatencyPageButton, 0, Qt::AlignRight);
    latencyEntryLayout->addLayout(latencyEntryRow);
    metadataLayout->addWidget(latencyEntryCard, 0);

    chartPage_ = new QWidget(editorStack_);
    chartPage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* chartLayout = new QVBoxLayout(chartPage_);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(0);
    // chartPage_ stays an (empty) stack page: seven call sites still read
    // editorStack_->currentWidget() to tell which field is showing, and the
    // export page needed a placeholder for the same reason. What it used to
    // hold — the chart editor beside the 复制区 panel — is QML now, and the
    // 复制区 had no v2 entry point at all.

    editorStack_->addWidget(welcomePage_);
    editorStack_->addWidget(metadataPage_);
    ui_.latencyPlaceholderPage_ = new QWidget(this);
    editorStack_->addWidget(ui_.latencyPlaceholderPage_);
    ui_.exportPlaceholderPage_ = new QWidget(this);
    editorStack_->addWidget(ui_.exportPlaceholderPage_);
    // Borrowed from the application assembly. A second instance here would mean
    // a second dialog host and a second progress surface.
    ui_.uiRequests_ = &applicationServices_.uiRequests();
    ui_.jobProgress_ = &applicationServices_.jobProgress();
    // The export worker runs out of process, so it cannot poll the cancel flag
    // at checkpoints the way an in-process job does. Route the shell's cancel
    // to it, but only while the job on the surface is actually the export's.
    connect(ui_.jobProgress_, &miacode::v2::JobProgressService::cancellationRequested,
            this, [this](quint64 token) {
                if (exportSection_ != nullptr && token == videoExportJobToken_
                    && videoExportJobToken_ != 0) {
                    exportSection_->cancelVideoExportWorker();
                }
            });
    ui_.qmlExportSession_ = new QmlExportSession(
        applicationServices_.shellNotifications(), applicationServices_.uiRequests(),
        applicationServices_.jobProgress(), applicationServices_.previewAppearance(),
        applicationServices_.exportEngineSlot(), applicationServices_.previewSurfaceSlot(),
        this);
    applicationServices_.setExportPageSession(ui_.qmlExportSession_);
    editorStack_->addWidget(chartPage_);
    centralLayout->addWidget(editorStack_, 1);
    if (editorFindBar_ != nullptr) {
        editorFindBar_->raise();
    }
    logStartupStage("editor_stack_ready");

    auto* outlineDock = new QDockWidget("Fields", this);
    outlineDock->setObjectName("OutlineDock");
    outlineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    outlineDock_ = outlineDock;
    auto* outlineTitle = new QWidget(outlineDock);
    outlineTitle->setFixedHeight(0);
    outlineDock->setTitleBarWidget(outlineTitle);
    outlineList_ = new QListWidget(outlineDock);
    outlineList_->setUniformItemSizes(false);
    outlineList_->setIconSize(QSize(14, 14));
    outlineList_->setSpacing(2);
    outlineList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outlineList_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    if (auto* vbar = outlineList_->verticalScrollBar()) {
        vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
    }
    outlineList_->setTextElideMode(Qt::ElideRight);
    outlineList_->setFont(uiAccentFont(11));
    outlineList_->setItemDelegate(new OutlineItemDelegate(outlineList_));
    // Inline bookmark rename is started programmatically (double-click /
    // context menu → editItem); automatic edit triggers stay off so plain
    // clicks never open an editor.
    outlineList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    outlineList_->setStyleSheet(UiTheme::outlineListStyleSheet());
    auto* outlineDockShell = new QWidget(outlineDock);
    auto* outlineDockShellLayout = new QHBoxLayout(outlineDockShell);
    outlineDockShellLayout->setContentsMargins(0, 0, 0, 0);
    outlineDockShellLayout->setSpacing(0);
    outlineDockShellLayout->addWidget(outlineList_, 1);
    outlineCollapseButton_ = new QToolButton(outlineDockShell);
    outlineCollapseButton_->setFocusPolicy(Qt::NoFocus);
    outlineCollapseButton_->setCursor(Qt::PointingHandCursor);
    outlineCollapseButton_->setFixedWidth(20);
    outlineCollapseButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    outlineCollapseButton_->setFont(uiAccentFont(10, QFont::Bold));
    outlineCollapseButton_->setStyleSheet(outlineCollapseButtonStyleSheet());
    outlineDockShellLayout->addWidget(outlineCollapseButton_, 0);
    outlineDock->setWidget(outlineDockShell);
    outlineList_->setMouseTracking(true);
    outlineList_->viewport()->setMouseTracking(true);
    connect(outlineCollapseButton_, &QToolButton::clicked, this, [this]() {
        windowSection_->setOutlineDockCollapsed(!outlineDockCollapsed_);
    });
    connect(outlineList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* current) {
        if (current == nullptr) {
            return;
        }
        const QString kind = current->data(Qt::UserRole).toString();
        const int difficultyId = current->data(Qt::UserRole + 1).toInt();
        const auto clearOutlineSelection = [this]() {
            if (outlineList_ == nullptr) {
                return;
            }
            QSignalBlocker blocker(outlineList_);
            outlineList_->setCurrentItem(nullptr);
            outlineList_->clearSelection();
            if (outlineList_->selectionModel() != nullptr) {
                outlineList_->selectionModel()->clearCurrentIndex();
                outlineList_->selectionModel()->clearSelection();
            }
            outlineList_->viewport()->update();
        };
        const auto clickedDifficultyFoldChevron = [this, current]() {
            if (outlineList_ == nullptr || current == nullptr) {
                return false;
            }
            if (current->data(kOutlineItemKindRole).toString() != QLatin1String("difficulty_chart")
                || current->data(kOutlineItemBookmarkCountRole).toInt() <= 0) {
                return false;
            }
            // Icon-only sidebar paints no chevron — the whole row switches
            // the difficulty there.
            const int listWidth = outlineList_->width();
            if (listWidth > 0 && listWidth < OutlineItemDelegate::kIconOnlyThreshold) {
                return false;
            }
            const QRect rowRect = outlineList_->visualItemRect(current);
            if (!rowRect.isValid()) {
                return false;
            }
            const QPoint pos = outlineList_->viewport()->mapFromGlobal(QCursor::pos());
            return pos.x() <= rowRect.left() + OutlineItemDelegate::kDifficultyFoldHitZone;
        };
        if (kind == "metadata") {
            activeOutlineKey_ = "metadata";
            if (switchToMetadataField() && titleEdit_ != nullptr) {
                titleEdit_->setFocus();
            }
            return;
        }
        if (kind == "export") {
            activeOutlineKey_ = "export";
            switchToExportField();
            return;
        }
        if (kind == "bookmark") {
            const int bookmarkDifficultyId = current->data(kOutlineItemDifficultyRole).toInt();
            const int bookmarkLine = current->data(kOutlineItemLineRole).toInt();
            if (SimaiDocument::isDifficultyId(bookmarkDifficultyId) && bookmarkDifficultyId != activeDifficultyId_) {
                activeOutlineKey_ = "chart";
                // NOTE: switching rebuilds the sidebar — `current` dangles from
                // here on; only the role values read above may be used.
                if (!switchToDifficultyField(bookmarkDifficultyId)) {
                    return;
                }
            }
            const double bookmarkSecond = timelineSecondForCursor(bookmarkLine, 1);
            if (bookmarkSecond >= 0.0 && qIsFinite(bookmarkSecond)) {
                navigateTimelineToSecond(bookmarkSecond, true);
            }
            jumpToLocation(bookmarkLine, 1);
            clearOutlineSelection();
            return;
        }
        if (kind == "add") {
            QMenu menu(outlineList_);
            menu.setFont(uiAccentFont(10));
            styleRoundedMenu(menu);
            for (int id = 1; id <= 7; ++id) {
                if (document_.difficulty(id) != nullptr) {
                    continue;
                }
                auto* action = new QWidgetAction(&menu);
                auto* button = new QToolButton(&menu);
                button->setAutoRaise(true);
                button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                button->setIcon(makeDifficultyBadgeIcon(id));
                button->setIconSize(QSize(14, 14));
                button->setText(SimaiDocument::difficultyName(id));
                button->setFont(uiAccentFont(10));
                button->setCursor(Qt::PointingHandCursor);
                const UiTheme::Colors& c = UiTheme::colors();
                button->setStyleSheet(
                    QStringLiteral(
                        "QToolButton {"
                        " color: %1;"
                        " background: transparent;"
                        " border: none;"
                        " padding: 6px 20px 6px 12px;"
                        " text-align: left;"
                        "}"
                        "QToolButton:hover {"
                        " background: %2;"
                        " border-radius: 6px;"
                        "}"
                    )
                        .arg(c.textPrimary.name(QColor::HexRgb))
                        .arg(c.menuHoverBg.name(QColor::HexRgb))
                );
                connect(button, &QToolButton::clicked, &menu, [action, &menu]() {
                    action->trigger();
                    menu.close();
                });
                action->setDefaultWidget(button);
                menu.addAction(action);
                connect(action, &QAction::triggered, this, [this, id]() {
                    if (!maybeSaveCurrentFieldChanges()) {
                        rebuildFieldSidebar();
                        return;
                    }
                    SimaiDifficultyData& newDiff = document_.ensureDifficulty(id);
                    // Honour the "all difficulties share the same designer
                    // name" project preference at creation time: seed the
                    // fresh difficulty's designer with the top-level &des
                    // so the user doesn't have to retype it. The runtime
                    // sync (applyCurrentFieldToDocument) only kicks in on
                    // edits — without this seed, a freshly-added
                    // difficulty would persist with an empty designer
                    // until the user manually broadcasts again.
                    if (unifiedDesignerEnabled_ && newDiff.designer.isEmpty()) {
                        newDiff.designer = document_.designer;
                    }
                    documentDirty_ = true;
                    activeOutlineKey_ = "chart";
                    updateDirtyState();
                    switchToDifficultyField(id);
                });
            }
            if (!menu.isEmpty()) {
                const QRect rowRect = outlineList_->visualItemRect(current);
                menu.exec(miacode::ui::mapWidgetPointToGlobal(
                    outlineList_->viewport(), rowRect.bottomRight()));
            }
            rebuildFieldSidebar();
            return;
        }
        if (kind == "toolbox") {
            if (toolboxMenu_ != nullptr) {
                const QRect rowRect = outlineList_->visualItemRect(current);
                const QPoint popupPos = miacode::ui::mapWidgetPointToGlobal(
                    outlineList_->viewport(),
                    QPoint(rowRect.right(), rowRect.top() + rowRect.height() / 2)
                );
                toolboxMenu_->exec(popupPos);
            }
            rebuildFieldSidebar();
            return;
        }
        if (SimaiDocument::isDifficultyId(difficultyId)) {
            if (clickedDifficultyFoldChevron()) {
                if (documentSection_ != nullptr) {
                    documentSection_->setBookmarkGroupExpanded(
                        difficultyId,
                        !documentSection_->isBookmarkGroupExpanded(difficultyId));
                }
                return;
            }
            activeOutlineKey_ = "chart";
            switchToDifficultyField(difficultyId);
        }
    });
    connect(outlineList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* current) {
        if (current == nullptr || outlineList_ == nullptr) {
            return;
        }
        const QString kind = current->data(kOutlineItemKindRole).toString();
        if (kind != QLatin1String("bookmark")) {
            return;
        }
        // Double-click = inline rename (the old jump-to-timeline action moved
        // to the context menu's "跳到时间轴位置").
        outlineList_->editItem(current);
    });
    outlineList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(outlineList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (outlineList_ == nullptr) {
            return;
        }
        Q_UNUSED(pos);
        QWidget* viewport = outlineList_->viewport();
        if (viewport == nullptr) {
            return;
        }
        // On the macOS QuickShell surface, the event's local point can still
        // be relative to the orphan NSPanel.  Hit-test from the real cursor
        // position through the adopted surface instead.
        const QPoint itemPos = miacode::ui::mapGlobalPointToWidget(viewport, QCursor::pos());
        QListWidgetItem* item = outlineList_->itemAt(itemPos);
        if (item == nullptr) {
            return;
        }
        const QString kind = item->data(kOutlineItemKindRole).toString();
        if (kind == QLatin1String("bookmark")) {
            QMenu menu(outlineList_);
            menu.setFont(uiAccentFont(10));
            styleRoundedMenu(menu);
            const int bookmarkDifficultyId = item->data(kOutlineItemDifficultyRole).toInt();
            const int line = item->data(kOutlineItemLineRole).toInt();
            QAction* renameAction = menu.addAction(UiText::text(QStringLiteral("metadata.rename")));
            connect(renameAction, &QAction::triggered, this, [this, bookmarkDifficultyId, line]() {
                if (documentSection_ != nullptr) {
                    documentSection_->revealBookmarkInSidebar(bookmarkDifficultyId, line, true);
                }
            });
            QAction* timelineAction = menu.addAction(UiText::text(QStringLiteral("metadata.jump_to_timeline_position")));
            connect(timelineAction, &QAction::triggered, this, [this, bookmarkDifficultyId, line]() {
                if (SimaiDocument::isDifficultyId(bookmarkDifficultyId) && bookmarkDifficultyId != activeDifficultyId_) {
                    activeOutlineKey_ = "chart";
                    if (!switchToDifficultyField(bookmarkDifficultyId)) {
                        return;
                    }
                }
                const double bookmarkSecond = timelineSecondForCursor(line, 1);
                if (bookmarkSecond >= 0.0 && qIsFinite(bookmarkSecond)) {
                    navigateTimelineToSecond(bookmarkSecond, true);
                } else {
                    jumpToLocation(line, 1);
                }
            });
            menu.exec(miacode::ui::mapWidgetPointToGlobal(viewport, itemPos));
            return;
        }
        const int difficultyId = item->data(kOutlineItemDifficultyRole).toInt();
        if (!SimaiDocument::isDifficultyId(difficultyId) || document_.difficulty(difficultyId) == nullptr) {
            return;
        }
        QMenu menu(outlineList_);
        menu.setFont(uiAccentFont(10));
        styleRoundedMenu(menu);
        QAction* deleteAction = menu.addAction(
            UiText::text(QStringLiteral("metadata.delete_1")).arg(SimaiDocument::difficultyName(difficultyId))
        );
        connect(deleteAction, &QAction::triggered, this, [this, difficultyId]() {
            deleteDifficultyField(difficultyId);
        });
        menu.exec(miacode::ui::mapWidgetPointToGlobal(viewport, itemPos));
    });

    toolboxMenu_ = new QMenu(outlineList_);
    styleRoundedMenu(*toolboxMenu_);
    const auto openToolboxUrl = [](const QString& url) {
        QDesktopServices::openUrl(QUrl(url));
    };

    // BPM & Latency was removed from the toolbox per the toolbox revamp; the
    // latencyDetectorAction_ stays alive in the top Tools menu (wired in
    // FrameBootstrap::setup) so the feature itself is still reachable.

    if (prependTrackSilenceAction_ != nullptr
        || prependPvBlackAction_ != nullptr
        || compressBackgroundVideoAction_ != nullptr
        || convertTrackTo44100HzAction_ != nullptr) {
        // The four audio/video tools now open in a popup dialog (one button
        // + a one-line description each) instead of a hover submenu — see
        // DialogsSection::onMediaProcessingTools(), which calls the same
        // handlers the old submenu actions were wired to.
        QAction* mediaProcessingAction = toolboxMenu_->addAction(
            UiText::text(QStringLiteral("media_tools.audio_video_processing"))
        );
        connect(mediaProcessingAction, &QAction::triggered, this, &MainWindow::onMediaProcessingTools);
    }

    if (normalizeWholeChartAction_ != nullptr) {
        toolboxMenu_->addAction(normalizeWholeChartAction_);
    }

    QAction* toolboxNetGroupEndAction = toolboxMenu_->addSeparator();

    QAction* toolboxOfficialChartMirrorAction = toolboxMenu_->addAction(
        UiText::text(QStringLiteral("menu.official_chart_mirror"))
    );
    connect(toolboxOfficialChartMirrorAction, &QAction::triggered, this, [openToolboxUrl]() {
        openToolboxUrl(QStringLiteral("https://www.maiviewer.net/"));
    });

    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    windowSection_->setOutlineDockCollapsed(false);
    logStartupStage("outline_ready");

    previewPanel_ = new QWidget(this);
    previewPanel_->setObjectName("PreviewPanel");
    previewPanel_->setStyleSheet(UiTheme::previewPanelStyleSheet());
    previewPanel_->setMinimumWidth(kEmbeddedPreviewPanelMinWidth);
    previewPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    previewCanvas_ = new PreviewRuntime(this);
    connect(previewCanvas_, &PreviewRuntime::touchPadAuthoringClicked, this, [this](const QString& pad, bool backtickSeparator) {
        if (editorSyncController_ != nullptr) {
            editorSyncController_->requestTouchPadAuthoring(pad, backtickSeparator);
        }
    });
    if (editorStack_ != nullptr) {
        connect(editorStack_, &QStackedWidget::currentChanged, this, [this](int) {
            applyEffectivePreviewOutlineVariantToCanvas();
        });
    }
    logStartupStage("preview_canvas_created");
    applyEffectivePreviewOutlineVariantToCanvas();
    applyPreviewSkinDirectoryToSurfaces();
    updatePreviewStageMediaPresentationMode(false);
    if (previewUsesStageMediaHostRoute()) {
        ensurePreviewStageMediaRouteInitialized();
    }
    logStartupStage("preview_skin_async_dispatched");
    previewCanvasFrame_ = new QFrame(previewPanel_);
    previewCanvasFrame_->setObjectName("PreviewCanvasFrame");
    previewCanvasFrame_->setMinimumSize(QSize(1, 1));
    previewCanvasFrame_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_ = new QWidget(previewCanvasFrame_);
    previewCanvasContainer_->setMinimumSize(QSize(1, 1));
    previewCanvasContainer_->setFocusPolicy(Qt::StrongFocus);
    previewPanel_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_->hide();
    logStartupStage("preview_canvas_container_ready");

    previewControlCard_ = nullptr;
    previewControlsLayout_ = nullptr;
    stopPreviewButton_ = nullptr;
    pausePreviewButton_ = nullptr;
    previewSlider_ = nullptr;
    previewSpeedButton_ = nullptr;
    previewFullscreenButton_ = nullptr;

    auto* previewStatsCard = new QFrame(previewPanel_);
    previewStatsCard_ = previewStatsCard;
    previewStatsCard->setObjectName("PreviewStatsCard");
    previewStatsCard->setMinimumWidth(kPreviewControlStatsCardMinWidth);
    previewStatsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* previewStatsCardLayout = new QVBoxLayout(previewStatsCard);
    previewStatsCardLayout->setContentsMargins(8, 8, 8, 8);
    previewStatsCardLayout->setSpacing(0);

    auto* previewStats = new QFrame(previewStatsCard);
    previewStats->setObjectName("PreviewStats");
    auto* previewStatsLayout = new QGridLayout(previewStats);
    previewStatsGridLayout_ = previewStatsLayout;
    previewStatsLayout->setContentsMargins(2, 2, 2, 2);
    previewStatsLayout->setHorizontalSpacing(10);
    previewStatsLayout->setVerticalSpacing(6);

    const auto addStatsChip = [previewStats, previewStatsLayout](const QString& labelText) -> QLabel* {
        auto* label = new QLabel(labelText, previewStats);
        label->setObjectName("PreviewStatChip");
        label->setFont(uiMonoFont(10, QFont::DemiBold));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setFixedHeight(30);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        previewStatsLayout->addWidget(label);
        return label;
    };

    previewTapStatsLabel_ = addStatsChip("Tap    0/0");
    previewHoldStatsLabel_ = addStatsChip("Hold   0/0");
    previewSlideStatsLabel_ = addStatsChip("Slide  0/0");
    previewTouchStatsLabel_ = addStatsChip("Touch  0/0");
    previewBreakStatsLabel_ = addStatsChip("Break  0/0");
    previewTotalStatsLabel_ = addStatsChip("Total  0/0");
    previewTotalStatsLabel_->setObjectName("PreviewStatChipTotal");
    previewStatsChips_.clear();
    previewStatsChips_ << previewTapStatsLabel_
                       << previewHoldStatsLabel_
                       << previewSlideStatsLabel_
                       << previewTouchStatsLabel_
                       << previewBreakStatsLabel_
                       << previewTotalStatsLabel_;
    previewStatsCardLayout->addWidget(previewStats, 0);
    previewStatsCardLayout->addStretch(1);
    updatePreviewStatsLayoutMode();
    logStartupStage("preview_controls_and_stats_ready");

    previewSfxRuntime_ = new QtPreviewSfxRuntime(this);
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::commandCompleted,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                using namespace miacode::preview_audio;
                if (completion.kind != CommandKind::ReloadAssets
                    || completion.identity.sequence != state_.previewSfxRuntimePreparationSequence_
                    || !acceptsAssetCompletion(
                        state_.previewSfxRuntimePreparationAssetGeneration_, completion)) {
                    return;
                }
                state_.previewSfxRuntimePrepared_ = completion.success
                    && previewSfxRuntime_ != nullptr
                    && previewSfxRuntime_->audioEngineInitialized();
                state_.previewSfxRuntimePreparationAssetGeneration_ = 0;
                state_.previewSfxRuntimePreparationSequence_ = 0;
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::previewPrepared,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->handlePreviewAudioPrepared(completion);
                }
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::retainedPlaybackCompleted,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->handlePreviewRetainedPlaybackCompleted(completion);
                }
            });
    connect(previewSfxRuntime_,
            &QtPreviewSfxRuntime::previewPlaybackPaused,
            this,
            [this](const QtPreviewSfxRuntime::Completion& completion) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->handlePreviewRetainedPlaybackCompleted(completion);
                }
            });
    logStartupStage("preview_sfx_runtime_created");
#ifdef MIACODE_HAS_BASS_AUDIO
    // BASS-only on purpose. docs/audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md fixes the
    // device-change desync (问题 3) to the BASS transport's anchor model on Windows and
    // macOS. Linux runs MiniaudioPreviewAudioBackend, a different seek/clock
    // implementation with a separate, unproven report (问题 4), so auto-pausing there
    // would interrupt playback on no evidence.
    previewAudioDeviceWatcher_ = new PreviewAudioDeviceWatcher(this);
    previewAudioDeviceWatcher_->setDirectCutoffHandler(
        [runtime = previewSfxRuntime_](PreviewAudioDeviceWatcher::Change) {
            return runtime != nullptr
                ? runtime->requestDeviceChangeCutoff()
                : miacode::preview_audio::PreviewAudioDeviceCutoff{};
        });
    connect(previewAudioDeviceWatcher_,
            &PreviewAudioDeviceWatcher::deviceCutoffRequested,
            this,
            [this](const PreviewAudioDeviceWatcher::DeviceCutoff& cutoff) {
                if (timelineSection_ != nullptr) {
                    timelineSection_->applyPreviewAudioDeviceCutoff(cutoff);
                }
            });
    logStartupStage("preview_audio_device_watcher_created");
#endif
    connect(previewCanvas_, &PreviewRuntime::framePresented, this, [this]() {
        timelineSection_->handlePreviewStartupCanvasPresented();
        if (!qtPreviewPlaying_) {
            return;
        }
        if (previewCanvasUsesFrameSwappedPacing()) {
            const bool matchedRequest = qtPreviewAwaitingFrameSwap_;
            const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
            const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
            const qint64 waitNs =
                matchedRequest && qtPreviewAwaitingFrameSwapSinceNs_ >= 0
                    ? qMax<qint64>(0, nowNs - qtPreviewAwaitingFrameSwapSinceNs_)
                    : 0;
            qtPreviewDisplayRefreshFramePresentSeq_ += 1;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->noteDisplayRefreshFramePresentation(waitNs, matchedRequest);
            }
            if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
                const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
                if (!matchedRequest
                    || qtPreviewFramePacingDiagLastPresentLogMs_ < 0
                    || nowMs - qtPreviewFramePacingDiagLastPresentLogMs_ >= sampleMs) {
                    qtPreviewFramePacingDiagLastPresentLogMs_ = nowMs;
                    appendPreviewFramePacingDiagLog(
                        matchedRequest ? QStringLiteral("display_present") : QStringLiteral("display_orphan_present"),
                        QStringLiteral("request_seq=%1 present_seq=%2 wait_ms=%3 queued=%4")
                            .arg(qtPreviewDisplayRefreshFrameRequestSeq_)
                            .arg(qtPreviewDisplayRefreshFramePresentSeq_)
                            .arg(static_cast<double>(waitNs) / 1000000.0, 0, 'f', 3)
                            .arg(qtPreviewDisplayRefreshTickQueued_ ? 1 : 0)
                    );
                }
            }
            if (!matchedRequest) {
                return;
            }
            qtPreviewAwaitingFrameSwap_ = false;
            qtPreviewAwaitingFrameSwapSinceMs_ = -1;
            qtPreviewAwaitingFrameSwapSinceNs_ = -1;
            qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
            if (qtPreviewDisplayRefreshTickQueued_) {
                return;
            }
            qtPreviewDisplayRefreshTickQueued_ = true;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->noteDisplayRefreshQueuedTick();
            }
            // Call onQtPreviewTick synchronously inside the framePresented callback. Every extra
            // event-loop hop between a present and the next update() is ~1-3ms of latency, and
            // on systems where the render pipeline takes ~14-15ms per frame that latency pushes
            // completion past the next vsync boundary, doubling the effective cycle time.
            // The doc's advice against synchronous tick here was cautionary, not load-bearing —
            // the tick body completes in ~0.5ms and cannot re-enter this callback (update() only
            // schedules a render; the next framePresented fires on the next vsync).
            qtPreviewDisplayRefreshTickQueued_ = false;
            if (qtPreviewPlaying_
                && previewCanvasUsesFrameSwappedPacing()
                && !qtPreviewAwaitingFrameSwap_) {
                onQtPreviewTick();
            }
            return;
        }
        // Fixed interval mode: present-driven gate (doc section 4.1). Each present clears the
        // awaiting flag and runs the FPS gate synchronously — same reasoning as DisplayRefresh
        // branch above (avoid event-loop latency that costs vsync alignment).
        const bool matchedRequest = qtPreviewFixedAwaitingFrame_;
        const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
        const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
        const qint64 waitNs =
            matchedRequest && qtPreviewFixedAwaitingFrameSinceNs_ >= 0
                ? qMax<qint64>(0, nowNs - qtPreviewFixedAwaitingFrameSinceNs_)
                : 0;
        qtPreviewFixedGateFramePresentSeq_ += 1;
        if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
            const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
            if (!matchedRequest
                || qtPreviewFramePacingDiagLastPresentLogMs_ < 0
                || nowMs - qtPreviewFramePacingDiagLastPresentLogMs_ >= sampleMs) {
                qtPreviewFramePacingDiagLastPresentLogMs_ = nowMs;
                appendPreviewFramePacingDiagLog(
                    matchedRequest
                        ? QStringLiteral("fixed_gate_present")
                        : QStringLiteral("fixed_gate_orphan_present"),
                    QStringLiteral("request_seq=%1 present_seq=%2 wait_ms=%3 queued=%4")
                        .arg(qtPreviewFixedGateFrameRequestSeq_)
                        .arg(qtPreviewFixedGateFramePresentSeq_)
                        .arg(static_cast<double>(waitNs) / 1000000.0, 0, 'f', 3)
                        .arg(qtPreviewFixedFrameTickQueued_ ? 1 : 0)
                );
            }
        }
        if (!matchedRequest) {
            return;
        }
        qtPreviewFixedAwaitingFrame_ = false;
        qtPreviewFixedAwaitingFrameSinceMs_ = -1;
        qtPreviewFixedAwaitingFrameSinceNs_ = -1;
        if (qtPreviewFixedFrameTickQueued_) {
            return;
        }
        qtPreviewFixedFrameTickQueued_ = true;
        // Synchronous call — skip the event-loop hop. See the DisplayRefresh branch above for the
        // rationale. advanceFixedIntervalGateAfterPresent internally re-checks playing / mode /
        // awaiting-frame state, so running it straight from this lambda is safe even if another
        // queued event (e.g. pause) is sitting behind the current signal.
        qtPreviewFixedFrameTickQueued_ = false;
        advanceFixedIntervalGateAfterPresent();
    });
    logStartupStage("preview_runtime_connections_ready");
    logStartupStage("preview_runtime_ready");

    bottomTabs_ = new QTabWidget(central);
    bottomTabs_->installEventFilter(this);
    if (QTabBar* bottomTabBar = bottomTabs_->tabBar(); bottomTabBar != nullptr) {
        bottomTabBar->installEventFilter(this);
    }
    quickShellBottomTabsProxy_ = new QTabWidget(this);
    if (QTabBar* proxyTabBar = quickShellBottomTabsProxy_->tabBar(); proxyTabBar != nullptr) {
        proxyTabBar->installEventFilter(this);
    }
    timelineQuickStateBridge_ = new TimelineQuickStateBridge(this);
    timelineQuickStateBridge_->setHeaderLineNumberFont(timelineHeaderLineNumberFont());
    timelineQuickStateBridge_->setShowSlideTracks(true);
    timelineQuickStateBridge_->setSkinDirectory(resolvePreviewSkinDir());
    timelineQuickStateBridge_->setViewportLockEnabled(previewViewportLockEnabled_);
    timelineQuickStateBridge_->setFollowProgressEnabled(previewProgressFollowEnabled_);
    timelineQuickStateBridge_->setTimelineSyncEnabled(timelineSyncEnabled_);
    timelineQuickStateBridge_->setZoomWheelShortcuts(
        ShortcutRegistry::instance().shortcutTexts(
            QStringLiteral("timeline.zoom_in"),
            {QStringLiteral("Ctrl+WheelUp")}),
        ShortcutRegistry::instance().shortcutTexts(
            QStringLiteral("timeline.zoom_out"),
            {QStringLiteral("Ctrl+WheelDown")}));
    timelineSection_->refreshTimelineWaveformPhaseCompensation();
    // Phase-locked playback sampling for the timeline. Fires once per timeline frame from
    // TimelineQuickItem::bindRenderCadence (QQuickWindow::afterAnimating, GUI thread), so the
    // second we sample is the one that frame renders. qtPreviewTimelineTimer_ remains armed as
    // a watchdog behind this; see MainWindow.FrameBootstrapFinalize.cpp.
    connect(timelineQuickStateBridge_,
            &TimelineQuickStateBridge::renderCadenceTick,
            this,
            &MainWindow::onTimelineRenderCadenceTick);
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::zoomScaleChanged, this, [this](double) {
        savePortableState();
    });
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::waveformBrightnessChanged, this, [this](double) {
        savePortableState();
    });
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::measureLineBrightnessChanged, this, [this](double) {
        savePortableState();
    });
    connect(titleEdit_, &QLineEdit::textChanged, this, [this]() {
        markCurrentFieldDirty();
        updateWindowTitle();
    });
    connect(artistEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(designerEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    if (metadataExtraEdit_ != nullptr) {
        connect(metadataExtraEdit_->document(), &QTextDocument::contentsChange, this, [this](int, int charsRemoved, int charsAdded) {
            if (suppressTextDirtyTracking_) {
                return;
            }
            if (charsRemoved == 0 && charsAdded == 0) {
                return;
            }
            QTimer::singleShot(0, this, [this]() {
                if (!suppressTextDirtyTracking_) {
                    markCurrentFieldDirty();
                }
            });
        });
    }
    connect(difficultyLevelEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(firstEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(difficultyDesignerEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    // Offset only repositions notes relative to the audio, so a live reflow on
    // commit (Enter / focus-out) is enough — no need to thrash the timeline on
    // every keystroke. parsedFirstSeconds() already reads the live field text,
    // so refreshTimelineMetadata() reflows using the just-typed value.
    connect(firstEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (hasActiveDifficulty()) {
            refreshTimelineMetadata();
        }
    });

    outputView_ = nullptr;

    errorList_ = new QListWidget(bottomTabs_);
    errorList_->setFont(uiOutputFont());
    errorList_->setUniformItemSizes(false);
    errorList_->setWordWrap(true);
    errorList_->setTextElideMode(Qt::ElideNone);
    errorList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (QScrollBar* vbar = errorList_->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    errorList_->setContextMenuPolicy(Qt::CustomContextMenu);
    errorList_->viewport()->installEventFilter(this);
    connect(errorList_, &QListWidget::itemActivated, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::itemClicked, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showIssueListContextMenu(errorList_, pos, false);
    });
    bottomTabs_->addTab(
        errorList_,
        UiText::text(QStringLiteral("window.syntax"))
    );

    muriList_ = new QListWidget(bottomTabs_);
    muriList_->setFont(uiOutputFont());
    muriList_->setUniformItemSizes(false);
    muriList_->setWordWrap(true);
    muriList_->setTextElideMode(Qt::ElideNone);
    muriList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (QScrollBar* vbar = muriList_->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    muriList_->setContextMenuPolicy(Qt::CustomContextMenu);
    muriList_->viewport()->installEventFilter(this);
    connect(muriList_, &QListWidget::itemActivated, this, &MainWindow::onMuriItemActivated);
    connect(muriList_, &QListWidget::itemClicked, this, &MainWindow::onMuriItemActivated);
    connect(muriList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showIssueListContextMenu(muriList_, pos, true);
    });
    bottomTabs_->addTab(
        muriList_,
        UiText::text(QStringLiteral("window.muri"))
    );
    connect(bottomTabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (!quickShellBottomTabsProxyActive()) {
            if (bottomTabs_->currentWidget() == errorList_) {
                currentBottomTabsTabId_ = BottomTabsTabId::Validation;
            } else if (bottomTabs_->currentWidget() == muriList_) {
                currentBottomTabsTabId_ = BottomTabsTabId::Muri;
            }
        }
        if (validationSection_ != nullptr && bottomTabs_->currentWidget() == muriList_) {
            validationSection_->flushPendingMuriDiagnosticsPanelRefresh();
        }
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    });
    connect(quickShellBottomTabsProxy_, &QTabWidget::currentChanged, this, [this](int) {
        if (quickShellBottomTabsProxy_->currentWidget() == errorList_) {
            currentBottomTabsTabId_ = BottomTabsTabId::Validation;
        } else if (quickShellBottomTabsProxy_->currentWidget() == muriList_) {
            currentBottomTabsTabId_ = BottomTabsTabId::Muri;
        }
        if (validationSection_ != nullptr && quickShellBottomTabsProxy_->currentWidget() == muriList_) {
            validationSection_->flushPendingMuriDiagnosticsPanelRefresh();
        }
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    });

    windowSection_->updateBottomTabsDeviceHeight();
    logStartupStage("timeline_and_tabs_ready");

    previewLeftColumn_ = new QWidget(this);
    // Content-column floor = export-page design-width budget (spec). Mirrors the
    // QuickShell content WindowContainer's Layout.minimumWidth.
    previewLeftColumn_->setMinimumWidth(miacode::window_parity::kWorkspaceContentMinWidth);
    previewLeftColumn_->setProperty("baseMinimumWidth", previewLeftColumn_->minimumWidth());
    previewLeftColumn_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* leftColumnLayout = new QVBoxLayout(previewLeftColumn_);
    leftColumnLayout->setContentsMargins(0, 0, 0, 0);
    leftColumnLayout->setSpacing(0);
    leftColumnLayout->addWidget(central, 1);
    leftColumnLayout->addWidget(bottomTabs_, 0);

    workspaceSplitter_ = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(0);
    workspaceSplitter_->addWidget(previewLeftColumn_);
    workspaceSplitter_->addWidget(previewPanel_);
    workspaceSplitter_->setStretchFactor(0, 1);
    workspaceSplitter_->setStretchFactor(1, 0);
    if (QSplitterHandle* handle = workspaceSplitter_->handle(1); handle != nullptr) {
        handle->setEnabled(false);
        handle->hide();
    }
    setCentralWidget(workspaceSplitter_);
    syncEditorHeaderMinimumWidth();
    applyWorkspacePanelArrangement();
    updatePreviewWorkspaceLayout();
    logStartupStage("workspace_and_central_widget_ready");

    finishFrameBootstrap(toolBar, logStartupStage);
}

miacode::latency::LatencySandboxController* MainWindow::latencySandboxController() const
{
    return latencySandboxController_.get();
}
