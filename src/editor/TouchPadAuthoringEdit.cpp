#include "editor/TouchPadAuthoringEdit.h"

#include "core/chart/parser/SimaiCommentScan.h"

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

int trimmedEnd(const QString& text, int start, int end)
{
    while (end > start && text.at(end - 1).isSpace()) {
        --end;
    }
    return end;
}

// Where a pad lands in `[start, end)` when that range holds no note: right
// after its leading controls. When the range covers a line break the beat
// belongs to the LAST line it reaches: appending at the trimmed content end
// would strand the pad on the previous line, behind that line's trailing
// comment.
int emptyTokenPadPosition(const QString& text, int start, int end)
{
    const QVector<miacode::simai::ChartContentSpan> spans =
        miacode::simai::chartContentSpans(text, start, end);
    const int lastNewline = end > start
        ? text.lastIndexOf(QLatin1Char('\n'), end - 1)
        : -1;
    int contentStart = start;
    int contentEnd = start;
    if (!spans.isEmpty()) {
        if (lastNewline >= start) {
            // Only that last line's own text counts; whatever preceded the line
            // break belongs to the bar above.
            const miacode::simai::ChartContentSpan& lastSpan = spans.constLast();
            contentStart = qMax(lastSpan.start, lastNewline + 1);
            contentEnd = trimmedEnd(text, contentStart, qMax(contentStart, lastSpan.end));
        } else {
            contentStart = spans.first().start;
            contentEnd = trimmedEnd(text, contentStart, spans.first().end);
        }
    }
    // Controls are part of the token prefix, not an each-note separator — a
    // `{16}` opening the line still has to precede the authored pad.
    return firstTouchCandidateStart(text, contentStart, contentEnd);
}

// A caret inside `(…)`, `{…}` or `<HS*…>` stands for the whole control, so it
// is moved past the control's closing bracket. Only called for a note-free
// token, whose spans hold nothing but whitespace and complete controls.
int positionAfterEnclosingControl(
    const QString& text,
    const QVector<miacode::simai::ChartContentSpan>& spans,
    int position)
{
    for (const miacode::simai::ChartContentSpan& span : spans) {
        const int spanEnd = trimmedEnd(text, span.start, span.end);
        int controlStart = skipSpaces(text, span.start, spanEnd);
        while (controlStart < spanEnd) {
            const QChar opening = text.at(controlStart);
            const bool isHsDirective = text.mid(controlStart, 4) == QStringLiteral("<HS*");
            const QChar closing = isHsDirective
                ? QLatin1Char('>')
                : opening == QLatin1Char('(')
                    ? QLatin1Char(')')
                    : opening == QLatin1Char('{') ? QLatin1Char('}') : QChar();
            if (closing.isNull()) {
                break;
            }
            const int closePosition = text.indexOf(closing, controlStart + (isHsDirective ? 4 : 1));
            if (closePosition < 0 || closePosition >= spanEnd) {
                break;
            }
            if (position > controlStart && position <= closePosition) {
                return closePosition + 1;
            }
            controlStart = skipSpaces(text, closePosition + 1, spanEnd);
        }
    }
    return position;
}

// Right-click in a note-free token ends that beat AT the caret when controls
// sit on both sides of it: `{24}|{16},` becomes `{24},{16}A1,` — the controls
// right of the caret open the new beat and the pad follows them, exactly where
// a left click would put it in that new beat. The comma goes right after the
// last control left of the caret, never after whitespace; when a line break
// (and any comment ending on it) sits between that control and the caret, the
// comma opens the caret's line instead, so the edit never moves across a
// comment or a line break. Returns -1 when the caret has no control on one
// side, so the caller falls back to the left-click position.
int caretBeatSplitPosition(
    const QString& text,
    const QVector<miacode::simai::ChartContentSpan>& spans,
    int caret)
{
    int leftContentEnd = -1;
    bool rightHasContent = false;
    for (const miacode::simai::ChartContentSpan& span : spans) {
        const int leftEnd = trimmedEnd(text, span.start, qMin(span.end, qMax(span.start, caret)));
        if (leftEnd > span.start) {
            leftContentEnd = leftEnd;
        }
        if (skipSpaces(text, qMax(span.start, caret), span.end) < span.end) {
            rightHasContent = true;
        }
    }
    if (!rightHasContent || leftContentEnd < 0) {
        return -1;
    }
    const int caretLineBreak = text.lastIndexOf(QLatin1Char('\n'), caret - 1);
    return caretLineBreak >= leftContentEnd ? caretLineBreak + 1 : leftContentEnd;
}

} // namespace

