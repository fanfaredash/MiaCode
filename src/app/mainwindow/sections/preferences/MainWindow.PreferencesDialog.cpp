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
#include "UiComponents.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "app/ui/AppBackgroundSettings.h"
#include "app/ui/EditableValueLabel.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "extensions/ExtensionManager.h"
#include "extensions/ExtensionManifest.h"
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
            UiText::isChineseUi() ? QStringLiteral("编辑器") : QStringLiteral("Editor"),
            {
                QStringLiteral("editor.font_decrease"),
                QStringLiteral("editor.font_increase"),
                QStringLiteral("editor.overwrite_mode"),
            },
        },
    };
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

void populateOpenBridgeTable(QTableWidget* table, const QJsonArray& objects)
{
    int rowCount = 0;
    for (const QJsonValue& value : objects) {
        const QJsonObject object = value.toObject();
        const QJsonArray methods = object.value(QStringLiteral("methods")).toArray();
        rowCount += qMax(1, methods.size());
    }
    table->setRowCount(rowCount);

    int row = 0;
    const auto setRow = [&](const QJsonObject& object, const QJsonObject& method) {
        const QString route = method.value(QStringLiteral("hostMethod")).toString(
            method.value(QStringLiteral("command")).toString());
        table->setItem(row, 0, readOnlyTableItem(object.value(QStringLiteral("id")).toString()));
        table->setItem(row, 1, readOnlyTableItem(method.value(QStringLiteral("name")).toString()));
        table->setItem(row, 2, readOnlyTableItem(object.value(QStringLiteral("stability")).toString()));
        table->setItem(row, 3, readOnlyTableItem(method.value(QStringLiteral("status")).toString()));
        table->setItem(row, 4, readOnlyTableItem(method.value(QStringLiteral("permission")).toString(
            object.value(QStringLiteral("permission")).toString())));
        table->setItem(row, 5, readOnlyTableItem(route));
        table->setItem(row, 6, readOnlyTableItem(object.value(QStringLiteral("experimentalRaw")).toBool(false) ? QStringLiteral("yes") : QStringLiteral("no")));
        table->setItem(row, 7, readOnlyTableItem(method.value(QStringLiteral("description")).toString(
            object.value(QStringLiteral("description")).toString())));
        ++row;
    };
    for (const QJsonValue& value : objects) {
        const QJsonObject object = value.toObject();
        const QJsonArray methods = object.value(QStringLiteral("methods")).toArray();
        if (methods.isEmpty()) {
            setRow(object, QJsonObject{});
            continue;
        }
        for (const QJsonValue& methodValue : methods) {
            setRow(object, methodValue.toObject());
        }
    }
}

void populateRecentCallsTable(QTableWidget* table, const QJsonArray& calls)
{
    table->setRowCount(calls.size());
    for (int row = 0; row < calls.size(); ++row) {
        const QJsonObject item = calls.at(row).toObject();
        table->setItem(row, 0, readOnlyTableItem(item.value(QStringLiteral("timestamp")).toString()));
        table->setItem(row, 1, readOnlyTableItem(item.value(QStringLiteral("extensionId")).toString()));
        table->setItem(row, 2, readOnlyTableItem(item.value(QStringLiteral("method")).toString()));
        table->setItem(row, 3, readOnlyTableItem(item.value(QStringLiteral("permission")).toString()));
        table->setItem(row, 4, readOnlyTableItem(item.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("error")));
        table->setItem(row, 5, readOnlyTableItem(compactJsonText(item.value(QStringLiteral("elapsedMs")))));
        table->setItem(row, 6, readOnlyTableItem(item.value(QStringLiteral("error")).toString()));
        table->setItem(row, 7, readOnlyTableItem(item.value(QStringLiteral("paramsPreview")).toString()));
    }
}

