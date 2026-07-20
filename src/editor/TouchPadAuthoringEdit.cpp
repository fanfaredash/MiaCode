#include "editor/TouchPadAuthoringEdit.h"

#include <QTextCursor>
#include <QTextDocument>

namespace miacode::editor {

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
    plan.insertionPosition = empty ? plan.tokenStart : tokenEnd;
    if (!empty) {
        while (plan.insertionPosition > plan.tokenStart
               && text.at(plan.insertionPosition - 1).isSpace()) {
            --plan.insertionPosition;
        }
    }
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
    editCursor.setPosition(qBound(0, plan.insertionPosition, document->characterCount() - 1));
    editCursor.insertText(plan.insertionText);
    editCursor.endEditBlock();
    *cursor = editCursor;
    return true;
}

} // namespace miacode::editor
