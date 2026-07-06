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
#include "extensions/ExtensionManager.h"
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
    if (sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift++")) {
        return QStringLiteral("Ctrl+Shift+=");
    }
    if (sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift+_")) {
        return QStringLiteral("Ctrl+Shift+-");
    }
    return sequence.isEmpty()
        ? QString()
        : sequence.toString(QKeySequence::NativeText);
}

QString shortcutSequenceKey(const QKeySequence& sequence)
{
    if (sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift++")) {
        return QStringLiteral("Ctrl+Shift+=");
    }
    if (sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift+_")) {
        return QStringLiteral("Ctrl+Shift+-");
    }
    return sequence.toString(QKeySequence::PortableText);
}

QString shortcutDefinitionLabel(const ShortcutRegistry::ShortcutDefinition& definition)
{
    return UiText::isChineseUi() && !definition.labelZh.isEmpty()
        ? definition.labelZh
        : definition.labelEn;
}

QString conflictingShortcutLabel(
    const QList<ShortcutRegistry::ShortcutDefinition>& definitions,
    const QString& currentId,
    const QKeySequence& sequence)
{
    if (sequence.isEmpty()) {
        return QString();
    }
    for (const auto& definition : definitions) {
        if (definition.id == currentId) {
            continue;
        }
        const QList<QKeySequence> sequences =
            ShortcutRegistry::instance().sequences(definition.id, definition.defaultSequences);
        for (const QKeySequence& existing : sequences) {
            if (!existing.isEmpty() && existing == sequence) {
                const QString label = shortcutDefinitionLabel(definition);
                return label.isEmpty() ? definition.id : label;
            }
        }
    }
    return QString();
}

class ShortcutCaptureEdit final : public QLineEdit {
public:
    explicit ShortcutCaptureEdit(QWidget* parent = nullptr, bool allowBareModifier = false)
        : QLineEdit(parent)
        , allowBareModifier_(allowBareModifier)
    {
        setAlignment(Qt::AlignCenter);
        setPlaceholderText(allowBareModifier_ ? QStringLiteral("Alt") : QStringLiteral("Ctrl+Alt+K"));
    }

    QKeySequence sequence() const { return sequence_; }

protected:
    bool event(QEvent* event) override
    {
        if (event != nullptr && event->type() == QEvent::ShortcutOverride) {
            // Capture mode owns the keyboard. Accepting ShortcutOverride keeps
            // application QAction shortcuts from firing while the user is
            // merely trying to type a new binding.
            event->accept();
            return true;
        }
        return QLineEdit::event(event);
    }

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
            // Record modifier-only input too, so Ctrl / Shift / Ctrl+Shift is
            // visible while composing a combo. If the user continues into
            // Ctrl+X, the non-modifier branch below overwrites this tentative
            // value. Only the hold shortcut is meant to store a single bare
            // modifier, but showing these keys during capture is still useful
            // feedback for every editable shortcut.
            if (!event->isAutoRepeat()) {
                Qt::KeyboardModifiers modifiers =
                    event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
                switch (key) {
                case Qt::Key_Control:
                    modifiers |= Qt::ControlModifier;
                    break;
                case Qt::Key_Shift:
                    modifiers |= Qt::ShiftModifier;
                    break;
                case Qt::Key_Alt:
                    modifiers |= Qt::AltModifier;
                    break;
                case Qt::Key_Meta:
                    modifiers |= Qt::MetaModifier;
                    break;
                default:
                    break;
                }
                sequence_ = allowBareModifier_ || modifiers != Qt::NoModifier
                    ? QKeySequence(static_cast<int>(modifiers))
                    : QKeySequence();
                setText(shortcutSequenceText(sequence_));
            }
            event->accept();
            return;
        }

        const Qt::KeyboardModifiers modifiers =
            event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if ((modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier)
            && (key == Qt::Key_Equal || key == Qt::Key_Plus)) {
            sequence_ = QKeySequence(QStringLiteral("Ctrl+Shift+="));
        } else if ((modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) == (Qt::ControlModifier | Qt::ShiftModifier)
            && (key == Qt::Key_Minus || key == Qt::Key_Underscore)) {
            sequence_ = QKeySequence(QStringLiteral("Ctrl+Shift+-"));
        } else {
            sequence_ = QKeySequence(modifiers | key);
        }
        setText(shortcutSequenceText(sequence_));
        event->accept();
    }

private:
    QKeySequence sequence_;
    bool allowBareModifier_ = false;
};

class ShortcutCaptureEventBlocker final : public QObject {
public:
    ShortcutCaptureEventBlocker(QDialog* dialog, ShortcutCaptureEdit* edit)
        : QObject(dialog)
        , dialog_(dialog)
        , edit_(edit)
    {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);
        if (event == nullptr || dialog_ == nullptr || edit_ == nullptr || !dialog_->isVisible()) {
            return false;
        }
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            if (QApplication::focusWidget() == edit_) {
                return false;
            }
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            QKeyEvent forwarded(
                keyEvent->type(),
                keyEvent->key(),
                keyEvent->modifiers(),
                keyEvent->text(),
                keyEvent->isAutoRepeat(),
                keyEvent->count());
            QApplication::sendEvent(edit_, &forwarded);
            event->accept();
            return true;
        }
        if (event->type() == QEvent::KeyRelease) {
            event->accept();
            return true;
        }
        return false;
    }

private:
    QPointer<QDialog> dialog_;
    QPointer<ShortcutCaptureEdit> edit_;
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

