#include "PlainCodeEditor.h"
#include "BracketCompletionPopup.h"
#include "common/AdoptedWidgetCoordinates.h"
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

bool PlainCodeEditor::applySimaiTextEditPolicy(
    const miacode::editor::SimaiTextEditRequest& request)
{
    if (isReadOnly()) {
        return false;
    }
    const auto result = miacode::editor::applySimaiTextEditPolicy(request);
    if (!result.consumed) {
        return false;
    }
    const bool completionWasActive = bracketCompletionActive();

    if (result.transaction.hasEdit) {
        const QTextBlockFormat blockFormat = textCursor().blockFormat();
        const QTextCharFormat charFormat = textCursor().charFormat();
        QTextCursor cursor(document());
        cursor.setPosition(result.transaction.replacementStart);
        cursor.setPosition(result.transaction.replacementEnd, QTextCursor::KeepAnchor);
        suppressCompletionFilter_ = true;
        cursor.beginEditBlock();
        if (result.transaction.insertsBlock) {
            cursor.insertBlock(blockFormat, charFormat);
        } else {
            cursor.insertText(result.transaction.replacementText);
        }
        cursor.endEditBlock();
        cursor.setPosition(result.transaction.anchor);
        cursor.setPosition(result.transaction.position, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
        suppressCompletionFilter_ = false;
    } else {
        QTextCursor cursor = textCursor();
        cursor.setPosition(result.transaction.anchor);
        cursor.setPosition(result.transaction.position, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
    }

    if (result.completion.active) {
        openCompletionPopup(result.completion.candidates, result.completion.opening,
                            result.completion.closingPresent);
    } else if (request.key == Qt::Key_Backspace
               || (request.input.size() == 1
                   && miacode::editor::isBracketClosing(request.input.at(0)))) {
        closeBracketCompletion();
    } else if (completionWasActive) {
        updateBracketCompletionFilter();
    }
    return true;
}


// Auto-close brackets — when the user types {, [, or (, insert the matching
// closing glyph immediately after and park the caret in between. Shared by the
// keyPressEvent path and the IME commit path (inputMethodEvent) so a bracket
// delivered either as a raw key or as an IME commit string — including a
// full-width 「（」/「【」/「｛」 normalized to half-width before we get here —
// gets paired the same way. The bracket-scope highlighter sees the new closing
// char through the document-changed signal and re-colors the block on the next
// paint, so the matched-pair color (and its cross-line scope) stays consistent.
bool PlainCodeEditor::tryAutoCloseBracket(const QString& text)
{
    if (!autoCompletionEnabled_ || text.size() != 1) {
        return false;
    }
    QChar opening;
    QChar closing;
    switch (text.at(0).toLatin1()) {
    case '{': opening = QLatin1Char('{'); closing = QLatin1Char('}'); break;
    case '[': opening = QLatin1Char('['); closing = QLatin1Char(']'); break;
    case '(': opening = QLatin1Char('('); closing = QLatin1Char(')'); break;
    default: return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        cursor.insertText(QString(opening) + QString(closing));
        cursor.movePosition(QTextCursor::PreviousCharacter);
        cursor.endEditBlock();
        setTextCursor(cursor);
    } else {
        cursor.insertText(QString(opening) + QString(closing));
        cursor.movePosition(QTextCursor::PreviousCharacter);
        cursor.endEditBlock();
        setTextCursor(cursor);
    }
    return true;
}

// Bracket input that also offers a completion popup. Wraps tryAutoCloseBracket
// so the suggestion list opens on the same normalized key/IME bracket. Returns
// true when the bracket was consumed (paired and popup opened).
bool PlainCodeEditor::tryBracketInput(const QString& text)
{
    if (text.size() != 1) {
        return false;
    }
    const QChar opening = text.at(0);
    if (!miacode::editor::isBracketOpening(opening)) {
        return false;
    }
    if (tryAutoCloseBracket(text)) {
        // Completion can now use that empty slot even after selection replacement.
        maybeOpenBracketCompletion(opening, /*closingPresent=*/true);
        return true;
    }
    // Auto-close declined (preference off, read-only, overwrite): leave the
    // bracket to the normal insert path. Completion follows the same single
    // preference as auto-close, so there is no completion-without-close case.
    return false;
}

// Type-over for existing duration slots: when the caret is immediately before
// a '[' and the user types '[', move into that bracket instead of inserting a
// duplicate pair. Kept square-only because '[' is the duration/hold slot users
// commonly enter after typing a note or `h`.
bool PlainCodeEditor::tryOverwriteOpeningSquareBracket(const QString& text)
{
    if (!autoCompletionEnabled_ || text != QLatin1String("[")) {
        return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    QTextCursor rightProbe(document());
    rightProbe.setPosition(cursor.position());
    if (!rightProbe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)
        || rightProbe.selectedText() != QLatin1String("[")) {
        return false;
    }
    if (bracketCompletionActive()) {
        closeBracketCompletion();
    }
    cursor.movePosition(QTextCursor::NextCharacter);
    setTextCursor(cursor);
    return true;
}

// simai "hold" shortcut: typing a lowercase `h` inserts/replaces with a bare `h` and offers
// the full-bracket hold-duration tokens ("[8:1]" …) as a completion popup —
// simai hold notation is `<lane>h[<beats>:<ticks>]`. Crucially it inserts NO
// bracket of its own, so a following `[` produces the normal `h[]` rather than
// the old contradictory `h[[]]`; the suggestion supplies the whole `[...]`.
// We require EXACTLY a single lowercase `h` so Shift+H, Ctrl+H, full-width 「ｈ」
// etc. fall through to a normal insert. Wired to the keyPressEvent path ONLY —
// CJK IMEs routinely commit stray latin letters mid-composition and triggering
// this on those would be disruptive.
bool PlainCodeEditor::tryHoldExpand(const QString& text)
{
    if (!autoCompletionEnabled_ || text != QLatin1String("h")) {
        return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    // A completion popup is already filtering — let the `h` be a normal filter
    // character instead of spawning a second, nested suggestion slot.
    if (bracketCompletionActive()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        cursor.insertText(QStringLiteral("h"));
    } else {
        cursor.insertText(QStringLiteral("h"));
    }
    setTextCursor(cursor);
    // Don't suggest when the caret already sits right before a '[': the user is
    // hold-filling the existing bracket, so the full-bracket tokens would only
    // duplicate it. The bare 'h' was still the right insert (yields `h[`).
    QTextCursor rightProbe(document());
    rightProbe.setPosition(cursor.position());
    if (rightProbe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)
        && rightProbe.selectedText() == QLatin1String("[")) {
        return true;
    }
    maybeOpenHoldCompletion();
    return true;
}

// Backspace inside an empty matching pair (`(|)`, `[|]`, `{|}`) deletes BOTH
// glyphs as a single undo step — the inverse of auto-close's pair insertion, so
// it's gated by the same preference. Ctrl/Alt/Meta+Backspace (word delete) and
// any selection fall through to the default handling.
bool PlainCodeEditor::tryDeleteBracketPair(QKeyEvent* event)
{
    if (!autoCompletionEnabled_ || event->key() != Qt::Key_Backspace) {
        return false;
    }
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    const int position = cursor.position();

    QTextCursor leftProbe(document());
    leftProbe.setPosition(position);
    if (!leftProbe.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor)) {
        return false;
    }
    const QString leftText = leftProbe.selectedText();
    if (leftText.size() != 1 || !miacode::editor::isBracketOpening(leftText.at(0))) {
        return false;
    }
    const QChar closing = miacode::editor::closingBracketFor(leftText.at(0));

    QTextCursor rightProbe(document());
    rightProbe.setPosition(position);
    if (!rightProbe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)
        || rightProbe.selectedText() != QString(closing)) {
        return false;
    }

    // An open completion popup is anchored to this slot — dismiss it first.
    if (bracketCompletionActive()) {
        closeBracketCompletion();
    }
    suppressCompletionFilter_ = true;
    cursor.beginEditBlock();
    cursor.setPosition(position - 1);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    setTextCursor(cursor);
    suppressCompletionFilter_ = false;
    event->accept();
    return true;
}