TouchPadAuthoringEditPlan planTouchPadAuthoringEdit(
    const QString& text,
    int cursorPosition,
    const QString& pad,
    QChar separator)
{
    TouchPadAuthoringEditPlan plan;
    const QString normalizedPad = pad.trimmed().toUpper();
    if (normalizedPad.isEmpty()) {
        return plan;
    }
    const int position = qBound(0, cursorPosition, text.size());
    // Commas inside a `||` comment are prose, not beat separators — both
    // parsers stop at the marker and resume on the next line.
    const int leftComma = miacode::simai::previousChartComma(text, position);
    const int rightComma = miacode::simai::nextChartComma(text, position);
    plan.tokenStart = leftComma + 1;
    const int tokenEnd = rightComma >= 0 ? rightComma : text.size();
    // A comment ends at ITS newline, not at the token end, so one comma token
    // can hold chart content on both sides of one (or several) comments.
    const QVector<miacode::simai::ChartContentSpan> spans =
        miacode::simai::chartContentSpans(text, plan.tokenStart, tokenEnd);

    // Reuse the ordinary-touch removal path's leading-control scan: BPM and
    // subdivision declarations do not make a comma token non-empty.
    bool empty = true;
    int lastContentEnd = plan.tokenStart;
    for (const miacode::simai::ChartContentSpan& span : spans) {
        const int spanEnd = trimmedEnd(text, span.start, span.end);
        if (spanEnd > span.start) {
            lastContentEnd = spanEnd;
        }
        if (firstTouchCandidateStart(text, span.start, spanEnd) < spanEnd) {
            empty = false;
        }
    }

    // A pad already in the beat is removed by a left or Ctrl+Shift click. The
    // right button always opens a new beat instead, even for a pad the current
    // beat already holds. Items are split on `/` and `` ` `` only: whitespace
    // between two notes is not valid simai, so `A1 B2` is one item.
    const bool removesExistingPad = separator != QLatin1Char(',');
    for (const miacode::simai::ChartContentSpan& span : spans) {
        if (empty || !removesExistingPad) {
            break;
        }
        const int spanEnd = trimmedEnd(text, span.start, span.end);
        int itemStart = span.start;
        int itemIndex = 0;
        while (itemStart <= spanEnd) {
            int itemEnd = itemStart;
            while (itemEnd < spanEnd && !isTouchItemSeparator(text.at(itemEnd))) {
                ++itemEnd;
            }
            const int padStart = itemIndex == 0
                ? firstTouchCandidateStart(text, itemStart, itemEnd)
                : skipSpaces(text, itemStart, itemEnd);
            const int padEnd = trimmedEnd(text, padStart, itemEnd);
            if (isSelectedOrdinaryTouch(text.mid(padStart, padEnd - padStart), normalizedPad)) {
                plan.insertionText.clear();
                if (itemIndex > 0) {
                    plan.insertionPosition = itemStart - 1;
                    plan.removalLength = itemEnd - plan.insertionPosition;
                } else if (itemEnd < spanEnd) {
                    plan.insertionPosition = padStart;
                    plan.removalLength = itemEnd + 1 - padStart;
                } else {
                    plan.insertionPosition = padStart;
                    plan.removalLength = itemEnd - padStart;
                }
                plan.valid = true;
                return plan;
            }
            if (itemEnd >= spanEnd) {
                break;
            }
            itemStart = itemEnd + 1;
            ++itemIndex;
        }
    }

    if (separator == QLatin1Char(',')) {
        const int splitPosition = empty
            ? caretBeatSplitPosition(
                  text, spans, positionAfterEnclosingControl(text, spans, position))
            : -1;
        if (splitPosition >= 0) {
            // One replacement carries both edits: the comma at the split and
            // the pad after the controls that move into the new beat.
            const int padPosition = emptyTokenPadPosition(text, splitPosition, tokenEnd);
            plan.insertionPosition = splitPosition;
            plan.removalLength = padPosition - splitPosition;
            plan.insertionText = QString(separator)
                + text.mid(splitPosition, plan.removalLength) + normalizedPad;
        } else {
            // The new beat opens exactly where a left click would write the
            // pad, so it keeps the same side of every comment and line break.
            plan.insertionPosition = empty
                ? emptyTokenPadPosition(text, plan.tokenStart, tokenEnd)
                : lastContentEnd;
            plan.insertionText = QString(separator) + normalizedPad;
        }
        plan.valid = true;
        return plan;
    }

    if (!empty) {
        plan.insertionPosition = lastContentEnd;
        const QChar validatedSeparator = separator == QLatin1Char('`')
            ? separator
            : QLatin1Char('/');
        plan.insertionText = QString(validatedSeparator) + normalizedPad;
        plan.valid = true;
        return plan;
    }

    // The pad is written flush against the controls: authoring never inserts
    // whitespace of its own.
    plan.insertionPosition = emptyTokenPadPosition(text, plan.tokenStart, tokenEnd);
    plan.insertionText = normalizedPad;
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