class DelayedTableTooltipFilter final : public QObject {
public:
    explicit DelayedTableTooltipFilter(QTableWidget* table, QObject* parent = nullptr)
        : QObject(parent)
        , table_(table)
    {
        timer_.setSingleShot(true);
        timer_.setInterval(2000);
        connect(&timer_, &QTimer::timeout, this, [this]() {
            showPendingTooltip();
        });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (table_.isNull() || watched != table_->viewport() || event == nullptr) {
            return QObject::eventFilter(watched, event);
        }

        switch (event->type()) {
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QModelIndex index = table_->indexAt(mouseEvent->pos());
            if (!index.isValid()) {
                clearPendingTooltip();
                break;
            }
            globalPos_ = mouseEvent->globalPosition().toPoint();
            if (index == pendingIndex_) {
                break;
            }
            pendingIndex_ = QPersistentModelIndex(index);
            timer_.start();
            break;
        }
        case QEvent::Leave:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            clearPendingTooltip();
            break;
        case QEvent::ToolTip:
            return true;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void clearPendingTooltip()
    {
        timer_.stop();
        pendingIndex_ = QPersistentModelIndex();
        QToolTip::hideText();
    }

    void showPendingTooltip()
    {
        if (table_.isNull() || !pendingIndex_.isValid()) {
            return;
        }
        const auto* item = table_->item(pendingIndex_.row(), pendingIndex_.column());
        if (item == nullptr) {
            return;
        }
        QString text = item->toolTip().trimmed();
        if (text.isEmpty()) {
            text = item->text().trimmed();
        }
        if (text.isEmpty()) {
            return;
        }
        const QRect cellRect = table_->visualRect(pendingIndex_);
        QToolTip::showText(globalPos_, text, table_->viewport(), cellRect);
    }

    QPointer<QTableWidget> table_;
    QTimer timer_;
    QPersistentModelIndex pendingIndex_;
    QPoint globalPos_;
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

void applyConfiguredShortcutList(
    QAction* action,
    const QString& id,
    const QList<QKeySequence>& fallback,
    Qt::ShortcutContext context = Qt::WindowShortcut)
{
    ShortcutRegistry::instance().applyShortcuts(action, id, fallback);
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
        QKeySequence(QStringLiteral("Ctrl+Alt+-")));
    const QString increase = shortcutHintFor(
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Alt+=")));
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
                QStringLiteral("transform.clear_complete_elements"),
                QStringLiteral("transform.subdivision_up"),
                QStringLiteral("transform.subdivision_down"),
                QStringLiteral("transform.subdivision_half_up"),
                QStringLiteral("transform.subdivision_half_down"),
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
                QStringLiteral("preview.pause_display_hold"),
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
        owner_.transformClearCompleteElementsAction_,
        QStringLiteral("transform.clear_complete_elements"),
        QKeySequence(Qt::CTRL | Qt::Key_Q));
    applyConfiguredShortcut(
        owner_.transformRaiseSubdivisionAction_,
        QStringLiteral("transform.subdivision_up"),
        QKeySequence(QStringLiteral("Ctrl+=")));
    applyConfiguredShortcut(
        owner_.transformLowerSubdivisionAction_,
        QStringLiteral("transform.subdivision_down"),
        QKeySequence(QStringLiteral("Ctrl+-")));
    applyConfiguredShortcutList(
        owner_.transformRaiseSubdivisionHalfStepAction_,
        QStringLiteral("transform.subdivision_half_up"),
        {QKeySequence(QStringLiteral("Ctrl+Shift+=")), QKeySequence(QStringLiteral("Ctrl++"))});
    applyConfiguredShortcutList(
        owner_.transformLowerSubdivisionHalfStepAction_,
        QStringLiteral("transform.subdivision_half_down"),
        {QKeySequence(QStringLiteral("Ctrl+Shift+-")), QKeySequence(QStringLiteral("Ctrl+_"))});
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
        QKeySequence(QStringLiteral("Ctrl+Alt+-")),
        Qt::WindowShortcut);
    applyConfiguredShortcut(
        owner_.fontIncreaseAction_,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Alt+=")),
        Qt::WindowShortcut);
}

