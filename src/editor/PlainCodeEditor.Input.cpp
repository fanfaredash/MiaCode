#include "PlainCodeEditor.h"
#include "BracketCompletionPopup.h"
#include "SimaiCompletionCatalog.h"
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

namespace {
QString transformCompleteElementsInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    bool resetToTap,
    int* changedCount)
{
    if (changedCount != nullptr) {
        *changedCount = 0;
    }
    if (text.isEmpty()) {
        return text;
    }

    const int boundedStart = qBound(0, selectionStart, text.size());
    const int boundedEnd = qBound(boundedStart, selectionEnd, text.size());
    if (boundedStart >= boundedEnd) {
        return text;
    }

    QString output;
    output.reserve(text.size());
    output.append(text.left(boundedStart));

    int segmentStart = boundedStart;
    const auto commentStartInLine = [&text](int start, int endExclusive) {
        const int index = text.indexOf(QStringLiteral("||"), start);
        return (index >= 0 && index < endExclusive) ? index : -1;
    };
    const int newlineBeforeSelection =
        boundedStart > 0 ? text.lastIndexOf(QLatin1Char('\n'), boundedStart - 1) : -1;
    int lineStart = newlineBeforeSelection + 1;
    int nextLineStart = text.indexOf(QLatin1Char('\n'), lineStart);
    if (nextLineStart < 0) {
        nextLineStart = text.size();
    } else {
        ++nextLineStart;
    }
    int commentStart = commentStartInLine(lineStart, nextLineStart);
    int squareDepth = 0;
    int braceDepth = 0;
    int parenDepth = 0;
    int changed = 0;

    const auto isTopLevel = [&]() {
        return squareDepth == 0 && braceDepth == 0 && parenDepth == 0;
    };
    const auto leadingPrefixEnd = [&](int start, int end) {
        int pos = start;
        while (pos < end) {
            const QChar opening = text.at(pos);
            QChar closing;
            if (opening == QLatin1Char('{')) {
                closing = QLatin1Char('}');
            } else if (opening == QLatin1Char('(')) {
                closing = QLatin1Char(')');
            } else {
                break;
            }
            const int closeIndex = text.indexOf(closing, pos + 1);
            if (closeIndex < 0 || closeIndex >= end) {
                break;
            }
            pos = closeIndex + 1;
        }
        return pos;
    };
    const auto isBoundaryBefore = [&](int pos) {
        if (pos <= 0) {
            return true;
        }
        const QChar previous = text.at(pos - 1);
        return previous == QLatin1Char(',')
            || previous == QLatin1Char('\n')
            || previous == QChar::ParagraphSeparator
            || previous == QChar::LineSeparator;
    };
    const auto partialLeadingPrefixEnd = [&](int start, int end) {
        const int openBrace = text.lastIndexOf(QLatin1Char('{'), start);
        const int openParen = text.lastIndexOf(QLatin1Char('('), start);
        const int openIndex = qMax(openBrace, openParen);
        if (openIndex < 0 || openIndex >= start || !isBoundaryBefore(openIndex)) {
            return start;
        }
        const QChar closing = text.at(openIndex) == QLatin1Char('{')
            ? QLatin1Char('}')
            : QLatin1Char(')');
        const int closeIndex = text.indexOf(closing, openIndex + 1);
        if (closeIndex < start || closeIndex >= end) {
            return start;
        }
        return closeIndex + 1;
    };
    const auto appendRawThrough = [&](int endExclusive) {
        output.append(text.mid(segmentStart, endExclusive - segmentStart));
        segmentStart = endExclusive;
    };
    const auto isElementBoundary = [&](int pos) {
        if (pos <= 0) {
            return true;
        }
        const QChar previous = text.at(pos - 1);
        if (previous == QLatin1Char(',')
            || previous == QLatin1Char('\n')
            || previous == QChar::ParagraphSeparator
            || previous == QChar::LineSeparator) {
            return true;
        }
        if (previous != QLatin1Char('}') && previous != QLatin1Char(')')) {
            return false;
        }
        const QChar opening = previous == QLatin1Char('}')
            ? QLatin1Char('{')
            : QLatin1Char('(');
        const int openIndex = text.lastIndexOf(opening, pos - 2);
        if (openIndex < 0) {
            return false;
        }
        if (openIndex == 0) {
            return true;
        }
        const QChar beforePrefix = text.at(openIndex - 1);
        return beforePrefix == QLatin1Char(',')
            || beforePrefix == QLatin1Char('\n')
            || beforePrefix == QChar::ParagraphSeparator
            || beforePrefix == QChar::LineSeparator;
    };
    // Emit [spanStart, spanEnd) with the note tokens removed but every {…}/(…)
    // directive AND all whitespace/newlines kept verbatim. This preserves the
    // "{} ," timing skeleton (subdivisions, BPM marks, line breaks) even when a
    // directive sits behind a newline/space instead of tight against the prior
    // comma — clearing a passage that spans several {} subdivisions must NOT
    // collapse them into one. Returns true if a note token was actually dropped.
    const auto appendTransformedNoteSpan = [&](int spanStart, int spanEnd) {
        bool droppedNote = false;
        QString noteText;
        int pos = spanStart;
        while (pos < spanEnd) {
            const QChar ch = text.at(pos);
            if (ch == QLatin1Char('{') || ch == QLatin1Char('(')) {
                const QChar closing = ch == QLatin1Char('{')
                    ? QLatin1Char('}')
                    : QLatin1Char(')');
                const int closeIndex = text.indexOf(closing, pos + 1);
                if (closeIndex < 0 || closeIndex >= spanEnd) {
                    // Unterminated directive — preserve the remainder verbatim
                    // rather than risk dropping structural text.
                    output.append(text.mid(pos, spanEnd - pos));
                    return droppedNote;
                }
                output.append(text.mid(pos, closeIndex - pos + 1));
                pos = closeIndex + 1;
            } else if (ch.isSpace()) {
                output.append(ch);
                ++pos;
            } else {
                if (resetToTap && noteText.isEmpty()) {
                    output.append(QLatin1Char('1'));
                }
                noteText.append(ch);
                droppedNote = true;
                ++pos;
            }
        }
        return droppedNote && (!resetToTap || noteText != QLatin1String("1"));
    };
    const auto appendClearedSegment = [&](int commaIndex) {
        const int fullPrefixEnd = leadingPrefixEnd(segmentStart, commaIndex);
        const int partialPrefixEnd = partialLeadingPrefixEnd(segmentStart, commaIndex);
        const int prefixEnd = qMax(fullPrefixEnd, partialPrefixEnd);
        const bool canClear = commaIndex > prefixEnd
            && (isElementBoundary(segmentStart) || partialPrefixEnd > segmentStart);
        output.append(text.mid(segmentStart, prefixEnd - segmentStart));
        if (canClear) {
            if (appendTransformedNoteSpan(prefixEnd, commaIndex)) {
                ++changed;
            }
            output.append(QLatin1Char(','));
        } else {
            output.append(text.mid(prefixEnd, commaIndex - prefixEnd + 1));
        }
        segmentStart = commaIndex + 1;
    };

    for (int i = boundedStart; i < boundedEnd; ++i) {
        while (i >= nextLineStart) {
            lineStart = nextLineStart;
            nextLineStart = text.indexOf(QLatin1Char('\n'), lineStart);
            if (nextLineStart < 0) {
                nextLineStart = text.size();
            } else {
                ++nextLineStart;
            }
            commentStart = commentStartInLine(lineStart, nextLineStart);
            squareDepth = 0;
            braceDepth = 0;
            parenDepth = 0;
        }
        if (commentStart >= lineStart && i >= commentStart) {
            // TODO: Ctrl+Q intentionally skips comment text for now. If comment
            // editing later needs this shortcut, add a separate comment-aware
            // grammar instead of reusing chart-token clearing.
            appendRawThrough(qMin(nextLineStart, boundedEnd));
            i = segmentStart - 1;
            continue;
        }
        const QChar ch = text.at(i);
        switch (ch.unicode()) {
        case '[':
            ++squareDepth;
            break;
        case ']':
            squareDepth = qMax(0, squareDepth - 1);
            break;
        case '{':
            ++braceDepth;
            break;
        case '}':
            braceDepth = qMax(0, braceDepth - 1);
            break;
        case '(':
            ++parenDepth;
            break;
        case ')':
            parenDepth = qMax(0, parenDepth - 1);
            break;
        case ',':
            if (isTopLevel()) {
                appendClearedSegment(i);
            }
            break;
        default:
            break;
        }
    }

    output.append(text.mid(segmentStart, boundedEnd - segmentStart));
    output.append(text.mid(boundedEnd));

    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}
}  // namespace

