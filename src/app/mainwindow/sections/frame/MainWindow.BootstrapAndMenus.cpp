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
#include "ShortcutRegistry.h"
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

    owner_.newAction_ = new QAction(UiText::text(QStringLiteral("action.new")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.newAction_,
        QStringLiteral("file.new"),
        QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    connect(owner_.newAction_, &QAction::triggered, &owner_, &MainWindow::onNewFile);
    fileMenu->addAction(owner_.newAction_);

    owner_.openAction_ = new QAction(UiText::text(QStringLiteral("action.open")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.openAction_,
        QStringLiteral("file.open"),
        QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    connect(owner_.openAction_, &QAction::triggered, &owner_, &MainWindow::onOpenFile);
    fileMenu->addAction(owner_.openAction_);

    fileMenu->addSeparator();

    owner_.openCurrentFolderAction_ = new QAction(UiText::text(QStringLiteral("action.open_current_folder")), &owner_);
    connect(owner_.openCurrentFolderAction_, &QAction::triggered, &owner_, &MainWindow::onOpenCurrentFolder);
    fileMenu->addAction(owner_.openCurrentFolderAction_);

    owner_.recentFilesMenu_ = fileMenu->addMenu(UiText::text(QStringLiteral("action.open_recent")));
    connect(owner_.recentFilesMenu_, &QMenu::aboutToShow, &owner_, [this]() {
        owner_.refreshRecentFilesMenu(owner_.recentFilesMenu_);
    });

    owner_.restoreBackupMenu_ = fileMenu->addMenu(UiText::text(QStringLiteral("action.restore_backup")));
    connect(owner_.restoreBackupMenu_, &QMenu::aboutToShow, &owner_, [this]() {
        owner_.refreshRestoreBackupMenu(owner_.restoreBackupMenu_);
    });
    connect(fileMenu, &QMenu::aboutToShow, &owner_, [this]() {
        if (owner_.openCurrentFolderAction_ != nullptr) {
            owner_.openCurrentFolderAction_->setEnabled(!owner_.currentFilePath_.isEmpty());
        }
        if (owner_.recentFilesMenu_ != nullptr) {
            owner_.recentFilesMenu_->setEnabled(!owner_.recentFilePaths_.isEmpty());
        }
        if (owner_.restoreBackupMenu_ != nullptr) {
            owner_.restoreBackupMenu_->setEnabled(!owner_.currentFilePath_.isEmpty());
        }
    });

    fileMenu->addSeparator();

    owner_.saveAction_ = new QAction(UiText::text(QStringLiteral("action.save")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.saveAction_,
        QStringLiteral("file.save"),
        QKeySequence::Save);
    connect(owner_.saveAction_, &QAction::triggered, &owner_, &MainWindow::onSaveFile);
    fileMenu->addAction(owner_.saveAction_);

    owner_.saveAsAction_ = new QAction(UiText::text(QStringLiteral("action.save_as")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.saveAsAction_,
        QStringLiteral("file.save_as"),
        QKeySequence::SaveAs);
    connect(owner_.saveAsAction_, &QAction::triggered, &owner_, &MainWindow::onSaveFileAs);
    fileMenu->addAction(owner_.saveAsAction_);

    owner_.packAsZipAction_ = new QAction(
        UiText::text(QStringLiteral("menu.export_as_zip")),
        &owner_
    );
    connect(owner_.packAsZipAction_, &QAction::triggered, &owner_, &MainWindow::onPackAsZip);
    fileMenu->addAction(owner_.packAsZipAction_);

    owner_.preferencesAction_ = new QAction(UiText::text(QStringLiteral("action.preferences")), &owner_);
    connect(owner_.preferencesAction_, &QAction::triggered, &owner_, &MainWindow::onPreferences);
    fileMenu->addAction(owner_.preferencesAction_);

    fileMenu->addSeparator();

    auto* quitAction = new QAction("Quit", &owner_);
    ShortcutRegistry::instance().applyShortcut(
        quitAction,
        QStringLiteral("file.quit"),
        QKeySequence(QStringLiteral("Ctrl+Esc")));
    connect(quitAction, &QAction::triggered, &owner_, [&owner = owner_]() {
        // QuickShell's visible root window owns the close relay and unsaved
        // confirmation. Closing its hidden QWidget backend leaves the shell
        // alive, while QApplication::quit() cannot be vetoed by Cancel.
        if (!owner.quickShellRootWindow_.isNull()) {
            owner.quickShellRootWindow_->close();
            return;
        }
        owner.close();
    });
    fileMenu->addAction(quitAction);

    owner_.findReplaceAction_ = new QAction(
        UiText::text(QStringLiteral("menu.find_replace")),
        &owner_
    );
    ShortcutRegistry::instance().applyShortcut(
        owner_.findReplaceAction_,
        QStringLiteral("edit.find_replace"),
        QKeySequence::Find);
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

    auto* cutAction = new QAction(UiText::text(QStringLiteral("action.cut")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        cutAction,
        QStringLiteral("edit.cut"),
        QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, &owner_, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->cut(); },
            [](QLineEdit* lineEdit) { lineEdit->cut(); }
        );
    });
    editMenu->addAction(cutAction);

    auto* copyAction = new QAction(UiText::text(QStringLiteral("action.copy")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        copyAction,
        QStringLiteral("edit.copy"),
        QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, &owner_, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->copy(); },
            [](QLineEdit* lineEdit) { lineEdit->copy(); }
        );
    });
    editMenu->addAction(copyAction);

    auto* pasteAction = new QAction(UiText::text(QStringLiteral("action.paste")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        pasteAction,
        QStringLiteral("edit.paste"),
        QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, &owner_, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->paste(); },
            [](QLineEdit* lineEdit) { lineEdit->paste(); }
        );
    });
    editMenu->addAction(pasteAction);

    editMenu->addSeparator();

    owner_.undoAction_ = new QAction(UiText::text(QStringLiteral("action.undo")), &owner_);
    ShortcutRegistry::instance().applyShortcuts(
        owner_.undoAction_,
        QStringLiteral("edit.undo"),
        QKeySequence::keyBindings(QKeySequence::Undo));
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

    owner_.redoAction_ = new QAction(UiText::text(QStringLiteral("action.redo")), &owner_);
    ShortcutRegistry::instance().applyShortcuts(
        owner_.redoAction_,
        QStringLiteral("edit.redo"),
        QKeySequence::keyBindings(QKeySequence::Redo));
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

    owner_.latencyDetectorAction_ = new QAction(
        UiText::text(QStringLiteral("menu.bpm_latency")),
        &owner_);
    connect(owner_.latencyDetectorAction_, &QAction::triggered, &owner_, [this]() {
        owner_.switchToLatencyField();
    });
    editMenu->addAction(owner_.latencyDetectorAction_);
    editMenu->addSeparator();

    owner_.prependTrackSilenceAction_ = new QAction(
        UiText::text(QStringLiteral("media_tools.prepend_track_silence_action")),
        &owner_
    );
    connect(owner_.prependTrackSilenceAction_, &QAction::triggered, &owner_, &MainWindow::onPrependTrackSilence);
    owner_.prependPvBlackAction_ = new QAction(
        UiText::text(QStringLiteral("media_tools.prepend_pv_black_screen_action")),
        &owner_
    );
    connect(owner_.prependPvBlackAction_, &QAction::triggered, &owner_, &MainWindow::onPrependPvBlack);
    owner_.compressBackgroundVideoAction_ = new QAction(
        UiText::text(QStringLiteral("media_tools.compress_video_action")),
        &owner_
    );
    connect(owner_.compressBackgroundVideoAction_, &QAction::triggered, &owner_, &MainWindow::onCompressBackgroundVideo);
    owner_.convertTrackTo44100HzAction_ = new QAction(
        UiText::text(QStringLiteral("media_tools.sample_rate_action")),
        &owner_
    );
    connect(owner_.convertTrackTo44100HzAction_, &QAction::triggered, &owner_, &MainWindow::onConvertTrackTo44100Hz);

    owner_.netBatchDownloadAction_ = new QAction(
        UiText::text(QStringLiteral("net.net_batch_download_action")),
        &owner_
    );
    connect(owner_.netBatchDownloadAction_, &QAction::triggered, &owner_, &MainWindow::onNetBatchDownload);
    owner_.normalizeWholeChartAction_ = new QAction(
        UiText::text(QStringLiteral("menu.format_chart")),
        &owner_
    );
    connect(owner_.normalizeWholeChartAction_, &QAction::triggered, &owner_, &MainWindow::onNormalizeWholeChart);

    owner_.transformMirrorLeftRightAction_ = new QAction(UiText::text(QStringLiteral("action.transform.mirror_lr")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformMirrorLeftRightAction_,
        QStringLiteral("transform.mirror_lr"),
        QKeySequence(Qt::CTRL | Qt::Key_J));
    connect(owner_.transformMirrorLeftRightAction_, &QAction::triggered, &owner_, &MainWindow::onMirrorLeftRight);
    transformMenu->addAction(owner_.transformMirrorLeftRightAction_);

    owner_.transformMirrorUpDownAction_ = new QAction(UiText::text(QStringLiteral("action.transform.mirror_ud")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformMirrorUpDownAction_,
        QStringLiteral("transform.mirror_ud"),
        QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(owner_.transformMirrorUpDownAction_, &QAction::triggered, &owner_, &MainWindow::onMirrorUpDown);
    transformMenu->addAction(owner_.transformMirrorUpDownAction_);

    owner_.transformRotate180Action_ = new QAction(UiText::text(QStringLiteral("action.transform.rotate_180")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformRotate180Action_,
        QStringLiteral("transform.rotate_180"),
        QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(owner_.transformRotate180Action_, &QAction::triggered, &owner_, &MainWindow::onRotate180);
    transformMenu->addAction(owner_.transformRotate180Action_);

    owner_.transformRotate45CounterClockwiseAction_ = new QAction(UiText::text(QStringLiteral("action.transform.rotate_ccw_45")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformRotate45CounterClockwiseAction_,
        QStringLiteral("transform.rotate_ccw_45"),
        QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    connect(owner_.transformRotate45CounterClockwiseAction_, &QAction::triggered, &owner_, &MainWindow::onRotate45CounterClockwise);
    transformMenu->addAction(owner_.transformRotate45CounterClockwiseAction_);

    owner_.transformRotate45ClockwiseAction_ = new QAction(UiText::text(QStringLiteral("action.transform.rotate_cw_45")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformRotate45ClockwiseAction_,
        QStringLiteral("transform.rotate_cw_45"),
        QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    connect(owner_.transformRotate45ClockwiseAction_, &QAction::triggered, &owner_, &MainWindow::onRotate45Clockwise);
    transformMenu->addAction(owner_.transformRotate45ClockwiseAction_);

    transformMenu->addSeparator();
    owner_.transformRaiseSubdivisionAction_ = new QAction(
        UiText::text(QStringLiteral("document.subdivision_plus_1")),
        &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformRaiseSubdivisionAction_,
        QStringLiteral("transform.subdivision_up"),
        QKeySequence(QStringLiteral("Ctrl+=")));
    connect(owner_.transformRaiseSubdivisionAction_, &QAction::triggered, &owner_, &MainWindow::onRaiseSubdivisionSelection);
    transformMenu->addAction(owner_.transformRaiseSubdivisionAction_);

    owner_.transformLowerSubdivisionAction_ = new QAction(
        UiText::text(QStringLiteral("document.subdivision_minus_1")),
        &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformLowerSubdivisionAction_,
        QStringLiteral("transform.subdivision_down"),
        QKeySequence(QStringLiteral("Ctrl+-")));
    connect(owner_.transformLowerSubdivisionAction_, &QAction::triggered, &owner_, &MainWindow::onLowerSubdivisionSelection);
    transformMenu->addAction(owner_.transformLowerSubdivisionAction_);

    owner_.transformRaiseSubdivisionHalfStepAction_ = new QAction(
        UiText::text(QStringLiteral("document.subdivision_plus_half")),
        &owner_);
    ShortcutRegistry::instance().applyShortcuts(
        owner_.transformRaiseSubdivisionHalfStepAction_,
        QStringLiteral("transform.subdivision_half_up"),
        {QKeySequence(QStringLiteral("Ctrl+Shift+=")), QKeySequence(QStringLiteral("Ctrl++"))});
    connect(owner_.transformRaiseSubdivisionHalfStepAction_, &QAction::triggered, &owner_, &MainWindow::onRaiseSubdivisionHalfStepSelection);
    transformMenu->addAction(owner_.transformRaiseSubdivisionHalfStepAction_);

    owner_.transformLowerSubdivisionHalfStepAction_ = new QAction(
        UiText::text(QStringLiteral("document.subdivision_minus_half")),
        &owner_);
    ShortcutRegistry::instance().applyShortcuts(
        owner_.transformLowerSubdivisionHalfStepAction_,
        QStringLiteral("transform.subdivision_half_down"),
        {QKeySequence(QStringLiteral("Ctrl+Shift+-")), QKeySequence(QStringLiteral("Ctrl+_"))});
    connect(owner_.transformLowerSubdivisionHalfStepAction_, &QAction::triggered, &owner_, &MainWindow::onLowerSubdivisionHalfStepSelection);
    transformMenu->addAction(owner_.transformLowerSubdivisionHalfStepAction_);
    transformMenu->addSeparator();

    owner_.transformClearCompleteElementsAction_ = new QAction(
        UiText::text(QStringLiteral("menu.clear_elements")),
        &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformClearCompleteElementsAction_,
        QStringLiteral("transform.clear_complete_elements"),
        QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(owner_.transformClearCompleteElementsAction_, &QAction::triggered, &owner_, &MainWindow::onClearCompleteElementsSelection);
    transformMenu->addAction(owner_.transformClearCompleteElementsAction_);
    owner_.transformResetTapNotesAction_ = new QAction(UiText::text(QStringLiteral("action.transform.reset_tap_notes")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformResetTapNotesAction_,
        QStringLiteral("transform.reset_tap_notes"),
        QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(owner_.transformResetTapNotesAction_, &QAction::triggered, &owner_, &MainWindow::onResetTapNotesSelection);
    transformMenu->addAction(owner_.transformResetTapNotesAction_);
    transformMenu->addSeparator();

    auto* moreTransformMenu = transformMenu->addMenu(UiText::text(QStringLiteral("action.transform.more")));
    owner_.transformToggleBreakAction_ = new QAction(UiText::text(QStringLiteral("action.transform.toggle_break")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformToggleBreakAction_,
        QStringLiteral("transform.toggle_break"),
        QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(owner_.transformToggleBreakAction_, &QAction::triggered, &owner_, &MainWindow::onToggleBreakSelection);
    moreTransformMenu->addAction(owner_.transformToggleBreakAction_);

    owner_.transformToggleExAction_ = new QAction(UiText::text(QStringLiteral("action.transform.toggle_ex")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformToggleExAction_,
        QStringLiteral("transform.toggle_ex"),
        QKeySequence(Qt::CTRL | Qt::Key_N));
    connect(owner_.transformToggleExAction_, &QAction::triggered, &owner_, &MainWindow::onToggleExSelection);
    moreTransformMenu->addAction(owner_.transformToggleExAction_);

    owner_.transformToggleFireworkAction_ = new QAction(UiText::text(QStringLiteral("action.transform.toggle_firework")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformToggleFireworkAction_,
        QStringLiteral("transform.toggle_firework"),
        QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(owner_.transformToggleFireworkAction_, &QAction::triggered, &owner_, &MainWindow::onToggleFireworkSelection);
    moreTransformMenu->addAction(owner_.transformToggleFireworkAction_);

    owner_.transformRandomRotateAction_ = new QAction(UiText::text(QStringLiteral("action.transform.random_rotate")), &owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.transformRandomRotateAction_,
        QStringLiteral("transform.random_rotate"),
        QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(owner_.transformRandomRotateAction_, &QAction::triggered, &owner_, &MainWindow::onRandomRotateSelection);
    moreTransformMenu->addAction(owner_.transformRandomRotateAction_);

    owner_.stopPreviewAction_ = new QAction(UiText::text(QStringLiteral("action.stop_preview")), &owner_);
    owner_.stopPreviewAction_->setIcon(makePreviewStopIcon(QColor("#2B3C4E")));
    owner_.stopPreviewAction_->setToolTip(QString());
    connect(owner_.stopPreviewAction_, &QAction::triggered, &owner_, &MainWindow::onStopPreview);
    previewMenu->addAction(owner_.stopPreviewAction_);

    owner_.pausePreviewAction_ = new QAction(UiText::text(QStringLiteral("action.pause_preview")), &owner_);
    owner_.pausePreviewAction_->setIcon(makePreviewPlayIcon(QColor("#2B3C4E")));
    owner_.pausePreviewAction_->setToolTip(QString());
    connect(owner_.pausePreviewAction_, &QAction::triggered, &owner_, &MainWindow::onTogglePreviewPause);
    previewMenu->addAction(owner_.pausePreviewAction_);
    owner_.stopOrPlayPreviewShortcutAction_ = new QAction(&owner_);
    ShortcutRegistry::instance().applyShortcut(
        owner_.stopOrPlayPreviewShortcutAction_,
        QStringLiteral("preview.stop_or_play"),
        QKeySequence(QStringLiteral("Ctrl+Shift+C")));
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
    ShortcutRegistry::instance().applyShortcut(
        owner_.playPausePreviewShortcutAction_,
        QStringLiteral("preview.play_pause_global"),
        QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    owner_.playPausePreviewShortcutAction_->setShortcutContext(Qt::ApplicationShortcut);
    owner_.addAction(owner_.playPausePreviewShortcutAction_);
    connect(owner_.playPausePreviewShortcutAction_, &QAction::triggered, &owner_, &MainWindow::onTogglePreviewPause);

    auto* previewSlowerAction = new QAction(
        UiText::text(QStringLiteral("action.preview_speed_down")),
        &owner_
    );
    owner_.previewSlowerAction_ = previewSlowerAction;
    ShortcutRegistry::instance().applyShortcut(
        previewSlowerAction,
        QStringLiteral("preview.speed_down"),
        QKeySequence(QStringLiteral("Ctrl+O")));
    // Default Qt::WindowShortcut: fires while the main window is active. Preview
    // FULLSCREEN is a separate QML window, so its Ctrl+O/P are handled by the
    // QML Shortcut block in QuickShellMain.qml → controller.adjustPreviewSpeed().
    connect(previewSlowerAction, &QAction::triggered, &owner_, [this]() {
        owner_.applyPreviewPlaybackRate(steppedPreviewPlaybackRate(owner_.previewPlaybackRate_, -1));
    });
    previewMenu->addAction(previewSlowerAction);

    auto* previewFasterAction = new QAction(
        UiText::text(QStringLiteral("action.preview_speed_up")),
        &owner_
    );
    owner_.previewFasterAction_ = previewFasterAction;
    ShortcutRegistry::instance().applyShortcut(
        previewFasterAction,
        QStringLiteral("preview.speed_up"),
        QKeySequence(QStringLiteral("Ctrl+P")));
    // Fullscreen Ctrl+P is handled in QML (see the speed-down action above).
    connect(previewFasterAction, &QAction::triggered, &owner_, [this]() {
        owner_.applyPreviewPlaybackRate(steppedPreviewPlaybackRate(owner_.previewPlaybackRate_, 1));
    });
    previewMenu->addAction(previewFasterAction);

    owner_.exportVideoAction_ = new QAction(
        UiText::text(QStringLiteral("action.export_chart")),
        &owner_
    );
    // Jumps to the Export hub page (same as the toolbar Export button) — the
    // modal-dialog direct entry was removed 2026-06-12 (spec decision D6
    // overturned); the page hosts the full embedded export panel.
    connect(owner_.exportVideoAction_, &QAction::triggered, &owner_, [this]() {
        owner_.switchToExportField();
    });
    previewMenu->addAction(owner_.exportVideoAction_);

    previewMenu->addSeparator();

    auto* renderModeGroup = new QActionGroup(previewMenu);
    renderModeGroup->setExclusive(true);
    const QIcon selectedRenderModeIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent);
    const QIcon unselectedRenderModeIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent, false);

    owner_.renderModeNativeAction_ = new QAction(
        UiText::text(QStringLiteral("menu.preview_mode_chart_review")),
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
        UiText::text(QStringLiteral("menu.preview_mode_muri_check")),
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

    owner_.renderModeEraseByAreaAction_ = new QAction(
        UiText::text(QStringLiteral("menu.preview_mode_erase_by_area")),
        &owner_
    );
    owner_.renderModeEraseByAreaAction_->setCheckable(true);
    owner_.renderModeEraseByAreaAction_->setChecked(owner_.muriRenderOptions_.renderMode == RenderMode::EraseByArea);
    owner_.renderModeEraseByAreaAction_->setIcon(
        owner_.renderModeEraseByAreaAction_->isChecked() ? selectedRenderModeIcon : unselectedRenderModeIcon
    );
    connect(owner_.renderModeEraseByAreaAction_, &QAction::triggered, &owner_, [this]() {
        owner_.setMuriRenderMode(RenderMode::EraseByArea);
    });
    renderModeGroup->addAction(owner_.renderModeEraseByAreaAction_);
    previewMenu->addAction(owner_.renderModeEraseByAreaAction_);

    owner_.editStaticTapOnSlideThresholdAction_ = new QAction(
        UiText::text(QStringLiteral("menu.tap_on_slide_threshold")),
        &owner_
    );
    connect(
        owner_.editStaticTapOnSlideThresholdAction_,
        &QAction::triggered,
        &owner_,
        &MainWindow::onEditStaticTapOnSlideThreshold);
    previewMenu->addAction(owner_.editStaticTapOnSlideThresholdAction_);

    previewMenu->addSeparator();

    owner_.previewAudioSettingsAction_ = new QAction(UiText::text(QStringLiteral("action.audio_settings")), &owner_);
    connect(owner_.previewAudioSettingsAction_, &QAction::triggered, &owner_, &MainWindow::onPreviewAudioSettings);
    previewMenu->addAction(owner_.previewAudioSettingsAction_);

    owner_.previewVideoSettingsAction_ = new QAction(UiText::text(QStringLiteral("action.video_settings")), &owner_);
    connect(owner_.previewVideoSettingsAction_, &QAction::triggered, &owner_, &MainWindow::onPreviewVideoSettings);
    previewMenu->addAction(owner_.previewVideoSettingsAction_);

    owner_.skinSettingsAction_ = new QAction(UiText::text(QStringLiteral("action.skin_settings")), &owner_);
    connect(owner_.skinSettingsAction_, &QAction::triggered, &owner_, &MainWindow::onSkinSettings);
    previewMenu->addAction(owner_.skinSettingsAction_);

    owner_.swapWorkspaceSidesAction_ = new QAction(
        UiText::text(QStringLiteral("menu.swap_side_panels")),
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
        UiText::text(QStringLiteral("menu.official_chart_mirror")),
        &owner_
    );
    connect(officialChartMirrorAction, &QAction::triggered, &owner_, [openExternalUrl]() {
        openExternalUrl(QStringLiteral("https://www.maiviewer.net/"));
    });
    helpMenu->addAction(officialChartMirrorAction);

    auto* simaiWikiAction = new QAction(
        UiText::text(QStringLiteral("menu.simaiwiki")),
        &owner_
    );
    connect(simaiWikiAction, &QAction::triggered, &owner_, [openExternalUrl]() {
        openExternalUrl(QStringLiteral("https://w.atwiki.jp/simai/"));
    });
    helpMenu->addAction(simaiWikiAction);
    helpMenu->addSeparator();

    owner_.aboutAction_ = new QAction(UiText::text(QStringLiteral("action.about")), &owner_);
    connect(owner_.aboutAction_, &QAction::triggered, &owner_, &MainWindow::onAbout);
    helpMenu->addAction(owner_.aboutAction_);
}

void MainWindow::setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu)
{
    frameSection_->setupMenusAndActions(fileMenu, editMenu, transformMenu, previewMenu, helpMenu);
}