void MainWindow::PreferencesSection::onPreferences()
{
    MC_OP("MainWindow::PreferencesSection::onPreferences");
    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(uiText("dialog.preferences.title", "Preferences"));
    dialog.setModal(true);
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
    // Explicit margins like the editor/performance pages. The flattened
    // group box (border: none via flattenPageGroup) zeroes the default
    // QSS layout margins, so without this the field column runs flush to
    // the pane edge and the menu buttons' right border gets clipped.
    interfaceLayout->setContentsMargins(12, 10, 12, 12);
    interfaceLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    interfaceLayout->setHorizontalSpacing(12);
    interfaceLayout->setVerticalSpacing(8);

    QString selectedLanguageToken = UiText::preferredLanguageToken();
    const UiText::ThemePreference currentThemePreference = UiText::preferredTheme();
    UiText::ThemePreference selectedThemePreference = currentThemePreference;
    const auto languageLabel = [](const QString& token) -> QString {
        const QString normalized = token.trimmed().toLower();
        for (const auto& option : UiText::availableLanguageOptions()) {
            if (option.id == normalized) {
                return option.label;
            }
        }
        return uiText("dialog.preferences.language.system", "Follow System");
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
    languageButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    languageButton->setMinimumWidth(
        languageButton->fontMetrics().horizontalAdvance(QStringLiteral("システムに合わせる")) + 28);
    languageButton->setText(languageLabel(selectedLanguageToken));
    auto* languageMenu = new QMenu(languageButton);
    languageMenu->setFont(uiAccentFont(10));
    styleRoundedMenu(*languageMenu);
    std::function<void()> rebuildLanguageMenu;
    rebuildLanguageMenu = [&]() {
        languageMenu->clear();
        for (const auto& option : UiText::availableLanguageOptions()) {
            QAction* action = languageMenu->addAction(option.label);
            action->setData(option.id);
            connect(action, &QAction::triggered, &dialog, [&, id = option.id, languageButton]() {
                selectedLanguageToken = id;
                languageButton->setText(languageLabel(selectedLanguageToken));
                UiText::setPreferredLanguageToken(selectedLanguageToken);
                owner_.statusBar()->showMessage(uiText("status.preferences_saved", "Preferences saved. Restart to apply."));
            });
        }
        if (!UiText::isLanguageAvailable(selectedLanguageToken)) {
            selectedLanguageToken = QStringLiteral("system");
            languageButton->setText(languageLabel(selectedLanguageToken));
        }
    };
    rebuildLanguageMenu();
    // No manual setFixedWidth here: the QSS PreferenceMenuButton rule
    // (min-width + padding + border) already defines the styled size, and
    // pinning a metrics-derived width below it is what clipped the button's
    // right border. The performance tab's buttons size the same way.
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
    connect(themeButton, &QToolButton::clicked, &dialog, [themeButton, themeMenu]() {
        themeMenu->popup(themeButton->mapToGlobal(QPoint(0, themeButton->height())));
    });
    themeRowLayout->addWidget(themeButton, 0);
    themeRowLayout->addStretch(1);
    interfaceLayout->addRow(themeLabelWidget, themeRow);

    // Chart-preview position (the same workspace left/right swap exposed by
    // the Preview menu's "左右面板互换"). false = preview on the right
    // (default), true = preview on the left.
    const auto previewSideLabel = [](bool previewOnLeft) -> QString {
        return previewOnLeft
            ? uiText("dialog.preferences.preview_side.left", "Left")
            : uiText("dialog.preferences.preview_side.right", "Right");
    };
    auto* previewSideLabelWidget =
        new QLabel(uiText("dialog.preferences.preview_side", "Preview Position"), interfaceGroup);
    auto* previewSideRow = new QWidget(interfaceGroup);
    auto* previewSideRowLayout = new QHBoxLayout(previewSideRow);
    previewSideRowLayout->setContentsMargins(0, 0, 0, 0);
    previewSideRowLayout->setSpacing(12);
    auto* previewSideButton = new QToolButton(previewSideRow);
    previewSideButton->setObjectName("PreferenceMenuButton");
    previewSideButton->setFont(uiAccentFont(10, QFont::DemiBold));
    previewSideButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    previewSideButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    previewSideButton->setText(previewSideLabel(owner_.workspacePanelsSwapped_));
    auto* previewSideMenu = new QMenu(previewSideButton);
    previewSideMenu->setFont(uiAccentFont(10));
    styleRoundedMenu(*previewSideMenu);
    const QList<bool> previewSideOptions{false, true};
    for (bool previewOnLeft : previewSideOptions) {
        QAction* action = previewSideMenu->addAction(previewSideLabel(previewOnLeft));
        action->setData(previewOnLeft);
        connect(action, &QAction::triggered, &dialog, [&, previewOnLeft, previewSideButton]() {
            previewSideButton->setText(previewSideLabel(previewOnLeft));
            owner_.setWorkspacePanelsSwapped(previewOnLeft, true);
            owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
        });
    }
    connect(previewSideButton, &QToolButton::clicked, &dialog, [previewSideButton, previewSideMenu]() {
        previewSideMenu->popup(previewSideButton->mapToGlobal(QPoint(0, previewSideButton->height())));
    });
    previewSideRowLayout->addWidget(previewSideButton, 0);
    previewSideRowLayout->addStretch(1);
    interfaceLayout->addRow(previewSideLabelWidget, previewSideRow);

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
    bool selectedEditorImeInputDisabled = state_.editorImeInputDisabled_;

    // Row order (top→bottom): 字号 · 行距 · 自动补全 · 顶部显示 · 忽略无理报错 ·
    // 中文输入. 中文输入 is the advanced input-mode picker the user did not call
    // out, so it trails the prioritised rows.
    auto* editorFontSizeLabel = new QLabel(uiText("dialog.preferences.editor_font_size", "Text Font Size"), editorGroup);
    auto* fontSizeRow = new QWidget(editorGroup);
    auto* fontSizeRowLayout = new QHBoxLayout(fontSizeRow);
    fontSizeRowLayout->setContentsMargins(0, 0, 0, 0);
    fontSizeRowLayout->setSpacing(8);
    auto* fontSizeControl = new QWidget(fontSizeRow);
    fontSizeControl->setObjectName(QStringLiteral("DialogSpinBoxFrame"));
    fontSizeControl->setStyleSheet(UiTheme::dialogSpinBoxStyleSheet());
    fontSizeControl->setFixedHeight(25);
    fontSizeControl->setFixedWidth(80);
    auto* fontSizeControlLayout = new QHBoxLayout(fontSizeControl);
    fontSizeControlLayout->setContentsMargins(0, 0, 4, 0);
    fontSizeControlLayout->setSpacing(0);
    auto* editorFontSizeSpin = new QSpinBox(fontSizeControl);
    editorFontSizeSpin->setObjectName(QStringLiteral("DialogSpinBoxEditor"));
    editorFontSizeSpin->setRange(kEditorTextFontSizeMin, kEditorTextFontSizeMax);
    editorFontSizeSpin->setValue(selectedEditorFontSize);
    editorFontSizeSpin->setSuffix(" pt");
    editorFontSizeSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    editorFontSizeSpin->setFrame(false);
    editorFontSizeSpin->setFixedHeight(23);
    editorFontSizeSpin->setMinimumWidth(editorFontSizeSpin->fontMetrics().horizontalAdvance(QStringLiteral("28 pt")) + 28);
    const auto makeSpinArrowIcon = [](Qt::ArrowType arrowType) {
        const QColor arrowColor = UiTheme::colors().textSecondary;
        QPixmap pixmap(9, 9);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(arrowColor);
        QPolygon triangle;
        if (arrowType == Qt::UpArrow) {
            triangle << QPoint(4, 2) << QPoint(7, 6) << QPoint(1, 6);
        } else {
            triangle << QPoint(1, 3) << QPoint(7, 3) << QPoint(4, 7);
        }
        painter.drawPolygon(triangle);
        return QIcon(pixmap);
    };
    auto* stepButtons = new QWidget(fontSizeControl);
    stepButtons->setFixedSize(16, 23);
    auto* stepLayout = new QVBoxLayout(stepButtons);
    stepLayout->setContentsMargins(0, 1, 0, 1);
    stepLayout->setSpacing(1);
    auto* increaseFontSizeButton = new QToolButton(stepButtons);
    auto* decreaseFontSizeButton = new QToolButton(stepButtons);
    for (QToolButton* button : {increaseFontSizeButton, decreaseFontSizeButton}) {
        button->setObjectName(QStringLiteral("DialogSpinBoxStepButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRepeat(true);
        button->setAutoRepeatDelay(300);
        button->setAutoRepeatInterval(70);
        button->setFixedSize(16, 10);
        button->setIconSize(QSize(9, 9));
        stepLayout->addWidget(button, 0);
    }
    increaseFontSizeButton->setIcon(makeSpinArrowIcon(Qt::UpArrow));
    decreaseFontSizeButton->setIcon(makeSpinArrowIcon(Qt::DownArrow));
    connect(increaseFontSizeButton, &QToolButton::clicked, editorFontSizeSpin, &QSpinBox::stepUp);
    connect(decreaseFontSizeButton, &QToolButton::clicked, editorFontSizeSpin, &QSpinBox::stepDown);
    fontSizeControlLayout->addWidget(editorFontSizeSpin, 1);
    fontSizeControlLayout->addWidget(stepButtons, 0);
    fontSizeControl->setFocusProxy(editorFontSizeSpin);
    auto* shortcutHint = new QLabel(fontShortcutHintText(), fontSizeRow);
    shortcutHint->setStyleSheet(QStringLiteral("color: %1;").arg(UiTheme::colors().textMuted.name(QColor::HexRgb)));
    connect(editorFontSizeSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int value) {
        selectedEditorFontSize = value;
        owner_.applyEditorTextFontSize(selectedEditorFontSize, true);
        owner_.statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    fontSizeRowLayout->addWidget(fontSizeControl, 0);
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
    UiTheme::applyComboBoxPopupLimit(lineSpacingCombo, 12);
    connect(lineSpacingCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) {
            return;
        }
        selectedEditorLineSpacingFactor = lineSpacingCombo->itemData(index).toDouble();
        owner_.applyEditorLineSpacingFactor(selectedEditorLineSpacingFactor, true);
        owner_.statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    editorLayout->addRow(lineSpacingLabel, lineSpacingCombo);

    // 自动补全 — one unified preference replacing the former three (auto-close
    // brackets / hold duration / bracket suggestions). It drives bracket
    // auto-close + type-over + empty-pair backspace, the bracket suggestion
    // popup, and the 'h' hold-duration suggestions together. Presented as a
    // 开启/关闭 menu (same idiom as the other editor rows) rather than a checkbox.
    auto* autoCompletionLabel = new QLabel(
        UiText::isChineseUi() ? QStringLiteral("自动补全") : QStringLiteral("Auto-completion"),
        editorGroup
    );
    auto* autoCompletionCombo = new QComboBox(editorGroup);
    autoCompletionCombo->addItem(UiText::isChineseUi() ? QStringLiteral("开启") : QStringLiteral("On"));
    autoCompletionCombo->addItem(UiText::isChineseUi() ? QStringLiteral("关闭") : QStringLiteral("Off"));
    autoCompletionCombo->setCurrentIndex(selectedAutoCompletionEnabled ? 0 : 1);
    UiTheme::applyComboBoxPopupLimit(autoCompletionCombo, 12);
    autoCompletionCombo->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("自动补全括号、给出括号/时值建议，并在输入 h 时提示 [8:1] 等 hold 时值。")
            : QStringLiteral("Auto-closes brackets, suggests durations/BPMs inside them, and offers [8:1]-style hold tokens after typing 'h'.")
    );
    connect(autoCompletionCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) {
            return;
        }
        selectedAutoCompletionEnabled = (index == 0);
        owner_.applyEditorAutoCompletionEnabled(selectedAutoCompletionEnabled, true);
        owner_.statusBar()->showMessage(uiText("status.editor_text_display_updated", "Editor text display updated."));
    });
    editorLayout->addRow(autoCompletionLabel, autoCompletionCombo);

    // 顶部显示 — what the difficulty-page header edits next to Lv: the
    // chart-wide offset (偏移, default) or the per-difficulty designer (谱师).
    const auto headerTopDisplayLabel = [](EditorHeaderTopDisplay mode) -> QString {
        return mode == EditorHeaderTopDisplay::Designer
            ? uiText("dialog.preferences.editor_top_display.designer", "Designer")
            : uiText("dialog.preferences.editor_top_display.latency", "Offset");
    };
    auto* headerTopDisplayLabelWidget =
        new QLabel(uiText("dialog.preferences.editor_top_display", "Header Field"), editorGroup);
    // Plain combo box, same idiom as the 行距 row above — the two-option
    // pick doesn't warrant the styled PreferenceMenuButton treatment.
    auto* headerTopDisplayCombo = new QComboBox(editorGroup);
    const QList<EditorHeaderTopDisplay> headerTopDisplayOptions{
        EditorHeaderTopDisplay::Offset,
        EditorHeaderTopDisplay::Designer,
    };
    for (EditorHeaderTopDisplay mode : headerTopDisplayOptions) {
        headerTopDisplayCombo->addItem(headerTopDisplayLabel(mode), static_cast<int>(mode));
    }
    const int headerTopDisplayIndex =
        headerTopDisplayCombo->findData(static_cast<int>(state_.editorHeaderTopDisplay_));
    headerTopDisplayCombo->setCurrentIndex(qMax(0, headerTopDisplayIndex));
    UiTheme::applyComboBoxPopupLimit(headerTopDisplayCombo, 12);
    headerTopDisplayCombo->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("谱面编辑页顶部 Lv 旁边显示的字段：偏移（&first）或当前难度的谱师（&des_N）。")
            : QStringLiteral("The field next to Lv in the chart header: the &first offset or this difficulty's &des_N designer.")
    );
    connect(headerTopDisplayCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&, headerTopDisplayCombo](int index) {
        if (index < 0) {
            return;
        }
        const auto mode =
            static_cast<EditorHeaderTopDisplay>(headerTopDisplayCombo->itemData(index).toInt());
        owner_.applyEditorHeaderTopDisplay(mode, true);
        owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
    });
    editorLayout->addRow(headerTopDisplayLabelWidget, headerTopDisplayCombo);

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
    // 中文输入 combo — merges the former "Lock half-width symbol input" checkbox
    // and "Disable IME input" checkbox into three graduated levels:
    //   index 0 开启           halfWidth=OFF imeDisabled=OFF (plain IME, no filtering)
    //   index 1 仅过滤全角字符  halfWidth=ON  imeDisabled=OFF (default: normalize commits)
    //   index 2 禁止中文输入法  halfWidth=ON  imeDisabled=ON  (block IME candidate window)
    auto* chineseInputLabel = new QLabel(
        UiText::isChineseUi() ? QStringLiteral("中文输入") : QStringLiteral("Chinese input"),
        editorGroup
    );
    const int initialChineseInputMode =
        state_.editorImeInputDisabled_ ? 2 :
        state_.editorHalfWidthInputEnabled_ ? 1 : 0;
    int selectedChineseInputMode = initialChineseInputMode;
    auto* chineseInputCombo = new QComboBox(editorGroup);
    if (UiText::isChineseUi()) {
        chineseInputCombo->addItem(QStringLiteral("开启"));
        chineseInputCombo->addItem(QStringLiteral("仅过滤全角字符"));
        chineseInputCombo->addItem(QStringLiteral("禁止中文输入法"));
    } else {
        chineseInputCombo->addItem(QStringLiteral("On"));
        chineseInputCombo->addItem(QStringLiteral("Filter full-width chars"));
        chineseInputCombo->addItem(QStringLiteral("Disable IME"));
    }
    chineseInputCombo->setCurrentIndex(selectedChineseInputMode);
    UiTheme::applyComboBoxPopupLimit(chineseInputCombo, 12);
    connect(chineseInputCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) {
            return;
        }
        selectedChineseInputMode = index;
        selectedEditorHalfWidthInputEnabled = (index >= 1);
        selectedEditorImeInputDisabled = (index >= 2);
        owner_.applyEditorHalfWidthInputEnabled(selectedEditorHalfWidthInputEnabled, false);
        owner_.applyEditorImeInputDisabled(selectedEditorImeInputDisabled, true);
        owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
    });
    editorLayout->addRow(chineseInputLabel, chineseInputCombo);

    // 忽略无理报错提示 sits below 中文输入 (last row) per the 2026-06-19 review.
    editorLayout->addRow(QString(), ignoreMuriIssuePromptsCheckbox);


    // The preferences dialog font spin-box reuses the editor.font_* shortcut
    // IDs so a single binding controls both the editor and the dialog —
    // there is no longer a separate preferences.font_* registry entry.
    auto* dialogDecreaseShortcut = new QShortcut(&dialog);
    ShortcutRegistry::instance().applyShortcut(
        dialogDecreaseShortcut,
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Alt+-")));
    dialogDecreaseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(dialogDecreaseShortcut, &QShortcut::activated, &dialog, [editorFontSizeSpin]() {
        editorFontSizeSpin->setValue(editorFontSizeSpin->value() - 1);
    });
    auto* dialogIncreaseShortcut = new QShortcut(&dialog);
    ShortcutRegistry::instance().applyShortcut(
        dialogIncreaseShortcut,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Alt+=")));
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

    // PV渲染 (preview video decode): 硬件渲染 (hardware, default) vs 软件渲染
    // (software). Placed first on the page per the user request. Two fixed
    // options, same QToolButton+QMenu visual pattern as the frame-rate rows
    // below. preferSoftware == false selects hardware.
    {
        struct VideoDecodeOption {
            bool preferSoftware;
            QString label;
        };
        const QList<VideoDecodeOption> videoDecodeOptions = {
            {false, uiText("dialog.preferences.performance.video_decode.hardware", "Hardware")},
            {true, uiText("dialog.preferences.performance.video_decode.software", "Software")},
        };
        const auto videoDecodeLabel = [&](bool preferSoftware) -> QString {
            for (const VideoDecodeOption& option : videoDecodeOptions) {
                if (option.preferSoftware == preferSoftware) {
                    return option.label;
                }
            }
            return videoDecodeOptions.front().label;
        };
        auto* button = new QToolButton(performanceGroup);
        button->setObjectName("PreferenceMenuButton");
        button->setFont(uiAccentFont(10, QFont::DemiBold));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setText(videoDecodeLabel(owner_.currentVideoDecodePrefersSoftware()));
        auto* menu = new QMenu(button);
        menu->setFont(uiAccentFont(10));
        styleRoundedMenu(*menu);
        for (const VideoDecodeOption& option : videoDecodeOptions) {
            QAction* action = menu->addAction(option.label);
            action->setData(option.preferSoftware);
            connect(action, &QAction::triggered, &dialog, [&, option, button]() {
                button->setText(option.label);
                owner_.setVideoDecodePrefersSoftware(option.preferSoftware, true);
                owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
            });
        }
        connect(button, &QToolButton::clicked, &dialog, [button, menu]() {
            menu->popup(button->mapToGlobal(QPoint(0, button->height())));
        });
        performanceLayout->addRow(
            uiText("dialog.preferences.performance.video_decode", "PV Render"),
            button);
    }

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
    // The Alt-hold pause-display behavior used to be a fixed hint label here;
    // it is now the editable preview.pause_display_hold entry in the table
    // (the capture edit accepts bare modifiers for hold-type shortcuts).
    flattenPageGroup(shortcutsGroup);
    shortcutsPageLayout->addWidget(shortcutsGroup);
    shortcutsPageLayout->addStretch(1);
    pageStack->addTab(shortcutsPage, uiText("dialog.preferences.shortcuts_group", "Shortcuts"));

    auto* extensionsPage = new QWidget(pageStack);
    auto* extensionsPageLayout = new QHBoxLayout(extensionsPage);
    extensionsPageLayout->setContentsMargins(0, 0, 0, 0);
    extensionsPageLayout->setSpacing(10);
    auto* extensionsActions = new QWidget(extensionsPage);
    auto* extensionsActionsLayout = new QVBoxLayout(extensionsActions);
    extensionsActionsLayout->setContentsMargins(12, 10, 8, 12);
    extensionsActionsLayout->setSpacing(8);
    auto* openExtensionsFolderButton =
        new QPushButton(uiText("dialog.preferences.extensions.open_folder", "Open Extensions Folder"), extensionsActions);
    auto* refreshExtensionsButton =
        new QPushButton(uiText("dialog.preferences.extensions.refresh", "Refresh Extensions"), extensionsActions);
    auto* openExtensionLogsButton =
        new QPushButton(uiText("dialog.preferences.extensions.open_logs", "Open Logs"), extensionsActions);
    openExtensionsFolderButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    refreshExtensionsButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    openExtensionLogsButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    extensionsActionsLayout->addWidget(openExtensionsFolderButton);
    extensionsActionsLayout->addWidget(refreshExtensionsButton);
    extensionsActionsLayout->addWidget(openExtensionLogsButton);
    extensionsActionsLayout->addStretch(1);

    auto* extensionsTable = new QTableWidget(extensionsPage);
    extensionsTable->setColumnCount(5);
    extensionsTable->setHorizontalHeaderLabels({
        uiText("dialog.preferences.extensions.enabled", "Enabled"),
        uiText("dialog.preferences.extensions.name", "Extension"),
        uiText("dialog.preferences.extensions.version", "Version"),
        uiText("dialog.preferences.extensions.contributions", "Contributions"),
        uiText("dialog.preferences.extensions.status", "Status"),
    });
    extensionsTable->verticalHeader()->hide();
    extensionsTable->setShowGrid(false);
    extensionsTable->setAlternatingRowColors(true);
    extensionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    extensionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    extensionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    extensionsTable->setTextElideMode(Qt::ElideRight);
    extensionsTable->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    extensionsTable->horizontalHeader()->setMinimumSectionSize(24);
    extensionsTable->horizontalHeader()->setDefaultSectionSize(96);
    extensionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    extensionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    extensionsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    extensionsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    extensionsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    extensionsTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    extensionsTable->setMinimumSize(0, 280);
    extensionsTable->setStyleSheet(QStringLiteral(
        "QTableWidget { border: 1px solid rgba(128,128,128,72); border-radius: 6px; }"
        "QTableWidget::item:selected { background: rgba(88, 145, 220, 58); }"
        "QHeaderView::section { padding: 6px 9px; border: 0; border-bottom: 1px solid rgba(128,128,128,72); font-weight: 600; }"
    ));
    extensionsTable->setMouseTracking(true);
    extensionsTable->viewport()->setMouseTracking(true);
    extensionsTable->viewport()->installEventFilter(new DelayedTableTooltipFilter(extensionsTable, extensionsTable));

    const auto contributionSummary = [](const miacode::extensions::ExtensionManifest& manifest) {
        QStringList parts;
        if (!manifest.commands.isEmpty()) {
            parts.append(QStringLiteral("%1 command(s)").arg(manifest.commands.size()));
        }
        if (!manifest.menus.isEmpty()) {
            parts.append(QStringLiteral("%1 menu item(s)").arg(manifest.menus.size()));
        }
        if (!manifest.languages.isEmpty()) {
            QStringList languages;
            for (const auto& language : manifest.languages) {
                languages.append(QStringLiteral("%1 (%2)").arg(language.label, language.id));
            }
            parts.append(QStringLiteral("language: %1").arg(languages.join(QStringLiteral(", "))));
        }
        return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral("; "));
    };

    std::function<void()> refreshExtensionRows;
    refreshExtensionRows = [&]() {
        extensionsTable->setRowCount(0);
        if (owner_.extensionManager_ == nullptr) {
            return;
        }
        const auto records = owner_.extensionManager_->records();
        extensionsTable->setRowCount(records.size());
        int row = 0;
        for (const auto& record : records) {
            auto* enabledBox = new QCheckBox(extensionsTable);
            enabledBox->setChecked(record.valid && record.enabled);
            enabledBox->setEnabled(record.valid);
            auto* enabledCell = new QWidget(extensionsTable);
            auto* enabledCellLayout = new QHBoxLayout(enabledCell);
            enabledCellLayout->setContentsMargins(0, 0, 0, 0);
            enabledCellLayout->setAlignment(Qt::AlignCenter);
            enabledCellLayout->addWidget(enabledBox);
            extensionsTable->setCellWidget(row, 0, enabledCell);

            const QString title = record.valid
                ? QStringLiteral("%1\n%2").arg(record.manifest.name, record.manifest.qualifiedId())
                : QFileInfo(record.sourcePath).fileName();
            auto* nameItem = new QTableWidgetItem(title);
            nameItem->setToolTip(QStringLiteral("%1\n%2").arg(title, record.sourcePath));
            extensionsTable->setItem(row, 1, nameItem);
            auto* versionItem = new QTableWidgetItem(record.valid ? record.manifest.version : QStringLiteral("-"));
            versionItem->setToolTip(versionItem->text());
            extensionsTable->setItem(row, 2, versionItem);
            auto* contributionItem = new QTableWidgetItem(record.valid ? contributionSummary(record.manifest) : QStringLiteral("-"));
            contributionItem->setToolTip(contributionItem->text());
            extensionsTable->setItem(row, 3, contributionItem);
            const QString status = !record.valid
                ? QStringLiteral("Invalid: %1").arg(record.diagnostic)
                : (record.enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
            auto* statusItem = new QTableWidgetItem(status);
            statusItem->setToolTip(record.diagnostic.trimmed().isEmpty() ? status : record.diagnostic);
            extensionsTable->setItem(row, 4, statusItem);
            if (record.valid) {
                QObject::connect(enabledBox, &QCheckBox::toggled, &dialog, [&, id = record.manifest.qualifiedId()](bool enabled) {
                    if (owner_.extensionManager_ == nullptr) {
                        return;
                    }
                    owner_.extensionManager_->setExtensionEnabled(id, enabled);
                    rebuildLanguageMenu();
                    refreshExtensionRows();
                    owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
                });
            }
            ++row;
        }
    };

    QObject::connect(openExtensionsFolderButton, &QPushButton::clicked, &dialog, [&]() {
        const QString dir = owner_.extensionManager_ != nullptr
            ? owner_.extensionManager_->userExtensionsDirectory()
            : miacode::extensions::userExtensionDirectoryPath();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    QObject::connect(refreshExtensionsButton, &QPushButton::clicked, &dialog, [&]() {
        if (owner_.extensionManager_ != nullptr) {
            owner_.extensionManager_->refreshExtensions();
        } else {
            UiText::reloadExtensionLanguagePacks();
            UiText::ensurePreferredLanguageAvailable();
        }
        rebuildLanguageMenu();
        refreshExtensionRows();
        owner_.statusBar()->showMessage(uiText("status.preferences_updated", "Preferences updated."));
    });
    QObject::connect(openExtensionLogsButton, &QPushButton::clicked, &dialog, [&]() {
        const QString dir = owner_.extensionManager_ != nullptr
            ? owner_.extensionManager_->extensionLogDirectory()
            : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    refreshExtensionRows();
    extensionsPageLayout->addWidget(extensionsActions, 0);
    extensionsPageLayout->addWidget(extensionsTable, 1);
    pageStack->addTab(extensionsPage, uiText("dialog.preferences.extensions_group", "Extensions"));

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
        const auto editableDefinitions = []() {
            return ShortcutRegistry::instance().editableShortcuts();
        };
        refreshRows = [&]() {
            const QList<ShortcutRegistry::ShortcutDefinition> definitions = editableDefinitions();
            QHash<QString, ShortcutRegistry::ShortcutDefinition> definitionById;
            for (const auto& definition : definitions) {
                definitionById.insert(definition.id, definition);
            }
            QHash<QString, int> shortcutUseCounts;
            for (const auto& definition : definitions) {
                const QList<QKeySequence> sequences =
                    ShortcutRegistry::instance().sequences(definition.id, definition.defaultSequences);
                for (const QKeySequence& sequence : sequences) {
                    if (!sequence.isEmpty()) {
                        ++shortcutUseCounts[shortcutSequenceKey(sequence)];
                    }
                }
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
                    const QString label = shortcutDefinitionLabel(definition);
                    auto* commandItem = new QTableWidgetItem(label);
                    commandItem->setData(Qt::UserRole, definition.id);
                    commandItem->setToolTip(definition.id);
                    table->setItem(row, 0, commandItem);
                    const QList<QKeySequence> sequences =
                        ShortcutRegistry::instance().sequences(definition.id, definition.defaultSequences);
                    auto* keybindingItem = new QTableWidgetItem(shortcutSequenceText(sequences));
                    keybindingItem->setData(Qt::UserRole, definition.id);
                    keybindingItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
                    keybindingItem->setToolTip(uiText("dialog.preferences.shortcuts.change", "Change Keybinding"));
                    bool duplicate = false;
                    for (const QKeySequence& sequence : sequences) {
                        if (!sequence.isEmpty() && shortcutUseCounts.value(shortcutSequenceKey(sequence)) > 1) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) {
                        keybindingItem->setForeground(QBrush(QColor(0xD9, 0x32, 0x32)));
                    }
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
            // Hold-type shortcuts may bind a bare modifier (Alt alone), which
            // the regular combo capture deliberately swallows.
            const bool allowBareModifier = (id == QStringLiteral("preview.pause_display_hold"));
            auto* prompt = new QLabel(
                allowBareModifier
                    ? uiText(
                          "dialog.preferences.shortcuts.capture_prompt_hold",
                          "Press the key to hold (a bare modifier like Alt works), then press Enter.")
                    : uiText(
                          "dialog.preferences.shortcuts.capture_prompt",
                          "Press the desired key combination, then press Enter."),
                &captureDialog);
            prompt->setAlignment(Qt::AlignCenter);
            auto* captureEdit = new ShortcutCaptureEdit(&captureDialog, allowBareModifier);
            captureEdit->setMinimumHeight(42);
            captureEdit->setStyleSheet(QStringLiteral("font-size: 15px; padding: 7px 10px;"));
            auto* captureBlocker = new ShortcutCaptureEventBlocker(&captureDialog, captureEdit);
            qApp->installEventFilter(captureBlocker);
            QObject::connect(&captureDialog, &QDialog::finished, &captureDialog, [captureBlocker]() {
                qApp->removeEventFilter(captureBlocker);
            });
            auto* previewLabel = new QLabel(&captureDialog);
            previewLabel->setAlignment(Qt::AlignCenter);
            previewLabel->setMinimumHeight(26);
            previewLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
            auto* conflictLabel = new QLabel(&captureDialog);
            conflictLabel->setAlignment(Qt::AlignCenter);
            conflictLabel->setMinimumHeight(24);
            conflictLabel->setWordWrap(true);
            conflictLabel->setStyleSheet(QStringLiteral("color: #D93232; font-weight: 600;"));
            auto* captureButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Reset | QDialogButtonBox::Cancel, &captureDialog);
            UiDialogs::localizeButtonBox(captureButtons);
            captureLayout->addWidget(prompt);
            captureLayout->addWidget(captureEdit);
            captureLayout->addWidget(previewLabel);
            captureLayout->addWidget(conflictLabel);
            captureLayout->addStretch(1);
            captureLayout->addWidget(captureButtons);
            const auto updateConflictState = [captureEdit, previewLabel, conflictLabel, editableDefinitions, id]() {
                previewLabel->setText(captureEdit->text());
                const QString conflict = conflictingShortcutLabel(editableDefinitions(), id, captureEdit->sequence());
                if (conflict.isEmpty()) {
                    previewLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
                    conflictLabel->clear();
                    return;
                }
                previewLabel->setStyleSheet(QStringLiteral("font-weight: 600; color: #D93232;"));
                conflictLabel->setText(
                    UiText::isChineseUi()
                        ? QStringLiteral("与「%1」重复").arg(conflict)
                        : QStringLiteral("Conflicts with \"%1\"").arg(conflict));
            };
            QObject::connect(captureEdit, &QLineEdit::textChanged, &captureDialog, updateConflictState);
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

    // Width floor: widest page + the tab widget's TRUE horizontal chrome,
    // measured from live geometry. Two facts force this shape (see the
    // qt-ui-layout-pitfalls skill, W2/W3):
    //   - the root layout's SetFixedSize constraint ignores dialog-level
    //     setMinimumWidth but honors a child's minimum, so the floor lives
    //     on the tab widget (same as the render-settings dialog, df0829d);
    //   - the QSS pane padding (8px per side) is absent from
    //     QTabWidget::sizeHint, so without compensation every page comes up
    //     ~16px short and Fixed-width fields (menu buttons, the font
    //     shortcut hint) clip at the right pane edge.
    // Measuring instead of hardcoding keeps the dialog as narrow as the
    // content allows while never re-clipping when rows change.
    int maxPageWidth = 0;
    for (int i = 0; i < pageStack->count(); ++i) {
        QWidget* page = pageStack->widget(i);
        if (page == nullptr) {
            continue;
        }
        if (page->layout() != nullptr) {
            page->layout()->activate();
        }
        maxPageWidth = qMax(maxPageWidth, page->sizeHint().width());
    }
    dialog.adjustSize();
    QCoreApplication::sendPostedEvents(pageStack, QEvent::LayoutRequest);
    auto* pageStackInner = pageStack->findChild<QStackedWidget*>();
    // Fallback chrome if the inner stack is not measurable: 2*8px QSS pane
    // padding + 2*1px pane border (must track the QSS in
    // UiTheme::dialogTabStripStyleSheet()).
    int tabChrome = 18;
    if (pageStackInner != nullptr && pageStack->width() > pageStackInner->width()) {
        tabChrome = pageStack->width() - pageStackInner->width();
    }
    // FIX (2026-06-19): a plain minimumWidth floor never narrowed the dialog —
    // the SetFixedSize root layout sizes the dialog to the QTabWidget's sizeHint,
    // which over-reports well past the widest page, so the floor sat below it and
    // never bound (the "didn't get narrower" report). Pin the tab widget to the
    // measured content width instead; SetFixedSize then collapses the dialog onto
    // it. Clamp up to the tab strip's own width so pinning can never clip the
    // tabs, and to maxPageWidth so every page still fits — no clipping, just the
    // dead right-hand margin removed.
    int tabBarWidth = 0;
    if (auto* tabBar = pageStack->findChild<QTabBar*>()) {
        tabBarWidth = tabBar->sizeHint().width();
    }
    const int pinnedWidth = qMax(maxPageWidth, tabBarWidth) + tabChrome;
    pageStack->setFixedWidth(pinnedWidth);
    // The adjustSize() above locked the dialog onto its (wide) natural sizeHint;
    // SetFixedSize won't recompute from a child's fixedWidth until the layout
    // re-activates, so force it now or the narrowing silently never lands (the
    // 2026-06-19 "still not narrower" report).
    rootLayout->activate();
    dialog.adjustSize();
    // --debug breadcrumb: if the dialog is still wide, this prints the real
    // per-page hint so the over-reporting page can be pinned without guessing.
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preferences/width"),
        QStringLiteral("maxPage=%1 tabBar=%2 chrome=%3 pinned=%4 dialogW=%5")
            .arg(maxPageWidth)
            .arg(tabBarWidth)
            .arg(tabChrome)
            .arg(pinnedWidth)
            .arg(dialog.width()),
        true);

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
