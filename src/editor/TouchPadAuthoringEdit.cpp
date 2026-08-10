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
        const bool isHsDirective = text.mid(position, 4) == QStringLiteral("<HS*");
        if (closing.isNull() && !isHsDirective) {
            break;
        }
        const int closePosition = isHsDirective
            ? text.indexOf(QLatin1Char('>'), position + 4)
            : text.indexOf(closing, position + 1);
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

bool isSelectedOrdinaryTouch(const QString& item, const QString& normalizedPad)
{
    if (item.compare(normalizedPad, Qt::CaseInsensitive) == 0) {
        return true;
    }
    return item.size() == normalizedPad.size() + 1
        && item.endsWith(QLatin1Char('f'), Qt::CaseInsensitive)
        && item.left(normalizedPad.size()).compare(normalizedPad, Qt::CaseInsensitive) == 0;
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
    // A `||` comment occupies the rest of the line and is not chart content.
    // Keep it outside both the emptiness test and the edit range so a pad is
    // inserted before the comment rather than separated from it by `/` or '`'.
    const int commentStart = text.indexOf(QStringLiteral("||"), plan.tokenStart);
    const int contentEnd = commentStart >= plan.tokenStart && commentStart < tokenEnd
        ? commentStart
        : tokenEnd;
    int meaningfulEnd = contentEnd;
    while (meaningfulEnd > plan.tokenStart && text.at(meaningfulEnd - 1).isSpace()) {
        --meaningfulEnd;
    }
    // Reuse the ordinary-touch removal path's leading-control scan: BPM and
    // subdivision declarations do not make a comma token non-empty.
    const bool empty = firstTouchCandidateStart(text, plan.tokenStart, meaningfulEnd) >= meaningfulEnd;

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
        if (isSelectedOrdinaryTouch(text.mid(padStart, padEnd - padStart), normalizedPad)) {
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

    plan.insertionPosition = meaningfulEnd;
    if (empty) {
        // Controls are part of the token prefix, not an each-note separator.
        // Keep the conventional whitespace gap when a BPM/subdivision prefix
        // immediately precedes the newly authored pad.
        const bool needsSpace = meaningfulEnd > plan.tokenStart
            && !text.at(meaningfulEnd - 1).isSpace();
        plan.insertionText = needsSpace ? QStringLiteral(" ") + normalizedPad : normalizedPad;
    } else {
        plan.insertionText = QString(useBacktickSeparator ? QLatin1Char('`') : QLatin1Char('/')) + normalizedPad;
    }
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
