void MainWindow::setupMenusAndActions(QMenu* fileMenu, QMenu* toolsMenu, QMenu* transformMenu, QMenu* helpMenu)
{
    if (fileMenu == nullptr || toolsMenu == nullptr || transformMenu == nullptr || helpMenu == nullptr) {
        return;
    }

    newAction_ = new QAction(uiText("action.new", "New"), this);
    newAction_->setShortcut(QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, &MainWindow::onNewFile);
    fileMenu->addAction(newAction_);

    openAction_ = new QAction(uiText("action.open", "Open..."), this);
    openAction_->setShortcut(QKeySequence::Open);
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

    validateAction_ = new QAction(uiText("action.validate", "Validate Simai"), this);
    validateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(validateAction_, &QAction::triggered, this, &MainWindow::onValidateSimai);
    toolsMenu->addAction(validateAction_);

    stopPreviewAction_ = new QAction(uiText("action.stop_preview", "Stop Preview"), this);
    stopPreviewAction_->setIcon(makePreviewStopIcon(QColor("#2B3C4E")));
    stopPreviewAction_->setToolTip(QString());
    connect(stopPreviewAction_, &QAction::triggered, this, &MainWindow::onStopPreview);
    toolsMenu->addAction(stopPreviewAction_);

    pausePreviewAction_ = new QAction(uiText("action.pause_preview", "Play/Pause Preview"), this);
    pausePreviewAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    pausePreviewAction_->setIcon(makePreviewPlayIcon(QColor("#2B3C4E")));
    pausePreviewAction_->setToolTip(QString());
    connect(pausePreviewAction_, &QAction::triggered, this, &MainWindow::onTogglePreviewPause);
    toolsMenu->addAction(pausePreviewAction_);

    latencyDetectorAction_ = new QAction(UiText::isChineseUi() ? QStringLiteral("BPM&&偏移检测") : QStringLiteral("BPM && Offset Detection..."), this);
    connect(latencyDetectorAction_, &QAction::triggered, this, &MainWindow::onOpenLatencyDetector);
    toolsMenu->addAction(latencyDetectorAction_);

    toolsMenu->addSeparator();

    toggleJudgeMarkersAction_ = new QAction("Show Judge Markers", this);
    toggleJudgeMarkersAction_->setCheckable(true);
    toggleJudgeMarkersAction_->setChecked(showJudgeMarkers_);
    connect(toggleJudgeMarkersAction_, &QAction::toggled, this, &MainWindow::onToggleJudgeMarkers);
    toolsMenu->addAction(toggleJudgeMarkersAction_);
    toggleJudgeMarkersAction_->setEnabled(false);
    toggleJudgeMarkersAction_->setVisible(false);

    toggleTouchTrailAction_ = new QAction("Show Touch Trail", this);
    toggleTouchTrailAction_->setCheckable(true);
    toggleTouchTrailAction_->setChecked(showTouchTrail_);
    connect(toggleTouchTrailAction_, &QAction::toggled, this, &MainWindow::onToggleTouchTrail);
    toolsMenu->addAction(toggleTouchTrailAction_);
    toggleTouchTrailAction_->setEnabled(false);
    toggleTouchTrailAction_->setVisible(false);

    toolsMenu->addSeparator();

    previewRenderSettingsAction_ = new QAction(uiText("action.render_settings", "Render Settings..."), this);
    connect(previewRenderSettingsAction_, &QAction::triggered, this, &MainWindow::onPreviewRenderSettings);
    toolsMenu->addAction(previewRenderSettingsAction_);

    transformMirrorLeftRightAction_ = new QAction(uiText("action.transform.mirror_lr", "Mirror Left/Right"), this);
    transformMirrorLeftRightAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    transformMirrorLeftRightAction_->setIcon(makeTransformMirrorLeftRightIcon(QColor("#2B3C4E")));
    connect(transformMirrorLeftRightAction_, &QAction::triggered, this, &MainWindow::onMirrorLeftRight);
    transformMenu->addAction(transformMirrorLeftRightAction_);

    transformMirrorUpDownAction_ = new QAction(uiText("action.transform.mirror_ud", "Mirror Up/Down"), this);
    transformMirrorUpDownAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    transformMirrorUpDownAction_->setIcon(makeTransformMirrorUpDownIcon(QColor("#2B3C4E")));
    connect(transformMirrorUpDownAction_, &QAction::triggered, this, &MainWindow::onMirrorUpDown);
    transformMenu->addAction(transformMirrorUpDownAction_);

    transformRotate180Action_ = new QAction(uiText("action.transform.rotate_180", "Rotate 180"), this);
    transformRotate180Action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    transformRotate180Action_->setIcon(makeTransformRotate180Icon(QColor("#2B3C4E")));
    connect(transformRotate180Action_, &QAction::triggered, this, &MainWindow::onRotate180);
    transformMenu->addAction(transformRotate180Action_);

    transformRotate45CounterClockwiseAction_ = new QAction(uiText("action.transform.rotate_ccw_45", "Rotate -45"), this);
    transformRotate45CounterClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    transformRotate45CounterClockwiseAction_->setIcon(makeTransformRotateCcw45Icon(QColor("#2B3C4E")));
    connect(transformRotate45CounterClockwiseAction_, &QAction::triggered, this, &MainWindow::onRotate45CounterClockwise);
    transformMenu->addAction(transformRotate45CounterClockwiseAction_);

    transformRotate45ClockwiseAction_ = new QAction(uiText("action.transform.rotate_cw_45", "Rotate +45"), this);
    transformRotate45ClockwiseAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    transformRotate45ClockwiseAction_->setIcon(makeTransformRotateCw45Icon(QColor("#2B3C4E")));
    connect(transformRotate45ClockwiseAction_, &QAction::triggered, this, &MainWindow::onRotate45Clockwise);
    transformMenu->addAction(transformRotate45ClockwiseAction_);

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

    const QString legacyPreviewEnv = qEnvironmentVariable("MIACODE_ENABLE_PYGAME_PREVIEW", qEnvironmentVariable("MAIMURI_ENABLE_PYGAME_PREVIEW")).trimmed();
    legacyPygamePreviewEnabled_ =
        legacyPreviewEnv == "1" || legacyPreviewEnv.compare("true", Qt::CaseInsensitive) == 0;

    setWindowModified(false);
    updateWindowTitle();
    setupInitialWindowGeometry();

    auto* fileMenu = menuBar()->addMenu(uiText("menu.file", "&File"));
    auto* toolsMenu = menuBar()->addMenu(uiText("menu.tools", "&Tools"));
    auto* transformMenu = menuBar()->addMenu(uiText("menu.transform", "&Transform"));
    auto* helpMenu = menuBar()->addMenu(uiText("menu.help", "&Help"));

    auto* toolBar = addToolBar("Main");
    toolBar->setMovable(false);
    setupMenusAndActions(fileMenu, toolsMenu, transformMenu, helpMenu);
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
    chartBracketHighlighter_ = new BracketScopeHighlighter(editor->document());
    editorWidget_ = editor;
    editorWidget_->setFont(codeFont);
    editorWidget_->setStyleSheet(
        "border: none;"
        "background: #FFFFFF;"
        "color: #1F1F1F;"
        "selection-background-color: #D7EBFF;"
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
        "QWidget#EditorBatchTransformControls QToolButton {"
        " color: #223042;"
        " min-width: 24px;"
        " min-height: 22px;"
        " padding: 0;"
        " border: 1px solid #D2DCE8;"
        " border-radius: 5px;"
        " background: #FFFFFF;"
        "}"
        "QWidget#EditorBatchTransformControls QToolButton:hover { background: #F3F8FF; border-color: #9FC1E9; }"
        "QWidget#EditorBatchTransformControls QToolButton:pressed { background: #E7F1FD; }"
        "QWidget#EditorDifficultyControls QLineEdit {"
        " background: #FFFFFF;"
        " color: #1F1F1F;"
        " border: 1px solid #CCD6E2;"
        " border-radius: 6px;"
        " padding: 4px 6px;"
        " selection-background-color: #D7EBFF;"
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
    editorContextLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    editorHeaderLayout->addWidget(editorContextLabel_, 1);

    editorBatchTransformControls_ = new QWidget(editorHeader);
    editorBatchTransformControls_->setObjectName("EditorBatchTransformControls");
    auto* editorBatchLayout = new QHBoxLayout(editorBatchTransformControls_);
    editorBatchLayout->setContentsMargins(0, 0, 0, 0);
    editorBatchLayout->setSpacing(4);
    const auto makeTransformButton = [this](QAction* action) -> QToolButton* {
        auto* button = new QToolButton(editorBatchTransformControls_);
        button->setObjectName("PreviewControlButton");
        button->setDefaultAction(action);
        button->setAutoRaise(false);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(15, 15));
        button->setFixedSize(26, 24);
        return button;
    };
    transformMirrorLeftRightButton_ = makeTransformButton(transformMirrorLeftRightAction_);
    transformMirrorUpDownButton_ = makeTransformButton(transformMirrorUpDownAction_);
    transformRotate180Button_ = makeTransformButton(transformRotate180Action_);
    transformRotate45CounterClockwiseButton_ = makeTransformButton(transformRotate45CounterClockwiseAction_);
    transformRotate45ClockwiseButton_ = makeTransformButton(transformRotate45ClockwiseAction_);
    editorBatchLayout->addWidget(transformMirrorLeftRightButton_);
    editorBatchLayout->addWidget(transformMirrorUpDownButton_);
    editorBatchLayout->addWidget(transformRotate180Button_);
    editorBatchLayout->addWidget(transformRotate45CounterClockwiseButton_);
    editorBatchLayout->addWidget(transformRotate45ClockwiseButton_);
    editorBatchTransformControls_->setVisible(false);

    editorDifficultyControls_ = new QWidget(editorHeader);
    editorDifficultyControls_->setObjectName("EditorDifficultyControls");
    auto* editorDifficultyLayout = new QHBoxLayout(editorDifficultyControls_);
    editorDifficultyLayout->setContentsMargins(0, 0, 0, 0);
    editorDifficultyLayout->setSpacing(8);
    auto* difficultyLevelLabel = new QLabel("Lv", editorDifficultyControls_);
    difficultyLevelLabel->setFont(uiAccentFont(10));
    auto* difficultyLevelLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyLevelLineEdit->setLeftPlaceholderText("&lv_n=");
    difficultyLevelEdit_ = difficultyLevelLineEdit;
    difficultyLevelEdit_->setMinimumWidth(0);
    difficultyLevelEdit_->setMaximumWidth(72);
    difficultyLevelEdit_->setAlignment(Qt::AlignCenter);
    difficultyLevelEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* difficultyDesignerLabel = new QLabel(uiText("editor.des", "Des"), editorDifficultyControls_);
    difficultyDesignerLabel->setFont(uiAccentFont(10));
    auto* difficultyDesignerLineEdit = new LeftPlaceholderLineEdit(editorDifficultyControls_);
    difficultyDesignerLineEdit->setLeftPlaceholderText("&des_n=");
    difficultyDesignerEdit_ = difficultyDesignerLineEdit;
    difficultyDesignerEdit_->setMinimumWidth(0);
    difficultyDesignerEdit_->setMaximumWidth(140);
    difficultyDesignerEdit_->setAlignment(Qt::AlignCenter);
    difficultyDesignerEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editorDifficultyLayout->addWidget(difficultyLevelLabel);
    editorDifficultyLayout->addWidget(difficultyLevelEdit_);
    editorDifficultyLayout->addWidget(difficultyDesignerLabel);
    editorDifficultyLayout->addWidget(difficultyDesignerEdit_, 1);
    editorDifficultyLayout->addSpacing(10);
    editorDifficultyLayout->addWidget(editorBatchTransformControls_);
    editorDifficultyControls_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editorDifficultyControls_->hide();
    editorHeaderLayout->addWidget(editorDifficultyControls_, 0);

    editorHeaderLayout->addStretch(1);

    editorCursorLabel_ = new QLabel("Ln 1, Col 1", editorHeader);
    editorCursorLabel_->setObjectName("EditorMeta");
    editorCursorLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editorCursorLabel_->setMinimumWidth(0);
    editorCursorLabel_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    editorHeaderLayout->addWidget(editorCursorLabel_, 0, Qt::AlignRight);
    centralLayout->addWidget(editorHeader, 0);

    editorStack_ = new QStackedWidget(central);

    metadataPage_ = new QWidget(editorStack_);
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
        " selection-background-color: #D7EBFF;"
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
    auto* chartLayout = new QVBoxLayout(chartPage_);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(0);
    chartLayout->addWidget(editorWidget_, 1);

    editorStack_->addWidget(metadataPage_);
    editorStack_->addWidget(chartPage_);
    centralLayout->addWidget(editorStack_, 1);
    logStartupStage("editor_stack_ready");

    auto* outlineDock = new QDockWidget("Fields", this);
    outlineDock->setObjectName("OutlineDock");
    outlineDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
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
    connect(outlineList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
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
                button->setStyleSheet(
                    "QToolButton {"
                    " color: #203040;"
                    " background: transparent;"
                    " border: none;"
                    " padding: 6px 20px 6px 12px;"
                    " text-align: left;"
                    "}"
                    "QToolButton:hover {"
                    " background: #EEF5FF;"
                    " border-radius: 6px;"
                    "}"
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
    outlineDock->setMinimumWidth(210);
    outlineDock->setMaximumWidth(210);
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
    logStartupStage("preview_skin_async_dispatched");
    previewCanvasFrame_ = new QFrame(previewPanel_);
    previewCanvasFrame_->setObjectName("PreviewCanvasFrame");
    previewCanvasFrame_->setMinimumSize(QSize(1, 1));
    previewCanvasContainer_ = QWidget::createWindowContainer(previewCanvas_, previewCanvasFrame_);
    previewCanvasContainer_->setMinimumSize(QSize(1, 1));
    previewCanvasContainer_->setFocusPolicy(Qt::StrongFocus);
    previewCanvasContainer_->hide();
    logStartupStage("preview_canvas_container_ready");

    previewControlCard_ = new QFrame(previewPanel_);
    previewControlCard_->setObjectName("PreviewControlCard");
    previewControlCard_->setMinimumWidth(kPreviewControlStatsCardMinWidth);
    previewControlCard_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
    previewControlsLayout->addWidget(stopPreviewButton_, 0);

    pausePreviewButton_ = new QToolButton(previewControls);
    pausePreviewButton_->setObjectName("PreviewControlButton");
    pausePreviewButton_->setDefaultAction(pausePreviewAction_);
    pausePreviewButton_->setIconSize(QSize(18, 18));
    pausePreviewButton_->setAutoRaise(false);
    pausePreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    pausePreviewButton_->setToolTip(QString());
    previewControlsLayout->addWidget(pausePreviewButton_, 0);

    previewSlider_ = new QSlider(Qt::Horizontal, previewControls);
    previewSlider_->setRange(0, 1000);
    previewSlider_->setSingleStep(25);
    previewSlider_->setPageStep(250);
    previewSlider_->setTracking(true);
    previewControlsLayout->addWidget(previewSlider_, 1);

    previewSpeedButton_ = new QToolButton(previewControls);
    previewSpeedButton_->setObjectName("PreviewControlButton");
    previewSpeedButton_->setPopupMode(QToolButton::InstantPopup);
    previewSpeedButton_->setText("1x");
    previewSpeedButton_->setFont(uiAccentFont(10));
    previewSpeedButton_->setFixedWidth(72);
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
        if (!qtPreviewPlaying_ || legacyPygamePreviewEnabled_ || !qtPreviewAwaitingFrameSwap_) {
            return;
        }
        qtPreviewAwaitingFrameSwap_ = false;
        qtPreviewAwaitingFrameSwapSinceMs_ = -1;
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
    timelineView_ = new TimelineView(bottomTabs_);
    timelineView_->setShowSlideTracks(true);
    connect(timelineView_, &TimelineView::noteNavigateRequested, this, [this](int line, int col) {
        jumpToLocation(line, col);
        statusBar()->showMessage(QString("Timeline jump: nearest object -> L%1 C%2").arg(line).arg(col));
    });
    bottomTabs_->addTab(timelineView_, uiText("tab.timeline", "Timeline"));

    connect(qobject_cast<PlainCodeEditor*>(editorWidget_), &QTextEdit::textChanged, this, [this]() {
        markCurrentFieldDirty();
        scheduleTimelineRefresh();
        updateEditorEmptyState();
        updateEditorStatus();
    });
    connect(qobject_cast<PlainCodeEditor*>(editorWidget_), &QTextEdit::cursorPositionChanged, this, [this]() {
        updateEditorStatus();
    });
    connect(titleEdit_, &QLineEdit::textChanged, this, [this]() {
        markCurrentFieldDirty();
        updateWindowTitle();
    });
    connect(artistEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(firstEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(designerEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(metadataExtraEdit_, &QTextEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(difficultyLevelEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);
    connect(difficultyDesignerEdit_, &QLineEdit::textChanged, this, &MainWindow::markCurrentFieldDirty);

    outputView_ = nullptr;

    errorList_ = new QListWidget(bottomTabs_);
    errorList_->setFont(uiOutputFont());
    connect(errorList_, &QListWidget::itemActivated, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::itemClicked, this, &MainWindow::onErrorItemActivated);
    bottomTabs_->addTab(errorList_, uiText("tab.validation_errors", "Validation Errors"));
    bottomTabs_->setMinimumHeight(220);
    bottomTabs_->setMaximumHeight(280);
    logStartupStage("timeline_and_tabs_ready");

    previewLeftColumn_ = new QWidget(this);
    previewLeftColumn_->setMinimumWidth(320);
    previewLeftColumn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    updatePreviewWorkspaceLayout();
    logStartupStage("workspace_and_central_widget_ready");

    toolBar->addAction(openAction_);
    toolBar->addAction(saveAction_);
    settingsPlaceholderAction_ = toolBar->addAction(
        makeSettingsGearIcon(QColor("#5D6E83")),
        uiText("action.preferences", "Preferences...")
    );
    settingsPlaceholderAction_->setToolTip(uiText("action.preferences", "Preferences..."));
    connect(settingsPlaceholderAction_, &QAction::triggered, this, &MainWindow::onPreferences);
    statusBar()->addPermanentWidget(new QLabel("Current File:", this));
    currentFileLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(currentFileLabel_, 1);
    updateCurrentFileLabel();
    updateLatencyDetectorAvailability();

    metadataRefreshTimer_ = new QTimer(this);
    metadataRefreshTimer_->setSingleShot(true);
    metadataRefreshTimer_->setInterval(0);
    connect(metadataRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshTimelineMetadata);

    qtPreviewTimer_ = new QTimer(this);
    qtPreviewTimer_->setInterval(16);
    qtPreviewTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimer_, &QTimer::timeout, this, [this]() {
        if (!qtPreviewPlaying_) {
            return;
        }
        if (previewCanvas_ == nullptr || legacyPygamePreviewEnabled_) {
            onQtPreviewTick();
            return;
        }
        if (!qtPreviewAwaitingFrameSwap_) {
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
            if (qtPreviewPlaying_) {
                stopQtPreviewPlayback(true);
            }
            previewSliderDragging_ = true;
            if (previewSlider_ != nullptr) {
                showPreviewSliderTimeHint(previewSlider_->value());
            }
        });
        connect(previewSlider_, &QSlider::sliderMoved, this, [this](int value) {
            if (previewSlider_ == nullptr) {
                return;
            }
            showPreviewSliderTimeHint(value);
            schedulePreviewSeek(static_cast<double>(value) / 1000.0, true);
        });
        connect(previewSlider_, &QSlider::sliderReleased, this, [this]() {
            previewSliderDragging_ = false;
            if (previewSlider_ == nullptr) {
                return;
            }
            showPreviewSliderTimeHint(previewSlider_->value());
            schedulePreviewSeek(static_cast<double>(previewSlider_->value()) / 1000.0, true);
        });
    }

    editorViewport_ = qobject_cast<PlainCodeEditor*>(editorWidget_)->viewport();
    if (editorViewport_ != nullptr) {
        editorViewport_->installEventFilter(this);
    }
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
    if (toggleJudgeMarkersAction_ != nullptr) {
        toggleJudgeMarkersAction_->setChecked(showJudgeMarkers_);
    }
    if (toggleTouchTrailAction_ != nullptr) {
        toggleTouchTrailAction_->setChecked(showTouchTrail_);
    }
    if (timelineView_ != nullptr) {
        timelineView_->setShowSlideTracks(true);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setBackgroundBrightness(previewBackgroundBrightness_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundBrightness(previewBackgroundBrightness_);
    }
    updatePauseButtonAppearance();
    loadDocument(SimaiDocument::createEmpty());
    logStartupStage("initial_empty_document_applied");
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

