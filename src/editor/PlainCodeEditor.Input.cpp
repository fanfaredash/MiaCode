#include "PlainCodeEditor.h"
#include "BracketCompletionPopup.h"
#include "SimaiCompletionCatalog.h"
#include "SimaiTextEditPolicy.h"
#include "common/DebugLog.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

namespace miacode::editor {
QChar normalizedHalfWidthChar(QChar ch)
{
    const ushort code = ch.unicode();
    if (code == 0x3000) {
        return QLatin1Char(' ');
    }
    if (code >= 0xFF01 && code <= 0xFF5E) {
        return QChar(code - 0xFEE0);
    }
    switch (code) {
    case 0x3001:
        return QLatin1Char('/');
    case 0x3002:
        return QLatin1Char('.');
    case 0x00B7:
        return QLatin1Char('`');
    case 0x300A:
        return QLatin1Char('<');
    case 0x300B:
        return QLatin1Char('>');
    case 0x3010:
        return QLatin1Char('[');
    case 0x3011:
        return QLatin1Char(']');
    case 0xFFE5:
        return QLatin1Char('$');
    default:
        return ch;
    }
}

QString normalizedHalfWidthText(QString text)
{
    if (text == QStringLiteral("……") || text == QStringLiteral("…")) {
        return QStringLiteral("^");
    }
    for (int i = 0; i < text.size(); ++i) {
        text[i] = normalizedHalfWidthChar(text.at(i));
    }
    return text;
}

QString normalizedHalfWidthKeyText(const QKeyEvent* event, const QString& text)
{
    if (event != nullptr
        && (event->modifiers() & Qt::ShiftModifier)
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (event->key() == Qt::Key_6) {
            return QStringLiteral("^");
        }
        if (event->key() == Qt::Key_4) {
            return QStringLiteral("$");
        }
    }
    return normalizedHalfWidthText(text);
}


bool completionPopupContainsPointer(const QRect& popupBounds, const QPoint& globalPointerPosition)
{
    return popupBounds.contains(globalPointerPosition);
}
}

namespace {
void logSelectionRestoreEditorShortcut(const QString& scope, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("selection_restore/%1").arg(scope),
        payload,
        true
    );
}

QKeySequence keySequenceForEvent(const QKeyEvent* event)
{
    if (event == nullptr) {
        return QKeySequence();
    }
    return QKeySequence(
        (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        | event->key());
}

bool matchesShortcutId(const QKeyEvent* event, const QString& id, const QList<QKeySequence>& fallback)
{
    if (event == nullptr) {
        return false;
    }
    const QKeySequence pressed = keySequenceForEvent(event);
    const QList<QKeySequence> sequences = ShortcutRegistry::instance().sequences(id, fallback);
    for (const QKeySequence& sequence : sequences) {
        if (!sequence.isEmpty() && pressed == sequence) {
            return true;
        }
        const QString portable = sequence.toString(QKeySequence::PortableText);
        if (portable == QStringLiteral("Ctrl+Shift+=")
            && (pressed == QKeySequence(QStringLiteral("Ctrl++"))
                || pressed == QKeySequence(QStringLiteral("Ctrl+Shift++")))) {
            return true;
        }
        if (portable == QStringLiteral("Ctrl+Shift+-")
            && (pressed == QKeySequence(QStringLiteral("Ctrl+_"))
                || pressed == QKeySequence(QStringLiteral("Ctrl+Shift+_")))) {
            return true;
        }
    }
    return false;
}

bool shouldRecordSelectionReplacementUndo(const QKeyEvent* event)
{
    if (event == nullptr) {
        return false;
    }
    if (event->matches(QKeySequence::Undo)
        || event->matches(QKeySequence::Redo)
        || event->matches(QKeySequence::Copy)
        || event->matches(QKeySequence::SelectAll)) {
        return false;
    }
    if (event->matches(QKeySequence::Cut)) {
        return true;
    }
    if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
        return !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        return !(event->modifiers() & (Qt::AltModifier | Qt::MetaModifier));
    }
    return !event->text().isEmpty()
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
}