// Type-over: when the user types a closing bracket and the identical glyph is
// already sitting immediately to the right of the caret, just hop past it rather
// than inserting a second one. This is the natural complement to auto-close — the
// pair you opened parks the caret at `[|]`, and typing the `]` you see walks the
// caret out instead of leaving `[]|]`. Gated by the same preference as auto-close.
// A selection, overwrite mode, or read-only falls through to a normal insert.
bool PlainCodeEditor::tryOverwriteClosingBracket(const QString& text)
{
    if (!autoCompletionEnabled_ || text.size() != 1) {
        return false;
    }
    const QChar closing = text.at(0);
    if (!miacode::editor::isBracketClosing(closing)) {
        return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    QTextCursor rightProbe(document());
    rightProbe.setPosition(cursor.position());
    if (!rightProbe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)
        || rightProbe.selectedText() != QString(closing)) {
        return false;
    }
    // Stepping over a glyph the popup might be filtering against would leave a
    // stale list anchored to the old slot — dismiss it first.
    if (bracketCompletionActive()) {
        closeBracketCompletion();
    }
    cursor.movePosition(QTextCursor::NextCharacter);
    setTextCursor(cursor);
    return true;
}

void PlainCodeEditor::ensureCompletionPopup()
{
    if (completionPopup_ != nullptr) {
        return;
    }
    completionPopup_ = new BracketCompletionPopup(this);
    const auto& colors = UiTheme::colors();
    completionPopup_->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  outline: 0;"
        "  padding: 2px;"
        "}"
        "QListWidget::item {"
        "  padding: 3px 10px;"
        "  border-radius: 3px;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: %4;"
        "  color: %5;"
        "}")
        .arg(colors.panelBg.name(QColor::HexRgb))
        .arg(colors.textPrimary.name(QColor::HexRgb))
        .arg(colors.border.name(QColor::HexRgb))
        .arg(colors.accent.name(QColor::HexRgb))
        .arg(colors.accentText.name(QColor::HexRgb)));
    // A mouse press commits the clicked row.
    connect(completionPopup_, &BracketCompletionPopup::candidateActivated, this,
            [this](const QString& candidate) { acceptCompletionCandidate(candidate); });
}

