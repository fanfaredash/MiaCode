#include "ChartBatchTransform.h"
#include "ChartBatchTransform.Internal.h"

#include <QRandomGenerator>
#include <QStringList>

namespace miacode::chart_transform::detail {

void scanSelectionTokens(const QString& input, const std::function<void(const QString&)>& visitToken)
{
    const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        QString token;
        const auto flushToken = [&]() {
            if (!token.isEmpty()) {
                visitToken(token);
                token.clear();
            }
        };

        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken();
                break;
            }
            if (ch.isSpace() || ch == QChar('/') || ch == QChar('`') || ch == QChar(',')) {
                flushToken();
                continue;
            }
            if (ch == QChar('(')) {
                flushToken();
                const int close = line.indexOf(QChar(')'), i + 1);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            if (ch == QChar('{')) {
                flushToken();
                const int close = line.indexOf(QChar('}'), i + 1);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            if (ch == QChar('<') && line.mid(i, 4) == QStringLiteral("<HS*")) {
                flushToken();
                const int close = line.indexOf(QChar('>'), i + 4);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            token.append(ch);
        }

        flushToken();
    }
}

QString rewriteSelectionTokens(const QString& input, const std::function<QString(const QString&)>& rewriteToken)
{
    QString transformed;
    transformed.reserve(input.size() + 32);
    const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        QString token;
        const auto flushToken = [&]() {
            if (!token.isEmpty()) {
                transformed.append(rewriteToken(token));
                token.clear();
            }
        };

        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken();
                transformed.append(line.mid(i));
                i = line.size();
                break;
            }
            if (ch.isSpace() || ch == QChar('/') || ch == QChar('`') || ch == QChar(',')) {
                flushToken();
                transformed.append(ch);
                continue;
            }
            if (ch == QChar('(')) {
                flushToken();
                const int close = line.indexOf(QChar(')'), i + 1);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('{')) {
                flushToken();
                const int close = line.indexOf(QChar('}'), i + 1);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('<') && line.mid(i, 4) == QStringLiteral("<HS*")) {
                flushToken();
                const int close = line.indexOf(QChar('>'), i + 4);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            token.append(ch);
        }

        flushToken();
        if (lineIndex + 1 < lines.size()) {
            transformed.append('\n');
        }
    }
    return transformed;
}

ToggleStats collectBreakStats(const QString& input)
{
    ToggleStats stats;
    scanSelectionTokens(input, [&stats](const QString& token) {
        if (isSimpleDigitCluster(token)) {
            stats.eligibleObjects += token.size();
            return;
        }

        TouchTokenParts touch;
        if (parseTouchTokenParts(token, &touch) && touch.valid) {
            if (!touch.hasHold) {
                ++stats.eligibleObjects;
                if (touch.hasBreak) {
                    ++stats.flaggedObjects;
                }
            }
            return;
        }

        SlideTokenParts slide;
        if (parseSlideTokenParts(token, &slide) && slide.valid) {
            stats.eligibleObjects += 1;  // head
            if (slide.headBreak) {
                ++stats.flaggedObjects;
            }
            stats.eligibleObjects += slide.segments.size();  // one per segment
            for (const auto& seg : slide.segments) {
                if (seg.segmentBreak) {
                    ++stats.flaggedObjects;
                }
            }
            return;
        }

        NoteTokenParts note;
        if (parseNoteTokenParts(token, &note) && note.valid) {
            ++stats.eligibleObjects;
            if (note.hasBreak) {
                ++stats.flaggedObjects;
            }
        }
    });
    return stats;
}

ToggleStats collectExStats(const QString& input)
{
    ToggleStats stats;
    scanSelectionTokens(input, [&stats](const QString& token) {
        if (isSimpleDigitCluster(token)) {
            stats.eligibleObjects += token.size();
            return;
        }

        SlideTokenParts slide;
        if (parseSlideTokenParts(token, &slide) && slide.valid) {
            ++stats.eligibleObjects;
            if (slide.headEx) {
                ++stats.flaggedObjects;
            }
            return;
        }

        NoteTokenParts note;
        if (parseNoteTokenParts(token, &note) && note.valid) {
            ++stats.eligibleObjects;
            if (note.hasEx) {
                ++stats.flaggedObjects;
            }
        }
    });
    return stats;
}

ToggleStats collectFireworkStats(const QString& input)
{
    ToggleStats stats;
    scanSelectionTokens(input, [&stats](const QString& token) {
        TouchTokenParts touch;
        if (parseTouchTokenParts(token, &touch) && touch.valid) {
            ++stats.eligibleObjects;
            if (touch.hasFirework) {
                ++stats.flaggedObjects;
            }
        }
    });
    return stats;
}