void insertLineBreakAtCursor(QTextEdit* editor)
{
    if (editor == nullptr || editor->isReadOnly()) {
        return;
    }
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.insertBlock(cursor.blockFormat(), cursor.charFormat());
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
}
}  // namespace

bool PlainCodeEditor::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent == nullptr) {
            return QTextEdit::event(event);
        }
        if (matchesShortcutId(
                keyEvent,
                QStringLiteral("transform.clear_complete_elements"),
                {QKeySequence(Qt::CTRL | Qt::Key_Q)})
            || matchesShortcutId(
                keyEvent,
                QStringLiteral("transform.subdivision_half_up"),
                {QKeySequence(QStringLiteral("Ctrl+Shift+=")), QKeySequence(QStringLiteral("Ctrl++"))})
            || matchesShortcutId(
                keyEvent,
                QStringLiteral("transform.subdivision_half_down"),
                {QKeySequence(QStringLiteral("Ctrl+Shift+-"))})) {
            event->accept();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Undo) || keyEvent->matches(QKeySequence::Redo)) {
            logSelectionRestoreEditorShortcut(
                QStringLiteral("editor_shortcut_override"),
                QStringLiteral("match=%1 key=%2 modifiers=%3 focus=%4")
                    .arg(keyEvent->matches(QKeySequence::Undo) ? QStringLiteral("undo") : QStringLiteral("redo"))
                    .arg(keyEvent->key())
                    .arg(static_cast<int>(keyEvent->modifiers()))
                    .arg(hasFocus() ? 1 : 0)
            );
            event->accept();
            return true;
        }
    }
    return QTextEdit::event(event);
}

void PlainCodeEditor::changeEvent(QEvent* event)
{
    QTextEdit::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::FontChange) {
        refreshLineNumberAreaLayout();
    }
}

void PlainCodeEditor::focusInEvent(QFocusEvent* event)
{
    QTextEdit::focusInEvent(event);
    // Re-apply IME disable after the base-class focus handler, which may
    // re-assert WA_InputMethodEnabled based on editability. Our
    // inputMethodQuery(Qt::ImEnabled) override already returns false, but
    // we also need to notify the platform so ImmAssociateContext fires now
    // rather than waiting for the next IME probe.
    if (imeInputDisabled_) {
        setAttribute(Qt::WA_InputMethodEnabled, false);
        QGuiApplication::inputMethod()->update(Qt::ImEnabled);
    }
}

void PlainCodeEditor::focusOutEvent(QFocusEvent* event)
{
    // The popup never takes focus, so losing focus means the user clicked or
    // tabbed away — dismiss any open suggestion list.
    const bool pointerInsideCompletionPopup =
        completionPopup_ != nullptr
        && completionPopup_->isVisible()
        && miacode::editor::completionPopupContainsPointer(
            completionPopup_->geometry(), completionPopupPointerPosition());
    // A non-pointer focus change is emitted by some platforms while the Tool
    // popup is first mapped. Do not mistake that native transition for the user
    // clicking away; mouse and tab focus changes remain dismissals.
    const bool userMovedFocus = event != nullptr
        && (event->reason() == Qt::MouseFocusReason || event->reason() == Qt::TabFocusReason
            || event->reason() == Qt::BacktabFocusReason);
    if (!pointerInsideCompletionPopup && userMovedFocus) {
        closeBracketCompletion();
    }
    QTextEdit::focusOutEvent(event);
}

QPoint PlainCodeEditor::completionPopupPointerPosition() const
{
    return QCursor::pos();
}

