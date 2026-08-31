#include "MainWindow.PreferencesSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "UiComponents.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "app/ui/EditableValueLabel.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/InputShortcutGesture.h"
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

QString shortcutTextListDisplay(const QStringList& shortcuts)
{
    QStringList parts;
    for (const QString& shortcut : shortcuts) {
        const QString text = miacode::input_shortcut::gestureDisplayText(shortcut);
        if (!text.isEmpty()) {
            parts.append(text);
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

QString shortcutGestureKey(const QString& shortcut)
{
    return miacode::input_shortcut::normalizeGestureText(shortcut);
}

QString shortcutDefinitionLabel(const ShortcutRegistry::ShortcutDefinition& definition)
{
    if (!definition.labelKey.isEmpty()) {
        const QString label = UiText::text(definition.labelKey);
        if (label != definition.labelKey) {
            return label;
        }
    }
    return definition.labelEn.isEmpty() ? definition.id : definition.labelEn;
}

QString conflictingShortcutLabel(
    const QList<ShortcutRegistry::ShortcutDefinition>& definitions,
    const QString& currentId,
    const QString& shortcut)
{
    const QString normalizedShortcut = shortcutGestureKey(shortcut);
    if (normalizedShortcut.isEmpty()) {
        return QString();
    }
    for (const auto& definition : definitions) {
        if (definition.id == currentId) {
            continue;
        }
        const QStringList shortcuts =
            ShortcutRegistry::instance().shortcutTexts(definition.id, definition.defaultShortcutTexts);
        for (const QString& existing : shortcuts) {
            if (shortcutGestureKey(existing) == normalizedShortcut) {
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
    QString shortcutText() const { return shortcutText_; }

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
        // sequence ("Esc"), which is never what the user means by
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
            shortcutText_.clear();
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
                shortcutText_ = sequence_.toString(QKeySequence::PortableText);
                setText(shortcutTextListDisplay({shortcutText_}));
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
        shortcutText_ = sequence_.toString(QKeySequence::PortableText);
        setText(shortcutTextListDisplay({shortcutText_}));
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (event == nullptr) {
            return;
        }
        const auto direction = miacode::input_shortcut::wheelDirectionFromEvent(event);
        if (direction == miacode::input_shortcut::WheelDirection::None) {
            event->accept();
            return;
        }
        sequence_ = QKeySequence();
        shortcutText_ = miacode::input_shortcut::wheelGestureText(event->modifiers(), direction);
        setText(shortcutTextListDisplay({shortcutText_}));
        event->accept();
    }

private:
    QKeySequence sequence_;
    QString shortcutText_;
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
        if (event->type() == QEvent::Wheel) {
            if (QApplication::focusWidget() == edit_) {
                return false;
            }
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            QWheelEvent forwarded(
                QPointF(edit_->mapFromGlobal(wheelEvent->globalPosition().toPoint())),
                wheelEvent->globalPosition(),
                wheelEvent->pixelDelta(),
                wheelEvent->angleDelta(),
                wheelEvent->buttons(),
                wheelEvent->modifiers(),
                wheelEvent->phase(),
                wheelEvent->inverted(),
                wheelEvent->source(),
                wheelEvent->pointingDevice());
            QApplication::sendEvent(edit_, &forwarded);
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

class DelayedTableToolTipFilter final : public QObject {
public:
    explicit DelayedTableToolTipFilter(QTableWidget* table, int delayMs, QObject* parent = nullptr)
        : QObject(parent)
        , table_(table)
    {
        timer_.setSingleShot(true);
        timer_.setInterval(delayMs);
        connect(&timer_, &QTimer::timeout, this, [this]() {
            showPendingToolTip();
        });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (table_ == nullptr || watched != table_->viewport()) {
            return QObject::eventFilter(watched, event);
        }
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QModelIndex index = table_->indexAt(mouseEvent->pos());
            if (!index.isValid()) {
                clearPendingToolTip();
                break;
            }
            if (index.row() == pendingRow_ && index.column() == pendingColumn_) {
                pendingViewportPos_ = mouseEvent->pos();
                break;
            }
            pendingRow_ = index.row();
            pendingColumn_ = index.column();
            pendingViewportPos_ = mouseEvent->pos();
            timer_.start();
            QToolTip::hideText();
            break;
        }
        case QEvent::Leave:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            clearPendingToolTip();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QString toolTipTextForCell(int row, int column) const
    {
        if (table_ == nullptr || row < 0 || column < 0) {
            return QString();
        }
        if (QTableWidgetItem* item = table_->item(row, column); item != nullptr) {
            const QString tip = item->toolTip().trimmed();
            return tip.isEmpty() ? item->text().trimmed() : tip;
        }
        if (QWidget* widget = table_->cellWidget(row, column); widget != nullptr) {
            const QString tip = widget->toolTip().trimmed();
            return tip.isEmpty() ? widget->accessibleDescription().trimmed() : tip;
        }
        return QString();
    }

    void clearPendingToolTip()
    {
        timer_.stop();
        pendingRow_ = -1;
        pendingColumn_ = -1;
        pendingViewportPos_ = QPoint();
        QToolTip::hideText();
    }

    void showPendingToolTip()
    {
        if (table_ == nullptr || pendingRow_ < 0 || pendingColumn_ < 0) {
            return;
        }
        const QModelIndex currentIndex = table_->indexAt(pendingViewportPos_);
        if (!currentIndex.isValid()
            || currentIndex.row() != pendingRow_
            || currentIndex.column() != pendingColumn_) {
            return;
        }
        const QString text = toolTipTextForCell(pendingRow_, pendingColumn_);
        if (text.isEmpty() || text == QStringLiteral("-")) {
            return;
        }
        QToolTip::showText(
            table_->viewport()->mapToGlobal(pendingViewportPos_),
            text,
            table_->viewport(),
            table_->visualRect(currentIndex));
    }

    QPointer<QTableWidget> table_;
    QTimer timer_;
    int pendingRow_ = -1;
    int pendingColumn_ = -1;
    QPoint pendingViewportPos_;
};

// Item delegate that gives every row a consistent text inset. Two passes:
//   1) Paint the panel (selection bg, hover, item bg) at the FULL cell rect
//      so a row-selected item gets one continuous blue band across both
//      columns; adjusting `opt.rect` in a single-pass paint would shrink
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
    QList<QPair<QString, QStringList>> groups{
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
                QStringLiteral("timeline.zoom_in"),
                QStringLiteral("timeline.zoom_out"),
            },
        },
        {
            UiText::isChineseUi() ? QStringLiteral("编辑器") : QStringLiteral("Editor"),
            {
                QStringLiteral("editor.font_decrease"),
                QStringLiteral("editor.font_increase"),
                QStringLiteral("editor.overwrite_mode"),
            },
        },
    };
    return groups;
}

QString compactJsonText(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'f', 2).replace(QRegularExpression(QStringLiteral("\\.?0+$")), QString());
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

QTableWidgetItem* readOnlyTableItem(const QString& text, const QString& tooltip = {})
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setToolTip(tooltip.isEmpty() ? text : tooltip);
    return item;
}

void configureDevToolsTable(QTableWidget* table, const QStringList& headers)
{
    if (table == nullptr) {
        return;
    }
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setTextElideMode(Qt::ElideRight);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setDefaultSectionSize(132);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->setStyleSheet(QStringLiteral(
        "QTableWidget { border: 1px solid rgba(128,128,128,72); border-radius: 6px; }"
        "QTableWidget::item:selected { background: rgba(88, 145, 220, 58); }"
        "QHeaderView::section { padding: 6px 9px; border: 0; border-bottom: 1px solid rgba(128,128,128,72); font-weight: 600; }"));
}

void populateApiRegistryTable(QTableWidget* table, const QJsonArray& api)
{
    table->setRowCount(api.size());
    for (int row = 0; row < api.size(); ++row) {
        const QJsonObject item = api.at(row).toObject();
        table->setItem(row, 0, readOnlyTableItem(item.value(QStringLiteral("id")).toString()));
        table->setItem(row, 1, readOnlyTableItem(item.value(QStringLiteral("method")).toString()));
        table->setItem(row, 2, readOnlyTableItem(item.value(QStringLiteral("status")).toString()));
        table->setItem(row, 3, readOnlyTableItem(item.value(QStringLiteral("permission")).toString()));
        table->setItem(row, 4, readOnlyTableItem(item.value(QStringLiteral("risk")).toString()));
        table->setItem(row, 5, readOnlyTableItem(item.value(QStringLiteral("description")).toString()));
    }
}

QString jsonArrayCountText(const QJsonObject& object, const QString& key)
{
    return QString::number(object.value(key).toArray().size());
}

QString firstDiagnosticText(const QJsonObject& object)
{
    const QJsonArray diagnostics = object.value(QStringLiteral("diagnostics")).toArray();
    if (diagnostics.isEmpty()) {
        return QString();
    }
    return compactJsonText(diagnostics.at(0));
}

void populateJsonArrayList(QListWidget* list, const QJsonArray& values)
{
    list->clear();
    for (const QJsonValue& value : values) {
        const QString text = compactJsonText(value);
        auto* item = new QListWidgetItem(text.isEmpty() ? QStringLiteral("-") : text, list);
        item->setToolTip(item->text());
    }
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
    if (owner_.timelineQuickStateBridge_ != nullptr) {
        owner_.timelineQuickStateBridge_->setZoomWheelShortcuts(
            ShortcutRegistry::instance().shortcutTexts(
                QStringLiteral("timeline.zoom_in"),
                {QStringLiteral("Ctrl+WheelUp")}),
            ShortcutRegistry::instance().shortcutTexts(
                QStringLiteral("timeline.zoom_out"),
                {QStringLiteral("Ctrl+WheelDown")}));
    }
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

void MainWindow::onPreferences()
{
    // The QML shell owns 偏好设置; the menu action and shortcut are forwarded
    // to it through the same request signal.
    emit preferencesRequested();
}

void MainWindow::applyConfiguredShortcuts()
{
    preferencesSection_->applyConfiguredShortcuts();
}
