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
#include "../validation/EditorSelectionUtils.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "editor/TouchPadAuthoringEdit.h"
#include "app/qml_ui/QmlTouchPadAuthoringBridge.h"
#include "audio/PreviewAudioDeviceWatcher.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
#include "TimelineView.h"
#include "BusySpinner.h"
#include "UiText.h"
#include "UiTheme.h"
#include "WindowParityMetrics.h"
#include "app/ui/AppBackgroundPainter.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/AdoptedSurfaceDragAutoScroll.h"
#include "common/AdoptedWidgetCoordinates.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "extensions/ExtensionManager.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/export_page/ExportLauncherPage.h"
#include "app/qml_ui/export/QmlExportSession.h"
#include "tools/latency/LatencyDetectionPage.h"
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
    auto* topMenuBar = new miacode::ui::AppBackgroundSurfaceMenuBar(this);
    topMenuBar->setNativeMenuBar(false);
    setMenuBar(topMenuBar);
    setStatusBar(new miacode::ui::AppBackgroundSurfaceStatusBar(this));

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

    auto* toolBar = new miacode::ui::AppBackgroundSurfaceToolBar(QStringLiteral("Main"), this);
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
    if (netBatchDownloadAction_ != nullptr) {
        toolsMenu->addSeparator();
        toolsMenu->addAction(netBatchDownloadAction_);
    }
    extensionManager_ = std::make_unique<miacode::extensions::ExtensionManager>(this);
    miacode::extensions::ExtensionHostCallbacks extensionCallbacks;
    extensionCallbacks.activeDocument = [this]() {
        miacode::extensions::ExtensionDocumentSnapshot snapshot;
        snapshot.uri = currentFilePath_.isEmpty()
            ? QStringLiteral("untitled:active")
            : QUrl::fromLocalFile(currentFilePath_).toString();
        snapshot.text = hasActiveDifficulty() ? editorText() : QString();
        snapshot.activeDifficultyId = activeDifficultyId_;
        snapshot.dirty = currentFieldDirty_;
        return snapshot;
    };
    extensionCallbacks.replaceActiveDocumentText = [this](const QString& text, QString* error) {
        if (!hasActiveDifficulty()) {
            if (error != nullptr) {
                *error = QStringLiteral("No active chart difficulty is open.");
            }
            return false;
        }
        setEditorText(text);
        markCurrentFieldDirty();
        refreshTimelineMetadata();
        return true;
    };
    extensionCallbacks.validateActiveDocument = [this]() {
        return runValidateSimaiSilently(false);
    };
    extensionCallbacks.mainWindowRequest = [this](const QString& method, const QJsonObject& params) -> QJsonObject {
        return handleExtensionHostRequest(method, params);
    };
    extensionCallbacks.showMessage = [this](const QString& severity, const QString& message) {
        const QMessageBox::Icon icon = severity == QStringLiteral("error")
            ? QMessageBox::Critical
            : (severity == QStringLiteral("warning") ? QMessageBox::Warning : QMessageBox::Information);
        UiDialogs::showMessageBox(
            icon,
            this,
            UiText::isChineseUi() ? QStringLiteral("扩展") : QStringLiteral("Extension"),
            message
        );
    };
    extensionCallbacks.logMessage = [this](const QString& message) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("extensions"),
            message
        );
        if (statusBar() != nullptr) {
            statusBar()->showMessage(message, 5000);
        }
    };
    extensionManager_->setCallbacks(std::move(extensionCallbacks));
    extensionManager_->initialize(
        menuBar(), fileMenu, editMenu, toolsMenu, transformMenu, previewMenu, helpMenu);
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
    auto* batchTransformGroupSeparator = new QAction(this);
    batchTransformGroupSeparator->setSeparator(true);
    auto* batchTransformClearSeparator = new QAction(this);
    batchTransformClearSeparator->setSeparator(true);
    editor->setBatchTransformActions({
        transformMirrorLeftRightAction_,
        transformMirrorUpDownAction_,
        transformRotate180Action_,
        transformRotate45CounterClockwiseAction_,
        transformRotate45ClockwiseAction_,
        batchTransformGroupSeparator,
        transformRaiseSubdivisionAction_,
        transformLowerSubdivisionAction_,
        transformRaiseSubdivisionHalfStepAction_,
        transformLowerSubdivisionHalfStepAction_,
        batchTransformClearSeparator,
        transformClearCompleteElementsAction_,
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
    connect(editor, &PlainCodeEditor::selectionReplacementAboutToEdit, this, [this](int anchor, int position) {
        if (documentSection_ != nullptr) {
            documentSection_->recordChartSelectionUndoRestoreAfterNextEdit(anchor, position);
        }
    });
    connect(editor, &PlainCodeEditor::clearCompleteElementsShortcutRequested, this, [this]() {
        if (transformClearCompleteElementsAction_ != nullptr) {
            transformClearCompleteElementsAction_->trigger();
        }
    });
    connect(editor, &PlainCodeEditor::raiseSubdivisionHalfStepShortcutRequested, this, [this]() {
        if (transformRaiseSubdivisionHalfStepAction_ != nullptr) {
            transformRaiseSubdivisionHalfStepAction_->trigger();
        }
    });
    connect(editor, &PlainCodeEditor::lowerSubdivisionHalfStepShortcutRequested, this, [this]() {
        if (transformLowerSubdivisionHalfStepAction_ != nullptr) {
            transformLowerSubdivisionHalfStepAction_->trigger();
        }
    });
    connect(editor, &PlainCodeEditor::editorOverwriteModeChanged, this, [this](bool enabled) {
        applyEditorOverwriteModeEnabled(enabled, true);
    });
    connect(editor, &PlainCodeEditor::lineNumberBookmarkActivated, this, &MainWindow::activateBookmarkAtLine);
    connect(editor, &PlainCodeEditor::lineNumberBookmarkRenameRequested, this, [this](int line) {
        if (documentSection_ != nullptr) {
            documentSection_->revealBookmarkInSidebar(activeDifficultyId_, line, true);
        }
    });
    connect(editor, &PlainCodeEditor::lineNumberBookmarkDeleteRequested, this, [this](int line) {
        if (editorSection_ != nullptr) {
            editorSection_->deleteBookmarkAtLineWithConfirmation(line);
        }
    });
    connect(editor, &PlainCodeEditor::lineNumberBookmarkContextMenuRequested, this,
            [this, editor](int line, const QPoint& globalPos) {
        // Line-number-gutter right-click: the same bookmark actions the editor
        // body menu offers, anchored at the gutter position.
        QMenu menu(editor);
        menu.setFont(uiAccentFont(10));
        styleRoundedMenu(menu);
        const bool hasBookmark = editor->bookmarkedLines().contains(line);
        if (!hasBookmark) {
            QAction* createAction = menu.addAction(UiText::text(QStringLiteral("metadata.insert_bookmark")));
            connect(createAction, &QAction::triggered, this, [this, line]() {
                if (editorSection_ != nullptr) {
                    editorSection_->createBookmarkAtLine(line, true);
                }
            });
        } else {
            QAction* renameAction = menu.addAction(UiText::text(QStringLiteral("metadata.rename_bookmark")));
            connect(renameAction, &QAction::triggered, this, [this, line]() {
                if (documentSection_ != nullptr) {
                    documentSection_->revealBookmarkInSidebar(activeDifficultyId_, line, true);
                }
            });
            QAction* deleteAction = menu.addAction(UiText::text(QStringLiteral("editor.delete_bookmark")));
            connect(deleteAction, &QAction::triggered, this, [this, line]() {
                if (editorSection_ != nullptr) {
                    editorSection_->deleteBookmarkAtLineWithConfirmation(line);
                }
            });
            QAction* revealAction = menu.addAction(UiText::text(QStringLiteral("metadata.show_in_sidebar")));
            connect(revealAction, &QAction::triggered, this, [this, line]() {
                if (documentSection_ != nullptr) {
                    documentSection_->revealBookmarkInSidebar(activeDifficultyId_, line, false);
                }
            });
        }
        menu.exec(globalPos);
    });
    chartBracketHighlighter_ = new BracketScopeHighlighter(editor->document());
    editorWidget_ = editor;
    editorWidget_->setFont(codeFont);
    editorWidget_->setStyleSheet(UiTheme::editorTextEditStyleSheet());
    editorWidget_->setAutoFillBackground(false);
    editorWidget_->setAttribute(Qt::WA_TranslucentBackground, true);
    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(editorWidget_)) {
        if (QWidget* editorViewport = scrollArea->viewport(); editorViewport != nullptr) {
            editorViewport->setAutoFillBackground(false);
            editorViewport->setAttribute(Qt::WA_TranslucentBackground, true);
        }
        if (QScrollBar* vbar = scrollArea->verticalScrollBar()) {
            vbar->setStyleSheet(modernScrollBarStyle());
        }
        if (QScrollBar* hbar = scrollArea->horizontalScrollBar()) {
            hbar->setStyleSheet(modernScrollBarStyle());
        }
    }
    logStartupStage("editor_widget_ready");

    auto* central = new miacode::ui::AppBackgroundSurfaceWidget(this);
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
    copyAreaEditor_->setPlaceholderText(UiText::text(QStringLiteral("metadata.copy_area")));
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
    ui_.exportPage_ = new miacode::export_page::ExportLauncherPage(this);
    editorStack_->addWidget(ui_.exportPage_);
    ui_.qmlExportSession_ = new QmlExportSession(*this, this);
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
    // Busy spinner floated over the "Export" sidebar row — shown while the
    // export page (its embedded video panel especially) is being built, which
    // is noticeably slow. Same viewport-overlay pattern as the delete button.
    outlineBusySpinner_ = new miacode::ui::BusySpinner(outlineList_->viewport());
    outlineBusySpinner_->hide();
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
            if (switchToDifficultyField(difficultyId) && editorWidget_ != nullptr) {
                editorWidget_->setFocus();
            }
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
    connect(outlineList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        // Inline-rename commit. Rebuilds run under QSignalBlocker and the
        // accent-marker updates block signals too, so reaching here means the
        // item editor wrote a new display text.
        if (item == nullptr || editorSection_ == nullptr) {
            return;
        }
        if (item->data(kOutlineItemKindRole).toString() != QLatin1String("bookmark")) {
            return;
        }
        const int difficultyId = item->data(kOutlineItemDifficultyRole).toInt();
        const int line = item->data(kOutlineItemLineRole).toInt();
        editorSection_->renameBookmark(difficultyId, line, item->text());
        // Rebuild queued (not inline): the view may still hold the closing
        // editor for this item. Restores canonical text on an empty/refused
        // rename and refreshes the tooltip on success.
        QTimer::singleShot(0, this, [this]() {
            if (documentSection_ != nullptr) {
                documentSection_->rebuildFieldSidebar();
            }
        });
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
            QAction* deleteAction = menu.addAction(UiText::text(QStringLiteral("metadata.delete")));
            connect(deleteAction, &QAction::triggered, this, [this, bookmarkDifficultyId, line]() {
                if (SimaiDocument::isDifficultyId(bookmarkDifficultyId) && bookmarkDifficultyId != activeDifficultyId_) {
                    activeOutlineKey_ = "chart";
                    if (!switchToDifficultyField(bookmarkDifficultyId)) {
                        return;
                    }
                }
                if (editorSection_ != nullptr) {
                    editorSection_->deleteBookmarkAtLineWithConfirmation(line);
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

    if (netBatchDownloadAction_ != nullptr) {
        toolboxMenu_->addSeparator();
        toolboxMenu_->addAction(netBatchDownloadAction_);
    }

    // Copy Area is intentionally hidden from the toolbox per the toolbox
    // revamp. The feature itself is kept intact — copyAreaPanel_/copyAreaEditor_,
    // fullCopyAreaAction_, and setFullCopyAreaVisible() all still exist — so
    // flipping this constant back to `true` resurfaces it unchanged.
    constexpr bool kCopyAreaIntegratedIntoToolbox = false;
    if (kCopyAreaIntegratedIntoToolbox) {
        QMenu* copyAreaMenu = toolboxMenu_->addMenu(
            UiText::text(QStringLiteral("metadata.copy_area_2"))
        );
        styleRoundedMenu(*copyAreaMenu);
        fullCopyAreaAction_ = copyAreaMenu->addAction(
            UiText::text(QStringLiteral("metadata.full_copy_area"))
        );
        fullCopyAreaAction_->setCheckable(true);
        connect(fullCopyAreaAction_, &QAction::toggled, this, &MainWindow::setFullCopyAreaVisible);
    }

    QAction* toolboxNetGroupEndAction = toolboxMenu_->addSeparator();

    QAction* toolboxOfficialChartMirrorAction = toolboxMenu_->addAction(
        UiText::text(QStringLiteral("menu.official_chart_mirror"))
    );
    if (extensionManager_ != nullptr) {
        extensionManager_->setToolboxMenu(toolboxMenu_, toolboxNetGroupEndAction);
    }
    connect(toolboxOfficialChartMirrorAction, &QAction::triggered, this, [openToolboxUrl]() {
        openToolboxUrl(QStringLiteral("https://www.maiviewer.net/"));
    });

    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    windowSection_->setOutlineDockCollapsed(false);
    logStartupStage("outline_ready");

    previewPanel_ = new miacode::ui::AppBackgroundSurfaceWidget(this);
    previewPanel_->setObjectName("PreviewPanel");
    previewPanel_->setStyleSheet(UiTheme::previewPanelStyleSheet());
    previewPanel_->setMinimumWidth(kEmbeddedPreviewPanelMinWidth);
    previewPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    previewCanvas_ = new PreviewRuntime(this);
    connect(previewCanvas_, &PreviewRuntime::touchPadAuthoringClicked, this, [this](const QString& pad, bool backtickSeparator) {
        const bool qmlHandlerInstalled = static_cast<bool>(qmlTouchPadAuthoringHandler_);
        const bool qmlHandled = qmlHandlerInstalled
            && qmlTouchPadAuthoringHandler_(pad, backtickSeparator);
        const auto route = miacode::qml_ui::resolveTouchPadAuthoringRoute(
            qmlHandlerInstalled, qmlHandled);
        if (route == miacode::qml_ui::TouchPadAuthoringRoute::Qml
            || route == miacode::qml_ui::TouchPadAuthoringRoute::Reject) {
            return;
        }
        auto* editor = qobject_cast<QTextEdit*>(editorWidget_);
        if (editor == nullptr || editor->document() == nullptr || editor->isReadOnly()
            || !hasActiveDifficulty() || editorStack_ == nullptr
            || editorStack_->currentWidget() != chartPage_
            || exportPreviewActive_ || QApplication::activeModalWidget() != nullptr
            || QApplication::activePopupWidget() != nullptr || pad.trimmed().isEmpty()) {
            return;
        }
        QTextCursor cursor = editor->textCursor();
        // Read the pre-edit offsets NOW: QTextCursor is document-attached and is
        // shifted by the very edit recorded below, so undo would otherwise be
        // handed an offset moved by the inserted/removed length.
        const int originalAnchor = cursor.anchor();
        const int originalPosition = cursor.position();
        // The click targets the TEXT CARET, always — never the preview-follow
        // highlight. Resolving a playhead second onto a token is not something a
        // user can predict, so the authoring point is the one they set by hand in
        // the editor. Paused caret moves already drag the preview along (the
        // cursorPositionChanged → timeline-sync path), so the two normally agree;
        // when a manual preview seek pulls them apart the caret still wins.
        const auto editPlan = miacode::editor::planTouchPadAuthoringEdit(
            editor->toPlainText(), originalPosition, pad, backtickSeparator);
        if (!editPlan.valid) {
            return;
        }
        QTextCursor tokenCursor(editor->document());
        tokenCursor.setPosition(editPlan.tokenStart);
        const QTextBlock tokenBlock = tokenCursor.block();
        double tokenSecond = 0.0;
        const bool hasTokenSecond = tokenBlock.isValid()
            && resolveTimelineSecondForCursor(
                tokenBlock.blockNumber() + 1,
                editPlan.tokenStart - tokenBlock.position() + 1,
                &tokenSecond);
        if (!miacode::editor::applyTouchPadAuthoringEdit(editor->document(), &cursor, editPlan)) {
            return;
        }
        editor->setTextCursor(cursor);
        const double seekSecond = hasTokenSecond
            ? qMax(0.0, tokenSecond - miacode::preview_interaction::kTouchPadAuthoringPreviewLeadSeconds)
            : -1.0;
        if (documentSection_ != nullptr) {
            // Undo restores the caret the click was made from, selection and all.
            documentSection_->recordChartCursorUndoEntry(
                originalAnchor,
                originalPosition,
                cursor,
                seekSecond);
        }
        editor->setFocus(Qt::OtherFocusReason);
        if (hasTokenSecond) {
            // The seek lands BEFORE the token so the touch stays visible, which
            // would otherwise draw the highlight on the preceding token. Record
            // what this parked playhead means first, so the seek's own deferred
            // decoration refresh already sees it.
            setTouchPadAuthoringAnchor(seekSecond, tokenSecond);
            seekPreviewDiscreteToSecond(seekSecond, true);
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

    auto* previewStatsCard = new miacode::ui::AppBackgroundSurfaceFrame(previewPanel_);
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

    bottomTabs_ = new miacode::ui::AppBackgroundSurfaceTabWidget(central);
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
        bottomTabs_->addTab(timelineView_, UiText::text(QStringLiteral("tab.timeline")));
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
            if (extensionManager_ != nullptr) {
                extensionManager_->publishEvent(QStringLiteral("editor.text.changed"), QJsonObject{
                    {QStringLiteral("source"), QStringLiteral("editor")},
                    {QStringLiteral("data"), QJsonObject{
                        {QStringLiteral("position"), position},
                        {QStringLiteral("removed"), charsRemoved},
                        {QStringLiteral("added"), charsAdded},
                    }},
                }, true);
            }
            syncCopyAreaLineCount();
            applyTimelineQuickChange(position, charsRemoved, charsAdded);
            if (editorSection_ != nullptr) {
                editorSection_->syncBookmarksFromEditorText(position, charsRemoved, charsAdded);
            }
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
        if (extensionManager_ != nullptr) {
            const QTextCursor cursor = qobject_cast<PlainCodeEditor*>(editorWidget_)->textCursor();
            extensionManager_->publishEvent(QStringLiteral("editor.selection.changed"), QJsonObject{
                {QStringLiteral("source"), QStringLiteral("editor")},
                {QStringLiteral("data"), QJsonObject{
                    {QStringLiteral("position"), cursor.position()},
                    {QStringLiteral("anchor"), cursor.anchor()},
                }},
            }, true);
        }
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

    previewLeftColumn_ = new miacode::ui::AppBackgroundSurfaceWidget(this);
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
    appBackgroundPainter_ = new miacode::ui::AppBackgroundPainter(this);
    applyAppBackgroundSettings(appBackgroundSettings_, false);
    syncEditorHeaderMinimumWidth();
    applyWorkspacePanelArrangement();
    updatePreviewWorkspaceLayout();
    logStartupStage("workspace_and_central_widget_ready");

    finishFrameBootstrap(toolBar, logStartupStage);
    QTimer::singleShot(0, this, [this]() {
        if (extensionManager_ != nullptr) {
            extensionManager_->refreshExtensions();
        }
    });
}

void MainWindow::applyAppBackgroundSettings(
    const miacode::ui::AppBackgroundSettings& settings,
    bool persistPreference,
    bool refreshTheme)
{
    appBackgroundSettings_ = miacode::ui::normalizedAppBackgroundSettings(settings);
    if (appBackgroundPainter_ != nullptr) {
        appBackgroundPainter_->setSettings(appBackgroundSettings_);
    }
    if (refreshTheme && windowSection_ != nullptr) {
        windowSection_->applyUiTheme();
    }
    update();

    if (persistPreference) {
        QJsonObject root = UiText::loadPreferencesObject();
        QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
        ui.insert(
            QStringLiteral("app_background"),
            miacode::ui::appBackgroundSettingsToJson(appBackgroundSettings_));
        root.insert(QStringLiteral("ui"), ui);
        UiText::savePreferencesObject(root);
        if (statusBar() != nullptr) {
            statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")), 3000);
        }
    }
}

miacode::latency::LatencySandboxController* MainWindow::latencySandboxController() const
{
    return latencySandboxController_.get();
}
