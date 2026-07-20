#include "editor/TouchPadAuthoringEdit.h"

#include <QTextCursor>
#include <QTextDocument>

namespace miacode::editor {

namespace {

int skipSpaces(const QString& text, int position, int end)
{
    while (position < end && text.at(position).isSpace()) {
        ++position;
    }
    return position;
}

int firstTouchCandidateStart(const QString& text, int start, int end)
{
    int position = skipSpaces(text, start, end);
    while (position < end) {
        const QChar opening = text.at(position);
        const QChar closing = opening == QLatin1Char('(')
            ? QLatin1Char(')')
            : opening == QLatin1Char('{') ? QLatin1Char('}') : QChar();
        if (closing.isNull()) {
            break;
        }
        const int closePosition = text.indexOf(closing, position + 1);
        if (closePosition < 0 || closePosition >= end) {
            break;
        }
        position = skipSpaces(text, closePosition + 1, end);
    }
    return position;
}

bool isTouchItemSeparator(QChar ch)
{
    return ch == QLatin1Char('/') || ch == QLatin1Char('`');
}

} // namespace

TouchPadAuthoringEditPlan planTouchPadAuthoringEdit(
    const QString& text,
    int cursorPosition,
    const QString& pad,
    bool useBacktickSeparator)
{
    TouchPadAuthoringEditPlan plan;
    const QString normalizedPad = pad.trimmed().toUpper();
    if (normalizedPad.isEmpty()) {
        return plan;
    }
    const int position = qBound(0, cursorPosition, text.size());
    const int leftComma = position > 0
        ? text.lastIndexOf(QLatin1Char(','), position - 1)
        : -1;
    const int rightComma = text.indexOf(QLatin1Char(','), position);
    plan.tokenStart = leftComma + 1;
    const int tokenEnd = rightComma >= 0 ? rightComma : text.size();
    const QString token = text.mid(plan.tokenStart, tokenEnd - plan.tokenStart);
    const bool empty = token.trimmed().isEmpty();
    int meaningfulEnd = tokenEnd;
    while (meaningfulEnd > plan.tokenStart && text.at(meaningfulEnd - 1).isSpace()) {
        --meaningfulEnd;
    }

    int itemStart = plan.tokenStart;
    int itemIndex = 0;
    while (!empty && itemStart <= meaningfulEnd) {
        int itemEnd = itemStart;
        while (itemEnd < meaningfulEnd && !isTouchItemSeparator(text.at(itemEnd))) {
            ++itemEnd;
        }
        const int padStart = itemIndex == 0
            ? firstTouchCandidateStart(text, itemStart, itemEnd)
            : skipSpaces(text, itemStart, itemEnd);
        int padEnd = itemEnd;
        while (padEnd > padStart && text.at(padEnd - 1).isSpace()) {
            --padEnd;
        }
        if (text.mid(padStart, padEnd - padStart).compare(normalizedPad, Qt::CaseInsensitive) == 0) {
            plan.insertionText.clear();
            if (itemIndex > 0) {
                plan.insertionPosition = itemStart - 1;
                plan.removalLength = itemEnd - plan.insertionPosition;
            } else if (itemEnd < meaningfulEnd) {
                plan.insertionPosition = padStart;
                plan.removalLength = itemEnd + 1 - padStart;
            } else {
                plan.insertionPosition = padStart;
                plan.removalLength = itemEnd - padStart;
            }
            plan.valid = true;
            return plan;
        }
        if (itemEnd >= meaningfulEnd) {
            break;
        }
        itemStart = itemEnd + 1;
        ++itemIndex;
    }

    plan.insertionPosition = empty ? plan.tokenStart : meaningfulEnd;
    plan.insertionText = empty
        ? normalizedPad
        : QString(useBacktickSeparator ? QLatin1Char('`') : QLatin1Char('/')) + normalizedPad;
    plan.valid = true;
    return plan;
}

bool applyTouchPadAuthoringEdit(
    QTextDocument* document,
    QTextCursor* cursor,
    const TouchPadAuthoringEditPlan& plan)
{
    if (document == nullptr || cursor == nullptr || !plan.valid) {
        return false;
    }
    QTextCursor editCursor(document);
    editCursor.beginEditBlock();
    const int documentLength = document->characterCount() - 1;
    const int editStart = qBound(0, plan.insertionPosition, documentLength);
    editCursor.setPosition(editStart);
    if (plan.removalLength > 0) {
        editCursor.setPosition(
            qBound(editStart, editStart + plan.removalLength, documentLength),
            QTextCursor::KeepAnchor);
    }
    editCursor.insertText(plan.insertionText);
    editCursor.endEditBlock();
    *cursor = editCursor;
    return true;
}

} // namespace miacode::editor
