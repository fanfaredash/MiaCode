void MainWindow::setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu)
{
    if (fileMenu == nullptr || editMenu == nullptr || transformMenu == nullptr || previewMenu == nullptr || helpMenu == nullptr) {
        return;
    }

    newAction_ = new QAction(uiText("action.new", "New"), this);
    newAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    connect(newAction_, &QAction::triggered, this, &MainWindow::onNewFile);
    fileMenu->addAction(newAction_);

    openAction_ = new QAction(uiText("action.open", "Open..."), this);
    openAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    connect(openAction_, &QAction::triggered, this, &MainWindow::onOpenFile);
    fileMenu->addAction(openAction_);

    saveAction_ = new QAction(uiText("action.save", "Save"), this);
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::onSaveFile);
    fileMenu->addAction(saveAction_);

    saveAsAction_ = new QAction(uiText("action.save_as", "Save As..."), this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::onSaveFileAs);
    fileMenu->addAction(saveAsAction_);

    fileMenu->addSeparator();

    preferencesAction_ = new QAction(uiText("action.preferences", "Preferences..."), this);
    connect(preferencesAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    fileMenu->addAction(preferencesAction_);

    fileMenu->addSeparator();

    auto* quitAction = new QAction("Quit", this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAction);

    findReplaceAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("查找/替换") : QStringLiteral("Find/Replace"),
        this
    );
    findReplaceAction_->setShortcut(QKeySequence::Find);
    connect(findReplaceAction_, &QAction::triggered, this, &MainWindow::onToggleFindReplace);
    editMenu->addAction(findReplaceAction_);

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
        if (auto* editor = qobject_cast<QTextEdit*>(editorWidget_); editor != nullptr) {
            textEditHandler(editor);
        }
    };

    auto* cutAction = new QAction(uiText("action.cut", "Cut"), this);
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->cut(); },
            [](QLineEdit* lineEdit) { lineEdit->cut(); }
        );
    });
    editMenu->addAction(cutAction);

    auto* copyAction = new QAction(uiText("action.copy", "Copy"), this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->copy(); },
            [](QLineEdit* lineEdit) { lineEdit->copy(); }
        );
    });
    editMenu->addAction(copyAction);

    auto* pasteAction = new QAction(uiText("action.paste", "Paste"), this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->paste(); },
            [](QLineEdit* lineEdit) { lineEdit->paste(); }
        );
    });
    editMenu->addAction(pasteAction);

    editMenu->addSeparator();

    auto* undoAction = new QAction(uiText("action.undo", "Undo"), this);
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, [this]() {
        bool handled = false;
        if (QWidget* focus = QApplication::focusWidget(); focus != nullptr) {
            if (auto* lineEdit = qobject_cast<QLineEdit*>(focus); lineEdit != nullptr) {
                if (lineEdit->isUndoAvailable()) {
                    lineEdit->undo();
                    handled = true;
                }
            } else if (auto* textEdit = qobject_cast<QTextEdit*>(focus); textEdit != nullptr) {
                if (textEdit->document() != nullptr && textEdit->document()->isUndoAvailable()) {
                    textEdit->undo();
                    handled = true;
                }
            }
        }
        if (!handled) {
            if (auto* editor = qobject_cast<QTextEdit*>(editorWidget_); editor != nullptr
                && editor->document() != nullptr
                && editor->document()->isUndoAvailable()) {
                editor->undo();
                handled = true;
            }
        }
        if (!handled) {
            (void)undoDeletedDifficultyField();
        }
    });
    editMenu->addAction(undoAction);

    auto* redoAction = new QAction(uiText("action.redo", "Redo"), this);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, [invokeOnFocusedTextWidget]() {
        invokeOnFocusedTextWidget(
            [](QTextEdit* textEdit) { textEdit->redo(); },
            [](QLineEdit* lineEdit) { lineEdit->redo(); }
        );
    });
    editMenu->addAction(redoAction);

    editMenu->addSeparator();

    latencyDetectorAction_ = new QAction(UiText::isChineseUi() ? QStringLiteral("BPM&&偏移检测") : QStringLiteral("BPM && Offset Detection..."), this);
    connect(latencyDetectorAction_, &QAction::triggered, this, &MainWindow::onOpenLatencyDetector);
    editMenu->addAction(latencyDetectorAction_);
    editMenu->addSeparator();

    validateAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("语法检查") : QStringLiteral("Syntax Check"),
        this
    );
    validateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(validateAction_, &QAction::triggered, this, &MainWindow::onValidateSimai);
    editMenu->addAction(validateAction_);

    transformMirrorLeftRightAction_ = new QAction(uiText("action.transform.mirror_lr", "Mirror Left/Right"), this);
    transformMirrorLeftRightAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    connect(transformMirrorLeftRightAction_, &QAction::triggered, this, &MainWindow::onMirrorLeftRight);
    transformMenu->addAction(transformMirrorLeftRightAction_);

    transformMirrorUpDownAction_ = new QAction(uiText("action.transform.mirror_ud", "Mirror Up/Down"), this);
    transformMirrorUpDownAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(transformMirrorUpDownAction_, &QAction::triggered, this, &MainWindow::onMirrorUpDown);
    transformMenu->addAction(transformMirrorUpDownAction_);

    transformRotate180Action_ = new QAction(uiText("action.transform.rotate_180", "Rotate 180"), this);
    transformRotate180Action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(transformRotate180Action_, &QAction::triggered, this, &MainWindow::onRotate180);
    transformMenu->addAction(transformRotate180Action_);

    transformRotate45CounterClockwiseAction_ = new QAction(uiText("action.transform.rotate_ccw_45", "Rotate -45"), this);
    transformRotate45CounterClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    connect(transformRotate45CounterClockwiseAction_, &QAction::triggered, this, &MainWindow::onRotate45CounterClockwise);
    transformMenu->addAction(transformRotate45CounterClockwiseAction_);

    transformRotate45ClockwiseAction_ = new QAction(uiText("action.transform.rotate_cw_45", "Rotate +45"), this);
    transformRotate45ClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    connect(transformRotate45ClockwiseAction_, &QAction::triggered, this, &MainWindow::onRotate45Clockwise);
    transformMenu->addAction(transformRotate45ClockwiseAction_);

    auto* moreTransformMenu = transformMenu->addMenu(uiText("action.transform.more", "More..."));
    transformToggleBreakAction_ = new QAction(uiText("action.transform.toggle_break", "Toggle Break"), this);
    transformToggleBreakAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(transformToggleBreakAction_, &QAction::triggered, this, &MainWindow::onToggleBreakSelection);
    moreTransformMenu->addAction(transformToggleBreakAction_);

    transformToggleExAction_ = new QAction(uiText("action.transform.toggle_ex", "Toggle EX"), this);
    transformToggleExAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    connect(transformToggleExAction_, &QAction::triggered, this, &MainWindow::onToggleExSelection);
    moreTransformMenu->addAction(transformToggleExAction_);

    transformToggleFireworkAction_ = new QAction(uiText("action.transform.toggle_firework", "Toggle Firework"), this);
    transformToggleFireworkAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(transformToggleFireworkAction_, &QAction::triggered, this, &MainWindow::onToggleFireworkSelection);
    moreTransformMenu->addAction(transformToggleFireworkAction_);

    transformRandomRotateAction_ = new QAction(uiText("action.transform.random_rotate", "Random Rotate"), this);
    transformRandomRotateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(transformRandomRotateAction_, &QAction::triggered, this, &MainWindow::onRandomRotateSelection);
    moreTransformMenu->addAction(transformRandomRotateAction_);

    stopPreviewAction_ = new QAction(uiText("action.stop_preview", "Stop Preview"), this);
    stopPreviewAction_->setIcon(makePreviewStopIcon(QColor("#2B3C4E")));
    stopPreviewAction_->setToolTip(QString());
    connect(stopPreviewAction_, &QAction::triggered, this, &MainWindow::onStopPreview);
    previewMenu->addAction(stopPreviewAction_);

    pausePreviewAction_ = new QAction(uiText("action.pause_preview", "Play/Pause Preview"), this);
    pausePreviewAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    pausePreviewAction_->setIcon(makePreviewPlayIcon(QColor("#2B3C4E")));
    pausePreviewAction_->setToolTip(QString());
    connect(pausePreviewAction_, &QAction::triggered, this, &MainWindow::onTogglePreviewPause);
    previewMenu->addAction(pausePreviewAction_);

    auto* previewSlowerAction = new QAction(
        uiText("action.preview_speed_down", "Playback Speed -"),
        this
    );
    previewSlowerAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(previewSlowerAction, &QAction::triggered, this, [this]() {
        applyPreviewPlaybackRate(steppedPreviewPlaybackRate(previewPlaybackRate_, -1));
    });
    previewMenu->addAction(previewSlowerAction);

    auto* previewFasterAction = new QAction(
        uiText("action.preview_speed_up", "Playback Speed +"),
        this
    );
    previewFasterAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    connect(previewFasterAction, &QAction::triggered, this, [this]() {
        applyPreviewPlaybackRate(steppedPreviewPlaybackRate(previewPlaybackRate_, 1));
    });
    previewMenu->addAction(previewFasterAction);

    exportVideoAction_ = new QAction(
        uiText("action.export_chart", "Export Chart"),
        this
    );
    connect(exportVideoAction_, &QAction::triggered, this, &MainWindow::onExportPreviewVideo);
    previewMenu->addAction(exportVideoAction_);

    batchExportVideoAction_ = new QAction(
        uiText("action.batch_export", "Batch Export"),
        this
    );
    connect(batchExportVideoAction_, &QAction::triggered, this, &MainWindow::onBatchExportPreviewVideo);
    previewMenu->addAction(batchExportVideoAction_);

    previewMenu->addSeparator();

    auto* renderModeGroup = new QActionGroup(previewMenu);
    renderModeGroup->setExclusive(true);
    const QIcon selectedRenderModeIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent);
    const QIcon unselectedRenderModeIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent, false);

    renderModeNativeAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("预览模式：谱面确认") : QStringLiteral("Preview Mode: Chart Review"),
        this
    );
    renderModeNativeAction_->setCheckable(true);
    renderModeNativeAction_->setChecked(muriRenderOptions_.renderMode == RenderMode::Native);
    renderModeNativeAction_->setIcon(
        renderModeNativeAction_->isChecked() ? selectedRenderModeIcon : unselectedRenderModeIcon
    );
    connect(renderModeNativeAction_, &QAction::triggered, this, [this]() {
        setMuriRenderMode(RenderMode::Native);
    });
    renderModeGroup->addAction(renderModeNativeAction_);
    previewMenu->addAction(renderModeNativeAction_);

    renderModeMaimuriDxAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("预览模式：无理检测") : QStringLiteral("Preview Mode: Muri Check"),
        this
    );
    renderModeMaimuriDxAction_->setCheckable(true);
    renderModeMaimuriDxAction_->setChecked(muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle);
    renderModeMaimuriDxAction_->setIcon(
        renderModeMaimuriDxAction_->isChecked() ? selectedRenderModeIcon : unselectedRenderModeIcon
    );
    connect(renderModeMaimuriDxAction_, &QAction::triggered, this, [this]() {
        setMuriRenderMode(RenderMode::MaimuriDxStyle);
    });
    renderModeGroup->addAction(renderModeMaimuriDxAction_);
    previewMenu->addAction(renderModeMaimuriDxAction_);

    editStaticTapOnSlideThresholdAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("撞尾阈值...") : QStringLiteral("Tap-On-Slide Threshold..."),
        this
    );
    connect(
        editStaticTapOnSlideThresholdAction_,
        &QAction::triggered,
        this,
        &MainWindow::onEditStaticTapOnSlideThreshold);
    previewMenu->addAction(editStaticTapOnSlideThresholdAction_);

    previewMenu->addSeparator();

    previewAudioSettingsAction_ = new QAction(uiText("action.audio_settings", "Audio Settings..."), this);
    connect(previewAudioSettingsAction_, &QAction::triggered, this, &MainWindow::onPreviewAudioSettings);
    previewMenu->addAction(previewAudioSettingsAction_);

    previewVideoSettingsAction_ = new QAction(uiText("action.video_settings", "Preview Settings..."), this);
    connect(previewVideoSettingsAction_, &QAction::triggered, this, &MainWindow::onPreviewVideoSettings);
    previewMenu->addAction(previewVideoSettingsAction_);

    swapWorkspaceSidesAction_ = new QAction(
        UiText::isChineseUi() ? QStringLiteral("左右面板互换") : QStringLiteral("Swap Side Panels"),
        this
    );
    swapWorkspaceSidesAction_->setCheckable(true);
    swapWorkspaceSidesAction_->setIcon(
        makeMenuSelectionCheckIcon(UiTheme::colors().accent, workspacePanelsSwapped_)
    );
    connect(swapWorkspaceSidesAction_, &QAction::toggled, this, [this](bool checked) {
        setWorkspacePanelsSwapped(checked, true);
    });
    previewMenu->addAction(swapWorkspaceSidesAction_);

    aboutAction_ = new QAction(uiText("action.about", "About"), this);
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::onAbout);
    helpMenu->addAction(aboutAction_);
}
MainWindow::MainWindow(QWidget* parent)
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

    configureRuntimeDebugOutput();
    logStartupStage("configure_runtime_debug_output");

    legacyPygamePreviewEnabled_ = miacode::debug_options::envFlagEnabled(
        "MIACODE_ENABLE_PYGAME_PREVIEW",
        "MAIMURI_ENABLE_PYGAME_PREVIEW"
    );

    setWindowModified(false);
    updateWindowTitle();
    setupInitialWindowGeometry();
    if (QGuiApplication* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance()); guiApp != nullptr) {
        if (QStyleHints* styleHints = guiApp->styleHints(); styleHints != nullptr) {
            connect(styleHints, &QStyleHints::colorSchemeChanged, this, [this]() {
                applyUiTheme();
            });
        }
    }

    auto* fileMenu = menuBar()->addMenu(uiText("menu.file", "&File"));
    auto* editMenu = menuBar()->addMenu(UiText::isChineseUi() ? QStringLiteral("编辑(&E)") : QStringLiteral("&Edit"));
    auto* toolsMenu = menuBar()->addMenu(uiText("menu.tools", "&Tools"));
    auto* transformMenu = menuBar()->addMenu(uiText("menu.transform", "变换(&T)"));
    auto* previewMenu = menuBar()->addMenu(UiText::isChineseUi() ? QStringLiteral("预览(&P)") : QStringLiteral("&Preview"));
    auto* helpMenu = menuBar()->addMenu(uiText("menu.help", "&Help"));

    auto* toolBar = addToolBar("Main");
    toolBar->setMovable(false);
    setupMenusAndActions(fileMenu, editMenu, transformMenu, previewMenu, helpMenu);
    if (latencyDetectorAction_ != nullptr) {
        editMenu->removeAction(latencyDetectorAction_);
        toolsMenu->addAction(latencyDetectorAction_);
    }
    if (validateAction_ != nullptr) {
        editMenu->removeAction(validateAction_);
        toolsMenu->addSeparator();
        toolsMenu->addAction(validateAction_);
    }
    if (exportVideoAction_ != nullptr) {
        previewMenu->removeAction(exportVideoAction_);
        toolsMenu->addSeparator();
        toolsMenu->addAction(exportVideoAction_);
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
    });
    editor->setMoreBatchTransformActions({
        transformToggleBreakAction_,
        transformToggleExAction_,
        transformToggleFireworkAction_,
        transformRandomRotateAction_,
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
    central->setStyleSheet(
        "QWidget#EditorShell { background: #F5F7FA; }"
        "QFrame#EditorHeader { background: #FFFFFF; border-bottom: 1px solid #DEE4EC; }"
        "QLabel#EditorContext { color: #1F2D3D; font-weight: 700; }"
        "QLabel#EditorMeta { color: #5F6B7A; }"
        "QWidget#EditorDifficultyControls QLabel { color: #5F6B7A; }"
        "QWidget#EditorDifficultyControls QLineEdit {"
        " background: #FFFFFF;"
        " color: #1F1F1F;"
        " border: 1px solid #CCD6E2;"
        " border-radius: 6px;"
        " padding: 4px 6px;"
        " selection-background-color: #B8CCE5;"
        " selection-color: #1F1F1F;"
        "}"
        "QWidget#EditorDifficultyControls QLineEdit:focus { border-color: #3B82F6; }"
    );
    central->setObjectName("EditorShell");
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 4, 0, 0);
    centralLayout->setSpacing(0);

    auto* editorHeader = new QFrame(central);
    editorHeader->setObjectName("EditorHeader");
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
    auto* editorDifficultyLayout = new QHBoxLayout(editorDifficultyControls_);
    editorDifficultyLayout->setContentsMargins(0, 0, 0, 0);
    editorDifficultyLayout->setSpacing(8);
    auto* difficultyLevelLabel = new QLabel("Lv", editorDifficultyControls_);
    difficultyLevelLabel_ = difficultyLevelLabel;
    difficultyLevelLabel->setFont(uiAccentFont(10));
    auto* difficultyLevelLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyLevelLineEdit->setLeftPlaceholderText("&lv_n=");
    difficultyLevelEdit_ = difficultyLevelLineEdit;
    difficultyLevelEdit_->setFixedWidth(72);
    difficultyLevelEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    difficultyLevelEdit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* difficultyDesignerLabel = new QLabel(uiText("editor.des", "Des"), editorDifficultyControls_);
    difficultyDesignerLabel_ = difficultyDesignerLabel;
    difficultyDesignerLabel->setFont(uiAccentFont(10));
    auto* difficultyDesignerLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyDesignerLineEdit->setLeftPlaceholderText("&des_n=");
    difficultyDesignerEdit_ = difficultyDesignerLineEdit;
    difficultyDesignerEdit_->setFixedWidth(140);
    difficultyDesignerEdit_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    difficultyDesignerEdit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorDifficultyLayout->addWidget(difficultyLevelLabel);
    editorDifficultyLayout->addWidget(difficultyLevelEdit_);
    editorDifficultyLayout->addWidget(difficultyDesignerLabel);
    editorDifficultyLayout->addWidget(difficultyDesignerEdit_);
    editorDifficultyControls_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    editorDifficultyControls_->hide();
    editorHeaderLayout->addWidget(editorDifficultyControls_, 0);

    editorHeaderLayout->addStretch(1);

    auto* editorHeaderTrailingWidget = new QWidget(editorHeader);
    auto* editorHeaderTrailingLayout = new QHBoxLayout(editorHeaderTrailingWidget);
    editorHeaderTrailingLayout->setContentsMargins(0, 0, 0, 0);
    editorHeaderTrailingLayout->setSpacing(16);

    editorValidationSummaryWidget_ = new QWidget(editorHeaderTrailingWidget);
    auto* editorValidationSummaryLayout = new QHBoxLayout(editorValidationSummaryWidget_);
    editorValidationSummaryLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationSummaryLayout->setSpacing(8);

    auto* editorValidationErrorGroup = new QWidget(editorValidationSummaryWidget_);
    auto* editorValidationErrorLayout = new QHBoxLayout(editorValidationErrorGroup);
    editorValidationErrorLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationErrorLayout->setSpacing(6);
    editorValidationErrorIconLabel_ = new QLabel(editorValidationErrorGroup);
    editorValidationErrorIconLabel_->setFixedSize(14, 14);
    editorValidationErrorCountLabel_ = new QLabel(QStringLiteral("0"), editorValidationErrorGroup);
    editorValidationErrorCountLabel_->setFont(uiMonoFont(10, QFont::DemiBold));
    editorValidationErrorLayout->addWidget(editorValidationErrorIconLabel_, 0, Qt::AlignVCenter);
    editorValidationErrorLayout->addWidget(editorValidationErrorCountLabel_, 0, Qt::AlignVCenter);

    auto* editorValidationWarningGroup = new QWidget(editorValidationSummaryWidget_);
    auto* editorValidationWarningLayout = new QHBoxLayout(editorValidationWarningGroup);
    editorValidationWarningLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationWarningLayout->setSpacing(3);
    editorValidationWarningIconLabel_ = new QLabel(editorValidationWarningGroup);
    editorValidationWarningIconLabel_->setFixedSize(14, 14);
    editorValidationWarningCountLabel_ = new QLabel(QStringLiteral("0"), editorValidationWarningGroup);
    editorValidationWarningCountLabel_->setFont(uiMonoFont(10, QFont::DemiBold));
    editorValidationWarningLayout->addWidget(editorValidationWarningIconLabel_, 0, Qt::AlignVCenter);
    editorValidationWarningLayout->addWidget(editorValidationWarningCountLabel_, 0, Qt::AlignVCenter);

    auto* editorValidationMuriGroup = new QWidget(editorValidationSummaryWidget_);
    auto* editorValidationMuriLayout = new QHBoxLayout(editorValidationMuriGroup);
    editorValidationMuriLayout->setContentsMargins(0, 0, 0, 0);
    editorValidationMuriLayout->setSpacing(4);
    editorValidationMuriIconLabel_ = new QLabel(editorValidationMuriGroup);
    editorValidationMuriIconLabel_->setFixedSize(14, 14);
    editorValidationMuriCountLabel_ = new QLabel(QStringLiteral("0"), editorValidationMuriGroup);
    editorValidationMuriCountLabel_->setFont(uiMonoFont(10, QFont::DemiBold));
    editorValidationMuriLayout->addWidget(editorValidationMuriIconLabel_, 0, Qt::AlignVCenter);
    editorValidationMuriLayout->addWidget(editorValidationMuriCountLabel_, 0, Qt::AlignVCenter);

    editorValidationSummaryLayout->addWidget(editorValidationErrorGroup, 0, Qt::AlignVCenter);
    editorValidationSummaryLayout->addWidget(editorValidationWarningGroup, 0, Qt::AlignVCenter);
    editorValidationSummaryLayout->addWidget(editorValidationMuriGroup, 0, Qt::AlignVCenter);
    editorValidationSummaryWidget_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    editorValidationSummaryWidget_->hide();
    editorHeaderTrailingLayout->addWidget(editorValidationSummaryWidget_, 0, Qt::AlignRight);

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
    metadataPage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    metadataPage_->setStyleSheet(
        "QWidget { background: #FFFFFF; color: #2A3440; }"
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
    latencyDetectorButton_ = new QToolButton(metadataPage_);
    latencyDetectorButton_->setText(UiText::isChineseUi() ? QStringLiteral("BPM&&偏移检测") : QStringLiteral("BPM && Offset Detection"));
    connect(latencyDetectorButton_, &QToolButton::clicked, this, &MainWindow::onOpenLatencyDetector);
    firstWrapLayout->addWidget(latencyDetectorButton_, 0, Qt::AlignLeft);
    firstWrapLayout->addStretch(1);

    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.title", "title")), titleEdit_);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.artist", "artist")), artistEdit_);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.des", "des")), designerEdit_);
    metadataForm->addRow(makeMetadataFieldLabel(uiText("metadata.field.first", "first")), firstWrap);
    metadataCardLayout->addLayout(metadataForm);

    auto* extraMetadataLabel = new QLabel(uiText("metadata.other_fields", "Other &xx Fields"), metadataPage_);
    extraMetadataLabel->setObjectName("SectionTitle");
    extraMetadataLabel->setFont(uiAccentFont(11));
    metadataCardLayout->addWidget(extraMetadataLabel);
    metadataExtraEdit_ = new QTextEdit(metadataPage_);
    metadataExtraEdit_->setFont(editorFont(editorTextFontPointSize_));
    metadataExtraEdit_->setLineWrapMode(QTextEdit::WidgetWidth);
    metadataExtraEdit_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    metadataExtraEdit_->setPlaceholderText("&dummy=...");
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
    chartLayout->addWidget(editorWidget_, 1);

    editorStack_->addWidget(welcomePage_);
    editorStack_->addWidget(metadataPage_);
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
    outlineDock->setWidget(outlineList_);
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
                    document_.ensureDifficulty(id);
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
    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    outlineDock->setMinimumWidth(190);
    outlineDock->setMaximumWidth(190);
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
        "QFrame#PreviewControlCard, QFrame#PreviewStatsCard {"
        " background: #EDF2F8;"
        " border: 1px solid #D5E0EC;"
        " border-radius: 10px;"
        "}"
        "QFrame#PreviewControls {"
        " background: transparent;"
        " border: none;"
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
        "QToolButton#PreviewControlButton {"
        " color: #223042;"
        " padding: 5px 8px;"
        " min-height: 28px;"
        " border: 1px solid #D8E0EA;"
        " border-radius: 6px;"
        " background: transparent;"
        " font-weight: 600;"
        "}"
        "QToolButton#PreviewControlButton:hover { background: #F5F8FC; border-color: #BCD0E5; }"
        "QToolButton#PreviewControlButton:pressed { background: #E8F1FB; }"
        "QSlider::groove:horizontal {"
        " height: 6px;"
        " background: #D8E0EA;"
        " border-radius: 3px;"
        "}"
        "QSlider::sub-page:horizontal {"
        " background: #2E77D0;"
        " border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        " width: 12px;"
        " margin: -4px 0;"
        " border-radius: 6px;"
        " background: #FFFFFF;"
        " border: 1px solid #AFC0D6;"
        "}"
    );
    previewPanel_->setMinimumWidth(kEmbeddedPreviewPanelMinWidth);
    previewPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    previewCanvas_ = new PreviewCanvas();
    logStartupStage("preview_canvas_created");
    previewCanvas_->setSkinDirectory(resolvePreviewSkinDir());
    previewCanvas_->setLegacyFireworkStackingEnabled(legacyFireworkStackingEasterEggEnabled_);
    logStartupStage("preview_skin_async_dispatched");
    previewCanvasFrame_ = new QFrame(previewPanel_);
    previewCanvasFrame_->setObjectName("PreviewCanvasFrame");
    previewCanvasFrame_->setMinimumSize(QSize(1, 1));
    previewCanvasFrame_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_ = QWidget::createWindowContainer(previewCanvas_, previewCanvasFrame_);
    previewCanvasContainer_->setMinimumSize(QSize(1, 1));
    previewCanvasContainer_->setFocusPolicy(Qt::StrongFocus);
    previewPanel_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_->hide();
    logStartupStage("preview_canvas_container_ready");

    previewControlCard_ = new QFrame(previewPanel_);
    previewControlCard_->setObjectName("PreviewControlCard");
    previewControlCard_->setMinimumWidth(kPreviewControlStatsCardMinWidth);
    previewControlCard_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    previewControlCard_->setMouseTracking(true);
    auto* previewControlCardLayout = new QVBoxLayout(previewControlCard_);
    previewControlCardLayout->setContentsMargins(8, 8, 8, 8);
    previewControlCardLayout->setSpacing(0);

    auto* previewControls = new QFrame(previewControlCard_);
    previewControls->setObjectName("PreviewControls");
    auto* previewControlsLayout = new QHBoxLayout(previewControls);
    previewControlsLayout->setContentsMargins(0, 0, 0, 0);
    previewControlsLayout->setSpacing(8);

    stopPreviewButton_ = new QToolButton(previewControls);
    stopPreviewButton_->setObjectName("PreviewControlButton");
    stopPreviewButton_->setDefaultAction(stopPreviewAction_);
    stopPreviewButton_->setIconSize(QSize(18, 18));
    stopPreviewButton_->setAutoRaise(false);
    stopPreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopPreviewButton_->setToolTip(QString());
    stopPreviewButton_->setMouseTracking(true);
    previewControlsLayout->addWidget(stopPreviewButton_, 0);

    pausePreviewButton_ = new QToolButton(previewControls);
    pausePreviewButton_->setObjectName("PreviewControlButton");
    pausePreviewButton_->setDefaultAction(pausePreviewAction_);
    pausePreviewButton_->setIconSize(QSize(18, 18));
    pausePreviewButton_->setAutoRaise(false);
    pausePreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    pausePreviewButton_->setToolTip(QString());
    pausePreviewButton_->setMouseTracking(true);
    previewControlsLayout->addWidget(pausePreviewButton_, 0);

    previewSlider_ = new QSlider(Qt::Horizontal, previewControls);
    previewSlider_->setRange(0, 1000);
    previewSlider_->setSingleStep(25);
    previewSlider_->setPageStep(250);
    previewSlider_->setTracking(true);
    previewSlider_->setMouseTracking(true);
    previewControlsLayout->addWidget(previewSlider_, 1);

    previewSpeedButton_ = new QToolButton(previewControls);
    previewSpeedButton_->setObjectName("PreviewControlButton");
    previewSpeedButton_->setPopupMode(QToolButton::InstantPopup);
    previewSpeedButton_->setText("1x");
    previewSpeedButton_->setFont(uiAccentFont(10));
    previewSpeedButton_->setFixedWidth(72);
    previewSpeedButton_->setMouseTracking(true);
    auto* speedMenu = new QMenu(previewSpeedButton_);
    speedMenu->setFont(uiAccentFont(10));
    styleRoundedMenu(*speedMenu);
    const QList<QPair<double, QString>> speedOptions{
        {0.25, "0.25x"},
        {0.5, "0.5x"},
        {0.75, "0.75x"},
        {1.0, "1x"},
        {1.25, "1.25x"},
        {2.0, "2x"},
    };
    for (const auto& speedOption : speedOptions) {
        const double speed = speedOption.first;
        QAction* speedAction = speedMenu->addAction(speedOption.second);
        speedAction->setCheckable(true);
        speedAction->setChecked(qFuzzyCompare(speed + 1.0, 2.0));
        speedAction->setData(speed);
        connect(speedAction, &QAction::triggered, this, [this, speed, speedMenu]() {
            const QList<QAction*> actions = speedMenu->actions();
            for (QAction* action : actions) {
                action->setChecked(false);
            }
            QAction* action = qobject_cast<QAction*>(sender());
            if (action != nullptr) {
                action->setChecked(true);
            }
            applyPreviewPlaybackRate(speed);
        });
    }
    previewSpeedButton_->setMenu(speedMenu);
    previewControlsLayout->addWidget(previewSpeedButton_, 0);
    previewFullscreenButton_ = new QToolButton(previewControls);
    previewFullscreenButton_->setObjectName("PreviewControlButton");
    previewFullscreenButton_->setCheckable(true);
    previewFullscreenButton_->setAutoRaise(false);
    previewFullscreenButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    previewFullscreenButton_->setIconSize(QSize(20, 20));
    previewFullscreenButton_->setMouseTracking(true);
    connect(previewFullscreenButton_, &QToolButton::clicked, this, [this]() {
        togglePreviewFullscreen();
    });
    updatePreviewFullscreenButtonAppearance();
    previewControlsLayout->addWidget(previewFullscreenButton_, 0);
    previewControlCardLayout->addWidget(previewControls, 0);

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
    connect(previewCanvas_, &QOpenGLWindow::frameSwapped, this, [this]() {
        if (!qtPreviewPlaying_
            || legacyPygamePreviewEnabled_
            || !previewCanvasUsesFrameSwappedPacing()
            || !qtPreviewAwaitingFrameSwap_) {
            return;
        }
        qtPreviewAwaitingFrameSwap_ = false;
        qtPreviewAwaitingFrameSwapSinceMs_ = -1;
        if (qtPreviewTimer_ != nullptr) {
            qtPreviewTimer_->stop();
        }
        QTimer::singleShot(0, this, [this]() {
            if (!qtPreviewPlaying_ || legacyPygamePreviewEnabled_) {
                return;
            }
            onQtPreviewTick();
        });
    });
    logStartupStage("preview_runtime_connections_ready");
    logStartupStage("preview_runtime_ready");

    bottomTabs_ = new QTabWidget(central);
    if (QTabBar* bottomTabBar = bottomTabs_->tabBar(); bottomTabBar != nullptr) {
        bottomTabBar->installEventFilter(this);
    }
    timelineView_ = new TimelineView(bottomTabs_);
    QFont timelineHeaderLineNumberFont = codeFont;
    timelineHeaderLineNumberFont.setPointSize(qMax(codeFont.pointSize() + 1, 12));
    timelineView_->setHeaderLineNumberFont(timelineHeaderLineNumberFont);
    timelineView_->setShowSlideTracks(true);
    connect(timelineView_, &TimelineView::noteNavigateRequested, this, [this](int line, int col) {
        jumpToLocation(line, col);
        statusBar()->showMessage(QString("Timeline jump: nearest object -> L%1 C%2").arg(line).arg(col));
    });
    connect(timelineView_, &TimelineView::headerNavigateRequested, this, [this](double second) {
        navigateTimelineToSecond(second, true);
    });
    connect(timelineView_, &TimelineView::timelineUserInteractionStarted, this, [this]() {
        if (!legacyPygamePreviewEnabled_ && qtPreviewPlaying_) {
            stopQtPreviewPlayback(true);
            updatePauseButtonAppearance();
        }
    });
    connect(timelineView_, &TimelineView::centerNavigateRequested, this, [this](double second) {
        const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
        const bool shouldRenderNow = !previewScrubRenderElapsed_.isValid()
            || previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
        if (shouldRenderNow) {
            if (previewSeekDebounceTimer_ != nullptr) {
                previewSeekDebounceTimer_->stop();
            }
            seekPreviewToSecond(clampedSecond, false);
            previewScrubRenderElapsed_.restart();
        } else {
            schedulePreviewSeek(clampedSecond, false);
        }
    });
    connect(timelineView_, &TimelineView::followPreviewToggled, this, [this](bool enabled) {
        if (!enabled) {
            clearPreviewFollowDecoration();
            syncTimelineToEditorCursor(!qtPreviewPlaying_);
            return;
        }
        double second = qMax(0.0, qtPreviewPauseSecond_);
        if (qtPreviewPlaying_) {
            if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
                second = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
            } else if (previewMediaController_ != nullptr) {
                second = qMax(0.0, previewMediaController_->currentPlaybackSecond());
            }
        }
        syncEditorCursorToPreviewSecond(second, true);
    });
    bottomTabs_->addTab(timelineView_, uiText("tab.timeline", "Timeline"));

    if (auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_); editor != nullptr) {
        connect(editor->document(), &QTextDocument::contentsChange, this, [this](int, int charsRemoved, int charsAdded) {
            if (suppressTextDirtyTracking_) {
                return;
            }
            if (charsRemoved == 0 && charsAdded == 0) {
                return;
            }
            markCurrentFieldDirty();
            scheduleTimelineRefresh();
            updateEditorEmptyState();
            updateEditorStatus();
        });
        connect(editor, &QTextEdit::textChanged, this, [this]() {
            scheduleAutoValidation();
        });
    }
    connect(qobject_cast<PlainCodeEditor*>(editorWidget_), &QTextEdit::cursorPositionChanged, this, [this]() {
        updateEditorStatus();
        if (!qtPreviewPlaying_) {
            // Keep editor editable while paused even if "follow preview" is enabled.
            // Preview takes over cursor only during active playback.
            syncTimelineToEditorCursor(true);
        }
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
            markCurrentFieldDirty();
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
        UiText::isChineseUi() ? QStringLiteral("语法检查") : QStringLiteral("Syntax Check")
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
        UiText::isChineseUi() ? QStringLiteral("无理检查") : QStringLiteral("Muri Check")
    );
    connect(bottomTabs_, &QTabWidget::currentChanged, this, [this](int) {
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    });
    updateBottomTabsDeviceHeight();
    logStartupStage("timeline_and_tabs_ready");

    previewLeftColumn_ = new QWidget(this);
    previewLeftColumn_->setMinimumWidth(320);
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
    applyWorkspacePanelArrangement();
    updatePreviewWorkspaceLayout();
    logStartupStage("workspace_and_central_widget_ready");

    constexpr int kToolbarLeadingSpacerWidth = 6;
    auto* toolbarLeadingSpacer = new QWidget(toolBar);
    toolbarLeadingSpacer->setFixedWidth(kToolbarLeadingSpacerWidth);
    toolBar->addWidget(toolbarLeadingSpacer);
    toolBar->addAction(openAction_);
    toolBar->addAction(saveAction_);
    constexpr int kToolbarActionButtonWidth = 64;
    constexpr int kToolbarActionButtonHorizontalPadding = 20;
    const auto compactToolbarButtonWidth = [](const QFont& font, QAction* action) -> int {
        if (action == nullptr) {
            return kToolbarActionButtonWidth;
        }
        if (UiText::isChineseUi()) {
            return kToolbarActionButtonWidth;
        }
        const QFontMetrics metrics(font);
        return qMax(
            kToolbarActionButtonWidth,
            metrics.horizontalAdvance(action->text()) + kToolbarActionButtonHorizontalPadding
        );
    };
    const auto makeCompactToolbarButton = [toolBar, compactToolbarButtonWidth](QAction* action) -> QToolButton* {
        auto* button = new QToolButton(toolBar);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        const int buttonWidth = compactToolbarButtonWidth(button->font(), action);
        button->setFixedWidth(buttonWidth);
        button->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
        return button;
    };

    previewAudioSettingsButton_ = makeCompactToolbarButton(previewAudioSettingsAction_);
    previewVideoSettingsButton_ = makeCompactToolbarButton(previewVideoSettingsAction_);
    int settingsButtonWidth = kToolbarActionButtonWidth;
    if (previewAudioSettingsButton_ != nullptr) {
        settingsButtonWidth = qMax(settingsButtonWidth, previewAudioSettingsButton_->width());
    }
    if (previewVideoSettingsButton_ != nullptr) {
        settingsButtonWidth = qMax(settingsButtonWidth, previewVideoSettingsButton_->width());
    }
    if (previewAudioSettingsButton_ != nullptr) {
        previewAudioSettingsButton_->setFixedWidth(settingsButtonWidth);
        toolBar->addWidget(previewAudioSettingsButton_);
    }
    if (previewVideoSettingsButton_ != nullptr) {
        previewVideoSettingsButton_->setFixedWidth(settingsButtonWidth);
        toolBar->addWidget(previewVideoSettingsButton_);
    }
    settingsPlaceholderAction_ = toolBar->addAction(
        makeSettingsGearIcon(QColor("#5D6E83")),
        uiText("action.preferences", "Preferences...")
    );
    settingsPlaceholderAction_->setToolTip(uiText("action.preferences", "Preferences..."));
    connect(settingsPlaceholderAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    exportVideoButton_ = makeCompactToolbarButton(exportVideoAction_);
    if (exportVideoButton_ != nullptr) {
        const auto syncExportToolbarButton = [this]() {
            if (exportVideoButton_ == nullptr) {
                return;
            }
            exportVideoButton_->setText(uiText("toolbar.export", "Export"));
            exportVideoButton_->setToolTip(exportVideoAction_ != nullptr ? exportVideoAction_->text() : QString());
        };
        syncExportToolbarButton();
        int openButtonWidth = kToolbarActionButtonWidth;
        if (QWidget* openWidget = toolBar->widgetForAction(openAction_); openWidget != nullptr) {
            openButtonWidth = qMax(1, openWidget->sizeHint().width());
        }
        exportVideoButton_->setFixedWidth(openButtonWidth);
        toolBar->insertWidget(settingsPlaceholderAction_, exportVideoButton_);
        exportVideoMenu_ = new QMenu(exportVideoButton_);
        if (exportVideoAction_ != nullptr) {
            exportVideoMenu_->addAction(exportVideoAction_);
        }
        if (batchExportVideoAction_ != nullptr) {
            exportVideoMenu_->addAction(batchExportVideoAction_);
        }
        exportVideoButton_->installEventFilter(this);
        exportVideoButton_->setMouseTracking(true);
        if (exportVideoAction_ != nullptr) {
            connect(exportVideoAction_, &QAction::changed, this, syncExportToolbarButton);
        }
    }

    exportVideoHoverMenuTimer_ = new QTimer(this);
    exportVideoHoverMenuTimer_->setSingleShot(true);
    exportVideoHoverMenuTimer_->setInterval(250);
    connect(exportVideoHoverMenuTimer_, &QTimer::timeout, this, &MainWindow::showExportToolbarMenu);
    statusBar()->addPermanentWidget(new QLabel("Current File:", this));
    currentFileLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(currentFileLabel_, 1);
    updateCurrentFileLabel();
    updateLatencyDetectorAvailability();

    metadataRefreshTimer_ = new QTimer(this);
    metadataRefreshTimer_->setSingleShot(true);
    metadataRefreshTimer_->setInterval(75);
    connect(metadataRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshTimelineMetadata);

    validationRefreshTimer_ = new QTimer(this);
    validationRefreshTimer_->setSingleShot(true);
    validationRefreshTimer_->setInterval(220);
    connect(validationRefreshTimer_, &QTimer::timeout, this, [this]() {
        (void)runValidateSimaiSilently(false);
    });

    muriRefreshTimer_ = new QTimer(this);
    muriRefreshTimer_->setSingleShot(true);
    muriRefreshTimer_->setInterval(280);
    connect(muriRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshDeferredMuriDiagnostics);

    qtPreviewTimer_ = new QTimer(this);
    qtPreviewTimer_->setInterval(16);
    qtPreviewTimer_->setSingleShot(true);
    qtPreviewTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimer_, &QTimer::timeout, this, [this]() {
        if (!qtPreviewPlaying_) {
            return;
        }
        const bool usingFrameSwapPacing =
            previewCanvas_ != nullptr && !legacyPygamePreviewEnabled_ && previewCanvasUsesFrameSwappedPacing();
        if (!usingFrameSwapPacing) {
            onQtPreviewTick();
            if (!qtPreviewPlaying_) {
                return;
            }
            if (previewCanvas_ != nullptr && !legacyPygamePreviewEnabled_ && !previewCanvasUsesFrameSwappedPacing()) {
                previewCanvas_->update();
            }
            if (!previewCanvasUsesFrameSwappedPacing()) {
                const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
                const qint64 frameIntervalNs = previewCanvasTargetFrameIntervalNs();
                if (qtPreviewNextFixedTickDueNs_ < 0) {
                    qtPreviewNextFixedTickDueNs_ = nowNs + frameIntervalNs;
                } else {
                    do {
                        qtPreviewNextFixedTickDueNs_ += frameIntervalNs;
                    } while (qtPreviewNextFixedTickDueNs_ <= nowNs);
                }
            }
            scheduleNextQtPreviewTick();
            return;
        }
        if (!qtPreviewAwaitingFrameSwap_) {
            scheduleNextQtPreviewTick();
            return;
        }
        const qint64 nowMs = qtPreviewWatchdogElapsed_.elapsed();
        if (qtPreviewAwaitingFrameSwapSinceMs_ >= 0 && nowMs - qtPreviewAwaitingFrameSwapSinceMs_ >= 40) {
            qtPreviewAwaitingFrameSwap_ = false;
            qtPreviewAwaitingFrameSwapSinceMs_ = -1;
            onQtPreviewTick();
            return;
        }
        previewCanvas_->update();
        scheduleNextQtPreviewTick();
    });

    qtPreviewTimelineTimer_ = new QTimer(this);
    qtPreviewTimelineTimer_->setInterval(16);
    qtPreviewTimelineTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimelineTimer_, &QTimer::timeout, this, &MainWindow::flushQtPreviewTimelinePosition);

    previewSeekDebounceTimer_ = new QTimer(this);
    previewSeekDebounceTimer_->setSingleShot(true);
    previewSeekDebounceTimer_->setInterval(120);
    connect(previewSeekDebounceTimer_, &QTimer::timeout, this, [this]() {
        seekPreviewToSecond(previewPendingSeekSecond_, previewPendingSeekCenterView_);
    });
    logStartupStage("timers_ready");

    if (previewSlider_ != nullptr) {
        previewSlider_->setFocusPolicy(Qt::StrongFocus);
        previewSlider_->installEventFilter(this);
        connect(previewSlider_, &QSlider::sliderPressed, this, [this]() {
            if (previewFullscreenActive_) {
                showPreviewFullscreenControls(false);
            }
            if (qtPreviewPlaying_) {
                stopQtPreviewPlayback(true);
            }
            previewSliderDragging_ = true;
            previewScrubRenderElapsed_.invalidate();
            if (previewSlider_ != nullptr) {
                showPreviewSliderTimeHint(previewSlider_->value());
            }
        });
        connect(previewSlider_, &QSlider::sliderMoved, this, [this](int value) {
            if (previewSlider_ == nullptr) {
                return;
            }
            if (previewFullscreenActive_) {
                showPreviewFullscreenControls(false);
            }
            showPreviewSliderTimeHint(value);
            const double second = static_cast<double>(value) / 1000.0;
            const bool shouldRenderNow = !previewScrubRenderElapsed_.isValid()
                || previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
            if (shouldRenderNow) {
                if (previewSeekDebounceTimer_ != nullptr) {
                    previewSeekDebounceTimer_->stop();
                }
                seekPreviewToSecond(second, true);
                previewScrubRenderElapsed_.restart();
            } else {
                schedulePreviewSeek(second, true);
            }
        });
        connect(previewSlider_, &QSlider::sliderReleased, this, [this]() {
            previewSliderDragging_ = false;
            previewScrubRenderElapsed_.invalidate();
            if (previewSlider_ == nullptr) {
                return;
            }
            if (previewFullscreenActive_) {
                showPreviewFullscreenControls(false);
            }
            showPreviewSliderTimeHint(previewSlider_->value());
            if (previewSeekDebounceTimer_ != nullptr) {
                previewSeekDebounceTimer_->stop();
            }
            seekPreviewToSecond(static_cast<double>(previewSlider_->value()) / 1000.0, true);
        });
    }
    if (previewControlCard_ != nullptr) {
        previewControlCard_->installEventFilter(this);
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->installEventFilter(this);
    }
    if (pausePreviewButton_ != nullptr) {
        pausePreviewButton_->installEventFilter(this);
    }
    if (previewSpeedButton_ != nullptr) {
        previewSpeedButton_->installEventFilter(this);
    }
    if (previewFullscreenButton_ != nullptr) {
        previewFullscreenButton_->installEventFilter(this);
    }
    if (previewCanvasContainer_ != nullptr) {
        previewCanvasContainer_->setMouseTracking(true);
        previewCanvasContainer_->installEventFilter(this);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->installEventFilter(this);
    }
    if (previewCanvasFrame_ != nullptr) {
        previewCanvasFrame_->setMouseTracking(true);
        previewCanvasFrame_->installEventFilter(this);
    }
    if (previewPanel_ != nullptr) {
        previewPanel_->setMouseTracking(true);
        previewPanel_->installEventFilter(this);
    }

    editorViewport_ = qobject_cast<PlainCodeEditor*>(editorWidget_)->viewport();
    if (editorViewport_ != nullptr) {
        editorViewport_->installEventFilter(this);
    }
    if (editorFindEdit_ != nullptr) {
        editorFindEdit_->installEventFilter(this);
        connect(editorFindEdit_, &QLineEdit::returnPressed, this, &MainWindow::onFindNext);
    }
    if (editorReplaceEdit_ != nullptr) {
        editorReplaceEdit_->installEventFilter(this);
    }
    if (editorFindPrevButton_ != nullptr) {
        connect(editorFindPrevButton_, &QToolButton::clicked, this, &MainWindow::onFindPrevious);
    }
    if (editorFindNextButton_ != nullptr) {
        connect(editorFindNextButton_, &QToolButton::clicked, this, &MainWindow::onFindNext);
    }
    if (editorFindCloseButton_ != nullptr) {
        connect(editorFindCloseButton_, &QToolButton::clicked, this, &MainWindow::hideFindReplaceBar);
    }
    if (editorReplaceButton_ != nullptr) {
        connect(editorReplaceButton_, &QPushButton::clicked, this, &MainWindow::onReplaceOne);
    }
    if (editorReplaceAllButton_ != nullptr) {
        connect(editorReplaceAllButton_, &QPushButton::clicked, this, &MainWindow::onReplaceAll);
    }
    if (editorFindBar_ != nullptr) {
        auto* toggleFindBarShortcut = new QShortcut(QKeySequence::Find, editorFindBar_);
        toggleFindBarShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(toggleFindBarShortcut, &QShortcut::activated, this, &MainWindow::onToggleFindReplace);
        auto* closeFindBarShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), editorFindBar_);
        closeFindBarShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(closeFindBarShortcut, &QShortcut::activated, this, &MainWindow::hideFindReplaceBar);
    }
    updateEditorFindBarGeometry();
    applyFindOverlayInset();
    auto* fontDecreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
    fontDecreaseShortcut->setContext(Qt::WindowShortcut);
    connect(fontDecreaseShortcut, &QShortcut::activated, this, [this]() {
        applyEditorTextFontSize(editorTextFontPointSize_ - 1, true);
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    auto* fontIncreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+=")), this);
    fontIncreaseShortcut->setContext(Qt::WindowShortcut);
    connect(fontIncreaseShortcut, &QShortcut::activated, this, [this]() {
        applyEditorTextFontSize(editorTextFontPointSize_ + 1, true);
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    auto* fontIncreaseShiftShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this);
    fontIncreaseShiftShortcut->setContext(Qt::WindowShortcut);
    connect(fontIncreaseShiftShortcut, &QShortcut::activated, this, [this]() {
        applyEditorTextFontSize(editorTextFontPointSize_ + 1, true);
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });

    statusBar()->showMessage("PlainCodeEditor ready.");

    loadPortableState();
    applyWorkspacePanelArrangement();
    logStartupStage("portable_state_loaded");
    if (runtimeDebugOutputEnabled_) {
        previewShowDebugInfo_ = true;
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
        previewMediaController_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackPath(lastTrackPath_);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
        logStartupStage("preview_sfx_set_chart_path_done");
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        logStartupStage("preview_sfx_set_playback_rate_done");
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        previewCanvas_->setShowTimestamp(previewShowTimestamp_);
        previewCanvas_->setShowObjectStatsHud(previewShowObjectStatsHud_);
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundBrightness(previewBackgroundBrightnessOuter_);
    }
    applyMuriRenderOptions();
    applyUiTheme();
    updatePauseButtonAppearance();
    if (restoreLastSessionFile()) {
        logStartupStage("restored_last_document_applied");
    } else {
        loadDocument(SimaiDocument::createEmpty());
        logStartupStage("initial_empty_document_applied");
    }
    updatePreviewSliderRange();
    updatePreviewSliderPosition(0.0);
    logStartupStage("initial_document_loaded");
    qtPreviewWatchdogElapsed_.start();
    if (legacyPygamePreviewEnabled_) {
        appendOutput("preview/bootstrap", "initializing resident preview session");
        bootstrapPreviewWindow();
        QTimer::singleShot(1500, this, [this]() {
            if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
                appendOutput("preview/bootstrap", "startup retry");
                bootstrapPreviewWindow();
            }
        });
    } else {
        appendOutput("preview/bootstrap", "legacy pygame preview disabled by default");
    }
    logStartupStage("preview_media_controller_lazy_init_deferred");
    QTimer::singleShot(0, this, [this]() {
        schedulePreviewSubsystemWarmup();
    });
    logStartupStage("preview_subsystem_warmup_scheduled");
    logStartupStage("constructor_done");
}
