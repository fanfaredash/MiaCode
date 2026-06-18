#include "timeline/TimelineQuickModel.h"

#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <limits>
#include <utility>

#include "timeline/TimelineQuickModelPrivate.h"

using namespace miacode::timeline::tqm_detail;

namespace {

// Skips a leading `||` comment (runs to end of line) so a comment sitting before the first
// note of a span — e.g. a lead-in label above `{16} 1,2,` — is not highlighted. Advances the
// cursor to the comment's terminating newline (left for the whitespace pass to consume) or to
// the span limit when the comment reaches it. Rebuild-time only; no per-tick cost.
bool tryConsumeLeadingFollowComment(const QString& text, int limit, int* cursor)
{
    if (cursor == nullptr || *cursor < 0 || *cursor + 1 >= limit || limit > text.size()) {
        return false;
    }
    if (text.at(*cursor) != QLatin1Char('|') || text.at(*cursor + 1) != QLatin1Char('|')) {
        return false;
    }
    const int newline = text.indexOf(QLatin1Char('\n'), *cursor + 2);
    *cursor = (newline >= 0 && newline < limit) ? newline : limit;
    return true;
}

}  // namespace

void TimelineQuickModel::rebuildAnchorLineIndices()
{
    cursorAnchorsBySecond_.clear();
    linesWithNotes_.clear();
    linesWithNotes_.reserve(lines_.size());

    int totalAnchorCount = 0;
    for (const LineState& line : lines_) {
        totalAnchorCount += line.cursorCache.segmentStarts.size();
    }
    cursorAnchorsBySecond_.reserve(totalAnchorCount);

    for (int index = 0; index < lines_.size(); ++index) {
        const LineState& line = lines_.at(index);
        for (const TimelineCursorAnchor& anchor : line.cursorCache.segmentStarts) {
            AbsoluteCursorAnchor absoluteAnchor;
            absoluteAnchor.lineNumber = line.lineNumber;
            absoluteAnchor.sourceCol = anchor.sourceCol;
            absoluteAnchor.second = qMax(0.0, timelineRenderAbsoluteSecond(line.render, anchor.secondOffset));
            cursorAnchorsBySecond_.append(absoluteAnchor);
        }
        if (line.hasNotes) {
            linesWithNotes_.append(index);
        }
    }

    std::sort(
        cursorAnchorsBySecond_.begin(),
        cursorAnchorsBySecond_.end(),
        [](const AbsoluteCursorAnchor& left, const AbsoluteCursorAnchor& right) {
            if (qAbs(left.second - right.second) > kTimelineEpsilon) {
                return left.second < right.second;
            }
            if (left.lineNumber != right.lineNumber) {
                return left.lineNumber < right.lineNumber;
            }
            return left.sourceCol < right.sourceCol;
        });
}

bool TimelineQuickModel::resolvePreviousCursorAnchorForTextPosition(
    int lineNumber,
    int sourceCol,
    int* line,
    int* col,
    double* second) const
{
    if (!lines_.isEmpty()) {
        const int targetLineIndex = qBound(0, lineNumber - 1, lines_.size() - 1);
        for (int lineIndex = targetLineIndex; lineIndex >= 0; --lineIndex) {
            const LineState& lineState = lines_.at(lineIndex);
            if (lineState.cursorCache.segmentStarts.isEmpty()) {
                continue;
            }

            const QVector<TimelineCursorAnchor>& anchors = lineState.cursorCache.segmentStarts;
            const TimelineCursorAnchor* resolvedAnchor = nullptr;
            if (lineIndex == targetLineIndex) {
                const auto it = std::upper_bound(
                    anchors.cbegin(),
                    anchors.cend(),
                    sourceCol,
                    [](int value, const TimelineCursorAnchor& anchor) { return value < anchor.sourceCol; });
                if (it != anchors.cbegin()) {
                    resolvedAnchor = &(*std::prev(it));
                }
            } else {
                resolvedAnchor = &anchors.constLast();
            }

            if (resolvedAnchor == nullptr) {
                continue;
            }
            if (line != nullptr) {
                *line = lineState.lineNumber;
            }
            if (col != nullptr) {
                *col = resolvedAnchor->sourceCol;
            }
            if (second != nullptr) {
                *second = qMax(0.0, timelineRenderAbsoluteSecond(lineState.render, resolvedAnchor->secondOffset));
            }
            return true;
        }
    }

    if (line != nullptr) {
        *line = 1;
    }
    if (col != nullptr) {
        *col = 1;
    }
    if (second != nullptr) {
        *second = 0.0;
    }
    return false;
}

