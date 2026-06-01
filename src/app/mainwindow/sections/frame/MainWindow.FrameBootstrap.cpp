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
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/latency/LatencyDetectionPage.h"
#include "tools/latency/LatencySandboxController.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

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

MainWindow::MainWindow(bool quickShellBootstrapMode, QWidget* parent)
    : QMainWindow(parent)
{
    QElapsedTimer startupStageTimer;
    startupStageTimer.start();
    qint64 startupLastMs = 0;
    const auto logStartupStage = [&](const QString& stageName) {
        const qint64 nowMs = startupStageTimer.elapsed();
        const qint64 deltaMs = nowMs - startupLastMs;
        startupLastMs = nowMs;
        appendStartupTimingStage(QString("mainwindow/%1").arg(stageName), nowMs, deltaMs);
    };

    quickShellBootstrapMode_ = quickShellBootstrapMode;
    timelineWidgetlessQuickRoute_ = quickShellBootstrapMode_;

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
    if (QMenuBar* topMenuBar = menuBar(); topMenuBar != nullptr) {
        topMenuBar->setNativeMenuBar(false);
    }

    // Beta20-fix — unified all top menus to the `Name(&L)` mnemonic
    // suffix style for both English and Chinese (was: English used the
    // bare `&L` underline form `&File`/`&Edit` while Transform used the
    // `Name(&L)` suffix form, producing visually inconsistent menu
    // labels). The trailing `(L)` parens render in both locales which
    // matches the existing Chinese convention.
    auto* fileMenu = menuBar()->addMenu(uiText("menu.file", "File(&F)"));
    auto* editMenu = menuBar()->addMenu(UiText::isChineseUi() ? QStringLiteral("编辑(&E)") : QStringLiteral("Edit(&E)"));
    auto* toolsMenu = menuBar()->addMenu(uiText("menu.tools", "Tools(&T)"));
    // Beta20-fix — Transform menu renamed to "Modify" / "调整" in both
    // languages so the Alt-T mnemonic is unambiguous for the Tools
    // menu. Picked "Modify" (Alt+M) over "Transform(&R)" because the
    // user requested a synonym, not just a different mnemonic letter.
    // The Chinese key in UiText.cpp likewise uses 调整(&M).
    auto* transformMenu = menuBar()->addMenu(uiText("menu.transform", "Modify(&M)"));
    auto* previewMenu = menuBar()->addMenu(UiText::isChineseUi() ? QStringLiteral("预览(&P)") : QStringLiteral("Preview(&P)"));
    auto* helpMenu = menuBar()->addMenu(uiText("menu.help", "Help(&H)"));
    styleRoundedMenu(*fileMenu);
    styleRoundedMenu(*editMenu);
    styleRoundedMenu(*toolsMenu);
    styleRoundedMenu(*transformMenu);
    styleRoundedMenu(*previewMenu);
    styleRoundedMenu(*helpMenu);

    auto* toolBar = addToolBar("Main");
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    setupMenusAndActions(fileMenu, editMenu, transformMenu, previewMenu, helpMenu);
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
        QAction* batchExportAction = toolsMenu->addAction(uiText("action.batch_export", "Batch Export"));
        connect(batchExportAction, &QAction::triggered, this, &MainWindow::onBatchExportPreviewVideo);
    }
    const QList<QAction*> editActions = editMenu->actions();
    if (!editActions.isEmpty() && editActions.constLast()->isSeparator()) {
        editMenu->removeAction(editActions.constLast());
    }
    logStartupStage("menus_and_actions_ready");

    auto* editor = new PlainCodeEditor(this);
    const QFont codeFont = editorFont();
    editorTextFontPointSize_ = qBound(kEditorTextFontSizeMin, codeFont.pointSize(), kEditorTextFontSizeMax);
    editor->setFont(codeFont);
    editor->setBlockSpacingPixels(blockSpacingPixelsForPointSize(editorTextFontPointSize_, editorLineSpacingFactor_));
    editor->refreshLineNumberAreaLayout();
    editor->setLineWrapMode(QTextEdit::WidgetWidth);
    editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    editor->setPlainText(QString());
    editor->setBatchTransformActions({
        transformMirrorLeftRightAction_,
        transformMirrorUpDownAction_,
        transformRotate180Action_,
        transformRotate45CounterClockwiseAction_,
        transformRotate45ClockwiseAction_,
        transformRaiseSubdivisionAction_,
        transformLowerSubdivisionAction_,
    });
    editor->setMoreBatchTransformActions({
        transformToggleBreakAction_,
        transformToggleExAction_,
        transformToggleFireworkAction_,
        transformRandomRotateAction_,
    });
    connect(editor, &PlainCodeEditor::undoShortcutRequested, this, [this]() {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("selection_restore/editor_shortcut_forward"),
            QStringLiteral("action=undo has_action=%1").arg(undoAction_ != nullptr ? 1 : 0),
            true
        );
        if (undoAction_ != nullptr) {
            undoAction_->trigger();
        }
    });
    connect(editor, &PlainCodeEditor::redoShortcutRequested, this, [this]() {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("selection_restore/editor_shortcut_forward"),
            QStringLiteral("action=redo has_action=%1").arg(redoAction_ != nullptr ? 1 : 0),
            true
        );
        if (redoAction_ != nullptr) {
            redoAction_->trigger();
        }
    });
    connect(editor, &PlainCodeEditor::editorOverwriteModeChanged, this, [this](bool enabled) {
        applyEditorOverwriteModeEnabled(enabled, true);
    });
    connect(editor, &PlainCodeEditor::lineNumberBookmarkActivated, this, &MainWindow::openBookmarkAtLine);
    connect(editor, &PlainCodeEditor::lineNumberBookmarkMoveRequested, this, [this](int fromLine, int toLine) {
        if (editorSection_ != nullptr) {
            editorSection_->replaceBookmarkLine(fromLine, toLine);
        }
    });
    chartBracketHighlighter_ = new BracketScopeHighlighter(editor->document());
    editorWidget_ = editor;
    editorWidget_->setFont(codeFont);
    editorWidget_->setStyleSheet(
        "border: none;"
        "background: #FFFFFF;"
        "color: #1F1F1F;"
        "selection-background-color: #B8CCE5;"
        "selection-color: #1F1F1F;"
    );
    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(editorWidget_)) {
        if (QScrollBar* vbar = scrollArea->verticalScrollBar()) {
            vbar->setStyleSheet(modernScrollBarStyle());
        }
        if (QScrollBar* hbar = scrollArea->horizontalScrollBar()) {
            hbar->setStyleSheet(modernScrollBarStyle());
        }
    }
    logStartupStage("editor_widget_ready");

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
    editorContextLabel_ = new QLabel(uiText("editor.welcome", "Welcome to MiaCode!"), editorHeader);
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
    auto* difficultyDesignerLabel = new QLabel(uiText("editor.des", "Des"), editorDifficultyControls_);
    difficultyDesignerLabel_ = difficultyDesignerLabel;
    difficultyDesignerLabel->setFont(uiAccentFont(10));
    auto* difficultyDesignerLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyDesignerLineEdit->setLeftPlaceholderText("&des_n=");
    difficultyDesignerEdit_ = difficultyDesignerLineEdit;
    difficultyDesignerEdit_->setFixedWidth(96);
    difficultyDesignerEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    difficultyDesignerEdit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorDifficultyLayout->addWidget(difficultyLevelLabel);
    editorDifficultyLayout->addWidget(difficultyLevelEdit_);
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
    const QString jumpToValidationToolTip = UiText::isChineseUi()
        ? QStringLiteral("点击跳转到「语法」选项卡")
        : QStringLiteral("Click to open the Syntax tab");
    const QString jumpToMuriToolTip = UiText::isChineseUi()
        ? QStringLiteral("点击跳转到「无理」选项卡")
        : QStringLiteral("Click to open the Muri tab");
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
        UiText::isChineseUi() ? QStringLiteral("1行 1列") : QStringLiteral("Ln 1, Col 1"),
        editorHeaderTrailingWidget);
    editorCursorLabel_->setObjectName("EditorMeta");
    editorCursorLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editorCursorLabel_->setFixedWidth(
        QFontMetrics(uiMonoFont(10)).horizontalAdvance(
            UiText::isChineseUi() ? QStringLiteral("9999行 9999列") : QStringLiteral("Ln 9999, Col 9999")) + 10);
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
    findBar->setStyleSheet(
        "QFrame#EditorFindBar {"
        " background: rgba(248, 250, 253, 248);"
        " border: 1px solid #DEE4EC;"
        " border-radius: 10px;"
        "}"
        "QFrame#EditorFindBar QLineEdit {"
        " background: #FFFFFF;"
        " border: 1px solid #CCD6E2;"
        " border-radius: 6px;"
        " min-height: 22px;"
        " padding: 1px 6px;"
        " selection-background-color: #B8CCE5;"
        " selection-color: #1F1F1F;"
        "}"
        "QFrame#EditorFindBar QLineEdit:focus { border-color: #3B82F6; }"
        "QFrame#EditorFindBar QToolButton, QFrame#EditorFindBar QPushButton {"
        " color: #223042;"
        " min-height: 22px;"
        " padding: 0 6px;"
        " border: 1px solid #D8E0EA;"
        " border-radius: 6px;"
        " background: #FFFFFF;"
        " font-weight: 400;"
        "}"
        "QFrame#EditorFindBar QToolButton:hover, QFrame#EditorFindBar QPushButton:hover {"
        " background: #F5F8FC;"
        " border-color: #BCD0E5;"
        "}"
        "QFrame#EditorFindBar QToolButton:pressed, QFrame#EditorFindBar QPushButton:pressed {"
        " background: #E8F1FB;"
        "}"
        "QFrame#EditorFindBar QToolButton#EditorFindPrevButton, QFrame#EditorFindBar QToolButton#EditorFindNextButton {"
        " min-width: 24px;"
        " padding: 0;"
        " font-size: 12px;"
        "}"
        "QFrame#EditorFindBar QToolButton#EditorFindCloseButton {"
        " min-width: 28px;"
        " padding: 0;"
        " font-size: 15px;"
        " font-weight: 400;"
        "}"
    );
    auto* findBarLayout = new QVBoxLayout(findBar);
    findBarLayout->setContentsMargins(10, 6, 10, 6);
    findBarLayout->setSpacing(4);

    auto* findRow = new QHBoxLayout();
    findRow->setContentsMargins(0, 0, 0, 0);
    findRow->setSpacing(6);
    editorFindEdit_ = new QLineEdit(findBar);
    editorFindEdit_->setPlaceholderText(UiText::isChineseUi() ? QStringLiteral("查找") : QStringLiteral("Find"));
    editorFindPrevButton_ = new QToolButton(findBar);
    editorFindPrevButton_->setObjectName("EditorFindPrevButton");
    editorFindPrevButton_->setText(QStringLiteral("↑"));
    editorFindPrevButton_->setToolTip(UiText::isChineseUi() ? QStringLiteral("查找上一个") : QStringLiteral("Find Previous"));
    editorFindPrevButton_->setFixedWidth(24);
    editorFindNextButton_ = new QToolButton(findBar);
    editorFindNextButton_->setObjectName("EditorFindNextButton");
    editorFindNextButton_->setText(QStringLiteral("↓"));
    editorFindNextButton_->setToolTip(UiText::isChineseUi() ? QStringLiteral("查找下一个") : QStringLiteral("Find Next"));
    editorFindNextButton_->setFixedWidth(24);
    editorFindCloseButton_ = new QToolButton(findBar);
    editorFindCloseButton_->setObjectName("EditorFindCloseButton");
    editorFindCloseButton_->setText(QStringLiteral("✕"));
    editorFindCloseButton_->setToolTip(UiText::isChineseUi() ? QStringLiteral("关闭查找栏") : QStringLiteral("Close"));
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
    editorReplaceEdit_->setPlaceholderText(UiText::isChineseUi() ? QStringLiteral("替换") : QStringLiteral("Replace"));
    editorReplaceButton_ = new QPushButton(UiText::isChineseUi() ? QStringLiteral("替换") : QStringLiteral("Replace"), findBar);
    editorReplaceAllButton_ = new QPushButton(UiText::isChineseUi() ? QStringLiteral("全部替换") : QStringLiteral("Replace All"), findBar);
    replaceRow->addWidget(editorReplaceEdit_, 1);
    replaceRow->addWidget(editorReplaceButton_, 0);
    replaceRow->addWidget(editorReplaceAllButton_, 0);
    findBarLayout->addLayout(replaceRow);

    findBar->hide();
    editorFindBar_ = findBar;

    welcomePage_ = new QWidget(editorStack_);
    welcomePage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    welcomePage_->setStyleSheet(
        "QWidget { background: #FFFFFF; color: #2A3440; }"
    );
    auto* welcomeLayout = new QVBoxLayout(welcomePage_);
    welcomeLayout->setContentsMargins(12, 8, 12, 12);
    welcomeLayout->setSpacing(8);
    welcomeEmptyHintLabel_ = new QLabel(uiText("metadata.empty_hint", "← Click to add a chart difficulty"), welcomePage_);
    welcomeEmptyHintLabel_->setFont(uiAccentFont(11));
    welcomeEmptyHintLabel_->setStyleSheet("color: #6A7890; background: transparent; padding-left: 6px;");
    welcomeLayout->addWidget(welcomeEmptyHintLabel_, 0, Qt::AlignLeft | Qt::AlignTop);
    welcomeLayout->addStretch(1);

    metadataPage_ = new QWidget(editorStack_);
    metadataPage_->setObjectName("MetadataPage");
    metadataPage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    metadataPage_->setStyleSheet(
        "QWidget { background: #FFFFFF; color: #2A3440; }"
        "QWidget#MetadataPage { background: #F8FAFD; }"
        "QFrame#MetadataCard { background: #FFFFFF; border: 1px solid #DEE4EC; border-radius: 8px; }"
        "QLabel#SectionTitle { color: #1F2D3D; font-weight: 700; padding-left: 4px; }"
        "QLabel#MetadataFieldLabel { color: #2A3440; background: transparent; padding-left: 8px; }"
        "QLineEdit, QTextEdit, QPlainTextEdit {"
        " background: #FFFFFF;"
        " color: #1F1F1F;"
        " border: 1px solid #CCD6E2;"
        " border-radius: 6px;"
        " padding: 6px 8px;"
        " selection-background-color: #B8CCE5;"
        " selection-color: #1F1F1F;"
        "}"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: #3B82F6; }"
    );
    auto* metadataLayout = new QVBoxLayout(metadataPage_);
    metadataLayout->setContentsMargins(12, 8, 12, 12);
    metadataLayout->setSpacing(8);

    auto* metadataCard = new QFrame(metadataPage_);
    metadataCard_ = metadataCard;
    metadataCard->setObjectName("MetadataCard");
    auto* metadataCardLayout = new QVBoxLayout(metadataCard);
    metadataCardLayout->setContentsMargins(14, 12, 14, 14);
    metadataCardLayout->setSpacing(12);

    auto* infoTitle = new QLabel(uiText("metadata.information", "Information"), metadataPage_);
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
    firstEdit_ = new QLineEdit(metadataPage_);
    auto* designerLineEdit = new LeftPlaceholderLineEdit(metadataPage_);
    designerLineEdit->setLeftPlaceholderText("&des=");
    designerEdit_ = designerLineEdit;
    titleEdit_->setPlaceholderText("&title=");
    artistEdit_->setPlaceholderText("&artist=");
    firstEdit_->setPlaceholderText("&first=");
    designerEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const auto makeMetadataFieldLabel = [this](const QString& text) {
        auto* label = new QLabel(text, metadataPage_);
        label->setObjectName("MetadataFieldLabel");
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setMinimumWidth(46);
        return label;
    };
    firstEdit_->setFixedWidth(98);
    auto* firstWrap = new QWidget(metadataPage_);
    auto* firstWrapLayout = new QHBoxLayout(firstWrap);
    firstWrapLayout->setContentsMargins(0, 0, 0, 0);
    firstWrapLayout->setSpacing(6);
    firstWrapLayout->addWidget(firstEdit_, 0, Qt::AlignLeft);
    firstWrapLayout->addStretch(1);

    // Title and artist each get their own "read from MP3" button on the
    // far right of the matching row, mirroring the BPM detection button
    // on the offset row below. Each button only fills its own field so
    // users who only need one of the two values aren't forced to accept
    // the other.
    auto* titleWrap = new QWidget(metadataPage_);
    auto* titleWrapLayout = new QHBoxLayout(titleWrap);
    titleWrapLayout->setContentsMargins(0, 0, 0, 0);
    titleWrapLayout->setSpacing(6);
    titleWrapLayout->addWidget(titleEdit_, 1);
    auto* readTitleButton = new QToolButton(metadataPage_);
    readTitleButton->setText(UiText::isChineseUi()
        ? QStringLiteral("从 MP3 读取")
        : QStringLiteral("Read from MP3"));
    readTitleButton->setToolTip(UiText::isChineseUi()
        ? QStringLiteral("从 track.mp3 的 ID3 标签里读取标题。")
        : QStringLiteral("Pull the title from track.mp3's ID3 tag."));
    connect(readTitleButton, &QToolButton::clicked, this, &MainWindow::onReadTitleFromTrack);
    titleWrapLayout->addWidget(readTitleButton, 0, Qt::AlignRight);

    auto* artistWrap = new QWidget(metadataPage_);
    auto* artistWrapLayout = new QHBoxLayout(artistWrap);
    artistWrapLayout->setContentsMargins(0, 0, 0, 0);
    artistWrapLayout->setSpacing(6);
    artistWrapLayout->addWidget(artistEdit_, 1);
    auto* readArtistButton = new QToolButton(metadataPage_);
    readArtistButton->setText(UiText::isChineseUi()
        ? QStringLiteral("从 MP3 读取")
        : QStringLiteral("Read from MP3"));
    readArtistButton->setToolTip(UiText::isChineseUi()
        ? QStringLiteral("从 track.mp3 的 ID3 标签里读取曲师。")
        : QStringLiteral("Pull the artist from track.mp3's ID3 tag."));
    connect(readArtistButton, &QToolButton::clicked, this, &MainWindow::onReadArtistFromTrack);
    artistWrapLayout->addWidget(readArtistButton, 0, Qt::AlignRight);

    // Designer row gets a "[ ] All difficulties share the same designer"
    // checkbox. When checked, edits to the top-level &des broadcast to
    // every per-difficulty &des_N, and edits to a per-difficulty designer
    // broadcast back to &des and the other difficulties. The checkbox
    // state persists per-project via ProjectPreferences.
    auto* designerWrap = new QWidget(metadataPage_);
    auto* designerWrapLayout = new QHBoxLayout(designerWrap);
    designerWrapLayout->setContentsMargins(0, 0, 0, 0);
    designerWrapLayout->setSpacing(6);
    designerWrapLayout->addWidget(designerEdit_, 1);
    unifiedDesignerCheckbox_ = new QCheckBox(metadataPage_);
    unifiedDesignerCheckbox_->setText(UiText::isChineseUi()
        ? QStringLiteral("所有难度采用相同名义")
        : QStringLiteral("All difficulties share this designer"));
    unifiedDesignerCheckbox_->setToolTip(UiText::isChineseUi()
        ? QStringLiteral("勾选后，&des 与每个难度的 &des_N 会双向同步。")
        : QStringLiteral("When checked, &des and every &des_N stay in sync."));
    // Explicit indicator styling so the checkbox is visible in dark mode —
    // the platform default leaves the unchecked box nearly invisible against
    // the metadata page's dark card background.
    unifiedDesignerCheckbox_->setStyleSheet(UiTheme::darkAwareCheckBoxStyleSheet());
    connect(unifiedDesignerCheckbox_, &QCheckBox::toggled, this, &MainWindow::onUnifiedDesignerToggled);
    designerWrapLayout->addWidget(unifiedDesignerCheckbox_, 0, Qt::AlignRight);

    // Cover-extraction row sits below `first`. The label is intentionally
    // short ("封面") so the form column stays compact; the button text
    // carries the description "从 track.mp3 中提取 bg.jpg".
    auto* coverWrap = new QWidget(metadataPage_);
    auto* coverWrapLayout = new QHBoxLayout(coverWrap);
    coverWrapLayout->setContentsMargins(0, 0, 0, 0);
    coverWrapLayout->setSpacing(6);
    auto* extractCoverButton = new QToolButton(metadataPage_);
    extractCoverButton->setText(UiText::isChineseUi()
        ? QStringLiteral("从 track.mp3 中提取 bg.jpg")
        : QStringLiteral("Extract bg.jpg from track.mp3"));
    extractCoverButton->setToolTip(UiText::isChineseUi()
        ? QStringLiteral("把 track.mp3 内嵌的封面图写到当前谱面目录的 bg.jpg。")
        : QStringLiteral("Write track.mp3's embedded cover artwork as bg.jpg next to the chart."));
    connect(extractCoverButton, &QToolButton::clicked, this, &MainWindow::onExtractBackgroundFromTrack);
    coverWrapLayout->addWidget(extractCoverButton, 0, Qt::AlignLeft);
    coverWrapLayout->addStretch(1);

    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.title", "title")), titleWrap);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.artist", "artist")), artistWrap);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.des", "des")), designerWrap);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.first", "first")), firstWrap);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.cover", "cover")), coverWrap);
    metadataCardLayout->addLayout(metadataForm);

    auto* extraMetadataLabel = new QLabel(uiText("metadata.other_fields", "Other &xx Fields"), metadataPage_);
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
    metadataEmptyHintLabel_ = new QLabel(uiText("metadata.empty_hint", "← Click to add a chart difficulty"), metadataPage_);
    metadataEmptyHintLabel_->setFont(uiAccentFont(11));
    metadataEmptyHintLabel_->setStyleSheet("color: #6A7890; background: transparent; padding-left: 6px;");
    metadataEmptyHintLabel_->hide();
    metadataLayout->addWidget(metadataEmptyHintLabel_, 0, Qt::AlignLeft | Qt::AlignTop);
    metadataLayout->addWidget(metadataCard, 1);

    chartPage_ = new QWidget(editorStack_);
    chartPage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* chartLayout = new QVBoxLayout(chartPage_);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(0);
    chartCopySplitter_ = new QSplitter(Qt::Horizontal, chartPage_);
    chartCopySplitter_->setChildrenCollapsible(false);
    chartCopySplitter_->setHandleWidth(1);
    chartCopySplitter_->addWidget(editorWidget_);
    copyAreaPanel_ = new QWidget(chartCopySplitter_);
    copyAreaPanel_->setObjectName("CopyAreaPanel");
    copyAreaPanel_->setAttribute(Qt::WA_StyledBackground, true);
    copyAreaPanel_->setStyleSheet(UiTheme::editorShellStyleSheet());
    auto* copyAreaLayout = new QVBoxLayout(copyAreaPanel_);
    copyAreaLayout->setContentsMargins(0, 0, 0, 0);
    copyAreaLayout->setSpacing(0);
    copyAreaEditor_ = new PlainCodeEditor(copyAreaPanel_);
    copyAreaEditor_->setLineWrapMode(QTextEdit::WidgetWidth);
    copyAreaEditor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    copyAreaEditor_->setPlaceholderText(UiText::isChineseUi() ? QStringLiteral("复制区") : QStringLiteral("Copy area"));
    copyAreaEditor_->setStyleSheet(UiTheme::editorTextEditStyleSheet());
    connect(copyAreaEditor_, &PlainCodeEditor::editorOverwriteModeChanged, this, [this](bool enabled) {
        applyEditorOverwriteModeEnabled(enabled, true);
    });
    if (QScrollBar* vbar = copyAreaEditor_->verticalScrollBar()) {
        vbar->setStyleSheet(modernScrollBarStyle());
    }
    if (QScrollBar* hbar = copyAreaEditor_->horizontalScrollBar()) {
        hbar->setStyleSheet(modernScrollBarStyle());
    }
    syncCopyAreaEditorAppearance();
    copyAreaLayout->addWidget(copyAreaEditor_, 1);
    chartCopySplitter_->addWidget(copyAreaPanel_);
    chartCopySplitter_->setStretchFactor(0, 1);
    chartCopySplitter_->setStretchFactor(1, 1);
    copyAreaPanel_->hide();
    chartLayout->addWidget(chartCopySplitter_, 1);

    editorStack_->addWidget(welcomePage_);
    editorStack_->addWidget(metadataPage_);
    ui_.latencyDetectionPage_ = new miacode::latency::LatencyDetectionPage(this);
    editorStack_->addWidget(ui_.latencyDetectionPage_);
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
    outlineList_->setUniformItemSizes(true);
    outlineList_->setIconSize(QSize(14, 14));
    outlineList_->setSpacing(2);
    outlineList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outlineList_->setTextElideMode(Qt::ElideRight);
    outlineList_->setFont(uiAccentFont(11));
    outlineList_->setItemDelegate(new OutlineItemDelegate(outlineList_));
    outlineList_->setStyleSheet(
        "QListWidget {"
        " background: #FFFFFF;"
        " color: #243447;"
        " border: 1px solid #E1E7EF;"
        " padding: 6px;"
        " outline: none;"
        "}"
        "QListWidget::item {"
        " min-height: 28px;"
        " padding: 4px 12px;"
        " border: 1px solid transparent;"
        " border-radius: 6px;"
        "}"
        "QListWidget::item:selected { color: #243447; }"
    );
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
    outlineList_->viewport()->installEventFilter(this);
    deleteDifficultyButton_ = new QToolButton(outlineList_->viewport());
    deleteDifficultyButton_->setAutoRaise(true);
    deleteDifficultyButton_->setIcon(makeOutlineCloseIcon(QColor("#5D6876")));
    deleteDifficultyButton_->setIconSize(QSize(12, 12));
    deleteDifficultyButton_->setToolTip("Delete the current difficulty");
    deleteDifficultyButton_->setCursor(Qt::PointingHandCursor);
    deleteDifficultyButton_->setFocusPolicy(Qt::NoFocus);
    deleteDifficultyButton_->setFixedSize(18, 18);
    deleteDifficultyButton_->setStyleSheet(
        "QToolButton {"
        " border: none;"
        " border-radius: 5px;"
        " background: transparent;"
        "}"
        "QToolButton:hover {"
        " background: #E9EEF4;"
        "}"
    );
    deleteDifficultyButton_->hide();
    connect(deleteDifficultyButton_, &QToolButton::clicked, this, [this]() {
        if (hasActiveDifficulty()) {
            deleteDifficultyField(activeDifficultyId_);
        }
    });
    connect(outlineCollapseButton_, &QToolButton::clicked, this, [this]() {
        windowSection_->setOutlineDockCollapsed(!outlineDockCollapsed_);
    });
    connect(outlineList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* current) {
        updateDifficultyDeleteButton(false);
        if (current == nullptr) {
            return;
        }
        const QString kind = current->data(Qt::UserRole).toString();
        const int difficultyId = current->data(Qt::UserRole + 1).toInt();
        if (kind == "metadata") {
            activeOutlineKey_ = "metadata";
            if (switchToMetadataField() && titleEdit_ != nullptr) {
                titleEdit_->setFocus();
            }
            return;
        }
        if (kind == "latency") {
            activeOutlineKey_ = "latency";
            switchToLatencyField();
            return;
        }
        if (kind == "add") {
            QMenu menu(this);
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
                menu.exec(outlineList_->viewport()->mapToGlobal(rowRect.bottomRight()));
            }
            rebuildFieldSidebar();
            return;
        }
        if (kind == "toolbox") {
            if (toolboxMenu_ != nullptr) {
                const QRect rowRect = outlineList_->visualItemRect(current);
                const QPoint popupPos = outlineList_->viewport()->mapToGlobal(
                    QPoint(rowRect.right(), rowRect.top() + rowRect.height() / 2)
                );
                toolboxMenu_->exec(popupPos);
            }
            rebuildFieldSidebar();
            return;
        }
        if (SimaiDocument::isDifficultyId(difficultyId)) {
            activeOutlineKey_ = "chart";
            if (switchToDifficultyField(difficultyId) && editorWidget_ != nullptr) {
                editorWidget_->setFocus();
            }
        }
    });
    outlineList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(outlineList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (outlineList_ == nullptr) {
            return;
        }
        QListWidgetItem* item = outlineList_->itemAt(pos);
        if (item == nullptr) {
            return;
        }
        const int difficultyId = item->data(Qt::UserRole + 1).toInt();
        if (!SimaiDocument::isDifficultyId(difficultyId) || document_.difficulty(difficultyId) == nullptr) {
            return;
        }
        QMenu menu(this);
        menu.setFont(uiAccentFont(10));
        styleRoundedMenu(menu);
        QAction* deleteAction = menu.addAction(
            makeOutlineCloseIcon(QColor("#5D6876")),
            QString("Delete %1").arg(SimaiDocument::difficultyName(difficultyId))
        );
        connect(deleteAction, &QAction::triggered, this, [this, difficultyId]() {
            deleteDifficultyField(difficultyId);
        });
        menu.exec(outlineList_->viewport()->mapToGlobal(pos));
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
            UiText::isChineseUi() ? QStringLiteral("音频/视频处理") : QStringLiteral("Audio/Video Processing")
        );
        connect(mediaProcessingAction, &QAction::triggered, this, &MainWindow::onMediaProcessingTools);
    }

    if (normalizeWholeChartAction_ != nullptr) {
        toolboxMenu_->addAction(normalizeWholeChartAction_);
    }

    toolboxMenu_->addSeparator();

    QMenu* bookmarkMenu = toolboxMenu_->addMenu(
        UiText::isChineseUi() ? QStringLiteral("书签") : QStringLiteral("Bookmarks")
    );
    styleRoundedMenu(*bookmarkMenu);
    createBookmarkAction_ = bookmarkMenu->addAction(
        UiText::isChineseUi() ? QStringLiteral("创建书签") : QStringLiteral("Create Bookmark")
    );
    connect(createBookmarkAction_, &QAction::triggered, this, &MainWindow::showCreateBookmarkDialog);
    bookmarkManagerAction_ = bookmarkMenu->addAction(
        UiText::isChineseUi() ? QStringLiteral("书签管理") : QStringLiteral("Bookmark Manager")
    );
    connect(bookmarkManagerAction_, &QAction::triggered, this, &MainWindow::showBookmarkManager);

    // Copy Area is intentionally hidden from the toolbox per the toolbox
    // revamp. The feature itself is kept intact — copyAreaPanel_/copyAreaEditor_,
    // fullCopyAreaAction_, and setFullCopyAreaVisible() all still exist — so
    // flipping this constant back to `true` resurfaces it unchanged.
    constexpr bool kCopyAreaIntegratedIntoToolbox = false;
    if (kCopyAreaIntegratedIntoToolbox) {
        QMenu* copyAreaMenu = toolboxMenu_->addMenu(
            UiText::isChineseUi() ? QStringLiteral("复制区") : QStringLiteral("Copy Area")
        );
        styleRoundedMenu(*copyAreaMenu);
        fullCopyAreaAction_ = copyAreaMenu->addAction(
            UiText::isChineseUi() ? QStringLiteral("完整复制区") : QStringLiteral("Full Copy Area")
        );
        fullCopyAreaAction_->setCheckable(true);
        connect(fullCopyAreaAction_, &QAction::toggled, this, &MainWindow::setFullCopyAreaVisible);
    }

    toolboxMenu_->addSeparator();

    QAction* toolboxOfficialChartMirrorAction = toolboxMenu_->addAction(
        UiText::isChineseUi() ? QStringLiteral("官谱镜像站") : QStringLiteral("Official Chart Mirror")
    );
    connect(toolboxOfficialChartMirrorAction, &QAction::triggered, this, [openToolboxUrl]() {
        openToolboxUrl(QStringLiteral("https://www.maiviewer.net/"));
    });

    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    windowSection_->setOutlineDockCollapsed(false);
    logStartupStage("outline_ready");

    previewPanel_ = new QWidget(this);
    previewPanel_->setObjectName("PreviewPanel");
    previewPanel_->setStyleSheet(
        "QWidget#PreviewPanel {"
        " background: #F5F7FA;"
        " border-left: 1px solid #DEE4EC;"
        "}"
        "QFrame#PreviewCanvasFrame {"
        " background: #000000;"
        " border: 1px solid #D8E0EA;"
        "}"
        "QFrame#PreviewStatsCard {"
        " background: #EDF2F8;"
        " border: 1px solid #D5E0EC;"
        " border-radius: 10px;"
        "}"
        "QFrame#PreviewStats {"
        " background: transparent;"
        " border: none;"
        "}"
        "QLabel#PreviewStatChip {"
        " color: #213246;"
        " background: #F6F9FD;"
        " border: 1px solid #D3DEEA;"
        " border-radius: 9px;"
        " padding: 2px 8px;"
        " font-weight: 600;"
        "}"
        "QLabel#PreviewStatChipTotal {"
        " color: #213246;"
        " background: #F0F4FA;"
        " border: 1px solid #CBD8E6;"
        " border-radius: 9px;"
        " padding: 2px 8px;"
        " font-weight: 700;"
        "}"
    );
    previewPanel_->setMinimumWidth(kEmbeddedPreviewPanelMinWidth);
    previewPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    previewCanvas_ = new PreviewRuntime(this);
    logStartupStage("preview_canvas_created");
    applyEffectivePreviewOutlineVariantToCanvas();
    previewCanvas_->setSkinDirectory(resolvePreviewSkinDir());
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
    logStartupStage("preview_sfx_runtime_created");
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
    timelineQuickStateBridge_->setViewportLockEnabled(previewViewportLockEnabled_);
    timelineQuickStateBridge_->setFollowProgressEnabled(previewProgressFollowEnabled_);
    timelineQuickStateBridge_->setTimelineSyncEnabled(timelineSyncEnabled_);
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::zoomScaleChanged, this, [this](double) {
        savePortableState();
    });
    connect(timelineQuickStateBridge_, &TimelineQuickStateBridge::waveformBrightnessChanged, this, [this](double) {
        savePortableState();
    });
    if (!timelineWidgetlessQuickRoute_) {
        timelineView_ = new TimelineView(bottomTabs_);
        timelineQuickStateBridge_->attachReferenceView(timelineView_);
        connect(timelineView_, &TimelineView::headerNavigateRequested, this, [this](double second) {
            timelineSection_->onTimelineHeaderNavigateRequested(second);
        });
        connect(timelineView_, &TimelineView::previewPlayPauseRequested, this, &MainWindow::onTogglePreviewPause);
        connect(timelineView_, &TimelineView::timelineUserInteractionStarted, this, [this]() {
            timelineSection_->onTimelineUserInteractionStarted();
        });
        connect(timelineView_, &TimelineView::timelineDragStarted, this, [this]() {
            timelineSection_->onTimelineDragStarted();
        });
        connect(timelineView_, &TimelineView::timelineWheelNavigateRequested, this, [this](double second) {
            timelineSection_->onTimelineWheelNavigateRequested(second);
        });
        connect(timelineView_, &TimelineView::centerNavigateRequested, this, [this](double second) {
            timelineSection_->onTimelineCenterNavigateRequested(second);
        });
        connect(timelineView_, &TimelineView::timelineDragFinished, this, [this](double second) {
            timelineSection_->onTimelineDragFinished(second);
        });
        connect(timelineView_, &TimelineView::followPreviewToggled, this, [this](bool enabled) {
            timelineSection_->onTimelineFollowPreviewToggled(enabled);
        });
        connect(timelineView_, &TimelineView::viewportLockToggled, this, [this](bool enabled) {
            timelineSection_->onTimelineViewportLockToggled(enabled);
        });
        connect(timelineView_, &TimelineView::followProgressToggled, this, [this](bool enabled) {
            timelineSection_->onTimelineFollowProgressToggled(enabled);
        });
        bottomTabs_->addTab(timelineView_, uiText("tab.timeline", "Timeline"));
    }

    if (auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_); editor != nullptr) {
        if (copyAreaEditor_ != nullptr) {
            auto* mainVScroll = editor->verticalScrollBar();
            auto* copyVScroll = copyAreaEditor_->verticalScrollBar();
            if (mainVScroll != nullptr && copyVScroll != nullptr) {
                connect(mainVScroll, &QScrollBar::valueChanged, this, [copyVScroll](int value) {
                    if (copyVScroll->value() != value) {
                        copyVScroll->setValue(qBound(copyVScroll->minimum(), value, copyVScroll->maximum()));
                    }
                });
                connect(copyVScroll, &QScrollBar::valueChanged, this, [mainVScroll](int value) {
                    if (mainVScroll->value() != value) {
                        mainVScroll->setValue(qBound(mainVScroll->minimum(), value, mainVScroll->maximum()));
                    }
                });
            }
        }
        connect(editor->document(), &QTextDocument::contentsChange, this, [this](int position, int charsRemoved, int charsAdded) {
            if (suppressTextDirtyTracking_) {
                return;
            }
            if (charsRemoved == 0 && charsAdded == 0) {
                return;
            }
            if (documentSection_ != nullptr) {
                documentSection_->syncChartSelectionTransformUndoState();
            }
            QTimer::singleShot(0, this, [this]() {
                if (!suppressTextDirtyTracking_) {
                    markCurrentFieldDirty();
                }
            });
            ++timelineRevision_;
            syncCopyAreaLineCount();
            applyTimelineQuickChange(position, charsRemoved, charsAdded);
            requestTimelineSlowRefresh();
            bool syncPreviewFollow = false;
            double previewFollowSecond = 0.0;
            if (hasActiveDifficulty() && previewFollowEnabled_) {
                syncPreviewFollow = true;
                previewFollowSecond = qMax(0.0, currentPreviewAuthoritativeAudioClockSecond());
            }
            const bool syncTimelineCursor =
                !syncPreviewFollow
                && (!quickShellUiFocusBridgeMode_ || quickTimelineSurfaceReady_);
            scheduleDeferredEditorUiUpdate(
                true,
                true,
                syncTimelineCursor,
                !qtPreviewPlaying_ && !syncPreviewFollow && timelineSyncEnabled_,
                syncPreviewFollow,
                previewFollowSecond,
                false
            );
        });
    }
    connect(qobject_cast<PlainCodeEditor*>(editorWidget_), &QTextEdit::cursorPositionChanged, this, [this]() {
        const bool syncTimelineCursor = !quickShellUiFocusBridgeMode_ || quickTimelineSurfaceReady_;
        scheduleDeferredEditorUiUpdate(
            true,
            false,
            syncTimelineCursor,
            !qtPreviewPlaying_ && timelineSyncEnabled_,
            false,
            0.0,
            false
        );
    });
    connect(titleEdit_, &QLineEdit::textChanged, this, [this]() {
        markCurrentFieldDirty();
        updateWindowTitle();
    });
    connect(artistEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(firstEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
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
    connect(difficultyDesignerEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);

    outputView_ = nullptr;

    errorList_ = new QListWidget(bottomTabs_);
    errorList_->setFont(uiOutputFont());
    errorList_->setUniformItemSizes(false);
    errorList_->setWordWrap(true);
    errorList_->setTextElideMode(Qt::ElideNone);
    errorList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    errorList_->setContextMenuPolicy(Qt::CustomContextMenu);
    errorList_->viewport()->installEventFilter(this);
    connect(errorList_, &QListWidget::itemActivated, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::itemClicked, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showIssueListContextMenu(errorList_, pos, false);
    });
    bottomTabs_->addTab(
        errorList_,
        UiText::isChineseUi() ? QStringLiteral("语法") : QStringLiteral("Syntax")
    );

    muriList_ = new QListWidget(bottomTabs_);
    muriList_->setFont(uiOutputFont());
    muriList_->setUniformItemSizes(false);
    muriList_->setWordWrap(true);
    muriList_->setTextElideMode(Qt::ElideNone);
    muriList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    muriList_->setContextMenuPolicy(Qt::CustomContextMenu);
    muriList_->viewport()->installEventFilter(this);
    connect(muriList_, &QListWidget::itemActivated, this, &MainWindow::onMuriItemActivated);
    connect(muriList_, &QListWidget::itemClicked, this, &MainWindow::onMuriItemActivated);
    connect(muriList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showIssueListContextMenu(muriList_, pos, true);
    });
    bottomTabs_->addTab(
        muriList_,
        UiText::isChineseUi() ? QStringLiteral("无理") : QStringLiteral("Muri")
    );
    connect(bottomTabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (!quickShellBottomTabsProxyActive() && !timelineWidgetlessQuickRoute_) {
            if (bottomTabs_->currentWidget() == timelineView_) {
                currentBottomTabsTabId_ = BottomTabsTabId::Timeline;
                if (timelineSection_ != nullptr) {
                    timelineSection_->flushDeferredTimelineBridgeState();
                }
            } else if (bottomTabs_->currentWidget() == errorList_) {
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
    previewLeftColumn_->setMinimumWidth(320);
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