QString toggleBreakToken(const QString& token, bool enable, int* changedCount)
{
    if (isSimpleDigitCluster(token)) {
        if (!enable) {
            return token;
        }
        QStringList expanded;
        expanded.reserve(token.size());
        for (QChar lane : token) {
            expanded.append(QString(lane) + QChar('b'));
        }
        if (changedCount != nullptr) {
            *changedCount += token.size();
        }
        return expanded.join(QChar('/'));
    }

    TouchTokenParts touch;
    if (parseTouchTokenParts(token, &touch) && touch.valid) {
        if (touch.hasHold) {
            return token;
        }
        const QString rebuilt = buildTouchToken(touch, enable, touch.hasEx, touch.hasFirework);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    SlideTokenParts slide;
    if (parseSlideTokenParts(token, &slide) && slide.valid) {
        // Count how many break flags would change
        int delta = 0;
        if (slide.headBreak != enable) ++delta;
        for (const auto& seg : slide.segments) {
            if (seg.segmentBreak != enable) ++delta;
        }

        slide.headBreak = enable;
        for (auto& seg : slide.segments) {
            seg.segmentBreak = enable;
        }

        const QString rebuilt = buildSlideToken(slide);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += delta;
        }
        return rebuilt;
    }

    NoteTokenParts note;
    if (parseNoteTokenParts(token, &note) && note.valid) {
        const QString rebuilt = buildNoteToken(note, enable, note.hasEx);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    return token;
}

QString toggleExToken(const QString& token, bool enable, int* changedCount)
{
    if (isSimpleDigitCluster(token)) {
        if (!enable) {
            return token;
        }
        QStringList expanded;
        expanded.reserve(token.size());
        for (QChar lane : token) {
            expanded.append(QString(lane) + QChar('x'));
        }
        if (changedCount != nullptr) {
            *changedCount += token.size();
        }
        return expanded.join(QChar('/'));
    }

    SlideTokenParts slide;
    if (parseSlideTokenParts(token, &slide) && slide.valid) {
        slide.headEx = enable;
        const QString rebuilt = buildSlideToken(slide);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    NoteTokenParts note;
    if (parseNoteTokenParts(token, &note) && note.valid) {
        const QString rebuilt = buildNoteToken(note, note.hasBreak, enable);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    return token;
}

QString toggleFireworkToken(const QString& token, bool enable, int* changedCount)
{
    TouchTokenParts touch;
    if (parseTouchTokenParts(token, &touch) && touch.valid) {
        const QString rebuilt = buildTouchToken(touch, touch.hasBreak, touch.hasEx, enable);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }
    return token;
}

QString rotateRandomToken(const QString& token, const std::function<int()>& nextStep, int* changedCount)
{
    if (!nextStep) {
        return token;
    }

    if (isSimpleDigitCluster(token)) {
        QString rotated = token;
        for (int i = 0; i < rotated.size(); ++i) {
            rotated[i] = rotateLaneChar(rotated.at(i), nextStep());
        }
        if (rotated != token && changedCount != nullptr) {
            int delta = 0;
            for (int i = 0; i < token.size(); ++i) {
                if (token.at(i) != rotated.at(i)) {
                    ++delta;
                }
            }
            *changedCount += delta;
        }
        return rotated;
    }

    TouchTokenParts touch;
    if (parseTouchTokenParts(token, &touch) && touch.valid) {
        const int step = nextStep();
        QString rebuilt = buildTouchToken(touch, touch.hasBreak, touch.hasEx, touch.hasFirework);
        if (touch.prefix.size() >= 2 && isDigitLane(touch.prefix.at(1))) {
            rebuilt[1] = rotateLaneChar(rebuilt.at(1), step);
        }
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    SlideTokenParts slide;
    if (parseSlideTokenParts(token, &slide) && slide.valid) {
        const int step = nextStep();
        for (auto& seg : slide.segments) {
            seg.text = rotateSlideCoreOutsideBrackets(seg.text, step);
        }
        const QString rebuilt = buildSlideToken(slide);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    NoteTokenParts note;
    if (parseNoteTokenParts(token, &note) && note.valid) {
        const int step = nextStep();
        QString rebuilt = buildNoteToken(note, note.hasBreak, note.hasEx);
        rebuilt[0] = rotateLaneChar(rebuilt.at(0), step);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    return token;
}

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
    // Emit [spanStart, spanEnd) with the note tokens removed or reduced to a
    // single lane-1 tap, but every {…}/(…)
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

}  // namespace miacode::chart_transform::detail

namespace miacode::chart_transform {

using namespace detail;

QString resetTapNotesInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    int* changedCount)
{
    return detail::transformCompleteElementsInSelection(
        text, selectionStart, selectionEnd, true, changedCount);
}

QString toggleBreakForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const SelectionEdgeSplit split = splitSelectionEdges(input);
    const ToggleStats stats = collectBreakStats(split.core);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionCore(split, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return toggleBreakToken(token, enable, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString toggleExForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const SelectionEdgeSplit split = splitSelectionEdges(input);
    const ToggleStats stats = collectExStats(split.core);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionCore(split, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return toggleExToken(token, enable, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString toggleFireworkForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const SelectionEdgeSplit split = splitSelectionEdges(input);
    const ToggleStats stats = collectFireworkStats(split.core);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionCore(split, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return toggleFireworkToken(token, enable, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString randomRotateForSelection(const QString& input, int* changedCount)
{
    return randomRotateForSelection(
        input,
        []() {
            return QRandomGenerator::global()->bounded(8);
        },
        changedCount
    );
}

QString randomRotateForSelection(const QString& input, const std::function<int()>& nextStep, int* changedCount)
{
    int changed = 0;
    const QString output = rewriteSelectionCore(input, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return rotateRandomToken(token, nextStep, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString clearCompleteElementsInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
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
    const auto appendClearedNoteSpan = [&](int spanStart, int spanEnd) {
        bool droppedNote = false;
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
                // Note token — cleared.
                droppedNote = true;
                ++pos;
            }
        }
        return droppedNote;
    };
    const auto appendClearedSegment = [&](int commaIndex) {
        const int fullPrefixEnd = leadingPrefixEnd(segmentStart, commaIndex);
        const int partialPrefixEnd = partialLeadingPrefixEnd(segmentStart, commaIndex);
        const int prefixEnd = qMax(fullPrefixEnd, partialPrefixEnd);
        const bool canClear = commaIndex > prefixEnd
            && (isElementBoundary(segmentStart) || partialPrefixEnd > segmentStart);
        output.append(text.mid(segmentStart, prefixEnd - segmentStart));
        if (canClear) {
            if (appendClearedNoteSpan(prefixEnd, commaIndex)) {
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

}  // namespace miacode::chart_transform