QString clearCompleteElementsInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    int* changedCount)
{
    return transformCompleteElementsInSelection(
        text, selectionStart, selectionEnd, false, changedCount);
}

QString resetTapNotesInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    int* changedCount)
{
    return transformCompleteElementsInSelection(
        text, selectionStart, selectionEnd, true, changedCount);
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
    const QRect previousRect = previewFollowVisualCaretRect();
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
    if (viewport() != nullptr && previousRect.isValid()) {
        viewport()->update(previousRect.adjusted(-1, -1, 1, 1).intersected(viewport()->rect()));
    }
}

void PlainCodeEditor::focusOutEvent(QFocusEvent* event)
{
    // The popup never takes focus, so losing focus means the user clicked or
    // tabbed away — dismiss any open suggestion list.
    const bool pointerInsideCompletionPopup =
        completionPopup_ != nullptr
        && completionPopup_->isVisible()
        && completionPopup_->geometry().contains(QCursor::pos());
    if (!pointerInsideCompletionPopup) {
        closeBracketCompletion();
    }
    const QRect previousRect = previewFollowVisualCaretRect();
    QTextEdit::focusOutEvent(event);
    const QRect currentRect = previewFollowVisualCaretRect();
    if (viewport() == nullptr) {
        return;
    }
    QRect dirtyRect;
    if (previousRect.isValid()) {
        dirtyRect = previousRect;
    }
    if (currentRect.isValid()) {
        dirtyRect = dirtyRect.isNull() ? currentRect : dirtyRect.united(currentRect);
    }
    if (dirtyRect.isValid()) {
        viewport()->update(dirtyRect.adjusted(-1, -1, 1, 1).intersected(viewport()->rect()));
    }
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

    auto* exportRangeAction = menu->addAction(
        translated(QStringLiteral("video_export.export_range_from_selection"), QStringLiteral("Export Selected Range")));
    exportRangeAction->setEnabled(textCursor().hasSelection());
    connect(exportRangeAction, &QAction::triggered, this, [this]() {
        const QTextCursor cursor = textCursor();
        if (cursor.hasSelection() && cursor.selectionEnd() > cursor.selectionStart()) {
            emit exportRangeRequested(cursor.selectionStart(), cursor.selectionEnd());
        }
    });

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
    if (event == nullptr || !halfWidthInputEnabled_) {
        QTextEdit::inputMethodEvent(event);
        return;
    }

    const QString commitString = event->commitString();
    const QString normalizedCommitString = miacode::editor::normalizedHalfWidthText(commitString);

    // Route a finalized single-bracket commit through the same auto-close
    // pairing as keyPressEvent. Without this, a bracket delivered via the IME
    // commit string — e.g. a full-width 「【」 normalized to a half-width "["
    // just above — is inserted unclosed, leaving a dangling scope that the
    // bracket-scope highlighter then carries onto the following line. Guard to a
    // finalized composition (empty preedit) with no replacement span so the
    // helper's caret math stays valid.
    if (event->preeditString().isEmpty()
        && event->replacementLength() == 0
        && normalizedCommitString.size() == 1
        && miacode::editor::isBracketOpening(normalizedCommitString.at(0))) {
        if (tryOverwriteOpeningSquareBracket(normalizedCommitString)) {
            event->accept();
            return;
        }
        const QChar opening = normalizedCommitString.at(0);
        if (tryAutoCloseBracket(normalizedCommitString)) {
            maybeOpenBracketCompletion(opening, /*closingPresent=*/true);
            event->accept();
            return;
        }
    }

    // A finalized closing-bracket commit (e.g. a full-width 「）」 normalized to a
    // half-width ")") gets the same type-over treatment as the keyPress path.
    if (event->preeditString().isEmpty()
        && event->replacementLength() == 0
        && normalizedCommitString.size() == 1
        && miacode::editor::isBracketClosing(normalizedCommitString.at(0))) {
        if (tryOverwriteClosingBracket(normalizedCommitString)) {
            event->accept();
            return;
        }
    }

    if (commitString == normalizedCommitString) {
        QTextEdit::inputMethodEvent(event);
        return;
    }

    QInputMethodEvent normalizedEvent(event->preeditString(), event->attributes());
    normalizedEvent.setCommitString(
        normalizedCommitString,
        event->replacementStart(),
        event->replacementLength()
    );
    QTextEdit::inputMethodEvent(&normalizedEvent);
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

    // Backspace between an empty matching pair removes both glyphs at once.
    if (tryDeleteBracketPair(event)) {
        return;
    }

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

    const bool plainEnterKey =
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool ctrlEnterKey =
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ControlModifier)
        && !(event->modifiers() & (Qt::AltModifier | Qt::MetaModifier));
    if (plainEnterKey || ctrlEnterKey) {
        insertLineBreakAtCursor(this);
        return;
    }
    if (matchesShortcutId(
            event,
            QStringLiteral("transform.reset_tap_notes"),
            {QKeySequence(Qt::CTRL | Qt::Key_W)})) {
        emit resetTapNotesShortcutRequested();
        event->accept();
        return;
    }

    // Bracket auto-pairing is handled by tryAutoCloseBracket() / tryBracketInput()
    // (members, so the IME commit path in inputMethodEvent can reuse them); the
    // 'h' hold shortcut by tryHoldExpand(). The call sites below pass the half-
    // width-normalized text so a full-width 「（」/「【」/「｛」 pairs the same way a
    // raw "(" / "[" / "{" does.
    if (!halfWidthInputEnabled_
        || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (tryOverwriteClosingBracket(event->text())
            || tryOverwriteOpeningSquareBracket(event->text())
            || tryBracketInput(event->text()) || tryHoldExpand(event->text())) {
            event->accept();
            return;
        }
        QTextEdit::keyPressEvent(event);
        return;
    }

    const QString inputText = event->text();
    if (inputText.isEmpty()) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    const QString normalizedText = miacode::editor::normalizedHalfWidthKeyText(event, inputText);
    if (tryOverwriteClosingBracket(normalizedText)
        || tryOverwriteOpeningSquareBracket(normalizedText)
        || tryBracketInput(normalizedText) || tryHoldExpand(normalizedText)) {
        event->accept();
        return;
    }
    if (normalizedText == inputText) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    QKeyEvent normalizedEvent(
        event->type(),
        event->key(),
        event->modifiers(),
        normalizedText,
        event->isAutoRepeat(),
        event->count()
    );
    QTextEdit::keyPressEvent(&normalizedEvent);
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