bool TimelineQuickModel::resolvePreviousCursorAnchorForSecond(
    double second,
    int* line,
    int* col,
    double* anchorSecond) const
{
    if (cursorAnchorsBySecond_.isEmpty()) {
        if (line != nullptr) {
            *line = 1;
        }
        if (col != nullptr) {
            *col = 1;
        }
        if (anchorSecond != nullptr) {
            *anchorSecond = 0.0;
        }
        return false;
    }

    const double targetSecond = qMax(0.0, second);
    const auto endIt = std::upper_bound(
        cursorAnchorsBySecond_.cbegin(),
        cursorAnchorsBySecond_.cend(),
        targetSecond + kTimelineEpsilon,
        [](double target, const AbsoluteCursorAnchor& anchor) {
            return target < anchor.second;
        });
    if (endIt == cursorAnchorsBySecond_.cbegin()) {
        if (line != nullptr) {
            *line = 1;
        }
        if (col != nullptr) {
            *col = 1;
        }
        if (anchorSecond != nullptr) {
            *anchorSecond = 0.0;
        }
        return false;
    }

    const AbsoluteCursorAnchor& bestAnchor = *std::prev(endIt);
    if (line != nullptr) {
        *line = bestAnchor.lineNumber;
    }
    if (col != nullptr) {
        *col = bestAnchor.sourceCol;
    }
    if (anchorSecond != nullptr) {
        *anchorSecond = bestAnchor.second;
    }
    return true;
}

void TimelineQuickModel::rebuildFollowSelectionRanges(LineState* lineState) const
{
    if (lineState == nullptr) {
        return;
    }

    lineState->cursorCache.followSelectionRanges.clear();
    lineState->cursorCache.followSelectionRanges.reserve(lineState->cursorCache.segmentStarts.size());

    const QString& text = lineState->text;
    for (const TimelineCursorAnchor& anchor : lineState->cursorCache.segmentStarts) {
        LineCursorCache::FollowSelectionRange range;
        range.anchorCol = qMax(1, anchor.sourceCol);
        range.startCol = range.anchorCol;
        range.endCol = range.anchorCol;
        trimFollowSelectionSegment(text, range.anchorCol, &range.startCol, &range.endCol);
        lineState->cursorCache.followSelectionRanges.append(range);
    }
}

