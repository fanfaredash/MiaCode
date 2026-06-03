#include "MainWindow.PreferencesSection.h"
#include "../../MainWindowShared.h"
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

namespace {

QString shortcutSequenceText(const QList<QKeySequence>& sequences)
{
    QStringList parts;
    for (const QKeySequence& sequence : sequences) {
        if (!sequence.isEmpty()) {
            parts.append(sequence.toString(QKeySequence::NativeText));
        }
    }
    return parts.join(QStringLiteral(", "));
}

QString shortcutSequenceText(const QKeySequence& sequence)
{
    return sequence.isEmpty()
        ? QString()
        : sequence.toString(QKeySequence::NativeText);
}

class ShortcutCaptureEdit final : public QLineEdit {
public:
    explicit ShortcutCaptureEdit(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {
        setAlignment(Qt::AlignCenter);
        setPlaceholderText(QStringLiteral("Ctrl+Alt+K"));
    }

    QKeySequence sequence() const { return sequence_; }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event == nullptr) {
            return;
        }
        // Esc closes the capture dialog without storing a new binding.
        // The line edit otherwise consumes every key, so without this
        // shortcut Esc would simply be captured as the user's chosen
        // sequence ("Esc") — which is never what the user means by
        // pressing Esc on a popup.
        if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
            if (auto* dialog = qobject_cast<QDialog*>(window()); dialog != nullptr) {
                dialog->reject();
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_unknown) {
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
            sequence_ = QKeySequence();
            clear();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QLineEdit::keyPressEvent(event);
            return;
        }

        const int key = event->key();
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) {
            event->accept();
            return;
        }

        const Qt::KeyboardModifiers modifiers =
            event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        sequence_ = QKeySequence(modifiers | key);
        setText(shortcutSequenceText(sequence_));
        event->accept();
    }

private:
    QKeySequence sequence_;
};

class ShortcutTableWidget final : public QTableWidget {
public:
    explicit ShortcutTableWidget(QWidget* parent = nullptr)
        : QTableWidget(parent)
    {
        setMouseTracking(true);
    }

protected:
    void leaveEvent(QEvent* event) override
    {
        QTableWidget::leaveEvent(event);
        setCurrentCell(-1, -1);
    }
};