void PlainCodeEditor::contextMenuEvent(QContextMenuEvent* event)
{
    if (event == nullptr) {
        return;
    }
    auto* menu = new QMenu(this);
    UiTheme::styleRoundedMenu(*menu);
    const UiTheme::Colors& c = UiTheme::colors();
    const QColor contextMenuBg = c.dark ? QColor(QStringLiteral("#2B3543")) : QColor(QStringLiteral("#FFFFFF"));
    const QColor contextMenuBorder = c.dark ? QColor(QStringLiteral("#667A91")) : c.menuBorder;
    const QColor contextMenuHover = c.dark ? QColor(QStringLiteral("#3D4A5C")) : c.menuHoverBg;
    const QColor contextMenuSeparator = c.dark ? QColor(QStringLiteral("#73859A")) : c.border;
    menu->setStyleSheet(
        QStringLiteral(
            "QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 7px; }"
            "QMenu::item { padding: 4px 12px; margin: 1px 4px; min-height: 20px; border-radius: 6px; color: %3; background: transparent; }"
            "QMenu::item:selected { background: %4; color: %3; }"
            "QMenu::item:disabled { color: %5; background: transparent; }"
            "QMenu::separator { height: 1px; margin: 7px 6px; background: %6; }"
        )
            .arg(contextMenuBg.name(QColor::HexRgb))
            .arg(contextMenuBorder.name(QColor::HexRgb))
            .arg(c.textPrimary.name(QColor::HexRgb))
            .arg(contextMenuHover.name(QColor::HexRgb))
            .arg(c.menuDisabledText.name(QColor::HexRgb))
            .arg(contextMenuSeparator.name(QColor::HexRgb))
    );

    const auto translated = [](const QString& key, const QString& fallback) {
        const QString value = UiText::text(key);
        return value.isEmpty() ? fallback : value;
    };

    auto* cutAction = menu->addAction(translated(QStringLiteral("action.cut"), QStringLiteral("Cut")));
    ShortcutRegistry::instance().applyShortcut(
        cutAction,
        QStringLiteral("edit.cut"),
        QKeySequence::Cut);
    cutAction->setShortcutVisibleInContextMenu(true);
    cutAction->setEnabled(textCursor().hasSelection());
    connect(cutAction, &QAction::triggered, this, [this]() {
        const QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) {
            emit selectionReplacementAboutToEdit(cursor.anchor(), cursor.position());
        }
        cut();
    });

    auto* copyAction = menu->addAction(translated(QStringLiteral("action.copy"), QStringLiteral("Copy")));
    ShortcutRegistry::instance().applyShortcut(
        copyAction,
        QStringLiteral("edit.copy"),
        QKeySequence::Copy);
    copyAction->setShortcutVisibleInContextMenu(true);
    copyAction->setEnabled(textCursor().hasSelection());
    connect(copyAction, &QAction::triggered, this, &QTextEdit::copy);

    auto* pasteAction = menu->addAction(translated(QStringLiteral("action.paste"), QStringLiteral("Paste")));
    ShortcutRegistry::instance().applyShortcut(
        pasteAction,
        QStringLiteral("edit.paste"),
        QKeySequence::Paste);
    pasteAction->setShortcutVisibleInContextMenu(true);
    pasteAction->setEnabled(canPaste());
    connect(pasteAction, &QAction::triggered, this, &QTextEdit::paste);

    const auto addStyledSubmenu = [&](const QString& title, const QList<QAction*>& actions) {
        auto* submenu = menu->addMenu(title);
        UiTheme::styleRoundedMenu(*submenu);
        submenu->setStyleSheet(
            QStringLiteral(
                "QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 7px; }"
                "QMenu::item { padding: 4px 12px; margin: 1px 4px; min-height: 20px; border-radius: 6px; color: %3; background: transparent; }"
                "QMenu::item:selected { background: %4; color: %3; }"
                "QMenu::item:disabled { color: %5; background: transparent; }"
                "QMenu::separator { height: 1px; margin: 7px 6px; background: %6; }"
            )
                .arg(contextMenuBg.name(QColor::HexRgb))
                .arg(contextMenuBorder.name(QColor::HexRgb))
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(contextMenuHover.name(QColor::HexRgb))
                .arg(c.menuDisabledText.name(QColor::HexRgb))
                .arg(contextMenuSeparator.name(QColor::HexRgb))
        );
        for (QAction* action : actions) {
            if (action == nullptr) {
                continue;
            }
            submenu->addAction(action);
        }
    };

    if (!batchTransformActions_.isEmpty() || !moreBatchTransformActions_.isEmpty()) {
        menu->addSeparator();
        // Surface the primary batch transforms inline so the right-click menu matches the top
        // "修改" menu one-for-one (镜像/旋转, then 分音, then 清空); embedded separator actions carry
        // the same group splits across. Only the less-frequent toggles stay under "更多".
        for (QAction* action : batchTransformActions_) {
            if (action == nullptr) {
                continue;
            }
            menu->addAction(action);
        }
        if (!moreBatchTransformActions_.isEmpty()) {
            if (!batchTransformActions_.isEmpty()) {
                menu->addSeparator();
            }
            addStyledSubmenu(
                translated(QStringLiteral("context.more_transform"), QStringLiteral("More...")),
                moreBatchTransformActions_
            );
        }
    }

    menu->exec(event->globalPos());
    delete menu;
}