void TimelineQuickModel::rebuildFollowSelectionSpans()
{
    previewFollowBindings_.clear();
    for (LineState& lineState : lines_) {
        lineState.cursorCache.followSelectionSpans.clear();
        lineState.cursorCache.followSelectionSpans.reserve(lineState.cursorCache.segmentStarts.size());
    }
    if (lines_.isEmpty()) {
        return;
    }

    struct DocumentAnchorEntry {
        int lineIndex = 0;
        int anchorCol = 1;
        double second = 0.0;
        int position = 0;
    };

    QVector<DocumentAnchorEntry> anchors;
    int totalAnchorCount = 0;
    for (const LineState& lineState : lines_) {
        totalAnchorCount += lineState.cursorCache.segmentStarts.size();
    }
    anchors.reserve(totalAnchorCount);

    for (int lineIndex = 0; lineIndex < lines_.size(); ++lineIndex) {
        const LineState& lineState = lines_.at(lineIndex);
        for (const TimelineCursorAnchor& anchor : lineState.cursorCache.segmentStarts) {
            DocumentAnchorEntry entry;
            entry.lineIndex = lineIndex;
            entry.anchorCol = qMax(1, anchor.sourceCol);
            entry.second = qMax(0.0, timelineRenderAbsoluteSecond(lineState.render, anchor.secondOffset));
            entry.position = lineState.startPosition + qMax(0, entry.anchorCol - 1);
            anchors.append(entry);
        }
    }
    if (anchors.isEmpty()) {
        return;
    }
    previewFollowBindings_.reserve(anchors.size());

    int documentTextEndExclusive = 0;
    for (int lineIndex = lines_.size() - 1; lineIndex >= 0; --lineIndex) {
        const LineState& lineState = lines_.at(lineIndex);
        const bool pureTerminalOnly = lineState.isTerminalE
            && lineState.render.beats.isEmpty()
            && lineState.render.notes.isEmpty()
            && lineState.render.measureLineSecondOffsets.isEmpty()
            && qAbs(lineState.render.endSecond - lineState.render.startSecond) <= kTimelineEpsilon
            && qAbs(lineState.terminalSecond - lineState.render.startSecond) <= kTimelineEpsilon;
        if (pureTerminalOnly) {
            continue;
        }
        documentTextEndExclusive = lineState.startPosition + lineState.text.size();
        break;
    }

    const auto resolveVisibleCharLineCol = [this](int position) -> std::pair<int, int> {
        if (lines_.isEmpty()) {
            return {1, 1};
        }
        const int lineIndex = qBound(0, lineIndexForStoredPosition(position), lines_.size() - 1);
        const LineState& lineState = lines_.at(lineIndex);
        const int maxOffset = qMax(0, lineState.text.size() - 1);
        const int localOffset = qBound(0, position - lineState.startPosition, maxOffset);
        return {lineState.lineNumber, localOffset + 1};
    };
    const auto resolveCaretLineCol = [this](int position) -> std::pair<int, int> {
        if (lines_.isEmpty()) {
            return {1, 1};
        }
        const int lineIndex = qBound(0, lineIndexForStoredPosition(position), lines_.size() - 1);
        const LineState& lineState = lines_.at(lineIndex);
        const int localOffset = qMax(0, position - lineState.startPosition);
        return {lineState.lineNumber, localOffset + 1};
    };
    const auto fallbackSpanForAnchor = [&](const DocumentAnchorEntry& anchor) {
        const LineState& lineState = lines_.at(anchor.lineIndex);
        PreviewFollowSpan span;
        span.startLine = lineState.lineNumber;
        span.startCol = anchor.anchorCol;
        span.endLine = lineState.lineNumber;
        span.endCol = anchor.anchorCol;
        span.hasVisibleBody = false;

        const int lineLength = lineState.text.size();
        const int startOffset = lineLength > 0
            ? qBound(0, anchor.anchorCol - 1, lineLength - 1)
            : 0;
        int endOffsetExclusive = lineLength > 0
            ? qBound(1, anchor.anchorCol, lineLength)
            : 0;

        span.startPosition = lineState.startPosition + startOffset;
        span.endPositionExclusive = lineState.startPosition + endOffsetExclusive;
        if (lineLength > 0 && span.endPositionExclusive <= span.startPosition) {
            span.endPositionExclusive = qMin(lineState.startPosition + lineLength, span.startPosition + 1);
        }
        span.cursorPosition = span.endPositionExclusive;
        const auto [cursorLine, cursorCol] = resolveCaretLineCol(span.cursorPosition);
        span.cursorLine = cursorLine;
        span.cursorCol = cursorCol;
        return span;
    };

    struct FlatFollowSpan {
        QString text;
        QVector<int> positions;
    };

    const auto buildFlatSpan = [this](int startPosition, int endPositionExclusive) {
        FlatFollowSpan flatSpan;
        if (endPositionExclusive <= startPosition || lines_.isEmpty()) {
            flatSpan.positions.append(startPosition);
            return flatSpan;
        }

        const int startLineIndex = qBound(0, lineIndexForStoredPosition(startPosition), lines_.size() - 1);
        const int endLineIndex = qBound(
            startLineIndex,
            lineIndexForStoredPosition(qMax(startPosition, endPositionExclusive - 1)),
            lines_.size() - 1);

        for (int lineIndex = startLineIndex; lineIndex <= endLineIndex; ++lineIndex) {
            const LineState& lineState = lines_.at(lineIndex);
            const int fromOffset = (lineIndex == startLineIndex)
                ? qBound(0, startPosition - lineState.startPosition, lineState.text.size())
                : 0;
            const int toOffset = (lineIndex == endLineIndex)
                ? qBound(fromOffset, endPositionExclusive - lineState.startPosition, lineState.text.size())
                : lineState.text.size();
            for (int offset = fromOffset; offset < toOffset; ++offset) {
                flatSpan.positions.append(lineState.startPosition + offset);
                flatSpan.text.append(lineState.text.at(offset));
            }

            const bool includeNewline = lineIndex < endLineIndex
                && endPositionExclusive > lineState.startPosition + lineState.text.size();
            if (includeNewline) {
                flatSpan.positions.append(lineState.startPosition + lineState.text.size());
                flatSpan.text.append(QLatin1Char('\n'));
            }
        }

        flatSpan.positions.append(endPositionExclusive);
        return flatSpan;
    };

    for (int index = 0; index < anchors.size();) {
        int runEnd = index + 1;
        while (runEnd < anchors.size()
               && qAbs(anchors.at(runEnd).second - anchors.at(index).second) <= kTimelineEpsilon) {
            ++runEnd;
        }

        const int rawStartPosition = anchors.at(index).position;
        const int rawEndExclusive = runEnd < anchors.size()
            ? qMax(rawStartPosition, anchors.at(runEnd).position - 1)
            : qMax(rawStartPosition, documentTextEndExclusive);
        const FlatFollowSpan flatSpan = buildFlatSpan(rawStartPosition, rawEndExclusive);

        int trimmedStartIndex = 0;
        int trimmedEndIndex = flatSpan.text.size();
        bool trimmedLeading = true;
        while (trimmedLeading) {
            trimmedLeading = false;
            while (trimmedStartIndex < trimmedEndIndex && flatSpan.text.at(trimmedStartIndex).isSpace()) {
                ++trimmedStartIndex;
                trimmedLeading = true;
            }
            if (tryConsumeLeadingFollowControlToken(flatSpan.text, trimmedEndIndex, &trimmedStartIndex)) {
                trimmedLeading = true;
            }
            if (tryConsumeLeadingFollowComment(flatSpan.text, trimmedEndIndex, &trimmedStartIndex)) {
                trimmedLeading = true;
            }
        }

        bool trimmedTrailing = true;
        while (trimmedTrailing) {
            trimmedTrailing = false;
            while (trimmedEndIndex > trimmedStartIndex && flatSpan.text.at(trimmedEndIndex - 1).isSpace()) {
                --trimmedEndIndex;
                trimmedTrailing = true;
            }

            const int commentStart = findTrailingCommentStart(
                flatSpan.text,
                trimmedStartIndex,
                trimmedEndIndex);
            if (commentStart >= 0) {
                trimmedEndIndex = commentStart;
                trimmedTrailing = true;
                continue;
            }

            if (tryConsumeTrailingFollowControlToken(flatSpan.text, trimmedStartIndex, &trimmedEndIndex)) {
                trimmedTrailing = true;
            }
        }

        while (trimmedStartIndex < trimmedEndIndex && flatSpan.text.at(trimmedStartIndex).isSpace()) {
            ++trimmedStartIndex;
        }
        while (trimmedEndIndex > trimmedStartIndex && flatSpan.text.at(trimmedEndIndex - 1).isSpace()) {
            --trimmedEndIndex;
        }

        PreviewFollowSpan resolvedSpan;
        const bool hasVisibleContent = trimmedStartIndex < trimmedEndIndex
            && trimmedStartIndex < flatSpan.positions.size()
            && trimmedEndIndex < flatSpan.positions.size();
        if (hasVisibleContent) {
            resolvedSpan.hasVisibleBody = true;
            resolvedSpan.startPosition = flatSpan.positions.at(trimmedStartIndex);
            resolvedSpan.endPositionExclusive = flatSpan.positions.at(trimmedEndIndex);
            resolvedSpan.cursorPosition = resolvedSpan.endPositionExclusive;
            const auto [startLine, startCol] = resolveVisibleCharLineCol(resolvedSpan.startPosition);
            const auto [endLine, endCol] = resolveVisibleCharLineCol(resolvedSpan.endPositionExclusive - 1);
            const auto [cursorLine, cursorCol] = resolveCaretLineCol(resolvedSpan.cursorPosition);
            resolvedSpan.startLine = startLine;
            resolvedSpan.startCol = startCol;
            resolvedSpan.endLine = endLine;
            resolvedSpan.endCol = endCol;
            resolvedSpan.cursorLine = cursorLine;
            resolvedSpan.cursorCol = cursorCol;
        }

        for (int runIndex = index; runIndex < runEnd; ++runIndex) {
            LineCursorCache::FollowSelectionSpan cachedSpan;
            cachedSpan.anchorCol = anchors.at(runIndex).anchorCol;
            cachedSpan.span = hasVisibleContent ? resolvedSpan : fallbackSpanForAnchor(anchors.at(runIndex));
            lines_[anchors.at(runIndex).lineIndex].cursorCache.followSelectionSpans.append(cachedSpan);
        }

        const DocumentAnchorEntry& effectiveAnchor = anchors.at(runEnd - 1);
        PreviewFollowBinding binding;
        binding.startSecond = anchors.at(index).second;
        binding.endSecondExclusive = runEnd < anchors.size()
            ? anchors.at(runEnd).second
            : std::numeric_limits<double>::infinity();
        binding.anchorSecond = effectiveAnchor.second;
        binding.anchorLine = lines_.at(effectiveAnchor.lineIndex).lineNumber;
        binding.anchorCol = effectiveAnchor.anchorCol;
        binding.span = hasVisibleContent ? resolvedSpan : fallbackSpanForAnchor(effectiveAnchor);
        binding.resolved = true;
        previewFollowBindings_.append(binding);

        index = runEnd;
    }
}