void populateExtensionsDevToolsTable(QTableWidget* table, const QJsonArray& extensions)
{
    table->setRowCount(extensions.size());
    for (int row = 0; row < extensions.size(); ++row) {
        const QJsonObject item = extensions.at(row).toObject();
        table->setItem(row, 0, readOnlyTableItem(item.value(QStringLiteral("id")).toString()));
        table->setItem(row, 1, readOnlyTableItem(item.value(QStringLiteral("name")).toString()));
        table->setItem(row, 2, readOnlyTableItem(item.value(QStringLiteral("version")).toString()));
        table->setItem(row, 3, readOnlyTableItem(item.value(QStringLiteral("enabled")).toBool() ? QStringLiteral("yes") : QStringLiteral("no")));
        table->setItem(row, 4, readOnlyTableItem(item.value(QStringLiteral("valid")).toBool() ? QStringLiteral("yes") : QStringLiteral("no")));
        table->setItem(row, 5, readOnlyTableItem(compactJsonText(item.value(QStringLiteral("permissions")))));
        table->setItem(row, 6, readOnlyTableItem(item.value(QStringLiteral("diagnostic")).toString()));
    }
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

void MainWindow::showExtensionDevToolsDialog()
{
    QDialog dialog(UiDialogs::effectiveParentWidget(this));
    dialog.setWindowTitle(UiText::text(QStringLiteral("dialog.preferences.extensions.devtools")));
    dialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());
    dialog.resize(980, 680);
    dialog.setMinimumSize(820, 560);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* summaryGroup = new QGroupBox(
        UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.summary")),
        &dialog);
    auto* summaryLayout = new QGridLayout(summaryGroup);
    summaryLayout->setContentsMargins(12, 10, 12, 12);
    summaryLayout->setHorizontalSpacing(18);
    summaryLayout->setVerticalSpacing(6);

    auto* apiCountLabel = new QLabel(summaryGroup);
    auto* extensionCountLabel = new QLabel(summaryGroup);
    auto* rawCountLabel = new QLabel(summaryGroup);
    auto* callbackCountLabel = new QLabel(summaryGroup);
    auto* callCountLabel = new QLabel(summaryGroup);
    auto* diagnosticCountLabel = new QLabel(summaryGroup);
    const QList<QPair<QString, QLabel*>> summaryRows{
        {UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.api_count")), apiCountLabel},
        {UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.extension_count")), extensionCountLabel},
        {UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.raw_count")), rawCountLabel},
        {UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.callback_count")), callbackCountLabel},
        {UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.call_count")), callCountLabel},
        {UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.diagnostic_count")), diagnosticCountLabel},
    };
    for (int index = 0; index < summaryRows.size(); ++index) {
        auto* name = new QLabel(summaryRows.at(index).first, summaryGroup);
        name->setStyleSheet(QStringLiteral("font-weight: 600;"));
        const int row = index / 3;
        const int column = (index % 3) * 2;
        summaryLayout->addWidget(name, row, column);
        summaryLayout->addWidget(summaryRows.at(index).second, row, column + 1);
    }
    miacode::ui::flattenGroupForTabPage(summaryGroup);
    rootLayout->addWidget(summaryGroup, 0);

    auto* tabs = new QTabWidget(&dialog);
    tabs->setObjectName(QStringLiteral("ExtensionDevToolsTabs"));
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rootLayout->addWidget(tabs, 1);

    auto* apiTable = new QTableWidget(tabs);
    configureDevToolsTable(apiTable, {
        QStringLiteral("ID"),
        QStringLiteral("Method"),
        QStringLiteral("Status"),
        QStringLiteral("Permission"),
        QStringLiteral("Risk"),
        QStringLiteral("Description"),
    });
    tabs->addTab(apiTable, UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.api")));

    auto* openBridgeTable = new QTableWidget(tabs);
    configureDevToolsTable(openBridgeTable, {
        QStringLiteral("Object"),
        QStringLiteral("Method"),
        QStringLiteral("Stability"),
        QStringLiteral("Status"),
        QStringLiteral("Permission"),
        QStringLiteral("Route"),
        QStringLiteral("Raw"),
        QStringLiteral("Description"),
    });
    tabs->addTab(openBridgeTable, UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.open_bridge")));

    auto* recentCallsTable = new QTableWidget(tabs);
    configureDevToolsTable(recentCallsTable, {
        QStringLiteral("Time"),
        QStringLiteral("Extension"),
        QStringLiteral("Method"),
        QStringLiteral("Permission"),
        QStringLiteral("OK"),
        QStringLiteral("ms"),
        QStringLiteral("Error"),
        QStringLiteral("Params"),
    });
    tabs->addTab(recentCallsTable, UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.recent_calls")));

    auto* extensionsTable = new QTableWidget(tabs);
    configureDevToolsTable(extensionsTable, {
        QStringLiteral("ID"),
        QStringLiteral("Name"),
        QStringLiteral("Version"),
        QStringLiteral("Enabled"),
        QStringLiteral("Valid"),
        QStringLiteral("Permissions"),
        QStringLiteral("Diagnostic"),
    });
    tabs->addTab(extensionsTable, UiText::text(QStringLiteral("dialog.preferences.extensions_group")));

    auto* uiContributionsList = new QListWidget(tabs);
    uiContributionsList->setAlternatingRowColors(true);
    tabs->addTab(uiContributionsList, UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.ui")));

    auto* diagnosticsList = new QListWidget(tabs);
    diagnosticsList->setAlternatingRowColors(true);
    tabs->addTab(diagnosticsList, UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.diagnostics")));

    auto* rawJsonEdit = new QPlainTextEdit(tabs);
    rawJsonEdit->setReadOnly(true);
    rawJsonEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawJsonEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    tabs->addTab(rawJsonEdit, UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.raw_json")));

    const auto refreshSnapshot = [&]() {
        const QJsonObject snapshot = extensionManager_ != nullptr ? extensionManager_->devtoolsSnapshotForUi() : QJsonObject();
        const QJsonArray api = snapshot.value(QStringLiteral("api")).toArray();
        const QJsonArray openObjects = snapshot.value(QStringLiteral("openBridgeObjects")).toArray();
        const QJsonArray rawTargets = snapshot.value(QStringLiteral("experimentalRawTargets")).toArray();
        const QJsonArray extensions = snapshot.value(QStringLiteral("extensions")).toArray();
        const QJsonArray recentCalls = snapshot.value(QStringLiteral("recentCalls")).toArray();
        const QJsonArray diagnostics = snapshot.value(QStringLiteral("diagnostics")).toArray();
        const int callbackCount = snapshot.value(QStringLiteral("eventCallbackCount")).toInt();
        const QJsonArray uiContributions = snapshot.value(QStringLiteral("uiContributions")).toArray();
        const QJsonArray uiViews = snapshot.value(QStringLiteral("uiViews")).toArray();

        apiCountLabel->setText(QString::number(api.size()));
        extensionCountLabel->setText(QString::number(extensions.size()));
        rawCountLabel->setText(QString::number(rawTargets.size()));
        callbackCountLabel->setText(QString::number(callbackCount));
        callCountLabel->setText(QString::number(recentCalls.size()));
        diagnosticCountLabel->setText(QString::number(diagnostics.size()));

        populateApiRegistryTable(apiTable, api);
        populateOpenBridgeTable(openBridgeTable, openObjects);
        populateRecentCallsTable(recentCallsTable, recentCalls);
        populateExtensionsDevToolsTable(extensionsTable, extensions);
        populateJsonArrayList(diagnosticsList, diagnostics);

        QJsonArray uiItems;
        for (const QJsonValue& value : uiContributions) {
            uiItems.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("contribution")},
                {QStringLiteral("value"), value},
            });
        }
        for (const QJsonValue& value : uiViews) {
            uiItems.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("view")},
                {QStringLiteral("value"), value},
            });
        }
        populateJsonArrayList(uiContributionsList, uiItems);
        rawJsonEdit->setPlainText(QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Indented)));
    };

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* refreshButton = buttonBox->addButton(
        UiText::text(QStringLiteral("dialog.preferences.extensions.devtools.refresh")),
        QDialogButtonBox::ActionRole);
    if (QPushButton* closeButton = buttonBox->button(QDialogButtonBox::Close)) {
        closeButton->setText(UiText::text(QStringLiteral("action.close")));
    }
    QObject::connect(refreshButton, &QPushButton::clicked, &dialog, refreshSnapshot);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox, 0);

    refreshSnapshot();
    centerDialogOnAnchor(&dialog, this);
    dialog.exec();
}

void MainWindow::PreferencesSection::onPreferences()
{
    MC_OP("MainWindow::PreferencesSection::onPreferences");
    miacode::ui::TabbedSettingsDialog dialog(
        &owner_,
        UiText::text(QStringLiteral("dialog.preferences.title")),
        miacode::ui::SettingsDialogChrome::Preferences);
    QVBoxLayout* rootLayout = dialog.contentLayout();

    // Tab strip across the top matches the render-settings dialog so
    // the two preference-style surfaces share one visual pattern. The
    // shared CSS lives in UiTheme::dialogTabStripStyleSheet(), already
    // appended to the preferences stylesheet. We still call the page
    // container `pageStack` to keep the downstream addWidget()/setCurrent
    // call sites intact; addTab() does the same wrapping under the hood.
    auto* pageStack = dialog.createTabs(QStringLiteral("PreferenceTabs"));
    if (QTabBar* preferenceTabBar = pageStack->tabBar(); preferenceTabBar != nullptr) {
        preferenceTabBar->setUsesScrollButtons(true);
        preferenceTabBar->setElideMode(Qt::ElideRight);
        preferenceTabBar->setExpanding(false);
    }

    // Each preference page wraps its controls in a QGroupBox whose
    // title duplicates the tab name (e.g. "Appearance" inside the "Appearance"
    // tab). The tab strip already labels the page, so strip the inner
    // title + frame chrome before the group enters the tab. Same
    // recipe as the render-settings dialog.
    auto* appearancePage = new QWidget(pageStack);
    auto* appearancePageLayout = new QVBoxLayout(appearancePage);
    appearancePageLayout->setContentsMargins(0, 0, 0, 0);
    appearancePageLayout->setSpacing(10);
    auto* interfaceGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.preferences.interface_group")), appearancePage);
    auto* interfaceLayout = new QFormLayout(interfaceGroup);
    // Explicit margins like the editor/performance pages. The flattened
    // group box (border: none via flattenGroupForTabPage) zeroes the default
    // QSS layout margins, so without this the field column runs flush to
    // the pane edge and the menu buttons' right border gets clipped.
    interfaceLayout->setContentsMargins(12, 10, 12, 12);
    interfaceLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    interfaceLayout->setHorizontalSpacing(12);
    interfaceLayout->setVerticalSpacing(8);

    QString selectedLanguageToken = UiText::preferredLanguageToken();
    const UiText::ThemePreference currentThemePreference = UiText::preferredTheme();
    UiText::ThemePreference selectedThemePreference = currentThemePreference;
    QList<QPointer<QComboBox>> themedDialogCombos;
    const auto styleRegisteredDialogCombo = [&themedDialogCombos](QComboBox* combo, int maxVisibleItems = 12) {
        if (combo == nullptr) {
            return;
        }
        // All Preferences dropdowns read left-aligned. Combos built with raw
        // `new QComboBox` (editor tab: line spacing / auto-completion / header /
        // IME) reach styling only through here, so stamp the alignment property
        // the styler consumes. Factory combos already carry it; re-stamp is
        // idempotent.
        combo->setProperty("miacode.combo_text_alignment",
                           static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
        miacode::ui::applyDialogComboBoxStyle(combo, maxVisibleItems);
        for (const QPointer<QComboBox>& registered : themedDialogCombos) {
            if (registered == combo) {
                return;
            }
        }
        themedDialogCombos.append(combo);
    };
    const auto refreshRegisteredDialogCombos = [&themedDialogCombos]() {
        for (const QPointer<QComboBox>& combo : themedDialogCombos) {
            if (!combo.isNull()) {
                miacode::ui::applyDialogComboBoxStyle(combo.data(), 12);
            }
        }
    };
    const auto themeLabel = [](UiText::ThemePreference preference) -> QString {
        switch (preference) {
        case UiText::ThemePreference::Light:
            return UiText::text(QStringLiteral("dialog.preferences.theme.light"));
        case UiText::ThemePreference::Dark:
            return UiText::text(QStringLiteral("dialog.preferences.theme.dark"));
        case UiText::ThemePreference::System:
        default:
            return UiText::text(QStringLiteral("dialog.preferences.theme.system"));
        }
    };
    auto* languageLabelWidget = new QLabel(UiText::text(QStringLiteral("dialog.preferences.language")), interfaceGroup);
    auto* languageRow = new QWidget(interfaceGroup);
    auto* languageRowLayout = new QHBoxLayout(languageRow);
    languageRowLayout->setContentsMargins(0, 0, 0, 0);
    languageRowLayout->setSpacing(12);
    auto* languageCombo =
        miacode::ui::createDialogComboBox(languageRow, 12, Qt::AlignLeft | Qt::AlignVCenter);
    languageCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    languageCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Extensions can add/remove language packs while the dialog is open, so
    // repopulation is reusable (blocked: refilling must not fire the restart
    // prompt below).
    std::function<void()> rebuildLanguageCombo = [&, languageCombo]() {
        const QSignalBlocker blocker(languageCombo);
        languageCombo->clear();
        for (const auto& option : UiText::availableLanguageOptions()) {
            languageCombo->addItem(option.label, option.id);
        }
        languageCombo->setCurrentIndex(
            qMax(0, languageCombo->findData(selectedLanguageToken.trimmed().toLower())));
        miacode::ui::applyDialogComboBoxStyle(languageCombo, 12);
    };
    rebuildLanguageCombo();
    styleRegisteredDialogCombo(languageCombo);
    connect(languageCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [&, languageCombo](int index) {
                if (index < 0) {
                    return;
                }
                selectedLanguageToken = languageCombo->itemData(index).toString();
                UiText::setPreferredLanguageToken(selectedLanguageToken);
                QMessageBox::information(
                    &dialog,
                    UiText::text(QStringLiteral("dialog.preferences.restart_title")),
                    UiText::text(QStringLiteral("dialog.preferences.restart_message")),
                    QMessageBox::Ok);
                owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_saved")));
            });
    languageRowLayout->addWidget(languageCombo, 0);
    languageRowLayout->addStretch(1);
    interfaceLayout->addRow(languageLabelWidget, languageRow);

    auto* themeLabelWidget = new QLabel(UiText::text(QStringLiteral("dialog.preferences.theme")), interfaceGroup);
    auto* themeRow = new QWidget(interfaceGroup);
    auto* themeRowLayout = new QHBoxLayout(themeRow);
    themeRowLayout->setContentsMargins(0, 0, 0, 0);
    themeRowLayout->setSpacing(12);
    auto* themeCombo =
        miacode::ui::createDialogComboBox(themeRow, 12, Qt::AlignLeft | Qt::AlignVCenter);
    themeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    themeCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const QList<UiText::ThemePreference> themeOptions{
        UiText::ThemePreference::System,
        UiText::ThemePreference::Light,
        UiText::ThemePreference::Dark,
    };
    for (UiText::ThemePreference preference : themeOptions) {
        themeCombo->addItem(themeLabel(preference), static_cast<int>(preference));
    }
    themeCombo->setCurrentIndex(
        qMax(0, themeCombo->findData(static_cast<int>(selectedThemePreference))));
    styleRegisteredDialogCombo(themeCombo);
    connect(themeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [&, themeCombo](int index) {
                if (index < 0) {
                    return;
                }
                selectedThemePreference =
                    static_cast<UiText::ThemePreference>(themeCombo->itemData(index).toInt());
                UiText::setPreferredTheme(selectedThemePreference);
                owner_.windowSection_->applyUiTheme();
                dialog.refreshStyleSheet();
                refreshRegisteredDialogCombos();
                owner_.windowSection_->applySystemWindowBackdrop(&dialog);
                owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
            });
    themeRowLayout->addWidget(themeCombo, 0);
    themeRowLayout->addStretch(1);
    interfaceLayout->addRow(themeLabelWidget, themeRow);

    // Chart-preview position (the same workspace left/right swap exposed by
    // the Preview menu's left/right panel swap. false = preview on the right
    // (default), true = preview on the left.
    const auto previewSideLabel = [](bool previewOnLeft) -> QString {
        return previewOnLeft
            ? UiText::text(QStringLiteral("dialog.preferences.preview_side.left"))
            : UiText::text(QStringLiteral("dialog.preferences.preview_side.right"));
    };
    auto* previewSideLabelWidget =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.preview_side")), interfaceGroup);
    auto* previewSideRow = new QWidget(interfaceGroup);
    auto* previewSideRowLayout = new QHBoxLayout(previewSideRow);
    previewSideRowLayout->setContentsMargins(0, 0, 0, 0);
    previewSideRowLayout->setSpacing(12);
    auto* previewSideCombo =
        miacode::ui::createDialogComboBox(previewSideRow, 12, Qt::AlignLeft | Qt::AlignVCenter);
    previewSideCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    previewSideCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    previewSideCombo->addItem(previewSideLabel(false), false);
    previewSideCombo->addItem(previewSideLabel(true), true);
    previewSideCombo->setCurrentIndex(owner_.workspacePanelsSwapped_ ? 1 : 0);
    styleRegisteredDialogCombo(previewSideCombo);
    connect(previewSideCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [&, previewSideCombo](int index) {
                if (index < 0) {
                    return;
                }
                owner_.setWorkspacePanelsSwapped(previewSideCombo->itemData(index).toBool(), true);
                owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
            });
    previewSideRowLayout->addWidget(previewSideCombo, 0);
    previewSideRowLayout->addStretch(1);
    interfaceLayout->addRow(previewSideLabelWidget, previewSideRow);

    miacode::ui::flattenGroupForTabPage(interfaceGroup);
    appearancePageLayout->addWidget(interfaceGroup);
    appearancePageLayout->addStretch(1);
    pageStack->addTab(appearancePage, UiText::text(QStringLiteral("dialog.preferences.interface_group")));

    auto* backgroundPage = new QWidget(pageStack);
    auto* backgroundPageLayout = new QVBoxLayout(backgroundPage);
    backgroundPageLayout->setContentsMargins(0, 0, 0, 0);
    backgroundPageLayout->setSpacing(10);
    auto* backgroundGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.preferences.background_group")), backgroundPage);
    auto* backgroundLayout = new QFormLayout(backgroundGroup);
    backgroundLayout->setContentsMargins(12, 10, 12, 12);
    backgroundLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    backgroundLayout->setHorizontalSpacing(12);
    backgroundLayout->setVerticalSpacing(8);

    miacode::ui::AppBackgroundSettings selectedBackgroundSettings = state_.appBackgroundSettings_;
    const auto persistBackgroundSettings = [&]() {
        selectedBackgroundSettings = miacode::ui::normalizedAppBackgroundSettings(selectedBackgroundSettings);
        owner_.applyAppBackgroundSettings(selectedBackgroundSettings, true);
    };
    bool backgroundSliderPersistPending = false;
    const auto persistBackgroundSettingsAfterSliderInput = [&](QSlider* slider) {
        if (slider != nullptr && slider->isSliderDown()) {
            backgroundSliderPersistPending = true;
            return;
        }
        persistBackgroundSettings();
    };
    const auto flushBackgroundSliderSettings = [&]() {
        if (!backgroundSliderPersistPending) {
            return;
        }
        backgroundSliderPersistPending = false;
        persistBackgroundSettings();
    };
    const auto createBackgroundComboRow = [](QComboBox* combo, QWidget* parent) -> QWidget* {
        auto* row = new QWidget(parent);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);
        if (combo != nullptr) {
            combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            rowLayout->addWidget(combo, 0);
        }
        rowLayout->addStretch(1);
        return row;
    };
    const auto backgroundModeLabel = [](miacode::ui::AppBackgroundSizeMode mode) -> QString {
        switch (mode) {
        case miacode::ui::AppBackgroundSizeMode::Contain:
            return UiText::text(QStringLiteral("dialog.preferences.background.scale.contain"));
        case miacode::ui::AppBackgroundSizeMode::Stretch:
            return UiText::text(QStringLiteral("dialog.preferences.background.scale.stretch"));
        case miacode::ui::AppBackgroundSizeMode::Center:
            return UiText::text(QStringLiteral("dialog.preferences.background.scale.center"));
        case miacode::ui::AppBackgroundSizeMode::Repeat:
            return UiText::text(QStringLiteral("dialog.preferences.background.scale.repeat"));
        case miacode::ui::AppBackgroundSizeMode::Cover:
        default:
            return UiText::text(QStringLiteral("dialog.preferences.background.scale.cover"));
        }
    };
    const auto backgroundPositionLabel = [](miacode::ui::AppBackgroundPosition position) -> QString {
        switch (position) {
        case miacode::ui::AppBackgroundPosition::Left:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.left"));
        case miacode::ui::AppBackgroundPosition::Right:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.right"));
        case miacode::ui::AppBackgroundPosition::Top:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.top"));
        case miacode::ui::AppBackgroundPosition::Bottom:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.bottom"));
        case miacode::ui::AppBackgroundPosition::LeftTop:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.left_top"));
        case miacode::ui::AppBackgroundPosition::RightTop:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.right_top"));
        case miacode::ui::AppBackgroundPosition::LeftBottom:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.left_bottom"));
        case miacode::ui::AppBackgroundPosition::RightBottom:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.right_bottom"));
        case miacode::ui::AppBackgroundPosition::Center:
        default:
            return UiText::text(QStringLiteral("dialog.preferences.background.position.center"));
        }
    };

    auto* backgroundEnabledLabel =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.background.enabled")), backgroundGroup);
    auto* backgroundEnabledCombo =
        miacode::ui::createDialogComboBox(backgroundGroup, 12, Qt::AlignLeft | Qt::AlignVCenter);
    backgroundEnabledCombo->addItem(UiText::text(QStringLiteral("preferences.on")), true);
    backgroundEnabledCombo->addItem(UiText::text(QStringLiteral("preferences.off")), false);
    backgroundEnabledCombo->setCurrentIndex(selectedBackgroundSettings.enabled ? 0 : 1);
    styleRegisteredDialogCombo(backgroundEnabledCombo, 12);
    connect(backgroundEnabledCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&, backgroundEnabledCombo](int index) {
        if (index < 0) {
            return;
        }
        selectedBackgroundSettings.enabled = backgroundEnabledCombo->itemData(index).toBool();
        persistBackgroundSettings();
    });
    backgroundLayout->addRow(backgroundEnabledLabel,
                             createBackgroundComboRow(backgroundEnabledCombo, backgroundGroup));

    auto* backgroundImageLabel =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.background.image")), backgroundGroup);
    auto* backgroundImageRow = new QWidget(backgroundGroup);
    auto* backgroundImageRowLayout = new QHBoxLayout(backgroundImageRow);
    backgroundImageRowLayout->setContentsMargins(0, 0, 0, 0);
    backgroundImageRowLayout->setSpacing(8);
    auto* backgroundImageEdit = new QLineEdit(backgroundImageRow);
    backgroundImageEdit->setReadOnly(true);
    backgroundImageEdit->setText(selectedBackgroundSettings.imagePath);
    backgroundImageEdit->setMinimumWidth(0);
    backgroundImageEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* chooseBackgroundButton = miacode::ui::createDialogPushButton(
        UiText::text(QStringLiteral("dialog.preferences.background.choose")),
        backgroundImageRow);
    auto* clearBackgroundButton = miacode::ui::createDialogPushButton(
        UiText::text(QStringLiteral("dialog.preferences.background.clear")),
        backgroundImageRow);
    backgroundImageRowLayout->addWidget(backgroundImageEdit, 1);
    backgroundImageRowLayout->addWidget(chooseBackgroundButton, 0);
    backgroundImageRowLayout->addWidget(clearBackgroundButton, 0);
    connect(chooseBackgroundButton, &QPushButton::clicked, &dialog, [&]() {
        const QString initialDir = selectedBackgroundSettings.imagePath.isEmpty()
            ? QString()
            : QFileInfo(selectedBackgroundSettings.imagePath).absolutePath();
        const QString filePath = QFileDialog::getOpenFileName(
            &dialog,
            UiText::text(QStringLiteral("dialog.preferences.background.choose")),
            initialDir,
            UiText::text(QStringLiteral("dialog.preferences.background.image_filter")));
        if (filePath.isEmpty()) {
            return;
        }
        selectedBackgroundSettings.imagePath = QDir::cleanPath(filePath);
        backgroundImageEdit->setText(selectedBackgroundSettings.imagePath);
        persistBackgroundSettings();
    });
    connect(clearBackgroundButton, &QPushButton::clicked, &dialog, [&]() {
        selectedBackgroundSettings.imagePath.clear();
        backgroundImageEdit->clear();
        persistBackgroundSettings();
    });
    backgroundLayout->addRow(backgroundImageLabel, backgroundImageRow);

    auto* backgroundOpacityLabel =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.background.opacity")), backgroundGroup);
    auto* backgroundOpacitySlider = new QSlider(Qt::Horizontal, backgroundGroup);
    backgroundOpacitySlider->setRange(0, 80);
    backgroundOpacitySlider->setValue(qRound(selectedBackgroundSettings.opacity * 100.0));
    backgroundOpacitySlider->setSingleStep(1);
    backgroundOpacitySlider->setPageStep(5);
    backgroundLayout->addRow(
        backgroundOpacityLabel,
        miacode::ui::createSliderValueRow(backgroundOpacitySlider, nullptr, QStringLiteral("%"), backgroundGroup));
    connect(backgroundOpacitySlider, &QSlider::valueChanged, &dialog, [&](int value) {
        selectedBackgroundSettings.opacity = static_cast<double>(value) / 100.0;
        persistBackgroundSettingsAfterSliderInput(backgroundOpacitySlider);
    });
    connect(backgroundOpacitySlider, &QSlider::sliderReleased, &dialog, flushBackgroundSliderSettings);

    const auto openBackgroundOverlayDialog = [&]() {
        QDialog overlayDialog(&dialog);
        overlayDialog.setWindowTitle(UiText::text(QStringLiteral("dialog.preferences.background.overlay_dialog_title")));
        overlayDialog.setModal(true);
        overlayDialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());

        auto* overlayLayout = new QVBoxLayout(&overlayDialog);
        overlayLayout->setContentsMargins(12, 12, 12, 12);
        overlayLayout->setSpacing(10);

        auto* overlayGroup = new QGroupBox(
            UiText::text(QStringLiteral("dialog.preferences.background.overlay")),
            &overlayDialog);
        auto* overlayFormLayout = new QFormLayout(overlayGroup);
        overlayFormLayout->setContentsMargins(12, 10, 12, 12);
        overlayFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        overlayFormLayout->setHorizontalSpacing(12);
        overlayFormLayout->setVerticalSpacing(8);

        struct OverlayRow {
            QLabel* label = nullptr;
            QSlider* slider = nullptr;
            miacode::ui::EditableValueLabel* valueLabel = nullptr;
            std::function<void(int)> setter;
        };

        const auto addOverlaySliderRow = [&](
            const QString& labelText,
            int value,
            const std::function<void(int)>& setter) -> OverlayRow {
            auto* slider = new QSlider(Qt::Horizontal, overlayGroup);
            miacode::ui::EditableValueLabel* valueLabel = nullptr;
            slider->setRange(miacode::ui::kAppBackgroundOverlayAlphaMin, miacode::ui::kAppBackgroundOverlayAlphaMax);
            slider->setSingleStep(1);
            slider->setPageStep(10);
            slider->setValue(value);
            auto* label = new QLabel(labelText, overlayGroup);
            overlayFormLayout->addRow(
                label,
                miacode::ui::createSliderValueRow(slider, &valueLabel, QString(), overlayGroup));
            return OverlayRow{label, slider, valueLabel, setter};
        };

        QList<OverlayRow> overlayRows;
        auto appendOverlayRow = [&](const QString& labelText, int value, const std::function<void(int)>& setter) {
            OverlayRow row = addOverlaySliderRow(labelText, value, setter);
            QSlider* rowSlider = row.slider;
            connect(row.slider, &QSlider::valueChanged, &overlayDialog, [&, setter, rowSlider](int sliderValue) {
                setter(sliderValue);
                persistBackgroundSettingsAfterSliderInput(rowSlider);
            });
            connect(row.slider, &QSlider::sliderReleased, &overlayDialog, flushBackgroundSliderSettings);
            overlayRows.append(row);
        };

        const bool showDarkThemeRows = UiTheme::isDarkTheme();
        const auto overlayRowLabel = [&](const QString& baseKey) {
            return UiText::text(
                showDarkThemeRows
                    ? QStringLiteral("dialog.preferences.background.overlay.dark")
                    : QStringLiteral("dialog.preferences.background.overlay.light"))
                .arg(UiText::text(baseKey));
        };
        const auto appendThemeOverlayRow = [&](const QString& baseKey,
                                               int darkValue,
                                               int lightValue,
                                               const std::function<void(int)>& darkSetter,
                                               const std::function<void(int)>& lightSetter) {
            appendOverlayRow(
                overlayRowLabel(baseKey),
                showDarkThemeRows ? darkValue : lightValue,
                showDarkThemeRows ? darkSetter : lightSetter);
        };

        appendThemeOverlayRow(
            QStringLiteral("dialog.preferences.background.overlay.toolbar"),
            selectedBackgroundSettings.overlays.toolbarAlphaDark,
            selectedBackgroundSettings.overlays.toolbarAlphaLight,
            [&](int value) { selectedBackgroundSettings.overlays.toolbarAlphaDark = value; },
            [&](int value) { selectedBackgroundSettings.overlays.toolbarAlphaLight = value; });
        appendThemeOverlayRow(
            QStringLiteral("dialog.preferences.background.overlay.editor_header"),
            selectedBackgroundSettings.overlays.editorHeaderAlphaDark,
            selectedBackgroundSettings.overlays.editorHeaderAlphaLight,
            [&](int value) { selectedBackgroundSettings.overlays.editorHeaderAlphaDark = value; },
            [&](int value) { selectedBackgroundSettings.overlays.editorHeaderAlphaLight = value; });
        appendThemeOverlayRow(
            QStringLiteral("dialog.preferences.background.overlay.code_editor"),
            selectedBackgroundSettings.overlays.codeEditorAlphaDark,
            selectedBackgroundSettings.overlays.codeEditorAlphaLight,
            [&](int value) { selectedBackgroundSettings.overlays.codeEditorAlphaDark = value; },
            [&](int value) { selectedBackgroundSettings.overlays.codeEditorAlphaLight = value; });
        appendThemeOverlayRow(
            QStringLiteral("dialog.preferences.background.overlay.panel"),
            selectedBackgroundSettings.overlays.panelAlphaDark,
            selectedBackgroundSettings.overlays.panelAlphaLight,
            [&](int value) { selectedBackgroundSettings.overlays.panelAlphaDark = value; },
            [&](int value) { selectedBackgroundSettings.overlays.panelAlphaLight = value; });
        if (!overlayRows.isEmpty()) {
            OverlayRow& previewFrameRow = overlayRows.last();
            if (previewFrameRow.label != nullptr) {
                previewFrameRow.label->setEnabled(false);
            }
            if (previewFrameRow.slider != nullptr) {
                previewFrameRow.slider->setEnabled(false);
            }
            if (previewFrameRow.valueLabel != nullptr) {
                previewFrameRow.valueLabel->setEnabled(false);
            }
        }
        appendThemeOverlayRow(
            QStringLiteral("dialog.preferences.background.overlay.status"),
            selectedBackgroundSettings.overlays.statusAlphaDark,
            selectedBackgroundSettings.overlays.statusAlphaLight,
            [&](int value) { selectedBackgroundSettings.overlays.statusAlphaDark = value; },
            [&](int value) { selectedBackgroundSettings.overlays.statusAlphaLight = value; });

        const auto syncOverlayDialogControls = [&]() {
            const miacode::ui::AppBackgroundOverlaySettings overlays = selectedBackgroundSettings.overlays;
            const QList<int> values{
                showDarkThemeRows ? overlays.toolbarAlphaDark : overlays.toolbarAlphaLight,
                showDarkThemeRows ? overlays.editorHeaderAlphaDark : overlays.editorHeaderAlphaLight,
                showDarkThemeRows ? overlays.codeEditorAlphaDark : overlays.codeEditorAlphaLight,
                showDarkThemeRows ? overlays.panelAlphaDark : overlays.panelAlphaLight,
                showDarkThemeRows ? overlays.statusAlphaDark : overlays.statusAlphaLight,
            };
            for (int index = 0; index < overlayRows.size() && index < values.size(); ++index) {
                if (overlayRows[index].slider == nullptr) {
                    continue;
                }
                QSignalBlocker blocker(overlayRows[index].slider);
                overlayRows[index].slider->setValue(values[index]);
                if (overlayRows[index].valueLabel != nullptr) {
                    overlayRows[index].valueLabel->setText(QString::number(values[index]));
                }
            }
        };

        overlayLayout->addWidget(overlayGroup);

        auto* buttonRow = new QDialogButtonBox(QDialogButtonBox::Close, &overlayDialog);
        if (QPushButton* closeButton = buttonRow->button(QDialogButtonBox::Close); closeButton != nullptr) {
            closeButton->setText(UiText::text(QStringLiteral("action.close")));
        }
        if (QPushButton* resetButton = buttonRow->addButton(
                UiText::text(QStringLiteral("dialog.preferences.background.overlay.reset_defaults")),
                QDialogButtonBox::ResetRole);
            resetButton != nullptr) {
            connect(resetButton, &QPushButton::clicked, &overlayDialog, [&]() {
                selectedBackgroundSettings.overlays = miacode::ui::AppBackgroundOverlaySettings{};
                syncOverlayDialogControls();
                persistBackgroundSettings();
            });
        }
        connect(buttonRow, &QDialogButtonBox::rejected, &overlayDialog, &QDialog::accept);
        overlayLayout->addWidget(buttonRow);
        overlayDialog.resize(560, 0);
        overlayDialog.exec();
    };

    auto* backgroundOverlayLabel =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.background.overlay")), backgroundGroup);
    auto* backgroundOverlayRow = new QWidget(backgroundGroup);
    auto* backgroundOverlayRowLayout = new QHBoxLayout(backgroundOverlayRow);
    backgroundOverlayRowLayout->setContentsMargins(0, 0, 0, 0);
    backgroundOverlayRowLayout->setSpacing(8);
    auto* backgroundOverlayButton = miacode::ui::createDialogPushButton(
        UiText::text(QStringLiteral("dialog.preferences.background.overlay_button")),
        backgroundOverlayRow);
    backgroundOverlayRowLayout->addWidget(backgroundOverlayButton, 0);
    backgroundOverlayRowLayout->addStretch(1);
    connect(backgroundOverlayButton, &QPushButton::clicked, &dialog, openBackgroundOverlayDialog);
    backgroundLayout->addRow(backgroundOverlayLabel, backgroundOverlayRow);

    auto* backgroundScaleCombo =
        miacode::ui::createDialogComboBox(backgroundGroup, 12, Qt::AlignLeft | Qt::AlignVCenter);
    const QList<miacode::ui::AppBackgroundSizeMode> backgroundScaleOptions{
        miacode::ui::AppBackgroundSizeMode::Cover,
        miacode::ui::AppBackgroundSizeMode::Contain,
        miacode::ui::AppBackgroundSizeMode::Stretch,
        miacode::ui::AppBackgroundSizeMode::Center,
        miacode::ui::AppBackgroundSizeMode::Repeat,
    };
    for (miacode::ui::AppBackgroundSizeMode mode : backgroundScaleOptions) {
        backgroundScaleCombo->addItem(backgroundModeLabel(mode), static_cast<int>(mode));
    }
    backgroundScaleCombo->setCurrentIndex(
        qMax(0, backgroundScaleCombo->findData(static_cast<int>(selectedBackgroundSettings.sizeMode))));
    styleRegisteredDialogCombo(backgroundScaleCombo, 12);
    connect(backgroundScaleCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&, backgroundScaleCombo](int index) {
        if (index < 0) {
            return;
        }
        selectedBackgroundSettings.sizeMode =
            static_cast<miacode::ui::AppBackgroundSizeMode>(backgroundScaleCombo->itemData(index).toInt());
        persistBackgroundSettings();
    });
    auto* backgroundPositionCombo =
        miacode::ui::createDialogComboBox(backgroundGroup, 12, Qt::AlignLeft | Qt::AlignVCenter);
    const QList<miacode::ui::AppBackgroundPosition> backgroundPositionOptions{
        miacode::ui::AppBackgroundPosition::Center,
        miacode::ui::AppBackgroundPosition::Left,
        miacode::ui::AppBackgroundPosition::Right,
        miacode::ui::AppBackgroundPosition::Top,
        miacode::ui::AppBackgroundPosition::Bottom,
        miacode::ui::AppBackgroundPosition::LeftTop,
        miacode::ui::AppBackgroundPosition::RightTop,
        miacode::ui::AppBackgroundPosition::LeftBottom,
        miacode::ui::AppBackgroundPosition::RightBottom,
    };
    for (miacode::ui::AppBackgroundPosition position : backgroundPositionOptions) {
        backgroundPositionCombo->addItem(backgroundPositionLabel(position), static_cast<int>(position));
    }
    backgroundPositionCombo->setCurrentIndex(
        qMax(0, backgroundPositionCombo->findData(static_cast<int>(selectedBackgroundSettings.position))));
    styleRegisteredDialogCombo(backgroundPositionCombo, 12);
    connect(backgroundPositionCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&, backgroundPositionCombo](int index) {
        if (index < 0) {
            return;
        }
        selectedBackgroundSettings.position =
            static_cast<miacode::ui::AppBackgroundPosition>(backgroundPositionCombo->itemData(index).toInt());
        persistBackgroundSettings();
    });

    const QList<QComboBox*> backgroundChoiceCombos{
        backgroundEnabledCombo,
        backgroundScaleCombo,
        backgroundPositionCombo,
    };
    int backgroundChoiceComboWidth = 0;
    for (QComboBox* combo : backgroundChoiceCombos) {
        if (combo == nullptr) {
            continue;
        }
        combo->ensurePolished();
        backgroundChoiceComboWidth = qMax(backgroundChoiceComboWidth, combo->sizeHint().width());
    }
    for (QComboBox* combo : backgroundChoiceCombos) {
        if (combo != nullptr && backgroundChoiceComboWidth > 0) {
            combo->setFixedWidth(backgroundChoiceComboWidth);
        }
    }

    auto* backgroundScaleLabel =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.background.scale")), backgroundGroup);
    backgroundLayout->addRow(backgroundScaleLabel,
                             createBackgroundComboRow(backgroundScaleCombo, backgroundGroup));

    auto* backgroundPositionLabelWidget =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.background.position")), backgroundGroup);
    backgroundLayout->addRow(backgroundPositionLabelWidget,
                             createBackgroundComboRow(backgroundPositionCombo, backgroundGroup));

    miacode::ui::flattenGroupForTabPage(backgroundGroup);
    backgroundPageLayout->addWidget(backgroundGroup);
    backgroundPageLayout->addStretch(1);
    pageStack->addTab(backgroundPage, UiText::text(QStringLiteral("dialog.preferences.background_group")));

    auto* editorPage = new QWidget(pageStack);
    auto* editorPageLayout = new QVBoxLayout(editorPage);
    editorPageLayout->setContentsMargins(0, 0, 0, 0);
    editorPageLayout->setSpacing(10);
    auto* editorGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.preferences.editor_group")), editorPage);
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

    // Row order (top to bottom): font size, line spacing, auto-completion,
    // header display, IME block, and ignore muri issue prompts.
    // out, so it trails the prioritised rows.
    auto* editorFontSizeLabel = new QLabel(UiText::text(QStringLiteral("dialog.preferences.editor_font_size")), editorGroup);
    auto* fontSizeRow = new QWidget(editorGroup);
    auto* fontSizeRowLayout = new QHBoxLayout(fontSizeRow);
    fontSizeRowLayout->setContentsMargins(0, 0, 0, 0);
    fontSizeRowLayout->setSpacing(8);
    auto* fontSizeControl = new QWidget(fontSizeRow);
    fontSizeControl->setObjectName(QStringLiteral("DialogSpinBoxFrame"));
    fontSizeControl->setStyleSheet(UiTheme::dialogSpinBoxStyleSheet());
    // Match the dialog combo/menu-button height (W1 max(sizeHint,30)+4 ≈ 34) so
    // 字号 sits level with the other editor rows; the frame's border-radius:8
    // needs the full height or the bottom border clips (why this was narrowed
    // to 25 before).
    fontSizeControl->setFixedHeight(34);
    fontSizeControl->setFixedWidth(80);
    auto* fontSizeControlLayout = new QHBoxLayout(fontSizeControl);
    fontSizeControlLayout->setContentsMargins(0, 0, 0, 0);
    fontSizeControlLayout->setSpacing(0);
    auto* editorFontSizeSpin = new QSpinBox(fontSizeControl);
    editorFontSizeSpin->setObjectName(QStringLiteral("DialogSpinBoxEditor"));
    editorFontSizeSpin->setRange(kEditorTextFontSizeMin, kEditorTextFontSizeMax);
    editorFontSizeSpin->setValue(selectedEditorFontSize);
    editorFontSizeSpin->setSuffix(" pt");
    editorFontSizeSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    editorFontSizeSpin->setFrame(false);
    editorFontSizeSpin->setFixedHeight(30);
    editorFontSizeSpin->setMinimumWidth(editorFontSizeSpin->fontMetrics().horizontalAdvance(QStringLiteral("28 pt")) + 28);
    const auto makeSpinArrowIcon = [](Qt::ArrowType arrowType) {
        const QColor arrowColor = UiTheme::colors().textSecondary;
        QPixmap pixmap(9, 17);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(arrowColor);
        QPolygon triangle;
        if (arrowType == Qt::UpArrow) {
            triangle << QPoint(4, 9) << QPoint(7, 13) << QPoint(1, 13);
        } else {
            triangle << QPoint(1, 3) << QPoint(7, 3) << QPoint(4, 7);
        }
        painter.drawPolygon(triangle);
        return QIcon(pixmap);
    };
    auto* stepButtons = new QWidget(fontSizeControl);
    stepButtons->setFixedSize(20, 34);
    auto* stepLayout = new QVBoxLayout(stepButtons);
    stepLayout->setContentsMargins(0, 0, 0, 0);
    stepLayout->setSpacing(0);
    auto* increaseFontSizeButton = new QToolButton(stepButtons);
    auto* decreaseFontSizeButton = new QToolButton(stepButtons);
    for (QToolButton* button : {increaseFontSizeButton, decreaseFontSizeButton}) {
        button->setObjectName(QStringLiteral("DialogSpinBoxStepButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRepeat(true);
        button->setAutoRepeatDelay(300);
        button->setAutoRepeatInterval(70);
        button->setFixedSize(20, 17);
        button->setIconSize(QSize(9, 17));
        stepLayout->addWidget(button, 0);
    }
    increaseFontSizeButton->setProperty("miacodeSpinStep", QStringLiteral("up"));
    decreaseFontSizeButton->setProperty("miacodeSpinStep", QStringLiteral("down"));
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
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.editor_text_display_updated")));
    });
    fontSizeRowLayout->addWidget(fontSizeControl, 0);
    fontSizeRowLayout->addWidget(shortcutHint, 0);
    fontSizeRowLayout->addStretch(1);
    editorLayout->addRow(editorFontSizeLabel, fontSizeRow);

    auto* lineSpacingLabel = new QLabel(UiText::text(QStringLiteral("dialog.preferences.editor_line_spacing")), editorGroup);
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
    styleRegisteredDialogCombo(lineSpacingCombo, 12);
    connect(lineSpacingCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) {
            return;
        }
        selectedEditorLineSpacingFactor = lineSpacingCombo->itemData(index).toDouble();
        owner_.applyEditorLineSpacingFactor(selectedEditorLineSpacingFactor, true);
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.editor_text_display_updated")));
    });
    editorLayout->addRow(lineSpacingLabel, lineSpacingCombo);

    // Auto-completion is one unified preference replacing the former three (auto-close
    // brackets / hold duration / bracket suggestions). It drives bracket
    // auto-close + type-over + empty-pair backspace, the bracket suggestion
    // popup, and the 'h' hold-duration suggestions together. Presented as a
    // On/Off menu (same idiom as the other editor rows) rather than a checkbox.
    auto* autoCompletionLabel = new QLabel(
        UiText::text(QStringLiteral("preferences.auto_completion")),
        editorGroup
    );
    auto* autoCompletionCombo = new QComboBox(editorGroup);
    autoCompletionCombo->addItem(UiText::text(QStringLiteral("preferences.on")));
    autoCompletionCombo->addItem(UiText::text(QStringLiteral("preferences.off")));
    autoCompletionCombo->setCurrentIndex(selectedAutoCompletionEnabled ? 0 : 1);
    styleRegisteredDialogCombo(autoCompletionCombo, 12);
    autoCompletionCombo->setToolTip(
        UiText::text(QStringLiteral("preferences.auto_closes_brackets_suggests_durations"))
    );
    connect(autoCompletionCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) {
            return;
        }
        selectedAutoCompletionEnabled = (index == 0);
        owner_.applyEditorAutoCompletionEnabled(selectedAutoCompletionEnabled, true);
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.editor_text_display_updated")));
    });
    editorLayout->addRow(autoCompletionLabel, autoCompletionCombo);

    // Header display controls what the difficulty-page header edits next to Lv:
    // the chart-wide offset (default) or the per-difficulty designer.
    const auto headerTopDisplayLabel = [](EditorHeaderTopDisplay mode) -> QString {
        return mode == EditorHeaderTopDisplay::Designer
            ? UiText::text(QStringLiteral("dialog.preferences.editor_top_display.designer"))
            : UiText::text(QStringLiteral("dialog.preferences.editor_top_display.latency"));
    };
    auto* headerTopDisplayLabelWidget =
        new QLabel(UiText::text(QStringLiteral("dialog.preferences.editor_top_display")), editorGroup);
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
    styleRegisteredDialogCombo(headerTopDisplayCombo, 12);
    headerTopDisplayCombo->setToolTip(
        UiText::text(QStringLiteral("preferences.the_field_next_to_lv"))
    );
    connect(headerTopDisplayCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&, headerTopDisplayCombo](int index) {
        if (index < 0) {
            return;
        }
        const auto mode =
            static_cast<EditorHeaderTopDisplay>(headerTopDisplayCombo->itemData(index).toInt());
        owner_.applyEditorHeaderTopDisplay(mode, true);
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
    });
    editorLayout->addRow(headerTopDisplayLabelWidget, headerTopDisplayCombo);

    auto* ignoreMuriIssuePromptsCheckbox = new QCheckBox(
        UiText::text(QStringLiteral("preferences.ignore_muri_issue_prompts")),
        editorGroup
    );
    ignoreMuriIssuePromptsCheckbox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ignoreMuriIssuePromptsCheckbox->setChecked(selectedIgnoreMuriIssuePrompts);
    ignoreMuriIssuePromptsCheckbox->setToolTip(
        UiText::text(QStringLiteral("preferences.hides_muri_from_the_editor"))
    );
    connect(ignoreMuriIssuePromptsCheckbox, &QCheckBox::toggled, &dialog, [&](bool checked) {
        selectedIgnoreMuriIssuePrompts = checked;
        owner_.applyIgnoreMuriIssuePrompts(selectedIgnoreMuriIssuePrompts, true);
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
    });
    // IME block combo merges the former "Lock half-width symbol input" checkbox
    // and "Disable IME input" checkbox into three graduated levels:
    //   index 0 block off              halfWidth=OFF imeDisabled=OFF (plain IME, no filtering)
    //   index 1 normalize full-width   halfWidth=ON  imeDisabled=OFF (default: normalize commits)
    //   index 2 block on               halfWidth=ON  imeDisabled=ON  (block IME candidate window)
    auto* chineseInputLabel = new QLabel(
        UiText::text(QStringLiteral("preferences.chinese_input")),
        editorGroup
    );
    const int initialChineseInputMode =
        state_.editorImeInputDisabled_ ? 2 :
        state_.editorHalfWidthInputEnabled_ ? 1 : 0;
    int selectedChineseInputMode = initialChineseInputMode;
    auto* chineseInputCombo = new QComboBox(editorGroup);
    chineseInputCombo->addItem(UiText::text(QStringLiteral("preferences.off")));
    chineseInputCombo->addItem(
        UiText::text(QStringLiteral("preferences.filter_full_width_chars")));
    chineseInputCombo->addItem(
        UiText::text(QStringLiteral("preferences.on")));
    chineseInputCombo->setCurrentIndex(selectedChineseInputMode);
    styleRegisteredDialogCombo(chineseInputCombo, 12);
    connect(chineseInputCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) {
            return;
        }
        selectedChineseInputMode = index;
        selectedEditorHalfWidthInputEnabled = (index >= 1);
        selectedEditorImeInputDisabled = (index >= 2);
        owner_.applyEditorHalfWidthInputEnabled(selectedEditorHalfWidthInputEnabled, false);
        owner_.applyEditorImeInputDisabled(selectedEditorImeInputDisabled, true);
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
    });
    editorLayout->addRow(chineseInputLabel, chineseInputCombo);

    // Ignore muri issue prompts sits below IME block (last row) per the 2026-06-19 review.
    editorLayout->addRow(QString(), ignoreMuriIssuePromptsCheckbox);


    // The preferences dialog font spin-box reuses the editor.font_* shortcut
    // IDs so a single binding controls both the editor and the dialog.
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
    miacode::ui::flattenGroupForTabPage(editorGroup);
    editorPageLayout->addWidget(editorGroup);
    editorPageLayout->addStretch(1);
    pageStack->addTab(editorPage, UiText::text(QStringLiteral("dialog.preferences.editor_group")));

    auto* performancePage = new QWidget(pageStack);
    auto* performancePageLayout = new QVBoxLayout(performancePage);
    performancePageLayout->setContentsMargins(0, 0, 0, 0);
    performancePageLayout->setSpacing(10);
    auto* performanceGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.preferences.performance_group")), performancePage);
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
        .arg(UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.display")))
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
            auto* combo = miacode::ui::createDialogComboBox(
                performanceGroup, 12, Qt::AlignLeft | Qt::AlignVCenter);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            for (const FrameRateOption& option : options) {
                combo->addItem(option.label, static_cast<int>(option.mode));
            }
            combo->setCurrentIndex(
                qMax(0, combo->findText(frameRateLabelForMode(selectedMode, options))));
            styleRegisteredDialogCombo(combo);
            connect(combo,
                    qOverload<int>(&QComboBox::currentIndexChanged),
                    &dialog,
                    [&, combo, applyMode](int index) {
                        if (index < 0) {
                            return;
                        }
                        applyMode(static_cast<PreviewCanvasFrameRateMode>(
                            combo->itemData(index).toInt()));
                        owner_.statusBar()->showMessage(
                            UiText::text(QStringLiteral("status.preferences_updated")));
                    });
            performanceLayout->addRow(label, combo);
        };

    QList<FrameRateOption> canvasFrameRateOptions;
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::Fps60,
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.60")),
    });
    if (detectedRefreshRate >= 119.5) {
        canvasFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps120,
            UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.120")),
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
            UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.30")),
        });
    }
    if (detectedRefreshRate >= 59.5) {
        appFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps60,
            UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.60")),
        });
    }
    if (detectedRefreshRate >= 119.5) {
        appFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps120,
            UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.120")),
        });
    }
    appFrameRateOptions.append({
        PreviewCanvasFrameRateMode::DisplayRefresh,
        displayRefreshLabel,
    });

    // Preview video decode: hardware (default) vs software.
    // (software). Placed first on the page per the user request. Two fixed
    // options, same QToolButton+QMenu visual pattern as the frame-rate rows
    // below. preferSoftware == false selects hardware.
    {
        struct VideoDecodeOption {
            bool preferSoftware;
            QString label;
        };
        const QList<VideoDecodeOption> videoDecodeOptions = {
            {false, UiText::text(QStringLiteral("dialog.preferences.performance.video_decode.hardware"))},
            {true, UiText::text(QStringLiteral("dialog.preferences.performance.video_decode.software"))},
        };
        auto* combo = miacode::ui::createDialogComboBox(
            performanceGroup, 12, Qt::AlignLeft | Qt::AlignVCenter);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        for (const VideoDecodeOption& option : videoDecodeOptions) {
            combo->addItem(option.label, option.preferSoftware);
        }
        combo->setCurrentIndex(owner_.currentVideoDecodePrefersSoftware() ? 1 : 0);
        styleRegisteredDialogCombo(combo);
        connect(combo,
                qOverload<int>(&QComboBox::currentIndexChanged),
                &dialog,
                [&, combo](int index) {
                    if (index < 0) {
                        return;
                    }
                    owner_.setVideoDecodePrefersSoftware(combo->itemData(index).toBool(), true);
                    owner_.statusBar()->showMessage(
                        UiText::text(QStringLiteral("status.preferences_updated")));
                });
        performanceLayout->addRow(
            UiText::text(QStringLiteral("dialog.preferences.performance.video_decode")),
            combo);
    }

    addFrameRateRow(
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate")),
        owner_.currentPreviewCanvasFrameRateMode(),
        canvasFrameRateOptions,
        [&](PreviewCanvasFrameRateMode mode) {
            owner_.setPreviewCanvasFrameRateMode(mode, true);
        }
    );
    addFrameRateRow(
        UiText::text(QStringLiteral("dialog.preferences.performance.pv_frame_rate")),
        owner_.currentPreviewStageMediaFrameRateMode(),
        appFrameRateOptions,
        [&](PreviewCanvasFrameRateMode mode) {
            owner_.setPreviewStageMediaFrameRateMode(mode, true);
        }
    );
    addFrameRateRow(
        UiText::text(QStringLiteral("dialog.preferences.performance.timeline_frame_rate")),
        owner_.currentTimelineFrameRateMode(),
        appFrameRateOptions,
        [&](PreviewCanvasFrameRateMode mode) {
            owner_.setTimelineFrameRateMode(mode, true);
        }
    );

    miacode::ui::flattenGroupForTabPage(performanceGroup);
    performancePageLayout->addWidget(performanceGroup);
    performancePageLayout->addStretch(1);
    pageStack->addTab(performancePage, UiText::text(QStringLiteral("dialog.preferences.performance_group")));

    auto* shortcutsPage = new QWidget(pageStack);
    auto* shortcutsPageLayout = new QVBoxLayout(shortcutsPage);
    shortcutsPageLayout->setContentsMargins(0, 0, 0, 0);
    shortcutsPageLayout->setSpacing(10);
    auto* shortcutsGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.preferences.shortcuts_group")), shortcutsPage);
    auto* shortcutsLayout = new QVBoxLayout(shortcutsGroup);
    shortcutsLayout->setContentsMargins(12, 10, 12, 12);
    shortcutsLayout->setSpacing(8);
    auto* editShortcutsButton = new QPushButton(UiText::text(QStringLiteral("dialog.preferences.shortcuts.edit")), shortcutsGroup);
    auto* resetShortcutsButton = new QPushButton(UiText::text(QStringLiteral("dialog.preferences.shortcuts.reset")), shortcutsGroup);
    editShortcutsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    resetShortcutsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    shortcutsLayout->addWidget(editShortcutsButton, 0, Qt::AlignLeft);
    shortcutsLayout->addWidget(resetShortcutsButton, 0, Qt::AlignLeft);
    // The Alt-hold pause-display behavior used to be a fixed hint label here;
    // it is now the editable preview.pause_display_hold entry in the table
    // (the capture edit accepts bare modifiers for hold-type shortcuts).
    miacode::ui::flattenGroupForTabPage(shortcutsGroup);
    shortcutsPageLayout->addWidget(shortcutsGroup);
    shortcutsPageLayout->addStretch(1);
    pageStack->addTab(shortcutsPage, UiText::text(QStringLiteral("dialog.preferences.shortcuts_group")));

    auto* extensionsPage = new QWidget(pageStack);
    auto* extensionsPageLayout = new QHBoxLayout(extensionsPage);
    extensionsPageLayout->setContentsMargins(0, 0, 0, 0);
    extensionsPageLayout->setSpacing(10);
    auto* extensionsActions = new QWidget(extensionsPage);
    auto* extensionsActionsLayout = new QVBoxLayout(extensionsActions);
    extensionsActionsLayout->setContentsMargins(12, 10, 8, 12);
    extensionsActionsLayout->setSpacing(8);
    auto* openExtensionsFolderButton =
        new QPushButton(UiText::text(QStringLiteral("dialog.preferences.extensions.open_folder")), extensionsActions);
    auto* refreshExtensionsButton =
        new QPushButton(UiText::text(QStringLiteral("dialog.preferences.extensions.refresh")), extensionsActions);
    auto* openExtensionLogsButton =
        new QPushButton(UiText::text(QStringLiteral("dialog.preferences.extensions.open_logs")), extensionsActions);
    auto* openExtensionDevToolsButton =
        new QPushButton(UiText::text(QStringLiteral("dialog.preferences.extensions.devtools")), extensionsActions);
    openExtensionsFolderButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    refreshExtensionsButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    openExtensionLogsButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    openExtensionDevToolsButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    extensionsActionsLayout->addWidget(openExtensionsFolderButton);
    extensionsActionsLayout->addWidget(refreshExtensionsButton);
    extensionsActionsLayout->addWidget(openExtensionLogsButton);
    extensionsActionsLayout->addWidget(openExtensionDevToolsButton);
    extensionsActionsLayout->addStretch(1);

    auto* extensionsTable = new QTableWidget(extensionsPage);
    extensionsTable->setColumnCount(4);
    extensionsTable->setHorizontalHeaderLabels({
        UiText::text(QStringLiteral("dialog.preferences.extensions.enabled")),
        UiText::text(QStringLiteral("dialog.preferences.extensions.name")),
        UiText::text(QStringLiteral("dialog.preferences.extensions.version")),
        UiText::text(QStringLiteral("dialog.preferences.extensions.status")),
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
    extensionsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    extensionsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    extensionsTable->setColumnWidth(2, 112);
    extensionsTable->setColumnWidth(3, 136);
    extensionsTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    extensionsTable->setMinimumSize(0, 280);
    extensionsTable->setStyleSheet(QStringLiteral(
        "QTableWidget { border: 1px solid rgba(128,128,128,72); border-radius: 6px; }"
        "QTableWidget::item:selected { background: rgba(88, 145, 220, 58); }"
        "QHeaderView::section { padding: 6px 9px; border: 0; border-bottom: 1px solid rgba(128,128,128,72); font-weight: 600; }"
    ));
    extensionsTable->setMouseTracking(true);
    extensionsTable->viewport()->setMouseTracking(true);
    auto* extensionsToolTipFilter = new DelayedTableToolTipFilter(extensionsTable, 1000, extensionsTable);
    extensionsTable->viewport()->installEventFilter(extensionsToolTipFilter);

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
            const QString enabledToolTip = record.valid
                ? (record.enabled
                    ? (UiText::isChineseUi()
                        ? QStringLiteral("当前扩展已启用。取消勾选后，该扩展贡献的命令、菜单、语言、诊断等内容都会消失。")
                        : QStringLiteral("This extension is enabled. Uncheck it to remove its commands, menus, languages, diagnostics, and other contributions."))
                    : (UiText::isChineseUi()
                        ? QStringLiteral("当前扩展已禁用。勾选后，该扩展贡献的命令、菜单、语言、诊断等内容会重新加载。")
                        : QStringLiteral("This extension is disabled. Check it to reload its commands, menus, languages, diagnostics, and other contributions.")))
                : (UiText::isChineseUi()
                    ? QStringLiteral("此扩展 manifest 无效，修复错误后才能启用。")
                    : QStringLiteral("This extension manifest is invalid. Fix the error before enabling it."));
            auto* enabledBox = new QCheckBox(extensionsTable);
            enabledBox->setChecked(record.valid && record.enabled);
            enabledBox->setEnabled(record.valid);
            enabledBox->setToolTip(enabledToolTip);
            auto* enabledCell = new QWidget(extensionsTable);
            enabledCell->setToolTip(enabledToolTip);
            auto* enabledCellLayout = new QHBoxLayout(enabledCell);
            enabledCellLayout->setContentsMargins(8, 0, 8, 0);
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
            const QString status = !record.valid
                ? (UiText::isChineseUi()
                    ? QStringLiteral("无效：%1").arg(record.diagnostic)
                    : QStringLiteral("Invalid: %1").arg(record.diagnostic))
                : (record.enabled
                    ? (UiText::isChineseUi() ? QStringLiteral("已启用") : QStringLiteral("Enabled"))
                    : (UiText::isChineseUi() ? QStringLiteral("已禁用") : QStringLiteral("Disabled")));
            auto* statusItem = new QTableWidgetItem(status);
            statusItem->setToolTip(record.diagnostic.trimmed().isEmpty() ? status : record.diagnostic);
            extensionsTable->setItem(row, 3, statusItem);
            if (record.valid) {
                QObject::connect(enabledBox, &QCheckBox::clicked, &dialog, [&, box = QPointer<QCheckBox>(enabledBox), id = record.manifest.qualifiedId()](bool enabled) {
                    if (owner_.extensionManager_ == nullptr) {
                        return;
                    }
                    QToolTip::hideText();
                    if (box != nullptr) {
                        box->setEnabled(false);
                    }
                    QTimer::singleShot(0, &dialog, [&, id, enabled]() {
                        if (owner_.extensionManager_ == nullptr) {
                            return;
                        }
                        owner_.extensionManager_->setExtensionEnabled(id, enabled);
                        rebuildLanguageCombo();
                        refreshExtensionRows();
                        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
                    });
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
        rebuildLanguageCombo();
        refreshExtensionRows();
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
    });
    QObject::connect(openExtensionLogsButton, &QPushButton::clicked, &dialog, [&]() {
        const QString dir = owner_.extensionManager_ != nullptr
            ? owner_.extensionManager_->extensionLogDirectory()
            : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    QObject::connect(openExtensionDevToolsButton, &QPushButton::clicked, &dialog, [&]() {
        owner_.showExtensionDevToolsDialog();
    });
    refreshExtensionRows();
    extensionsPageLayout->addWidget(extensionsActions, 0);
    extensionsPageLayout->addWidget(extensionsTable, 1);
    pageStack->addTab(extensionsPage, UiText::text(QStringLiteral("dialog.preferences.extensions_group")));

    const auto openShortcutEditDialog = [&]() {
        miacode::ui::TabbedSettingsDialog shortcutsDialog(
            &dialog,
            UiText::text(QStringLiteral("dialog.preferences.shortcuts.title")),
            miacode::ui::SettingsDialogChrome::Preferences);
        shortcutsDialog.resize(740, 500);
        shortcutsDialog.setMinimumSize(680, 440);

        QVBoxLayout* shortcutRootLayout = shortcutsDialog.contentLayout();
        shortcutRootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
        shortcutRootLayout->setContentsMargins(10, 10, 10, 10);
        shortcutRootLayout->setSpacing(6);
        auto* table = new ShortcutTableWidget(&shortcutsDialog);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels({
            UiText::text(QStringLiteral("dialog.preferences.shortcuts.command")),
            UiText::text(QStringLiteral("dialog.preferences.shortcuts.keybinding")),
        });
        table->verticalHeader()->hide();
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
        // Highlight the whole row when a shortcut is clicked; the previous
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
        // the selection background; the inset comes from the delegate.
        table->setStyleSheet(QStringLiteral(
            "QTableWidget { border: 1px solid rgba(128,128,128,72); border-radius: 6px; }"
            "QTableWidget::item:selected { background: rgba(88, 145, 220, 58); }"
            "QHeaderView::section { padding: 6px 9px; border: 0; border-bottom: 1px solid rgba(128,128,128,72); font-weight: 600; }"
        ));
        // Content rows get a generous +/-28 px inset; category section
        // headers use the smaller +/-14 px so the section title sits closer
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
            // opaque so it overrides QTableWidget's alternating-row brush.
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
                    keybindingItem->setToolTip(UiText::text(QStringLiteral("dialog.preferences.shortcuts.change")));
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
            miacode::ui::TabbedSettingsDialog captureDialog(
                &shortcutsDialog,
                UiText::text(QStringLiteral("dialog.preferences.shortcuts.capture_title")),
                miacode::ui::SettingsDialogChrome::Preferences);
            captureDialog.resize(600, 270);
            captureDialog.setMinimumSize(520, 240);
            QVBoxLayout* captureLayout = captureDialog.contentLayout();
            captureLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
            captureLayout->setContentsMargins(24, 22, 24, 18);
            captureLayout->setSpacing(12);
            // Hold-type shortcuts may bind a bare modifier (Alt alone), which
            // the regular combo capture deliberately swallows.
            const bool allowBareModifier = (id == QStringLiteral("preview.pause_display_hold"));
            auto* prompt = new QLabel(
                allowBareModifier
                    ? UiText::text(QStringLiteral("dialog.preferences.shortcuts.capture_prompt_hold"))
                    : UiText::text(QStringLiteral("dialog.preferences.shortcuts.capture_prompt")),
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
            captureLayout->addWidget(prompt);
            captureLayout->addWidget(captureEdit);
            captureLayout->addWidget(previewLabel);
            captureLayout->addWidget(conflictLabel);
            captureLayout->addStretch(1);
            auto* captureButtons = captureDialog.buttonBox();
            captureButtons->setStandardButtons(
                QDialogButtonBox::Ok | QDialogButtonBox::Reset | QDialogButtonBox::Cancel);
            UiDialogs::localizeButtonBox(captureButtons);
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
                    UiText::text(QStringLiteral("preferences.conflicts_with_1")).arg(conflict));
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
                    owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
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
                        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
                    }
                    captureDialog.accept();
                });
            }
            captureEdit->setFocus(Qt::OtherFocusReason);
            captureDialog.exec();
        };

        QObject::connect(table, &QTableWidget::cellClicked, &shortcutsDialog, [&](int row, int column) {
            // Category header rows span both columns and aren't editable.
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

        auto* closeBox = shortcutsDialog.buttonBox();
        shortcutsDialog.addCloseButton(UiDialogs::text("action.close", "Close"), false);
        shortcutRootLayout->setAlignment(closeBox, Qt::AlignRight);
        shortcutsDialog.exec();
    };

    connect(editShortcutsButton, &QPushButton::clicked, &dialog, openShortcutEditDialog);
    connect(resetShortcutsButton, &QPushButton::clicked, &dialog, [&]() {
        const int choice = QMessageBox::question(
            &dialog,
            UiText::text(QStringLiteral("dialog.preferences.shortcuts.reset_confirm_title")),
            UiText::text(QStringLiteral("dialog.preferences.shortcuts.reset_confirm_message")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return;
        }
        if (ShortcutRegistry::instance().resetEditableShortcuts()) {
            applyConfiguredShortcuts();
            shortcutHint->setText(fontShortcutHintText());
            // beta51+ overwriteModeShortcutHint label was bound to the
            // former overwrite-mode preference row, which was removed in
            // favour of the "Disable IME input" toggle. The Insert key
            // binding still lives in the shortcuts registry under
            // `editor.overwrite_mode`; reset still affects it, just no
            // dialog label needs refreshing now.
            owner_.statusBar()->showMessage(UiText::text(QStringLiteral("status.preferences_updated")));
        }
    });

    pageStack->setCurrentIndex(0);

    const miacode::ui::TabWidgetWidthMetrics tabWidthMetrics =
        miacode::ui::pinTabWidgetToContentWidth(pageStack, &dialog, rootLayout);
    // --debug breadcrumb: if the dialog is still wide, this prints the real
    // per-page hint so the over-reporting page can be pinned without guessing.
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preferences/width"),
        QStringLiteral("maxPage=%1 tabBar=%2 chrome=%3 pinned=%4 dialogW=%5")
            .arg(tabWidthMetrics.maxPageWidth)
            .arg(tabWidthMetrics.tabBarWidth)
            .arg(tabWidthMetrics.tabChrome)
            .arg(tabWidthMetrics.pinnedWidth)
            .arg(dialog.width()),
        true);

    auto* buttonBox = dialog.buttonBox();
    dialog.addCloseButton(UiDialogs::text("action.close", "Close"), false);
    rootLayout->setAlignment(buttonBox, Qt::AlignRight);

    dialog.exec();
}

void MainWindow::onPreferences()
{
    preferencesSection_->onPreferences();
}