// Item delegate that gives every row a consistent text inset. Two passes:
//   1) Paint the panel (selection bg, hover, item bg) at the FULL cell rect
//      so a row-selected item gets one continuous blue band across both
//      columns — adjusting `opt.rect` in a single-pass paint would shrink
//      the selection visual on every cell, leaving a darker stripe at the
//      column-1 left edge where the inset selection bg stops short.
//   2) Draw the cell text manually at the inset rect. The inset differs
//      between category headers and regular content rows so each looks
//      right in isolation while staying consistent across selection state.
class ShortcutItemDelegate final : public QStyledItemDelegate {
public:
    ShortcutItemDelegate(int contentHorizontalPadding,
                         int categoryHorizontalPadding,
                         QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , contentPadding_(contentHorizontalPadding)
        , categoryPadding_(categoryHorizontalPadding)
    {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        QStyle* style = (opt.widget != nullptr) ? opt.widget->style() : QApplication::style();

        // Pass 1: panel at full rect so selection / hover / per-item bg
        // span the entire cell width on both columns of a selected row.
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

        if (opt.text.isEmpty()) {
            return;
        }

        // Pass 2: text at inset rect.
        const int pad = paddingFor(index);
        const QRect textRect = opt.rect.adjusted(pad, 0, -pad, 0);
        if (!textRect.isValid() || textRect.width() <= 0) {
            return;
        }
        painter->save();
        painter->setClipRect(opt.rect);
        QColor textColor;
        if (const QVariant fg = index.data(Qt::ForegroundRole); fg.isValid()) {
            textColor = qvariant_cast<QBrush>(fg).color();
        } else if ((opt.state & QStyle::State_Selected) != 0) {
            textColor = opt.palette.color(opt.palette.currentColorGroup(), QPalette::HighlightedText);
        } else {
            textColor = opt.palette.color(opt.palette.currentColorGroup(), QPalette::Text);
        }
        painter->setPen(textColor);
        painter->setFont(opt.font);
        const QFontMetrics fm(opt.font);
        const QString elided = fm.elidedText(opt.text, opt.textElideMode, textRect.width());
        painter->drawText(textRect, opt.displayAlignment, elided);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.rwidth() += paddingFor(index) * 2;
        return size;
    }

private:
    // Category/header rows are emitted with flags == Qt::ItemIsEnabled (no
    // Qt::ItemIsSelectable); the smaller categoryPadding_ keeps the section
    // title close to the cell edge while command rows below sit further in.
    int paddingFor(const QModelIndex& index) const
    {
        return index.flags().testFlag(Qt::ItemIsSelectable)
            ? contentPadding_
            : categoryPadding_;
    }

    int contentPadding_;
    int categoryPadding_;
};

void applyConfiguredShortcut(
    QAction* action,
    const QString& id,
    const QKeySequence& fallback,
    Qt::ShortcutContext context = Qt::WindowShortcut)
{
    ShortcutRegistry::instance().applyShortcut(action, id, fallback);
    if (action != nullptr) {
        action->setShortcutContext(context);
    }
}

QString shortcutHintFor(const QString& id, const QKeySequence& fallback)
{
    return shortcutSequenceText(
        ShortcutRegistry::instance().sequences(
            id,
            fallback.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{fallback}));
}

QString fontShortcutHintText()
{
    // Reuses the editor.font_* shortcut IDs so the preferences-dialog spin-box
    // hint stays in lock-step with the user's editor font shortcut bindings.
    const QString decrease = shortcutHintFor(
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Shift+-")));
    const QString increase = shortcutHintFor(
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Shift+=")));
    return QStringLiteral("%1 / %2").arg(decrease, increase);
}

QList<QPair<QString, QStringList>> shortcutCategoryGroups()
{
    return {
        {
            QStringLiteral("谱面变换"),
            {
                QStringLiteral("transform.mirror_lr"),
                QStringLiteral("transform.mirror_ud"),
                QStringLiteral("transform.rotate_180"),
                QStringLiteral("transform.rotate_ccw_45"),
                QStringLiteral("transform.rotate_cw_45"),
                QStringLiteral("transform.subdivision_up"),
                QStringLiteral("transform.subdivision_down"),
                QStringLiteral("transform.toggle_break"),
                QStringLiteral("transform.toggle_ex"),
                QStringLiteral("transform.toggle_firework"),
                QStringLiteral("transform.random_rotate"),
            },
        },
        {
            QStringLiteral("预览"),
            {
                QStringLiteral("preview.stop_or_play"),
                QStringLiteral("preview.play_pause_global"),
                QStringLiteral("preview.speed_down"),
                QStringLiteral("preview.speed_up"),
            },
        },
        {
            QStringLiteral("编辑器"),
            {
                QStringLiteral("editor.font_decrease"),
                QStringLiteral("editor.font_increase"),
                QStringLiteral("editor.overwrite_mode"),
            },
        },
    };
}

}  // namespace

using namespace miacode::mainwindow::shared;

MainWindow::PreferencesSection::PreferencesSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::PreferencesSection::applyConfiguredShortcuts()
{
    applyConfiguredShortcut(
        owner_.transformMirrorLeftRightAction_,
        QStringLiteral("transform.mirror_lr"),
        QKeySequence(Qt::CTRL | Qt::Key_J));
    applyConfiguredShortcut(
        owner_.transformMirrorUpDownAction_,
        QStringLiteral("transform.mirror_ud"),
        QKeySequence(Qt::CTRL | Qt::Key_K));
    applyConfiguredShortcut(
        owner_.transformRotate180Action_,
        QStringLiteral("transform.rotate_180"),
        QKeySequence(Qt::CTRL | Qt::Key_L));
    applyConfiguredShortcut(
        owner_.transformRotate45CounterClockwiseAction_,
        QStringLiteral("transform.rotate_ccw_45"),
        QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    applyConfiguredShortcut(
        owner_.transformRotate45ClockwiseAction_,
        QStringLiteral("transform.rotate_cw_45"),
        QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    applyConfiguredShortcut(
        owner_.transformRaiseSubdivisionAction_,
        QStringLiteral("transform.subdivision_up"),
        QKeySequence(QStringLiteral("Ctrl+=")));
    applyConfiguredShortcut(
        owner_.transformLowerSubdivisionAction_,
        QStringLiteral("transform.subdivision_down"),
        QKeySequence(QStringLiteral("Ctrl+-")));
    applyConfiguredShortcut(
        owner_.transformToggleBreakAction_,
        QStringLiteral("transform.toggle_break"),
        QKeySequence(Qt::CTRL | Qt::Key_B));
    applyConfiguredShortcut(
        owner_.transformToggleExAction_,
        QStringLiteral("transform.toggle_ex"),
        QKeySequence(Qt::CTRL | Qt::Key_N));
    applyConfiguredShortcut(
        owner_.transformToggleFireworkAction_,
        QStringLiteral("transform.toggle_firework"),
        QKeySequence(Qt::CTRL | Qt::Key_M));
    applyConfiguredShortcut(
        owner_.transformRandomRotateAction_,
        QStringLiteral("transform.random_rotate"),
        QKeySequence(Qt::CTRL | Qt::Key_Comma));
    applyConfiguredShortcut(
        owner_.stopOrPlayPreviewShortcutAction_,
        QStringLiteral("preview.stop_or_play"),
        QKeySequence(QStringLiteral("Ctrl+Shift+C")),
        Qt::ApplicationShortcut);
    applyConfiguredShortcut(
        owner_.playPausePreviewShortcutAction_,
        QStringLiteral("preview.play_pause_global"),
        QKeySequence(QStringLiteral("Ctrl+Shift+X")),
        Qt::ApplicationShortcut);
    applyConfiguredShortcut(
        owner_.previewSlowerAction_,
        QStringLiteral("preview.speed_down"),
        QKeySequence(QStringLiteral("Ctrl+O")));
    applyConfiguredShortcut(
        owner_.previewFasterAction_,
        QStringLiteral("preview.speed_up"),
        QKeySequence(QStringLiteral("Ctrl+P")));
    applyConfiguredShortcut(
        owner_.fontDecreaseAction_,
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Shift+-")),
        Qt::WindowShortcut);
    applyConfiguredShortcut(
        owner_.fontIncreaseAction_,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Shift+=")),
        Qt::WindowShortcut);
}

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

    // Tab strip across the top — matches the render-settings dialog so
    // the two preference-style surfaces share one visual pattern. The
    // shared CSS lives in UiTheme::dialogTabStripStyleSheet(), already
    // appended to the preferences stylesheet. We still call the page
    // container `pageStack` to keep the downstream addWidget()/setCurrent
    // call sites intact; addTab() does the same wrapping under the hood.
    auto* pageStack = new QTabWidget(&dialog);
    pageStack->setObjectName(QStringLiteral("PreferenceTabs"));
    rootLayout->addWidget(pageStack);

    // Each preference page wraps its controls in a QGroupBox whose
    // title duplicates the tab name (e.g. "外观" inside the "外观"
    // tab). The tab strip already labels the page, so strip the inner
    // title + frame chrome before the group enters the tab. Same
    // recipe as the render-settings dialog.
    const auto flattenPageGroup = [](QGroupBox* group) {
        if (group == nullptr) {
            return;
        }
        group->setTitle(QString());
        group->setFlat(true);
        group->setStyleSheet(QStringLiteral(
            "QGroupBox { border: none; margin-top: 0; padding-top: 0; }"
        ));
    };

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
            owner_.windowSection_->applySystemWindowBackdrop(&dialog);
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
    flattenPageGroup(interfaceGroup);
    appearancePageLayout->addWidget(interfaceGroup);
    appearancePageLayout->addStretch(1);
    pageStack->addTab(appearancePage, uiText("dialog.preferences.interface_group", "Appearance"));

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
    bool selectedAutoCompletionEnabled = state_.editorAutoCompletionEnabled_;
    bool selectedIgnoreMuriIssuePrompts = state_.ignoreMuriIssuePrompts_;

    auto* editorFontSizeLabel = new QLabel(uiText("dialog.preferences.editor_font_size", "Text Font Size"), editorGroup);
    auto* fontSizeRow = new QWidget(editorGroup);
    auto* fontSizeRowLayout = new QHBoxLayout(fontSizeRow);
    fontSizeRowLayout->setContentsMargins(0, 0, 0, 0);
    fontSizeRowLayout->setSpacing(8);
    auto* editorFontSizeSpin = new QSpinBox(fontSizeRow);
    editorFontSizeSpin->setRange(kEditorTextFontSizeMin, kEditorTextFontSizeMax);
    editorFontSizeSpin->setValue(selectedEditorFontSize);
    editorFontSizeSpin->setSuffix(" pt");
    auto* shortcutHint = new QLabel(fontShortcutHintText(), fontSizeRow);
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

    // One unified toggle replacing the former three (auto-close brackets / hold
    // duration / bracket suggestions). It drives bracket auto-close + type-over +
    // empty-pair backspace, the bracket suggestion popup, and the 'h' hold-
    // duration suggestions together.
    auto* autoCompletionCheckbox = new QCheckBox(
        uiText("dialog.preferences.editor_auto_completion", "Auto-complete brackets"),
        editorGroup
    );
    autoCompletionCheckbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    autoCompletionCheckbox->setChecked(selectedAutoCompletionEnabled);
    autoCompletionCheckbox->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("自动补全括号、给出括号/时值建议，并在输入 h 时提示 [8:1] 等 hold 时值。")
            : QStringLiteral("Auto-closes brackets, suggests durations/BPMs inside them, and offers [8:1]-style hold tokens after typing 'h'.")
    );
    connect(autoCompletionCheckbox, &QCheckBox::toggled, &dialog, [&](bool checked) {
        selectedAutoCompletionEnabled = checked;
        owner_.applyEditorAutoCompletionEnabled(selectedAutoCompletionEnabled, true);
        owner_.statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    editorLayout->addRow(QString(), autoCompletionCheckbox);

    auto* ignoreMuriIssuePromptsCheckbox = new QCheckBox(
        UiText::isChineseUi()
            ? QStringLiteral("忽略无理报错提示")
            : QStringLiteral("Ignore muri issue prompts"),
        editorGroup
    );
    ignoreMuriIssuePromptsCheckbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ignoreMuriIssuePromptsCheckbox->setChecked(selectedIgnoreMuriIssuePrompts);
    ignoreMuriIssuePromptsCheckbox->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("开启后不在编辑器标题栏和时间轴小点中提示无理。设置保存到当前谱面文件夹的 .miacode。")
            : QStringLiteral("Hides muri from the editor header and timeline dots. Saved in the current chart folder's .miacode data.")
    );
    connect(ignoreMuriIssuePromptsCheckbox, &QCheckBox::toggled, &dialog, [&](bool checked) {
        selectedIgnoreMuriIssuePrompts = checked;
        owner_.applyIgnoreMuriIssuePrompts(selectedIgnoreMuriIssuePrompts, true);
        owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
    });
    editorLayout->addRow(QString(), ignoreMuriIssuePromptsCheckbox);

    // [BETA51 IME-DISABLE: DISABLED PENDING FIX]
    // The "禁止中文輸入法輸入" preference shipped in 4f9a27e (UI), backed by
    // Qt::WA_InputMethodEnabled = false on PlainCodeEditor. On user testing
    // it didn't actually block Windows CJK IMEs — the IME composition window
    // still appeared on Chinese input. Until we find an approach that reliably
    // blocks all platform IMEs (likely involving the Windows TSF / ImmAssociateContext
    // path rather than just the Qt attribute) the entire feature is hidden:
    // this UI block is commented out, the load/save/apply call sites in
    // MainWindow.EditorDisplay.cpp are also commented out, and the editor-
    // creation bootstrap no longer calls setImeInputDisabled. The state
    // member, applyEditorImeInputDisabled method, and
    // PlainCodeEditor::setImeInputDisabled API all stay in place so the
    // eventual fix can re-enable everything at once by un-commenting these
    // sites. The orphan UiText key dialog.preferences.editor_ime_input_disabled
    // is kept too.
    //
    // auto* imeInputDisabledCheckbox = new QCheckBox(
    //     uiText("dialog.preferences.editor_ime_input_disabled", "Disable IME input (force ASCII)"),
    //     editorGroup
    // );
    // imeInputDisabledCheckbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // imeInputDisabledCheckbox->setChecked(selectedEditorImeInputDisabled);
    // connect(imeInputDisabledCheckbox, &QCheckBox::toggled, &dialog, [&](bool checked) {
    //     selectedEditorImeInputDisabled = checked;
    //     owner_.applyEditorImeInputDisabled(selectedEditorImeInputDisabled, true);
    //     owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
    // });
    // editorLayout->addRow(QString(), imeInputDisabledCheckbox);

    // The preferences dialog font spin-box reuses the editor.font_* shortcut
    // IDs so a single binding controls both the editor and the dialog —
    // there is no longer a separate preferences.font_* registry entry.
    auto* dialogDecreaseShortcut = new QShortcut(&dialog);
    ShortcutRegistry::instance().applyShortcut(
        dialogDecreaseShortcut,
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Shift+-")));
    dialogDecreaseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogDecreaseShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() - 1);
    });
    auto* dialogIncreaseShortcut = new QShortcut(&dialog);
    ShortcutRegistry::instance().applyShortcut(
        dialogIncreaseShortcut,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Shift+=")));
    dialogIncreaseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogIncreaseShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() + 1);
    });
    flattenPageGroup(editorGroup);
    editorPageLayout->addWidget(editorGroup);
    editorPageLayout->addStretch(1);
    pageStack->addTab(editorPage, uiText("dialog.preferences.editor_group", "Editor"));

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
    flattenPageGroup(performanceGroup);
    performancePageLayout->addWidget(performanceGroup);
    performancePageLayout->addStretch(1);
    pageStack->addTab(performancePage, uiText("dialog.preferences.performance_group", "Performance"));

    auto* shortcutsPage = new QWidget(pageStack);
    auto* shortcutsPageLayout = new QVBoxLayout(shortcutsPage);
    shortcutsPageLayout->setContentsMargins(0, 0, 0, 0);
    shortcutsPageLayout->setSpacing(10);
    auto* shortcutsGroup = new QGroupBox(uiText("dialog.preferences.shortcuts_group", "Shortcuts"), shortcutsPage);
    auto* shortcutsLayout = new QVBoxLayout(shortcutsGroup);
    shortcutsLayout->setContentsMargins(12, 10, 12, 12);
    shortcutsLayout->setSpacing(8);
    auto* editShortcutsButton = new QPushButton(uiText("dialog.preferences.shortcuts.edit", "Edit Shortcuts"), shortcutsGroup);
    auto* resetShortcutsButton = new QPushButton(uiText("dialog.preferences.shortcuts.reset", "Restore Shortcuts"), shortcutsGroup);
    editShortcutsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    resetShortcutsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    shortcutsLayout->addWidget(editShortcutsButton, 0, Qt::AlignLeft);
    shortcutsLayout->addWidget(resetShortcutsButton, 0, Qt::AlignLeft);
    flattenPageGroup(shortcutsGroup);
    shortcutsPageLayout->addWidget(shortcutsGroup);
    shortcutsPageLayout->addStretch(1);
    pageStack->addTab(shortcutsPage, uiText("dialog.preferences.shortcuts_group", "Shortcuts"));

    const auto openShortcutEditDialog = [&]() {
        QDialog shortcutsDialog(&dialog);
        shortcutsDialog.setWindowTitle(uiText("dialog.preferences.shortcuts.title", "Keyboard Shortcuts"));
        shortcutsDialog.setModal(true);
        shortcutsDialog.resize(740, 500);
        shortcutsDialog.setMinimumSize(680, 440);
        shortcutsDialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());
        owner_.windowSection_->applySystemWindowBackdrop(&shortcutsDialog);
        UiDialogs::prepareDialogWindow(&shortcutsDialog, &dialog);

        auto* shortcutRootLayout = new QVBoxLayout(&shortcutsDialog);
        shortcutRootLayout->setContentsMargins(10, 10, 10, 10);
        shortcutRootLayout->setSpacing(6);
        auto* table = new ShortcutTableWidget(&shortcutsDialog);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels({
            uiText("dialog.preferences.shortcuts.command", "Command"),
            uiText("dialog.preferences.shortcuts.keybinding", "Keybinding"),
        });
        table->verticalHeader()->hide();
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
        // Highlight the whole row when a shortcut is clicked — the previous
        // per-cell selection left an uneven look where only the command
        // cell got the blue tint and the keybinding cell kept its row
        // background. A click on either column of a content row opens the
        // capture dialog (see the cellClicked handler below).
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setStretchLastSection(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        table->setColumnWidth(1, 170);
        table->verticalHeader()->setDefaultSectionSize(32);
        table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        table->setFrameShape(QFrame::NoFrame);
        // Horizontal padding is applied via ShortcutItemDelegate (installed
        // below) instead of QSS so it renders identically in selected and
        // unselected states. The QSS rule for `::item:selected` only sets
        // the selection background — the inset comes from the delegate.
        table->setStyleSheet(QStringLiteral(
            "QTableWidget { border: 1px solid rgba(128,128,128,72); border-radius: 6px; }"
            "QTableWidget::item:selected { background: rgba(88, 145, 220, 58); }"
            "QHeaderView::section { padding: 6px 9px; border: 0; border-bottom: 1px solid rgba(128,128,128,72); font-weight: 600; }"
        ));
        // Content rows get a generous ±28 px inset; category section
        // headers use the smaller ±14 px so the section title sits closer
        // to the cell edge than the commands listed beneath it.
        table->setItemDelegate(new ShortcutItemDelegate(28, 14, table));
        shortcutRootLayout->addWidget(table, 1);

        std::function<void()> refreshRows;
        std::function<void(int)> openCaptureForRow;
        refreshRows = [&]() {
            const QList<ShortcutRegistry::ShortcutDefinition> definitions =
                ShortcutRegistry::instance().editableShortcuts();
            QHash<QString, ShortcutRegistry::ShortcutDefinition> definitionById;
            for (const auto& definition : definitions) {
                definitionById.insert(definition.id, definition);
            }
            int row = 0;
            table->setRowCount(0);
            // Theme-aware category-row colors. The background is fully
            // opaque so it overrides QTableWidget's alternating-row brush —
            // otherwise the first category row (which falls on the base
            // brush) looks visibly dimmer than the subsequent ones (which
            // fall on the alternate brush). The foreground uses an accent
            // tint so the sub-heading stands out from regular rows but is
            // still recognizable as a category label rather than a command.
            const auto& themeColors = UiTheme::colors();
            const QColor categoryBackground = themeColors.dark
                ? QColor(0x2A, 0x3A, 0x52)
                : QColor(0xE3, 0xEE, 0xFC);
            const QColor categoryForeground = themeColors.dark
                ? QColor(0x9D, 0xC0, 0xF2)
                : QColor(0x1F, 0x5D, 0xAD);
            for (const auto& group : shortcutCategoryGroups()) {
                table->insertRow(row);
                table->setSpan(row, 0, 1, 2);
                auto* categoryItem = new QTableWidgetItem(group.first);
                categoryItem->setFlags(Qt::ItemIsEnabled);
                categoryItem->setBackground(QBrush(categoryBackground));
                categoryItem->setForeground(QBrush(categoryForeground));
                QFont categoryFont = table->font();
                categoryFont.setPointSize(categoryFont.pointSize() + 1);
                categoryFont.setWeight(QFont::DemiBold);
                categoryItem->setFont(categoryFont);
                categoryItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
                table->setItem(row, 0, categoryItem);
                table->setRowHeight(row, 32);
                ++row;

                for (const QString& id : group.second) {
                    if (!definitionById.contains(id)) {
                        continue;
                    }
                    table->insertRow(row);
                    const auto definition = definitionById.value(id);
                    const QString label = UiText::isChineseUi() && !definition.labelZh.isEmpty()
                        ? definition.labelZh
                        : definition.labelEn;
                    auto* commandItem = new QTableWidgetItem(label);
                    commandItem->setData(Qt::UserRole, definition.id);
                    commandItem->setToolTip(definition.id);
                    table->setItem(row, 0, commandItem);
                    auto* keybindingItem = new QTableWidgetItem(
                        shortcutSequenceText(ShortcutRegistry::instance().sequences(definition.id, definition.defaultSequences)));
                    keybindingItem->setData(Qt::UserRole, definition.id);
                    keybindingItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
                    keybindingItem->setToolTip(uiText("dialog.preferences.shortcuts.change", "Change Keybinding"));
                    table->setItem(row, 1, keybindingItem);
                    table->setRowHeight(row, 32);
                    ++row;
                }
            }
        };

        openCaptureForRow = [&](int row) {
            QTableWidgetItem* idItem = table->item(row, 1);
            if (idItem == nullptr) {
                return;
            }
            const QString id = idItem->data(Qt::UserRole).toString();
            if (id.isEmpty()) {
                return;
            }
            QDialog captureDialog(&shortcutsDialog);
            captureDialog.setWindowTitle(uiText("dialog.preferences.shortcuts.capture_title", "Change Keybinding"));
            captureDialog.setModal(true);
            captureDialog.resize(600, 270);
            captureDialog.setMinimumSize(520, 240);
            captureDialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());
            owner_.windowSection_->applySystemWindowBackdrop(&captureDialog);
            UiDialogs::prepareDialogWindow(&captureDialog, &shortcutsDialog);
            auto* captureLayout = new QVBoxLayout(&captureDialog);
            captureLayout->setContentsMargins(24, 22, 24, 18);
            captureLayout->setSpacing(12);
            auto* prompt = new QLabel(uiText("dialog.preferences.shortcuts.capture_prompt", "Press the desired key combination, then press Enter."), &captureDialog);
            prompt->setAlignment(Qt::AlignCenter);
            auto* captureEdit = new ShortcutCaptureEdit(&captureDialog);
            captureEdit->setMinimumHeight(42);
            captureEdit->setStyleSheet(QStringLiteral("font-size: 15px; padding: 7px 10px;"));
            auto* previewLabel = new QLabel(&captureDialog);
            previewLabel->setAlignment(Qt::AlignCenter);
            previewLabel->setMinimumHeight(26);
            previewLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
            auto* captureButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Reset | QDialogButtonBox::Cancel, &captureDialog);
            UiDialogs::localizeButtonBox(captureButtons);
            captureLayout->addWidget(prompt);
            captureLayout->addWidget(captureEdit);
            captureLayout->addWidget(previewLabel);
            captureLayout->addStretch(1);
            captureLayout->addWidget(captureButtons);
            QObject::connect(captureEdit, &QLineEdit::textChanged, &captureDialog, [captureEdit, previewLabel]() {
                previewLabel->setText(captureEdit->text());
            });
            QObject::connect(captureButtons, &QDialogButtonBox::accepted, &captureDialog, [&]() {
                if (captureEdit->sequence().isEmpty()) {
                    return;
                }
                if (ShortcutRegistry::instance().setUserShortcut(id, captureEdit->sequence())) {
                    applyConfiguredShortcuts();
                    shortcutHint->setText(fontShortcutHintText());
                    refreshRows();
                    owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
                }
                captureDialog.accept();
            });
            QObject::connect(captureButtons, &QDialogButtonBox::rejected, &captureDialog, &QDialog::reject);
            if (QPushButton* resetButton = captureButtons->button(QDialogButtonBox::Reset); resetButton != nullptr) {
                QObject::connect(resetButton, &QPushButton::clicked, &captureDialog, [&]() {
                    if (ShortcutRegistry::instance().resetUserShortcut(id)) {
                        applyConfiguredShortcuts();
                        shortcutHint->setText(fontShortcutHintText());
                        refreshRows();
                        owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
                    }
                    captureDialog.accept();
                });
            }
            captureEdit->setFocus(Qt::OtherFocusReason);
            captureDialog.exec();
        };

        QObject::connect(table, &QTableWidget::cellClicked, &shortcutsDialog, [&](int row, int column) {
            // Category header rows span both columns and aren't editable —
            // clicking them just clears the stray selection. Every other
            // row opens the capture dialog from either column so users can
            // hit either the command name or its key binding to rebind.
            if (table->columnSpan(row, 0) > 1) {
                table->clearSelection();
                return;
            }
            Q_UNUSED(column);
            openCaptureForRow(row);
        });
        refreshRows();

        auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, &shortcutsDialog);
        UiDialogs::localizeButtonBox(closeBox);
        QObject::connect(closeBox, &QDialogButtonBox::rejected, &shortcutsDialog, &QDialog::reject);
        shortcutRootLayout->addWidget(closeBox, 0, Qt::AlignRight);
        shortcutsDialog.exec();
    };

    connect(editShortcutsButton, &QPushButton::clicked, &dialog, openShortcutEditDialog);
    connect(resetShortcutsButton, &QPushButton::clicked, &dialog, [&]() {
        const int choice = QMessageBox::question(
            &dialog,
            uiText("dialog.preferences.shortcuts.reset_confirm_title", "Restore Shortcuts"),
            uiText("dialog.preferences.shortcuts.reset_confirm_message", "Restore all editable shortcuts to their defaults?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return;
        }
        if (ShortcutRegistry::instance().resetEditableShortcuts()) {
            applyConfiguredShortcuts();
            shortcutHint->setText(fontShortcutHintText());
            // beta51+ — overwriteModeShortcutHint label was bound to the
            // former overwrite-mode preference row, which was removed in
            // favour of the "禁止中文輸入法輸入" toggle. The Insert key
            // binding still lives in the shortcuts registry under
            // `editor.overwrite_mode`; reset still affects it, just no
            // dialog label needs refreshing now.
            owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
        }
    });

    pageStack->setCurrentIndex(0);

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
