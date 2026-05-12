#include "MainWindow.PreferencesSection.h"
#include "../../MainWindowShared.h"
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
#include "common/OperationLog.h"
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

#include <functional>

using namespace miacode::mainwindow::shared;

MainWindow::PreferencesSection::PreferencesSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::PreferencesSection::onPreferences()
{
    MC_OP("MainWindow::PreferencesSection::onPreferences");
    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(uiText("dialog.preferences.title", "Preferences"));
    dialog.setModal(true);
    dialog.setMinimumWidth(620);
    dialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* contentRow = new QWidget(&dialog);
    auto* contentLayout = new QHBoxLayout(contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    auto* categoryColumn = new QWidget(contentRow);
    auto* categoryColumnLayout = new QVBoxLayout(categoryColumn);
    categoryColumnLayout->setContentsMargins(0, 24, 0, 0);
    categoryColumnLayout->setSpacing(0);
    auto* categoryList = new QListWidget(categoryColumn);
    categoryList->setObjectName(QStringLiteral("PreferenceCategoryList"));
    categoryList->setFixedWidth(88);
    categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    categoryList->addItem(uiText("dialog.preferences.interface_group", "Appearance"));
    categoryList->addItem(uiText("dialog.preferences.editor_group", "Editor"));
    categoryList->addItem(uiText("dialog.preferences.performance_group", "Performance"));
    categoryList->setSpacing(2);
    for (int index = 0; index < categoryList->count(); ++index) {
        categoryList->item(index)->setSizeHint(QSize(0, 30));
    }
    auto* pageStack = new QStackedWidget(contentRow);
    pageStack->setObjectName(QStringLiteral("PreferencePageStack"));
    categoryColumnLayout->addWidget(categoryList, 0, Qt::AlignTop);
    contentLayout->addWidget(categoryColumn, 0);
    contentLayout->addWidget(pageStack, 1);
    rootLayout->addWidget(contentRow);

    auto* appearancePage = new QWidget(pageStack);
    auto* appearancePageLayout = new QVBoxLayout(appearancePage);
    appearancePageLayout->setContentsMargins(0, 0, 0, 0);
    appearancePageLayout->setSpacing(10);
    auto* interfaceGroup = new QGroupBox(uiText("dialog.preferences.interface_group", "Appearance"), appearancePage);
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
    for (UiText::LanguagePreference preference : languageOptions) {
        QAction* action = languageMenu->addAction(languageLabel(preference));
        action->setData(static_cast<int>(preference));
        connect(action, &QAction::triggered, &dialog, [&, preference, languageButton]() {
            selectedPreference = preference;
            languageButton->setText(languageLabel(selectedPreference));
            UiText::setPreferredLanguage(selectedPreference);
            owner_.statusBar()->showMessage(uiText("status.preferences_saved", "Preferences saved. Restart to apply."));
        });
    }
    int languageButtonWidth = 0;
    const QFontMetrics languageMetrics(languageButton->font());
    for (UiText::LanguagePreference preference : languageOptions) {
        languageButtonWidth = qMax(languageButtonWidth, languageMetrics.horizontalAdvance(languageLabel(preference)));
    }
    languageButton->setFixedWidth(languageButtonWidth + 28);
    connect(languageButton, &QToolButton::clicked, &dialog, [languageButton, languageMenu]() {
        languageMenu->popup(languageButton->mapToGlobal(QPoint(0, languageButton->height())));
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
    for (UiText::ThemePreference preference : themeOptions) {
        QAction* action = themeMenu->addAction(themeLabel(preference));
        action->setData(static_cast<int>(preference));
        connect(action, &QAction::triggered, &dialog, [&, preference, themeButton]() {
            selectedThemePreference = preference;
            themeButton->setText(themeLabel(selectedThemePreference));
            UiText::setPreferredTheme(selectedThemePreference);
            owner_.windowSection_->applyUiTheme();
            dialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());
            owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
        });
    }
    int themeButtonWidth = 0;
    const QFontMetrics themeMetrics(themeButton->font());
    for (UiText::ThemePreference preference : themeOptions) {
        themeButtonWidth = qMax(themeButtonWidth, themeMetrics.horizontalAdvance(themeLabel(preference)));
    }
    themeButton->setFixedWidth(themeButtonWidth + 28);
    connect(themeButton, &QToolButton::clicked, &dialog, [themeButton, themeMenu]() {
        themeMenu->popup(themeButton->mapToGlobal(QPoint(0, themeButton->height())));
    });
    themeRowLayout->addWidget(themeButton, 0);
    themeRowLayout->addStretch(1);
    interfaceLayout->addRow(themeLabelWidget, themeRow);
    appearancePageLayout->addWidget(interfaceGroup);
    appearancePageLayout->addStretch(1);
    pageStack->addWidget(appearancePage);

    auto* editorPage = new QWidget(pageStack);
    auto* editorPageLayout = new QVBoxLayout(editorPage);
    editorPageLayout->setContentsMargins(0, 0, 0, 0);
    editorPageLayout->setSpacing(10);
    auto* editorGroup = new QGroupBox(uiText("dialog.preferences.editor_group", "Editor"), editorPage);
    auto* editorLayout = new QFormLayout(editorGroup);
    editorLayout->setContentsMargins(12, 10, 12, 12);
    editorLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    editorLayout->setHorizontalSpacing(12);
    editorLayout->setVerticalSpacing(8);
    int selectedEditorFontSize = state_.editorTextFontPointSize_;
    double selectedEditorLineSpacingFactor = state_.editorLineSpacingFactor_;
    bool selectedEditorHalfWidthInputEnabled = state_.editorHalfWidthInputEnabled_;
    bool selectedPreserveDifficultySwitchView = state_.preserveDifficultySwitchView_;

    auto* editorFontSizeLabel = new QLabel(uiText("dialog.preferences.editor_font_size", "Text Font Size"), editorGroup);
    auto* fontSizeRow = new QWidget(editorGroup);
    auto* fontSizeRowLayout = new QHBoxLayout(fontSizeRow);
    fontSizeRowLayout->setContentsMargins(0, 0, 0, 0);
    fontSizeRowLayout->setSpacing(8);
    auto* editorFontSizeSpin = new QSpinBox(fontSizeRow);
    editorFontSizeSpin->setRange(kEditorTextFontSizeMin, kEditorTextFontSizeMax);
    editorFontSizeSpin->setValue(selectedEditorFontSize);
    editorFontSizeSpin->setSuffix(" pt");
    auto* shortcutHint = new QLabel(QStringLiteral("Ctrl+Shift+- / Ctrl+Shift++"), fontSizeRow);
    shortcutHint->setStyleSheet(QStringLiteral("color: %1;").arg(UiTheme::colors().textMuted.name(QColor::HexRgb)));
    connect(editorFontSizeSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int value) {
        selectedEditorFontSize = value;
        owner_.applyEditorTextFontSize(selectedEditorFontSize, true);
        owner_.statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
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
    int lineSpacingIndex = lineSpacingCombo->findData(selectedEditorLineSpacingFactor);
    if (lineSpacingIndex < 0) {
        lineSpacingIndex = lineSpacingCombo->findData(normalizeEditorLineSpacingFactor(selectedEditorLineSpacingFactor));
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
        owner_.applyEditorLineSpacingFactor(selectedEditorLineSpacingFactor, true);
        owner_.statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    editorLayout->addRow(lineSpacingLabel, lineSpacingCombo);

    auto* halfWidthInputCheckbox = new QCheckBox(
        uiText("dialog.preferences.editor_half_width_input", "Lock half-width symbol input"),
        editorGroup
    );
    halfWidthInputCheckbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    halfWidthInputCheckbox->setChecked(selectedEditorHalfWidthInputEnabled);
    connect(halfWidthInputCheckbox, &QCheckBox::toggled, &dialog, [&](bool checked) {
        selectedEditorHalfWidthInputEnabled = checked;
        owner_.applyEditorHalfWidthInputEnabled(selectedEditorHalfWidthInputEnabled, true);
        owner_.statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    editorLayout->addRow(QString(), halfWidthInputCheckbox);

    auto* preserveDifficultySwitchViewCheckbox = new QCheckBox(
        uiText("dialog.preferences.preserve_difficulty_switch_view", "Preserve editor position and preview progress when switching difficulties"),
        editorGroup
    );
    preserveDifficultySwitchViewCheckbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    preserveDifficultySwitchViewCheckbox->setChecked(selectedPreserveDifficultySwitchView);
    connect(preserveDifficultySwitchViewCheckbox, &QCheckBox::toggled, &dialog, [&](bool checked) {
        selectedPreserveDifficultySwitchView = checked;
        owner_.applyPreserveDifficultySwitchView(selectedPreserveDifficultySwitchView, true);
        owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
    });
    editorLayout->addRow(QString(), preserveDifficultySwitchViewCheckbox);

    auto* dialogDecreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+-")), &dialog);
    dialogDecreaseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogDecreaseShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() - 1);
    });
    auto* dialogIncreaseShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+=")), &dialog);
    dialogIncreaseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogIncreaseShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() + 1);
    });
    editorPageLayout->addWidget(editorGroup);
    editorPageLayout->addStretch(1);
    pageStack->addWidget(editorPage);

    auto* performancePage = new QWidget(pageStack);
    auto* performancePageLayout = new QVBoxLayout(performancePage);
    performancePageLayout->setContentsMargins(0, 0, 0, 0);
    performancePageLayout->setSpacing(10);
    auto* performanceGroup = new QGroupBox(uiText("dialog.preferences.performance_group", "Performance"), performancePage);
    auto* performanceLayout = new QFormLayout(performanceGroup);
    performanceLayout->setContentsMargins(12, 10, 12, 12);
    performanceLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    performanceLayout->setHorizontalSpacing(12);
    performanceLayout->setVerticalSpacing(8);

    struct FrameRateOption {
        PreviewCanvasFrameRateMode mode;
        QString label;
    };
    const double detectedRefreshRate = owner_.currentPreviewCanvasRefreshRate();
    const QString displayRefreshLabel = QStringLiteral("%1 (%2 Hz)")
        .arg(uiText(
            "dialog.render_settings.preview.canvas_frame_rate.display",
            "Display Refresh Rate"
        ))
        .arg(QString::number(detectedRefreshRate, 'f', detectedRefreshRate >= 100.0 ? 0 : 1));
    const auto frameRateLabelForMode =
        [](PreviewCanvasFrameRateMode mode, const QList<FrameRateOption>& options) -> QString {
            for (const FrameRateOption& option : options) {
                if (option.mode == mode) {
                    return option.label;
                }
            }
            for (const FrameRateOption& option : options) {
                if (option.mode == PreviewCanvasFrameRateMode::DisplayRefresh) {
                    return option.label;
                }
            }
            return !options.isEmpty() ? options.front().label : QString();
        };
    const auto addFrameRateRow =
        [&](const QString& label,
            PreviewCanvasFrameRateMode selectedMode,
            const QList<FrameRateOption>& options,
            const std::function<void(PreviewCanvasFrameRateMode)>& applyMode) {
            auto* button = new QToolButton(performanceGroup);
            button->setObjectName("PreferenceMenuButton");
            button->setFont(uiAccentFont(10, QFont::DemiBold));
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            button->setText(frameRateLabelForMode(selectedMode, options));
            auto* menu = new QMenu(button);
            menu->setFont(uiAccentFont(10));
            styleRoundedMenu(*menu);
            for (const FrameRateOption& option : options) {
                QAction* action = menu->addAction(option.label);
                action->setData(static_cast<int>(option.mode));
                connect(action, &QAction::triggered, &dialog, [&, option, button, applyMode]() {
                    button->setText(option.label);
                    applyMode(option.mode);
                    owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
                });
            }
            connect(button, &QToolButton::clicked, &dialog, [button, menu]() {
                menu->popup(button->mapToGlobal(QPoint(0, button->height())));
            });
            performanceLayout->addRow(label, button);
        };

    QList<FrameRateOption> canvasFrameRateOptions;
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::Fps60,
        uiText("dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"),
    });
    if (detectedRefreshRate >= 119.5) {
        canvasFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps120,
            uiText("dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"),
        });
    }
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::DisplayRefresh,
        displayRefreshLabel,
    });

    QList<FrameRateOption> appFrameRateOptions;
    if (detectedRefreshRate >= 29.5) {
        appFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps30,
            uiText("dialog.render_settings.preview.canvas_frame_rate.30", "30 FPS"),
        });
    }
    if (detectedRefreshRate >= 59.5) {
        appFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps60,
            uiText("dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"),
        });
    }
    if (detectedRefreshRate >= 119.5) {
        appFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps120,
            uiText("dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"),
        });
    }
    appFrameRateOptions.append({
        PreviewCanvasFrameRateMode::DisplayRefresh,
        displayRefreshLabel,
    });

    addFrameRateRow(
        uiText("dialog.render_settings.preview.canvas_frame_rate", "Preview Refresh Rate"),
        owner_.currentPreviewCanvasFrameRateMode(),
        canvasFrameRateOptions,
        [&](PreviewCanvasFrameRateMode mode) {
            owner_.setPreviewCanvasFrameRateMode(mode, true);
        }
    );
    addFrameRateRow(
        uiText("dialog.preferences.performance.pv_frame_rate", "PV Refresh Rate"),
        owner_.currentPreviewStageMediaFrameRateMode(),
        appFrameRateOptions,
        [&](PreviewCanvasFrameRateMode mode) {
            owner_.setPreviewStageMediaFrameRateMode(mode, true);
        }
    );
    addFrameRateRow(
        uiText("dialog.preferences.performance.timeline_frame_rate", "Timeline Refresh Rate"),
        owner_.currentTimelineFrameRateMode(),
        appFrameRateOptions,
        [&](PreviewCanvasFrameRateMode mode) {
            owner_.setTimelineFrameRateMode(mode, true);
        }
    );
    performancePageLayout->addWidget(performanceGroup);
    performancePageLayout->addStretch(1);
    pageStack->addWidget(performancePage);

    QObject::connect(categoryList, &QListWidget::currentRowChanged, pageStack, &QStackedWidget::setCurrentIndex);
    categoryList->setCurrentRow(0);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);

    dialog.exec();
}

void MainWindow::onPreferences()
{
    preferencesSection_->onPreferences();
}