void PlainCodeEditor::inputMethodEvent(QInputMethodEvent* event)
{
    if (event == nullptr) {
        QTextEdit::inputMethodEvent(event);
        return;
    }
    if (isReadOnly()) {
        event->ignore();
        return;
    }
    if (!halfWidthInputEnabled_) {
        QTextEdit::inputMethodEvent(event);
        return;
    }

    if (event->preeditString().isEmpty() && event->replacementLength() == 0
        && !event->commitString().isEmpty()) {
        const QTextCursor cursor = textCursor();
        miacode::editor::SimaiTextEditRequest request;
        request.text = toPlainText();
        request.anchor = cursor.anchor();
        request.position = cursor.position();
        request.input = event->commitString();
        request.isImeCommit = true;
        request.halfWidthInputEnabled = halfWidthInputEnabled_;
        request.overwriteMode = overwriteMode();
        request.autoCompletionEnabled = autoCompletionEnabled_;
        request.wholeBpm = wholeBpmCandidate_;
        request.completionActive = bracketCompletionActive();
        if (applySimaiTextEditPolicy(request)) {
            event->accept();
            return;
        }
    }
    QTextEdit::inputMethodEvent(event);
}

void PlainCodeEditor::insertFromMimeData(const QMimeData* source)
{
    if (source == nullptr) {
        return;
    }

    const QString text = source->text();
    if (text.isEmpty()) {
        QTextEdit::insertFromMimeData(source);
        return;
    }

    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        emit selectionReplacementAboutToEdit(cursor.anchor(), cursor.position());
    }
    const int selectionStart = cursor.selectionStart();
    cursor.beginEditBlock();
    cursor.insertText(text);

    QTextCursor blockCursor(document());
    blockCursor.setPosition(selectionStart);
    blockCursor.setPosition(cursor.position(), QTextCursor::KeepAnchor);
    QTextBlockFormat fmt;
    fmt.setBottomMargin(static_cast<qreal>(blockSpacingPixels_));
    blockCursor.mergeBlockFormat(fmt);
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void PlainCodeEditor::keyPressEvent(QKeyEvent* event)
{
    if (event == nullptr) {
        QTextEdit::keyPressEvent(event);
        return;
    }
    if (isReadOnly()) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    // While the completion popup is open it owns the navigation / accept /
    // dismiss keys (↑ ↓ Tab Enter Esc). Printable characters and Backspace
    // fall through to normal editing; the cursorPositionChanged slot then
    // re-filters (or dismisses) the list. This must run before the line-break
    // handling below so a popup-Enter commits instead of inserting a newline.
    if (bracketCompletionActive() && handleCompletionPopupKey(event)) {
        return;
    }

    const QTextCursor selectionBeforeEdit = textCursor();
    const bool recordSelectionReplacement =
        selectionBeforeEdit.hasSelection() && shouldRecordSelectionReplacementUndo(event);
    const auto emitSelectionReplacementIfNeeded = [this, recordSelectionReplacement, selectionBeforeEdit]() {
        if (recordSelectionReplacement) {
            emit selectionReplacementAboutToEdit(selectionBeforeEdit.anchor(), selectionBeforeEdit.position());
        }
    };
    emitSelectionReplacementIfNeeded();

    // The default plain Insert binding deliberately avoids Shift+Insert
    // paste and Ctrl+Insert copy. Custom bindings are matched through
    // the shortcut registry. The Preferences UI no longer exposes a
    // checkbox for this (beta59) — Insert key + persistence stay so the
    // user can still toggle overwrite mode on demand.
    const QKeySequence overwriteModeSequence =
        ShortcutRegistry::instance().sequence(
            QStringLiteral("editor.overwrite_mode"),
            QKeySequence(Qt::Key_Insert));
    const QKeySequence pressedSequence = keySequenceForEvent(event);
    if (matchesShortcutId(
            event,
            QStringLiteral("transform.clear_complete_elements"),
            {QKeySequence(Qt::CTRL | Qt::Key_Q)})) {
        emit clearCompleteElementsShortcutRequested();
        event->accept();
        return;
    }
    if (matchesShortcutId(
            event,
            QStringLiteral("transform.subdivision_half_up"),
            {QKeySequence(QStringLiteral("Ctrl+Shift+=")), QKeySequence(QStringLiteral("Ctrl++"))})) {
        emit raiseSubdivisionHalfStepShortcutRequested();
        event->accept();
        return;
    }
    if (matchesShortcutId(
            event,
            QStringLiteral("transform.subdivision_half_down"),
            {QKeySequence(QStringLiteral("Ctrl+Shift+-")), QKeySequence(QStringLiteral("Ctrl+_"))})) {
        emit lowerSubdivisionHalfStepShortcutRequested();
        event->accept();
        return;
    }
    const bool overwriteModeKey =
        !overwriteModeSequence.isEmpty()
        && pressedSequence == overwriteModeSequence;
    if (overwriteModeKey) {
        if (!event->isAutoRepeat()) {
            setEditorOverwriteMode(!overwriteMode());
        }
        event->accept();
        return;
    }

    // Undo/Redo are handled on auto-repeat too, so holding Ctrl+Z / Ctrl+Y keeps
    // stepping through the history like every other text editor. (Earlier this was
    // gated behind !isAutoRepeat(), which is correct for the one-shot overwrite-mode
    // toggle above but wrongly suppressed repeated undo/redo.) The emit routes through
    // MainWindow's selection-restore-aware undo/redo actions.
    if (event->matches(QKeySequence::Undo)) {
        logSelectionRestoreEditorShortcut(
            QStringLiteral("editor_shortcut_press"),
            QStringLiteral("match=undo key=%1 modifiers=%2 repeat=%3")
                .arg(event->key())
                .arg(static_cast<int>(event->modifiers()))
                .arg(event->isAutoRepeat() ? 1 : 0)
        );
        emit undoShortcutRequested();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        logSelectionRestoreEditorShortcut(
            QStringLiteral("editor_shortcut_press"),
            QStringLiteral("match=redo key=%1 modifiers=%2 repeat=%3")
                .arg(event->key())
                .arg(static_cast<int>(event->modifiers()))
                .arg(event->isAutoRepeat() ? 1 : 0)
        );
        emit redoShortcutRequested();
        event->accept();
        return;
    }

    const QTextCursor cursor = textCursor();
    miacode::editor::SimaiTextEditRequest request;
    request.text = toPlainText();
    request.anchor = cursor.anchor();
    request.position = cursor.position();
    request.input = event->text();
    request.key = event->key();
    request.modifiers = event->modifiers();
    request.halfWidthInputEnabled = halfWidthInputEnabled_;
    request.overwriteMode = overwriteMode();
    request.autoCompletionEnabled = autoCompletionEnabled_;
    request.wholeBpm = wholeBpmCandidate_;
    request.completionActive = bracketCompletionActive();
    if (applySimaiTextEditPolicy(request)) {
        event->accept();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void PlainCodeEditor::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr) {
        QTextEdit::mousePressEvent(event);
        return;
    }

    const QPointF adjustedPosition = normalizedViewportHitPosition(event->position());
    if (adjustedPosition == event->position()) {
        QTextEdit::mousePressEvent(event);
        return;
    }

    const QPointF delta = adjustedPosition - event->position();
    QMouseEvent adjustedEvent(
        event->type(),
        adjustedPosition,
        event->scenePosition() + delta,
        event->globalPosition() + delta,
        event->button(),
        event->buttons(),
        event->modifiers(),
        event->pointingDevice()
    );
    adjustedEvent.setAccepted(false);
    QTextEdit::mousePressEvent(&adjustedEvent);
    event->setAccepted(adjustedEvent.isAccepted());
}
