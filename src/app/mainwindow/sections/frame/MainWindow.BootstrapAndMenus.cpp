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
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

MainWindow::FrameSection::FrameSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::FrameSection::setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu)
{
    if (fileMenu == nullptr || editMenu == nullptr || transformMenu == nullptr || previewMenu == nullptr || helpMenu == nullptr) {
        return;
    }

    const auto openExternalUrl = [](const QString& url) {
        QDesktopServices::openUrl(QUrl(url));
    };

    owner_.newAction_ = new QAction(uiText("action.new", "New"), &owner_);
    owner_.newAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    connect(owner_.newAction_, &QAction::triggered, &owner_, &MainWindow::onNewFile);
    fileMenu->addAction(owner_.newAction_);

    owner_.openAction_ = new QAction(uiText("action.open", "Open..."), &owner_);
    owner_.openAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    connect(owner_.openAction_, &QAction::triggered, &owner_, &MainWindow::onOpenFile);
    fileMenu->addAction(owner_.openAction_);

    owner_.saveAction_ = new QAction(uiText("action.save", "Save"), &owner_);
    owner_.saveAction_->setShortcut(QKeySequence::Save);
    connect(owner_.saveAction_, &QAction::triggered, &owner_, &MainWindow::onSaveFile);
    fileMenu->addAction(owner_.saveAction_);

    owner_.saveAsAction_ = new QAction(uiText("action.save_as", "Save As..."), &owner_);
    owner_.saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(owner_.saveAsAction_, &QAction::triggered, &owner_, &MainWindow::onSaveFileAs);
    fileMenu->addAction(owner_.saveAsAction_);

    fileMenu->addSeparator();

    owner_.preferencesAction_ = new QAction(uiText("action.preferences", "Preferences..."), &owner_);
    connect(owner_.preferencesAction_, &QAction::triggered, &owner_, &MainWindow::onPreferences);
    fileMenu->addAction(owner_.preferencesAction_);

    fileMenu->addSeparator();

    auto* quitAction = new QAction("Quit", &owner_);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAction);

    owner_.findReplaceAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("查找/替换") : QStringLiteral("Find/Replace"),
        &owner_
    );
    owner_.findReplaceAction_->setShortcut(QKeySequence::Find);
    connect(owner_.findReplaceAction_, &QAction::triggered, &owner_, &MainWindow::onToggleFindReplace);
    editMenu->addAction(owner_.findReplaceAction_);

    editMenu->addSeparator();

    const auto invokeOnFocusedTextWidget = [this](const auto& textEditHandler, const auto& lineEditHandler) {
        if (QWidget* focus = QApplication::focusWidget(); focus != nullptr) {
            if (auto* lineEdit = qobject_cast<QLineEdit*>(focus); lineEdit != nullptr) {
                lineEditHandler(lineEdit);
                return;
            }
            if (auto* textEdit = qobject_cast<QTextEdit*>(focus); textEdit != nullptr) {
                textEditHandler(textEdit);
                return;
            }
        }
        if (auto* editor = qobject_cast<QTextEdit*>(owner_.editorWidget_); editor != nullptr) {
            textEditHandler(editor);
        }
    };

    auto* cutAction = new QAction(uiText("action.cut", "Cut"), &owner_);
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, &owner_, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->cut(); },
            [](QLineEdit* lineEdit) { lineEdit->cut(); }
        );
    });
    editMenu->addAction(cutAction);

    auto* copyAction = new QAction(uiText("action.copy", "Copy"), &owner_);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, &owner_, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->copy(); },
            [](QLineEdit* lineEdit) { lineEdit->copy(); }
        );
    });
    editMenu->addAction(copyAction);

    auto* pasteAction = new QAction(uiText("action.paste", "Paste"), &owner_);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, &owner_, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->paste(); },
            [](QLineEdit* lineEdit) { lineEdit->paste(); }
        );
    });
    editMenu->addAction(pasteAction);

    editMenu->addSeparator();

    owner_.undoAction_ = new QAction(uiText("action.undo", "Undo"), &owner_);
    owner_.undoAction_->setShortcuts(QKeySequence::keyBindings(QKeySequence::Undo));
    connect(owner_.undoAction_, &QAction::triggered, &owner_, [this]() {
        const auto focusSummary = []() {
            if (QWidget* focus = QApplication::focusWidget(); focus != nullptr) {
                return QStringLiteral("%1(name=%2 ptr=0x%3)")
                    .arg(focus->metaObject() != nullptr ? focus->metaObject()->className() : QStringLiteral("unknown"))
                    .arg(focus->objectName().isEmpty() ? QStringLiteral("(none)") : focus->objectName())
                    .arg(reinterpret_cast<quintptr>(focus), 0, 16);
            }
            return QStringLiteral("null");
        };
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("selection_restore/undo_action"),
            QStringLiteral("triggered focus=%1").arg(focusSummary()),
            true
        );
        const auto undoChartEditor = [this]() {
            if (owner_.documentSection_ != nullptr) {
                const bool handled = owner_.documentSection_->undoChartEditorWithSelectionRestore();
                miacode::debug_log::appendLine(
                    miacode::debug_log::Channel::Runtime,
                    QStringLiteral("selection_restore/undo_action"),
                    QStringLiteral("chart_editor_path handled=%1").arg(handled ? 1 : 0),
                    true
                );
                return handled;
            }
            if (auto* editor = qobject_cast<QTextEdit*>(owner_.editorWidget_); editor != nullptr
                && editor->document() != nullptr
                && editor->document()->isUndoAvailable()) {
                editor->undo();
                miacode::debug_log::appendLine(
                    miacode::debug_log::Channel::Runtime,
                    QStringLiteral("selection_restore/undo_action"),
                    QStringLiteral("chart_editor_fallback handled=1"),
                    true
                );
                return true;
            }
            return false;
        };
        bool handled = false;
        if (QWidget* focus = QApplication::focusWidget(); focus != nullptr) {
            if (auto* lineEdit = qobject_cast<QLineEdit*>(focus); lineEdit != nullptr) {
                if (lineEdit->isUndoAvailable()) {
                    lineEdit->undo();
                    handled = true;
                }
            } else if (auto* textEdit = qobject_cast<QTextEdit*>(focus); textEdit != nullptr) {
                if (textEdit == owner_.editorWidget_) {
                    handled = undoChartEditor();
                } else if (textEdit->document() != nullptr && textEdit->document()->isUndoAvailable()) {
                    textEdit->undo();
                    handled = true;
                }
            }
        }
        if (!handled) {
            handled = undoChartEditor();
        }
        if (!handled) {
            (void)owner_.undoDeletedDifficultyField();
        }
    });
    editMenu->addAction(owner_.undoAction_);

    owner_.redoAction_ = new QAction(uiText("action.redo", "Redo"), &owner_);
    owner_.redoAction_->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));
    connect(owner_.redoAction_, &QAction::triggered, &owner_, [this]() {
        const auto focusSummary = []() {
            if (QWidget* focus = QApplication::focusWidget(); focus != nullptr) {
                return QStringLiteral("%1(name=%2 ptr=0x%3)")
                    .arg(focus->metaObject() != nullptr ? focus->metaObject()->className() : QStringLiteral("unknown"))
                    .arg(focus->objectName().isEmpty() ? QStringLiteral("(none)") : focus->objectName())
                    .arg(reinterpret_cast<quintptr>(focus), 0, 16);
            }
            return QStringLiteral("null");
        };
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("selection_restore/redo_action"),
            QStringLiteral("triggered focus=%1").arg(focusSummary()),
            true
        );
        const auto redoChartEditor = [this]() {
            if (owner_.documentSection_ != nullptr) {
                const bool handled = owner_.documentSection_->redoChartEditorWithSelectionRestore();
                miacode::debug_log::appendLine(
                    miacode::debug_log::Channel::Runtime,
                    QStringLiteral("selection_restore/redo_action"),
                    QStringLiteral("chart_editor_path handled=%1").arg(handled ? 1 : 0),
                    true
                );
                return handled;
            }
            if (auto* editor = qobject_cast<QTextEdit*>(owner_.editorWidget_); editor != nullptr
                && editor->document() != nullptr
                && editor->document()->isRedoAvailable()) {
                editor->redo();
                miacode::debug_log::appendLine(
                    miacode::debug_log::Channel::Runtime,
                    QStringLiteral("selection_restore/redo_action"),
                    QStringLiteral("chart_editor_fallback handled=1"),
                    true
                );
                return true;
            }
            return false;
        };
        bool handled = false;
        if (QWidget* focus = QApplication::focusWidget(); focus != nullptr) {
            if (auto* lineEdit = qobject_cast<QLineEdit*>(focus); lineEdit != nullptr) {
                if (lineEdit->isRedoAvailable()) {
                    lineEdit->redo();
                    handled = true;
                }
            } else if (auto* textEdit = qobject_cast<QTextEdit*>(focus); textEdit != nullptr) {
                if (textEdit == owner_.editorWidget_) {
                    handled = redoChartEditor();
                } else if (textEdit->document() != nullptr && textEdit->document()->isRedoAvailable()) {
                    textEdit->redo();
                    handled = true;
                }
            }
        }
        if (!handled) {
            handled = redoChartEditor();
        }
    });
    editMenu->addAction(owner_.redoAction_);

    editMenu->addSeparator();

    owner_.latencyDetectorAction_ = new QAction(UiText::isChineseUi() ? QStringLiteral("BPM&&偏移检测") : QStringLiteral("BPM && Offset Detection..."), &owner_);
    connect(owner_.latencyDetectorAction_, &QAction::triggered, &owner_, &MainWindow::onOpenLatencyDetector);
    editMenu->addAction(owner_.latencyDetectorAction_);
    editMenu->addSeparator();

    owner_.validateAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("语法检查") : QStringLiteral("Syntax Check"),
        &owner_
    );
    owner_.validateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(owner_.validateAction_, &QAction::triggered, &owner_, &MainWindow::onValidateSimai);
    editMenu->addAction(owner_.validateAction_);

    owner_.normalizeWholeChartAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("谱面整理") : QStringLiteral("Format Chart"),
        &owner_
    );
    connect(owner_.normalizeWholeChartAction_, &QAction::triggered, &owner_, &MainWindow::onNormalizeWholeChart);

    owner_.transformMirrorLeftRightAction_ = new QAction(uiText("action.transform.mirror_lr", "Mirror Left/Right"), &owner_);
    owner_.transformMirrorLeftRightAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    connect(owner_.transformMirrorLeftRightAction_, &QAction::triggered, &owner_, &MainWindow::onMirrorLeftRight);
    transformMenu->addAction(owner_.transformMirrorLeftRightAction_);

    owner_.transformMirrorUpDownAction_ = new QAction(uiText("action.transform.mirror_ud", "Mirror Up/Down"), &owner_);
    owner_.transformMirrorUpDownAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(owner_.transformMirrorUpDownAction_, &QAction::triggered, &owner_, &MainWindow::onMirrorUpDown);
    transformMenu->addAction(owner_.transformMirrorUpDownAction_);

    owner_.transformRotate180Action_ = new QAction(uiText("action.transform.rotate_180", "Rotate 180"), &owner_);
    owner_.transformRotate180Action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(owner_.transformRotate180Action_, &QAction::triggered, &owner_, &MainWindow::onRotate180);
    transformMenu->addAction(owner_.transformRotate180Action_);

    owner_.transformRotate45CounterClockwiseAction_ = new QAction(uiText("action.transform.rotate_ccw_45", "Rotate -45"), &owner_);
    owner_.transformRotate45CounterClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    connect(owner_.transformRotate45CounterClockwiseAction_, &QAction::triggered, &owner_, &MainWindow::onRotate45CounterClockwise);
    transformMenu->addAction(owner_.transformRotate45CounterClockwiseAction_);

    owner_.transformRotate45ClockwiseAction_ = new QAction(uiText("action.transform.rotate_cw_45", "Rotate +45"), &owner_);
    owner_.transformRotate45ClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    connect(owner_.transformRotate45ClockwiseAction_, &QAction::triggered, &owner_, &MainWindow::onRotate45Clockwise);
    transformMenu->addAction(owner_.transformRotate45ClockwiseAction_);

    transformMenu->addSeparator();
    owner_.transformRaiseSubdivisionAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("分音提升一档") : QStringLiteral("Subdivision +1"),
        &owner_);
    owner_.transformRaiseSubdivisionAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+=")));
    connect(owner_.transformRaiseSubdivisionAction_, &QAction::triggered, &owner_, &MainWindow::onRaiseSubdivisionSelection);
    transformMenu->addAction(owner_.transformRaiseSubdivisionAction_);

    owner_.transformLowerSubdivisionAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("分音降低一档") : QStringLiteral("Subdivision -1"),
        &owner_);
    owner_.transformLowerSubdivisionAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    connect(owner_.transformLowerSubdivisionAction_, &QAction::triggered, &owner_, &MainWindow::onLowerSubdivisionSelection);
    transformMenu->addAction(owner_.transformLowerSubdivisionAction_);
    transformMenu->addSeparator();

    auto* moreTransformMenu = transformMenu->addMenu(uiText("action.transform.more", "More..."));
    owner_.transformToggleBreakAction_ = new QAction(uiText("action.transform.toggle_break", "Toggle Break"), &owner_);
    owner_.transformToggleBreakAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(owner_.transformToggleBreakAction_, &QAction::triggered, &owner_, &MainWindow::onToggleBreakSelection);
    moreTransformMenu->addAction(owner_.transformToggleBreakAction_);

    owner_.transformToggleExAction_ = new QAction(uiText("action.transform.toggle_ex", "Toggle EX"), &owner_);
    owner_.transformToggleExAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    connect(owner_.transformToggleExAction_, &QAction::triggered, &owner_, &MainWindow::onToggleExSelection);
    moreTransformMenu->addAction(owner_.transformToggleExAction_);

    owner_.transformToggleFireworkAction_ = new QAction(uiText("action.transform.toggle_firework", "Toggle Firework"), &owner_);
    owner_.transformToggleFireworkAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(owner_.transformToggleFireworkAction_, &QAction::triggered, &owner_, &MainWindow::onToggleFireworkSelection);
    moreTransformMenu->addAction(owner_.transformToggleFireworkAction_);

    owner_.transformRandomRotateAction_ = new QAction(uiText("action.transform.random_rotate", "Random Rotate"), &owner_);
    owner_.transformRandomRotateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(owner_.transformRandomRotateAction_, &QAction::triggered, &owner_, &MainWindow::onRandomRotateSelection);
    moreTransformMenu->addAction(owner_.transformRandomRotateAction_);

    owner_.stopPreviewAction_ = new QAction(uiText("action.stop_preview", "Stop Preview"), &owner_);
    owner_.stopPreviewAction_->setIcon(makePreviewStopIcon(QColor("#2B3C4E")));
    owner_.stopPreviewAction_->setToolTip(QString());
    connect(owner_.stopPreviewAction_, &QAction::triggered, &owner_, &MainWindow::onStopPreview);
    previewMenu->addAction(owner_.stopPreviewAction_);

    owner_.pausePreviewAction_ = new QAction(uiText("action.pause_preview", "Play/Pause Preview"), &owner_);
    owner_.pausePreviewAction_->setIcon(makePreviewPlayIcon(QColor("#2B3C4E")));
    owner_.pausePreviewAction_->setToolTip(QString());
    connect(owner_.pausePreviewAction_, &QAction::triggered, &owner_, &MainWindow::onTogglePreviewPause);
    previewMenu->addAction(owner_.pausePreviewAction_);
    owner_.stopOrPlayPreviewShortcutAction_ = new QAction(&owner_);
    owner_.stopOrPlayPreviewShortcutAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    owner_.stopOrPlayPreviewShortcutAction_->setShortcutContext(Qt::ApplicationShortcut);
    owner_.addAction(owner_.stopOrPlayPreviewShortcutAction_);
    connect(owner_.stopOrPlayPreviewShortcutAction_, &QAction::triggered, &owner_, [this]() {
        if (state_.qtPreviewPlaying_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
            owner_.onStopPreview();
            return;
        }
        owner_.onTogglePreviewPause();
    });
    owner_.playPausePreviewShortcutAction_ = new QAction(&owner_);
    owner_.playPausePreviewShortcutAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    owner_.playPausePreviewShortcutAction_->setShortcutContext(Qt::ApplicationShortcut);
    owner_.addAction(owner_.playPausePreviewShortcutAction_);
    connect(owner_.playPausePreviewShortcutAction_, &QAction::triggered, &owner_, &MainWindow::onTogglePreviewPause);

    auto* previewSlowerAction = new QAction(
        uiText("action.preview_speed_down", "Playback Speed -"),
        &owner_
    );
    owner_.previewSlowerAction_ = previewSlowerAction;
    previewSlowerAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(previewSlowerAction, &QAction::triggered, &owner_, [this]() {
        owner_.applyPreviewPlaybackRate(steppedPreviewPlaybackRate(owner_.previewPlaybackRate_, -1));
    });
    previewMenu->addAction(previewSlowerAction);

    auto* previewFasterAction = new QAction(
        uiText("action.preview_speed_up", "Playback Speed +"),
        &owner_
    );
    owner_.previewFasterAction_ = previewFasterAction;
    previewFasterAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    connect(previewFasterAction, &QAction::triggered, &owner_, [this]() {
        owner_.applyPreviewPlaybackRate(steppedPreviewPlaybackRate(owner_.previewPlaybackRate_, 1));
    });
    previewMenu->addAction(previewFasterAction);

    owner_.exportVideoAction_ = new QAction(
        uiText("action.export_chart", "Export Chart"),
        &owner_
    );
    connect(owner_.exportVideoAction_, &QAction::triggered, &owner_, &MainWindow::onExportPreviewVideo);
    previewMenu->addAction(owner_.exportVideoAction_);

    previewMenu->addSeparator();

    auto* renderModeGroup = new QActionGroup(previewMenu);
    renderModeGroup->setExclusive(true);
    const QIcon selectedRenderModeIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent);
    const QIcon unselectedRenderModeIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent, false);

    owner_.renderModeNativeAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("预览模式：谱面确认") : QStringLiteral("Preview Mode: Chart Review"),
        &owner_
    );
    owner_.renderModeNativeAction_->setCheckable(true);
    owner_.renderModeNativeAction_->setChecked(owner_.muriRenderOptions_.renderMode == RenderMode::Native);
    owner_.renderModeNativeAction_->setIcon(
        owner_.renderModeNativeAction_->isChecked() ? selectedRenderModeIcon : unselectedRenderModeIcon
    );
    connect(owner_.renderModeNativeAction_, &QAction::triggered, &owner_, [this]() {
        owner_.setMuriRenderMode(RenderMode::Native);
    });
    renderModeGroup->addAction(owner_.renderModeNativeAction_);
    previewMenu->addAction(owner_.renderModeNativeAction_);

    owner_.renderModeMaimuriDxAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("预览模式：无理检测") : QStringLiteral("Preview Mode: Muri Check"),
        &owner_
    );
    owner_.renderModeMaimuriDxAction_->setCheckable(true);
    owner_.renderModeMaimuriDxAction_->setChecked(owner_.muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle);
    owner_.renderModeMaimuriDxAction_->setIcon(
        owner_.renderModeMaimuriDxAction_->isChecked() ? selectedRenderModeIcon : unselectedRenderModeIcon
    );
    connect(owner_.renderModeMaimuriDxAction_, &QAction::triggered, &owner_, [this]() {
        owner_.setMuriRenderMode(RenderMode::MaimuriDxStyle);
    });
    renderModeGroup->addAction(owner_.renderModeMaimuriDxAction_);
    previewMenu->addAction(owner_.renderModeMaimuriDxAction_);

    owner_.editStaticTapOnSlideThresholdAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("撞尾阈值...") : QStringLiteral("Tap-On-Slide Threshold..."),
        &owner_
    );
    connect(
        owner_.editStaticTapOnSlideThresholdAction_,
        &QAction::triggered,
        &owner_,
        &MainWindow::onEditStaticTapOnSlideThreshold);
    previewMenu->addAction(owner_.editStaticTapOnSlideThresholdAction_);

    previewMenu->addSeparator();

    owner_.previewAudioSettingsAction_ = new QAction(uiText("action.audio_settings", "Audio Settings..."), &owner_);
    connect(owner_.previewAudioSettingsAction_, &QAction::triggered, &owner_, &MainWindow::onPreviewAudioSettings);
    previewMenu->addAction(owner_.previewAudioSettingsAction_);

    owner_.previewVideoSettingsAction_ = new QAction(uiText("action.video_settings", "Video Settings..."), &owner_);
    connect(owner_.previewVideoSettingsAction_, &QAction::triggered, &owner_, &MainWindow::onPreviewVideoSettings);
    previewMenu->addAction(owner_.previewVideoSettingsAction_);

    owner_.swapWorkspaceSidesAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("左右面板互换") : QStringLiteral("Swap Side Panels"),
        &owner_
    );
    owner_.swapWorkspaceSidesAction_->setCheckable(true);
    owner_.swapWorkspaceSidesAction_->setIcon(
        makeMenuSelectionCheckIcon(UiTheme::colors().accent, owner_.workspacePanelsSwapped_)
    );
    connect(owner_.swapWorkspaceSidesAction_, &QAction::toggled, &owner_, [this](bool checked) {
        owner_.setWorkspacePanelsSwapped(checked, true);
    });
    previewMenu->addAction(owner_.swapWorkspaceSidesAction_);

    auto* officialChartMirrorAction = new QAction(
        UiText::isChineseUi() ? QStringLiteral("官谱镜像站") : QStringLiteral("Official Chart Mirror"),
        &owner_
    );
    connect(officialChartMirrorAction, &QAction::triggered, &owner_, [openExternalUrl]() {
        openExternalUrl(QStringLiteral("https://www.maiviewer.net/"));
    });
    helpMenu->addAction(officialChartMirrorAction);

    auto* simaiWikiAction = new QAction(
        UiText::isChineseUi() ? QStringLiteral("simaiwiki") : QStringLiteral("simaiwiki"),
        &owner_
    );
    connect(simaiWikiAction, &QAction::triggered, &owner_, [openExternalUrl]() {
        openExternalUrl(QStringLiteral("https://w.atwiki.jp/simai/"));
    });
    helpMenu->addAction(simaiWikiAction);
    helpMenu->addSeparator();

    owner_.aboutAction_ = new QAction(uiText("action.about", "About"), &owner_);
    connect(owner_.aboutAction_, &QAction::triggered, &owner_, &MainWindow::onAbout);
    helpMenu->addAction(owner_.aboutAction_);
}

void MainWindow::setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu)
{
    frameSection_->setupMenusAndActions(fileMenu, editMenu, transformMenu, previewMenu, helpMenu);
}