// Shared core: anchor the popup under the caret and record the slot state.
// `opening` governs the filter's "left the slot" check (via its matching close);
// `closingPresent` says whether an auto-inserted close sits to the right so the
// accept path can swallow it. No-op when there is nothing to suggest.
void PlainCodeEditor::openCompletionPopup(const QStringList& candidates, QChar opening, bool closingPresent)
{
    if (candidates.isEmpty()) {
        return;
    }
    completionOpening_ = opening;
    completionClosingPresent_ = closingPresent;
    completionStartPos_ = textCursor().position();
    ensureCompletionPopup();
    const QRect caret = cursorRect();
    const QPoint anchor = miacode::ui::mapWidgetPointToGlobal(
        viewport(), caret.bottomLeft() + QPoint(0, 2));
    completionPopup_->showCandidates(candidates, anchor);
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("bracket_completion/open"),
        QStringLiteral("glyph=%1 candidates=%2 closing=%3")
            .arg(opening)
            .arg(candidates.size())
            .arg(closingPresent ? 1 : 0));
}

// Opens the suggestion popup for the bracket the caret now sits inside. closing-
// Present says whether a matching closing glyph was auto-inserted to the right.
void PlainCodeEditor::maybeOpenBracketCompletion(QChar opening, bool closingPresent)
{
    if (!autoCompletionEnabled_ || isReadOnly() || overwriteMode()) {
        return;
    }
    // e.g. '(' before any BPM is known yields an empty list — nothing to show.
    openCompletionPopup(
        miacode::editor::candidatesForOpening(opening, wholeBpmCandidate_, toPlainText()),
        opening,
        closingPresent);
}

