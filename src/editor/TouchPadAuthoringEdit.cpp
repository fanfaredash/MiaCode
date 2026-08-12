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

struct NoteRange {
    int start = 0;
    int end = 0;
};

// Whitespace separates notes just like `/` does — both parsers flush the
// pending token on every space — so `A1 B2` is a two-note each, not one item.
// Finds the whitespace-delimited note equal to `normalizedPad` inside
// `[start, end)` when that range holds more than one, and widens the removal
// over one adjacent whitespace run so the surviving notes stay separated.
bool findWhitespaceSeparatedNote(
    const QString& text,
    int start,
    int end,
    const QString& normalizedPad,
    int* removalStart,
    int* removalEnd)
{
    QVector<NoteRange> notes;
    int position = start;
    while (position < end) {
        position = skipSpaces(text, position, end);
        if (position >= end) {
            break;
        }
        int noteEnd = position;
        while (noteEnd < end && !text.at(noteEnd).isSpace()) {
            ++noteEnd;
        }
        notes.append(NoteRange{position, noteEnd});
        position = noteEnd;
    }
    if (notes.size() < 2) {
        return false;
    }
    for (int index = 0; index < notes.size(); ++index) {
        const NoteRange& note = notes.at(index);
        if (!isSelectedOrdinaryTouch(text.mid(note.start, note.end - note.start), normalizedPad)) {
            continue;
        }
        if (index > 0) {
            *removalStart = notes.at(index - 1).end;
            *removalEnd = note.end;
        } else {
            *removalStart = note.start;
            *removalEnd = notes.at(1).start;
        }
        return true;
    }
    return false;
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

    for (const miacode::simai::ChartContentSpan& span : spans) {
        if (empty) {
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
            int removalStart = 0;
            int removalEnd = 0;
            if (findWhitespaceSeparatedNote(text, padStart, padEnd, normalizedPad, &removalStart, &removalEnd)) {
                plan.insertionText.clear();
                plan.insertionPosition = removalStart;
                plan.removalLength = removalEnd - removalStart;
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

    if (!empty) {
        plan.insertionPosition = lastContentEnd;
        plan.insertionText = QString(useBacktickSeparator ? QLatin1Char('`') : QLatin1Char('/')) + normalizedPad;
        plan.valid = true;
        return plan;
    }

    // Empty token. When it covers a line break the beat belongs to the LAST
    // line it reaches: appending at the trimmed content end would strand the
    // pad on the previous line, behind that line's trailing comment.
    const int lastNewline = tokenEnd > plan.tokenStart
        ? text.lastIndexOf(QLatin1Char('\n'), tokenEnd - 1)
        : -1;
    int contentStart = plan.tokenStart;
    int contentEnd = plan.tokenStart;
    if (!spans.isEmpty()) {
        if (lastNewline >= plan.tokenStart) {
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
    plan.insertionPosition = firstTouchCandidateStart(text, contentStart, contentEnd);
    // Keep the conventional whitespace gap when a BPM/subdivision prefix
    // immediately precedes the newly authored pad.
    const bool needsSpace = plan.insertionPosition > plan.tokenStart
        && !text.at(plan.insertionPosition - 1).isSpace();
    plan.insertionText = needsSpace ? QStringLiteral(" ") + normalizedPad : normalizedPad;
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
