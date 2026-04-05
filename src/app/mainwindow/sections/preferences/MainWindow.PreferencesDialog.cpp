void MainWindow::onPreferences()
{
    QDialog dialog(this);
    dialog.setWindowTitle(uiText("dialog.preferences.title", "Preferences"));
    dialog.setModal(true);
    dialog.setMinimumWidth(400);
    dialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* interfaceGroup = new QGroupBox(uiText("dialog.preferences.interface_group", "Interface"), &dialog);
    auto* interfaceLayout = new QFormLayout(interfaceGroup);
    interfaceLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    interfaceLayout->setHorizontalSpacing(12);
    interfaceLayout->setVerticalSpacing(8);

    const UiText::LanguagePreference currentPreference = UiText::preferredLanguage();
    UiText::LanguagePreference selectedPreference = currentPreference;
    const UiText::ThemePreference currentThemePreference = UiText::preferredTheme();
    UiText::ThemePreference selectedThemePreference = currentThemePreference;
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
    const auto themeLabel = [](UiText::ThemePreference preference) -> QString {
        switch (preference) {
        case UiText::ThemePreference::Light:
            return uiText("dialog.preferences.theme.light", "Light");
        case UiText::ThemePreference::Dark:
            return uiText("dialog.preferences.theme.dark", "Dark");
        case UiText::ThemePreference::System:
        default:
            return uiText("dialog.preferences.theme.system", "Follow System");
        }
    };
    const QIcon selectedMenuOptionIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent);
    const QIcon unselectedMenuOptionIcon = makeMenuSelectionCheckIcon(UiTheme::colors().accent, false);

    auto* languageLabelWidget = new QLabel(uiText("dialog.preferences.language", "Language"), interfaceGroup);
    auto* languageRow = new QWidget(interfaceGroup);
    auto* languageRowLayout = new QHBoxLayout(languageRow);
    languageRowLayout->setContentsMargins(0, 0, 0, 0);
    languageRowLayout->setSpacing(12);
    auto* languageButton = new QToolButton(languageRow);
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
    const auto refreshLanguageMenuIcons = [&]() {
        for (QAction* action : languageMenu->actions()) {
            const auto actionPreference = static_cast<UiText::LanguagePreference>(action->data().toInt());
            action->setIcon(actionPreference == selectedPreference ? selectedMenuOptionIcon : unselectedMenuOptionIcon);
        }
    };
    for (UiText::LanguagePreference preference : languageOptions) {
        QAction* action = languageMenu->addAction(languageLabel(preference));
        action->setData(static_cast<int>(preference));
        action->setIcon(preference == selectedPreference ? selectedMenuOptionIcon : unselectedMenuOptionIcon);
        connect(action, &QAction::triggered, &dialog, [&, preference, languageButton]() {
            selectedPreference = preference;
            refreshLanguageMenuIcons();
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
    languageRowLayout->addWidget(languageButton, 0);
    languageRowLayout->addStretch(1);
    interfaceLayout->addRow(languageLabelWidget, languageRow);

    auto* themeLabelWidget = new QLabel(uiText("dialog.preferences.theme", "Theme"), interfaceGroup);
    auto* themeRow = new QWidget(interfaceGroup);
    auto* themeRowLayout = new QHBoxLayout(themeRow);
    themeRowLayout->setContentsMargins(0, 0, 0, 0);
    themeRowLayout->setSpacing(12);
    auto* themeButton = new QToolButton(themeRow);
    themeButton->setObjectName("PreferenceMenuButton");
    themeButton->setFont(uiAccentFont(10, QFont::DemiBold));
    themeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    themeButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    themeButton->setText(themeLabel(selectedThemePreference));
    auto* themeMenu = new QMenu(themeButton);
    themeMenu->setFont(uiAccentFont(10));
    styleRoundedMenu(*themeMenu);
    const QList<UiText::ThemePreference> themeOptions{
        UiText::ThemePreference::System,
        UiText::ThemePreference::Light,
        UiText::ThemePreference::Dark,
    };
    const auto refreshThemeMenuIcons = [&]() {
        for (QAction* action : themeMenu->actions()) {
            const auto actionPreference = static_cast<UiText::ThemePreference>(action->data().toInt());
            action->setIcon(actionPreference == selectedThemePreference ? selectedMenuOptionIcon : unselectedMenuOptionIcon);
        }
    };
    for (UiText::ThemePreference preference : themeOptions) {
        QAction* action = themeMenu->addAction(themeLabel(preference));
        action->setData(static_cast<int>(preference));
        action->setIcon(preference == selectedThemePreference ? selectedMenuOptionIcon : unselectedMenuOptionIcon);
        connect(action, &QAction::triggered, &dialog, [&, preference, themeButton]() {
            selectedThemePreference = preference;
            refreshThemeMenuIcons();
            themeButton->setText(themeLabel(selectedThemePreference));
        });
    }
    int themeButtonWidth = 0;
    const QFontMetrics themeMetrics(themeButton->font());
    for (UiText::ThemePreference preference : themeOptions) {
        themeButtonWidth = qMax(themeButtonWidth, themeMetrics.horizontalAdvance(themeLabel(preference)));
    }
    themeButton->setFixedWidth(themeButtonWidth + 28);
    connect(themeButton, &QToolButton::clicked, &dialog, [themeButton, themeLabelWidget, themeMenu]() {
        const int estimatedItemHeight = qMax(32, themeButton->sizeHint().height() + 2);
        const QPoint labelCenterGlobal = themeLabelWidget->mapToGlobal(QPoint(themeLabelWidget->width(), themeLabelWidget->height() / 2));
        const QPoint buttonTopLeftGlobal = themeButton->mapToGlobal(QPoint(0, 0));
        const QPoint popupPos(buttonTopLeftGlobal.x(), labelCenterGlobal.y() - estimatedItemHeight / 2 - 7);
        themeMenu->popup(popupPos);
    });
    themeRowLayout->addWidget(themeButton, 0);
    themeRowLayout->addStretch(1);
    interfaceLayout->addRow(themeLabelWidget, themeRow);
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
    shortcutHint->setStyleSheet(QStringLiteral("color: %1;").arg(UiTheme::colors().textMuted.name(QColor::HexRgb)));
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
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);

    const int dialogResult = dialog.exec();
    refreshEmbeddedPreviewSurface();
    scheduleEmbeddedPreviewSurfaceRefresh(0);
    scheduleEmbeddedPreviewSurfaceRefresh(120);
    if (dialogResult != QDialog::Accepted) {
        applyEditorLineSpacingFactor(originalEditorLineSpacingFactor, false);
        applyEditorTextFontSize(originalEditorFontSize, false);
        return;
    }

    const bool languageChanged = selectedPreference != currentPreference;
    const bool themeChanged = selectedThemePreference != currentThemePreference;
    const bool editorFontChanged = selectedEditorFontSize != originalEditorFontSize;
    const bool editorLineSpacingChanged = !qFuzzyCompare(
        selectedEditorLineSpacingFactor + 1.0,
        originalEditorLineSpacingFactor + 1.0
    );
    if (!languageChanged && !themeChanged && !editorFontChanged && !editorLineSpacingChanged) {
        return;
    }

    if (editorFontChanged || editorLineSpacingChanged) {
        persistEditorTextFontPreference();
        statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    }
    if (themeChanged) {
        UiText::setPreferredTheme(selectedThemePreference);
        applyUiTheme();
        statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
    }
    if (languageChanged) {
        UiText::setPreferredLanguage(selectedPreference);
        statusBar()->showMessage(uiText("status.preferences_saved", "Preferences saved. Restart to apply."));
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            this,
            uiText("dialog.preferences.restart_title", "Restart Required"),
            uiText("dialog.preferences.restart_message", "Language preference saved. Restart MiaCode to apply menu, font, and UI text updates.")
        );
    }
}