// Opens the 'h' hold-duration suggestions ("[8:1]" …) anchored right after the
// just-typed 'h'. No bracket was inserted (closingPresent=false); completion-
// Opening is '[' so the filter's "left the slot" check keys off ']', matching
// the '[' duration popup.
void PlainCodeEditor::maybeOpenHoldCompletion()
{
    if (!autoCompletionEnabled_ || isReadOnly() || overwriteMode()) {
        return;
    }
    openCompletionPopup(
        miacode::editor::holdDurationCandidates(),
        QLatin1Char('['),
        /*closingPresent=*/false);
}

bool PlainCodeEditor::bracketCompletionActive() const
{
    return completionPopup_ != nullptr && completionPopup_->isVisible()
        && !completionOpening_.isNull();
}

// Intercepts the navigation / accept / dismiss keys while the popup is open.
// Printable characters and Backspace deliberately return false so they reach the
// normal insert path and the cursorPositionChanged slot re-filters the list.
bool PlainCodeEditor::handleCompletionPopupKey(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        closeBracketCompletion();
        event->accept();
        return true;
    case Qt::Key_Up:
        completionPopup_->moveSelection(-1);
        event->accept();
        return true;
    case Qt::Key_Down:
        completionPopup_->moveSelection(1);
        event->accept();
        return true;
    case Qt::Key_Tab:
        acceptCompletionCandidate();
        event->accept();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Plain Enter commits the highlighted candidate (and swallows the line
        // break). Ctrl/Alt/Meta + Enter fall through to the editor's own
        // line-break handling.
        if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            acceptCompletionCandidate();
            event->accept();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void PlainCodeEditor::acceptCompletionCandidate()
{
    if (!bracketCompletionActive()) {
        return;
    }
    const QString candidate = completionPopup_->currentCandidate();
    acceptCompletionCandidate(candidate);
}

void PlainCodeEditor::acceptCompletionCandidate(const QString& candidate)
{
    if (completionOpening_.isNull() || completionStartPos_ < 0) {
        return;
    }
    if (candidate.isEmpty()) {
        closeBracketCompletion();
        return;
    }
    const QChar closing = miacode::editor::closingBracketFor(completionOpening_);

    suppressCompletionFilter_ = true;
    QTextCursor cursor = textCursor();
    int selectionEnd = cursor.position();
    // The candidate carries its own closing glyph, so swallow the auto-inserted
    // one immediately to the right (if present) instead of duplicating it.
    if (completionClosingPresent_ && !closing.isNull()) {
        QTextCursor probe(document());
        probe.setPosition(selectionEnd);
        probe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (probe.selectedText() == QString(closing)) {
            selectionEnd += 1;
        }
    }
    cursor.beginEditBlock();
    cursor.setPosition(completionStartPos_);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    cursor.insertText(candidate);
    cursor.endEditBlock();
    setTextCursor(cursor);
    suppressCompletionFilter_ = false;
    closeBracketCompletion();
}

void PlainCodeEditor::updateBracketCompletionFilter()
{
    const int position = textCursor().position();
    if (position < completionStartPos_) {
        closeBracketCompletion();
        return;
    }
    QTextCursor span(document());
    span.setPosition(completionStartPos_);
    span.setPosition(position, QTextCursor::KeepAnchor);
    const QString typed = span.selectedText();
    // A newline or the closing glyph means the caret left the completion slot.
    const QChar closing = miacode::editor::closingBracketFor(completionOpening_);
    if (typed.contains(QChar::ParagraphSeparator) || typed.contains(QChar::LineSeparator)
        || (!closing.isNull() && typed.contains(closing))) {
        closeBracketCompletion();
        return;
    }
    if (!completionPopup_->applyFilter(typed)) {
        closeBracketCompletion();
        return;
    }
    const QRect caret = cursorRect();
    const QPoint anchor = miacode::ui::mapWidgetPointToGlobal(
        viewport(), caret.bottomLeft() + QPoint(0, 2));
    completionPopup_->moveToAnchor(anchor);
}

void PlainCodeEditor::closeBracketCompletion()
{
    if (completionPopup_ != nullptr) {
        completionPopup_->hide();
    }
    completionOpening_ = QChar();
    completionClosingPresent_ = false;
    completionStartPos_ = -1;
}
