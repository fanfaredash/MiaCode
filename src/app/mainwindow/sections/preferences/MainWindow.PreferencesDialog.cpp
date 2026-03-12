void MainWindow::onPreferences()
{
    QDialog dialog(this);
    dialog.setWindowTitle(uiText("dialog.preferences.title", "Preferences"));
    dialog.setModal(true);
    dialog.setMinimumWidth(460);
    dialog.setStyleSheet(
        "QDialog { background: #F8FAFD; }"
        "QGroupBox { background: #FFFFFF; border: 1px solid #DCE5F0; border-radius: 10px; margin-top: 12px; padding-top: 10px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }"
        "QLabel { color: #203040; }"
        "QToolButton#PreferenceMenuButton {"
        " min-height: 30px;"
        " min-width: 180px;"
        " border: 1px solid #D8E0EA;"
        " border-radius: 6px;"
        " padding: 4px 10px;"
        " background: #FFFFFF;"
        " color: #223042;"
        " font-weight: 600;"
        " text-align: left;"
        "}"
        "QToolButton#PreferenceMenuButton:hover { background: #F5F8FC; border-color: #BCD0E5; }"
        "QToolButton#PreferenceMenuButton:pressed { background: #E8F1FB; border-color: #9FC1E9; }"
        "QPushButton { min-width: 92px; min-height: 30px; border: 1px solid #BFD0E3; border-radius: 6px; background: #FFFFFF; color: #223042; }"
        "QPushButton:hover { background: #F3F8FF; border-color: #9FC1E9; }"
    );

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* interfaceGroup = new QGroupBox(uiText("dialog.preferences.interface_group", "Interface"), &dialog);
    auto* interfaceLayout = new QFormLayout(interfaceGroup);
    interfaceLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    interfaceLayout->setHorizontalSpacing(12);
    interfaceLayout->setVerticalSpacing(8);

    const UiText::LanguagePreference currentPreference = UiText::preferredLanguage();
    UiText::LanguagePreference selectedPreference = currentPreference;
    const auto languageLabel = [](UiText::LanguagePreference preference) -> QString {
        switch (preference) {
        case UiText::LanguagePreference::English:
            return uiText("dialog.preferences.language.english", "English");
        case UiText::LanguagePreference::Chinese:
            return uiText("dialog.preferences.language.chinese", "Simplified Chinese");
        case UiText::LanguagePreference::System:
        default:
            return uiText("dialog.preferences.language.system", "Follow System");
        }
    };

    auto* languageLabelWidget = new QLabel(uiText("dialog.preferences.language", "Language"), interfaceGroup);
    auto* languageButton = new QToolButton(interfaceGroup);
    languageButton->setObjectName("PreferenceMenuButton");
    languageButton->setFont(uiAccentFont(10, QFont::DemiBold));
    languageButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    languageButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    languageButton->setText(languageLabel(selectedPreference));
    auto* languageMenu = new QMenu(languageButton);
    languageMenu->setFont(uiAccentFont(10));
    styleRoundedMenu(*languageMenu);
    const QList<UiText::LanguagePreference> languageOptions{
        UiText::LanguagePreference::System,
        UiText::LanguagePreference::English,
        UiText::LanguagePreference::Chinese,
    };
    for (UiText::LanguagePreference preference : languageOptions) {
        QAction* action = languageMenu->addAction(languageLabel(preference));
        action->setCheckable(true);
        action->setChecked(preference == selectedPreference);
        connect(action, &QAction::triggered, &dialog, [&, preference, languageMenu, languageButton]() {
            selectedPreference = preference;
            for (QAction* candidate : languageMenu->actions()) {
                candidate->setChecked(candidate->text() == languageLabel(selectedPreference));
            }
            languageButton->setText(languageLabel(selectedPreference));
        });
    }
    int languageButtonWidth = 0;
    const QFontMetrics languageMetrics(languageButton->font());
    for (UiText::LanguagePreference preference : languageOptions) {
        languageButtonWidth = qMax(languageButtonWidth, languageMetrics.horizontalAdvance(languageLabel(preference)));
    }
    languageButton->setFixedWidth(languageButtonWidth + 28);
    connect(languageButton, &QToolButton::clicked, &dialog, [languageButton, languageLabelWidget, languageMenu]() {
        const int estimatedItemHeight = qMax(32, languageButton->sizeHint().height() + 2);
        const QPoint labelCenterGlobal = languageLabelWidget->mapToGlobal(QPoint(languageLabelWidget->width(), languageLabelWidget->height() / 2));
        const QPoint buttonTopLeftGlobal = languageButton->mapToGlobal(QPoint(0, 0));
        const QPoint popupPos(buttonTopLeftGlobal.x(), labelCenterGlobal.y() - estimatedItemHeight / 2 - 7);
        languageMenu->popup(popupPos);
    });
    interfaceLayout->addRow(languageLabelWidget, languageButton);
    rootLayout->addWidget(interfaceGroup);

    auto* editorGroup = new QGroupBox(uiText("dialog.preferences.editor_group", "Editor"), &dialog);
    auto* editorLayout = new QFormLayout(editorGroup);
    editorLayout->setContentsMargins(12, 10, 12, 12);
    editorLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    editorLayout->setHorizontalSpacing(12);
    editorLayout->setVerticalSpacing(8);
    const int originalEditorFontSize = editorTextFontPointSize_;
    const double originalEditorLineSpacingFactor = editorLineSpacingFactor_;
    int selectedEditorFontSize = originalEditorFontSize;
    double selectedEditorLineSpacingFactor = originalEditorLineSpacingFactor;

    auto* editorFontSizeLabel = new QLabel(uiText("dialog.preferences.editor_font_size", "Text Font Size"), editorGroup);
    auto* fontSizeRow = new QWidget(editorGroup);
    auto* fontSizeRowLayout = new QHBoxLayout(fontSizeRow);
    fontSizeRowLayout->setContentsMargins(0, 0, 0, 0);
    fontSizeRowLayout->setSpacing(8);
    auto* editorFontSizeSpin = new QSpinBox(fontSizeRow);
    editorFontSizeSpin->setRange(kEditorTextFontSizeMin, kEditorTextFontSizeMax);
    editorFontSizeSpin->setValue(selectedEditorFontSize);
    editorFontSizeSpin->setSuffix(" pt");
    auto* shortcutHint = new QLabel(QStringLiteral("Ctrl+-/Ctrl+="), fontSizeRow);
    shortcutHint->setStyleSheet("color: #607086;");
    connect(editorFontSizeSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int value) {
        selectedEditorFontSize = value;
        applyEditorTextFontSize(selectedEditorFontSize, false);
    });
    fontSizeRowLayout->addWidget(editorFontSizeSpin, 0);
    fontSizeRowLayout->addWidget(shortcutHint, 0);
    fontSizeRowLayout->addStretch(1);
    editorLayout->addRow(editorFontSizeLabel, fontSizeRow);

    auto* lineSpacingLabel = new QLabel(uiText("dialog.preferences.editor_line_spacing", "Line Spacing"), editorGroup);
    auto* lineSpacingCombo = new QComboBox(editorGroup);
    for (double factor : kEditorLineSpacingFactorOptions) {
        lineSpacingCombo->addItem(editorLineSpacingFactorLabel(factor), factor);
    }
    int lineSpacingIndex = lineSpacingCombo->findData(originalEditorLineSpacingFactor);
    if (lineSpacingIndex < 0) {
        lineSpacingIndex = lineSpacingCombo->findData(normalizeEditorLineSpacingFactor(originalEditorLineSpacingFactor));
    }
    if (lineSpacingIndex < 0) {
        lineSpacingIndex = 0;
    }
    lineSpacingCombo->setCurrentIndex(lineSpacingIndex);
    connect(lineSpacingCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) {
            return;
        }
        selectedEditorLineSpacingFactor = lineSpacingCombo->itemData(index).toDouble();
        applyEditorLineSpacingFactor(selectedEditorLineSpacingFactor, false);
    });
    editorLayout->addRow(lineSpacingLabel, lineSpacingCombo);

    auto* dialogDecreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), &dialog);
    dialogDecreaseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogDecreaseShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() - 1);
    });
    auto* dialogIncreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+=")), &dialog);
    dialogIncreaseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogIncreaseShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() + 1);
    });
    auto* dialogIncreaseShiftShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), &dialog);
    dialogIncreaseShiftShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogIncreaseShiftShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() + 1);
    });
    rootLayout->addWidget(editorGroup);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);

    if (dialog.exec() != QDialog::Accepted) {
        applyEditorLineSpacingFactor(originalEditorLineSpacingFactor, false);
        applyEditorTextFontSize(originalEditorFontSize, false);
        return;
    }

    const bool languageChanged = selectedPreference != currentPreference;
    const bool editorFontChanged = selectedEditorFontSize != originalEditorFontSize;
    const bool editorLineSpacingChanged = !qFuzzyCompare(
        selectedEditorLineSpacingFactor + 1.0,
        originalEditorLineSpacingFactor + 1.0
    );
    if (!languageChanged && !editorFontChanged && !editorLineSpacingChanged) {
        return;
    }

    if (editorFontChanged || editorLineSpacingChanged) {
        persistEditorTextFontPreference();
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    }
    if (languageChanged) {
        UiText::setPreferredLanguage(selectedPreference);
        statusBar()->showMessage(uiText("status.preferences_saved", "Preferences saved. Restart to apply."));
        QMessageBox::information(
            this,
            uiText("dialog.preferences.restart_title", "Restart Required"),
            uiText("dialog.preferences.restart_message", "Language preference saved. Restart MiaCode to apply menu, font, and UI text updates.")
        );
    }
}

