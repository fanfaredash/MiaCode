#include "timeline/TimelineQuickModel.h"

#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <limits>
#include <utility>

#include "timeline/TimelineQuickModelPrivate.h"

using namespace miacode::timeline::tqm_detail;

namespace {

constexpr int kDefaultMeterNumerator = 4;
constexpr int kDefaultMeterDenominator = 4;

void applyInitialTimingMetadata(
    int* meterNumerator,
    int* meterDenominator,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    if (meterNumerator == nullptr || meterDenominator == nullptr) {
        return;
    }
    *meterNumerator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureNumerator
        : kDefaultMeterNumerator;
    *meterDenominator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureDenominator
        : kDefaultMeterDenominator;
}

QString lineTextForBlock(const QTextBlock& block)
{
    QString text = block.text();
    if (text.endsWith(QLatin1Char('\r'))) {
        text.chop(1);
    }
    return text;
}

}  // namespace

void TimelineQuickModel::clear()
{
    nextLineId_ = 1;
    nextEachGroupId_ = 0;
    lines_.clear();
    snapshot_.lines.clear();
    snapshot_.measureLineSeconds.clear();
    snapshot_.measureLineMeterNumerators.clear();
    snapshot_.measureLineMeterDenominators.clear();
    snapshot_.measureLineBeatStepSeconds.clear();
    snapshot_.noteVisualEndPrefixMaxWithSlideTracks.clear();
    snapshot_.noteVisualEndPrefixMaxWithoutSlideTracks.clear();
    snapshot_.trailingMeasureLineStartSecond = 0.0;
    snapshot_.trailingMeasureLineStepSeconds = 0.0;
    snapshot_.trailingMeasureLineMeterNumerator = 4;
    snapshot_.trailingMeasureLineMeterDenominator = 4;
    snapshot_.durationSeconds = 0.0;
    snapshot_.minimumSecond = -0.5;
    snapshot_.maximumSecond = 1.0;
    cursorAnchorsBySecond_.clear();
    previewFollowBindings_.clear();
    linesWithNotes_.clear();
}

bool TimelineQuickModel::replaceDocumentTail(
    const QTextDocument* document,
    int startLineIndex,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    Q_UNUSED(startLineIndex);
    return rebuildFromDocument(document, firstSeconds, timingMetadata);
}

bool TimelineQuickModel::rebuildFromText(
    const QString& text,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    QVector<QString> lines = text.split(QLatin1Char('\n')).toVector();
    if (lines.isEmpty()) {
        lines.append(QString());
    }
    return rebuildFromLineTexts(lines, firstSeconds, timingMetadata);
}

bool TimelineQuickModel::rebuildFromDocument(
    const QTextDocument* document,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    return rebuildFromLineTexts(collectDocumentLines(document), firstSeconds, timingMetadata);
}

bool TimelineQuickModel::applyContentsChange(
    const QTextDocument* document,
    int position,
    int charsRemoved,
    int charsAdded,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    if (document == nullptr || lines_.isEmpty()) {
        return rebuildFromDocument(document, firstSeconds, timingMetadata);
    }

    const int startLineIndex = lineIndexForStoredPosition(position);
    const int oldEndLineIndex = charsRemoved > 0
        ? lineIndexForStoredPosition(position + charsRemoved)
        : startLineIndex;
    const int newEndLineIndex = charsAdded > 0
        ? lineIndexForDocumentPosition(document, position + charsAdded)
        : startLineIndex;
    const QVector<QString> replacementLines = collectDocumentLines(document, startLineIndex, newEndLineIndex);

    QVector<LineState> inserted;
    inserted.reserve(replacementLines.size());
    for (const QString& text : replacementLines) {
        LineState line;
        line.lineId = allocateLineId();
        line.text = text;
        inserted.append(line);
    }

    const int replaceCount = qMax(0, oldEndLineIndex - startLineIndex + 1);
    lines_.erase(lines_.begin() + startLineIndex, lines_.begin() + startLineIndex + replaceCount);
    for (int insertIndex = 0; insertIndex < inserted.size(); ++insertIndex) {
        lines_.insert(startLineIndex + insertIndex, inserted.at(insertIndex));
    }
    resequenceLineMetadata(startLineIndex);

    ParseState currentState;
    if (startLineIndex > 0) {
        currentState = lines_.at(startLineIndex - 1).endState;
    } else {
        currentState.second = firstSeconds;
        currentState.bpm = kDefaultBpm;
        currentState.beats = kDefaultBeats;
        currentState.subdivisionIndex = 0;
        applyInitialTimingMetadata(
            &currentState.meterNumerator,
            &currentState.meterDenominator,
            timingMetadata);
        currentState.currentMeasureStartSecond = firstSeconds;
    }

    const int guaranteedReparseEnd = qMin(lines_.size() - 1, startLineIndex + inserted.size() - 1);
    for (int index = startLineIndex; index < lines_.size(); ++index) {
        LineState& line = lines_[index];
        const ParseState oldStartState = line.startState;
        const ParseState oldEndState = line.endState;
        const double secondShift = currentState.second - oldStartState.second;
        const double measureShift =
            currentState.currentMeasureStartSecond - oldStartState.currentMeasureStartSecond;
        const bool mustReparse = index <= guaranteedReparseEnd
            || qAbs(currentState.bpm - oldStartState.bpm) > kTimelineEpsilon
            || currentState.beats != oldStartState.beats
            || currentState.subdivisionIndex != oldStartState.subdivisionIndex
            || currentState.meterNumerator != oldStartState.meterNumerator
            || currentState.meterDenominator != oldStartState.meterDenominator
            || qAbs(secondShift - measureShift) > kTimelineEpsilon;

        if (mustReparse) {
            parseLine(&line, currentState);
        } else {
            shiftLineTiming(&line, secondShift);
            line.startState = currentState;
            line.endState.bpm = oldEndState.bpm;
            line.endState.beats = oldEndState.beats;
            line.endState.subdivisionIndex = oldEndState.subdivisionIndex;
            line.endState.meterNumerator = oldEndState.meterNumerator;
            line.endState.meterDenominator = oldEndState.meterDenominator;
        }

        currentState = line.endState;
        if (index > guaranteedReparseEnd && parseStatesEqual(line.endState, oldEndState)) {
            break;
        }
    }

    rebuildSlideDerivedFlags();
    rebuildAnchorLineIndices();
    rebuildFollowSelectionSpans();
    rebuildSnapshotDuration();
    return true;
}

const TimelineRenderSnapshot& TimelineQuickModel::snapshot() const
{
    return snapshot_;
}

double TimelineQuickModel::timelineSecondForCursor(int lineNumber, int col) const
{
    double second = 0.0;
    resolvePreviousCursorAnchorForTextPosition(lineNumber, col, nullptr, nullptr, &second);
    return second;
}

bool TimelineQuickModel::resolveTimelineSecondForCursor(int lineNumber, int col, double* second) const
{
    return resolvePreviousCursorAnchorForTextPosition(lineNumber, col, nullptr, nullptr, second);
}

bool TimelineQuickModel::resolveTimelineNavigateCursor(double second, int* line, int* col, double* cursorSecond) const
{
    if (resolvePreviousCursorAnchorForSecond(second, line, col, cursorSecond)) {
        return true;
    }

    if (line != nullptr) {
        *line = 1;
    }
    if (col != nullptr) {
        *col = 1;
    }
    if (cursorSecond != nullptr) {
        *cursorSecond = 0.0;
    }
    return false;
}

bool TimelineQuickModel::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    if (linesWithNotes_.isEmpty()) {
        return false;
    }

    struct Candidate {
        bool found = false;
        int line = 1;
        int col = 1;
        double second = 0.0;
        double delta = 0.0;
    };

    const auto inspectLine = [second](const LineState& lineState, int laneFilter, Candidate* best) {
        if (best == nullptr || lineState.render.notes.isEmpty()) {
            return;
        }
        for (const TimelineRenderNote& note : lineState.render.notes) {
            if (laneFilter >= 1 && note.lane != laneFilter) {
                continue;
            }
            const double absoluteSecond = timelineRenderAbsoluteSecond(lineState.render, note.secondOffset);
            const double delta = qAbs(absoluteSecond - second);
            if (!best->found
                || delta + kTimelineEpsilon < best->delta
                || (qAbs(delta - best->delta) <= kTimelineEpsilon
                    && (lineState.lineNumber < best->line
                        || (lineState.lineNumber == best->line && note.sourceCol < best->col)))) {
                best->found = true;
                best->line = lineState.lineNumber;
                best->col = note.sourceCol;
                best->second = absoluteSecond;
                best->delta = delta;
            }
        }
    };

    const auto findCandidate = [&](int laneFilter) -> Candidate {
        Candidate best;
        int right = static_cast<int>(std::lower_bound(
                        linesWithNotes_.cbegin(),
                        linesWithNotes_.cend(),
                        second,
                        [this](int lineIndex, double targetSecond) {
                            return lines_.at(lineIndex).lastNoteSecond < targetSecond;
                        })
                        - linesWithNotes_.cbegin());
        int left = right - 1;
        while (left >= 0 || right < linesWithNotes_.size()) {
            double leftMinDelta = std::numeric_limits<double>::infinity();
            double rightMinDelta = std::numeric_limits<double>::infinity();
            if (left >= 0) {
                const LineState& lineState = lines_.at(linesWithNotes_.at(left));
                if (second < lineState.firstNoteSecond) {
                    leftMinDelta = lineState.firstNoteSecond - second;
                } else if (second > lineState.lastNoteSecond) {
                    leftMinDelta = second - lineState.lastNoteSecond;
                } else {
                    leftMinDelta = 0.0;
                }
            }
            if (right < linesWithNotes_.size()) {
                const LineState& lineState = lines_.at(linesWithNotes_.at(right));
                if (second < lineState.firstNoteSecond) {
                    rightMinDelta = lineState.firstNoteSecond - second;
                } else if (second > lineState.lastNoteSecond) {
                    rightMinDelta = second - lineState.lastNoteSecond;
                } else {
                    rightMinDelta = 0.0;
                }
            }
            if (best.found && leftMinDelta > best.delta + kTimelineEpsilon && rightMinDelta > best.delta + kTimelineEpsilon) {
                break;
            }

            if (rightMinDelta <= leftMinDelta) {
                if (right < linesWithNotes_.size()) {
                    inspectLine(lines_.at(linesWithNotes_.at(right)), laneFilter, &best);
                    ++right;
                } else {
                    inspectLine(lines_.at(linesWithNotes_.at(left)), laneFilter, &best);
                    --left;
                }
            } else {
                inspectLine(lines_.at(linesWithNotes_.at(left)), laneFilter, &best);
                --left;
            }
        }
        return best;
    };

    Candidate best = (lane >= 1) ? findCandidate(lane) : Candidate();
    if (!best.found) {
        best = findCandidate(-1);
    }
    if (!best.found) {
        return false;
    }

    if (line != nullptr) {
        *line = best.line;
    }
    if (col != nullptr) {
        *col = best.col;
    }
    if (noteSecond != nullptr) {
        *noteSecond = best.second;
    }
    return true;
}

bool TimelineQuickModel::resolvePreviewFollowSelectionRange(int line, int anchorCol, int* startCol, int* endCol) const
{
    if (startCol != nullptr) {
        *startCol = qMax(1, anchorCol);
    }
    if (endCol != nullptr) {
        *endCol = qMax(1, anchorCol);
    }

    if (line < 1 || line > lines_.size()) {
        return false;
    }

    const LineState& lineState = lines_.at(line - 1);
    const auto followRangeIt = std::lower_bound(
        lineState.cursorCache.followSelectionRanges.cbegin(),
        lineState.cursorCache.followSelectionRanges.cend(),
        anchorCol,
        [](const LineCursorCache::FollowSelectionRange& range, int value) {
            return range.anchorCol < value;
        });
    if (followRangeIt != lineState.cursorCache.followSelectionRanges.cend()
        && followRangeIt->anchorCol == anchorCol) {
        if (startCol != nullptr) {
            *startCol = followRangeIt->startCol;
        }
        if (endCol != nullptr) {
            *endCol = followRangeIt->endCol;
        }
        return true;
    }

    trimFollowSelectionSegment(lineState.text, anchorCol, startCol, endCol);
    return true;
}

bool TimelineQuickModel::resolvePreviewFollowCursor(
    double second,
    int* line,
    int* col,
    double* noteSecond) const
{
    PreviewFollowBinding binding;
    if (resolvePreviewFollowBinding(second, &binding)) {
        if (line != nullptr) {
            *line = binding.anchorLine;
        }
        if (col != nullptr) {
            *col = binding.anchorCol;
        }
        if (noteSecond != nullptr) {
            *noteSecond = binding.anchorSecond;
        }
        return true;
    }

    if (line != nullptr) {
        *line = 1;
    }
    if (col != nullptr) {
        *col = 1;
    }
    if (noteSecond != nullptr) {
        *noteSecond = 0.0;
    }
    return false;
}

bool TimelineQuickModel::resolvePreviewFollowSpan(
    double second,
    PreviewFollowSpan* span,
    double* anchorSecond) const
{
    if (span != nullptr) {
        *span = PreviewFollowSpan();
    }

    PreviewFollowBinding binding;
    if (!resolvePreviewFollowBinding(second, &binding)) {
        if (anchorSecond != nullptr) {
            *anchorSecond = 0.0;
        }
        return false;
    }

    if (anchorSecond != nullptr) {
        *anchorSecond = binding.anchorSecond;
    }
    if (span != nullptr) {
        *span = binding.span;
    }
    return binding.resolved;
}

bool TimelineQuickModel::resolvePreviewFollowBinding(double second, PreviewFollowBinding* binding) const
{
    if (binding != nullptr) {
        *binding = PreviewFollowBinding();
    }
    if (previewFollowBindings_.isEmpty()) {
        return false;
    }

    const double targetSecond = qMax(0.0, second);
    const auto endIt = std::upper_bound(
        previewFollowBindings_.cbegin(),
        previewFollowBindings_.cend(),
        targetSecond + kTimelineEpsilon,
        [](double target, const PreviewFollowBinding& entry) {
            return target < entry.startSecond;
        });
    if (endIt == previewFollowBindings_.cbegin()) {
        return false;
    }

    const PreviewFollowBinding& candidate = *std::prev(endIt);
    if (qIsFinite(candidate.endSecondExclusive)
        && targetSecond + kTimelineEpsilon >= candidate.endSecondExclusive) {
        return false;
    }
    if (binding != nullptr) {
        *binding = candidate;
    }
    return candidate.resolved;
}

bool TimelineQuickModel::resolvePreviewFollowSelection(
    double second,
    int* line,
    int* startCol,
    int* endCol,
    double* anchorSecond) const
{
    int resolvedLine = 1;
    int anchorCol = 1;
    if (!resolvePreviewFollowCursor(second, &resolvedLine, &anchorCol, anchorSecond)) {
        if (line != nullptr) {
            *line = 1;
        }
        if (startCol != nullptr) {
            *startCol = 1;
        }
        if (endCol != nullptr) {
            *endCol = 1;
        }
        return false;
    }

    int resolvedStartCol = anchorCol;
    int resolvedEndCol = anchorCol;
    resolvePreviewFollowSelectionRange(resolvedLine, anchorCol, &resolvedStartCol, &resolvedEndCol);
    if (line != nullptr) {
        *line = resolvedLine;
    }
    if (startCol != nullptr) {
        *startCol = resolvedStartCol;
    }
    if (endCol != nullptr) {
        *endCol = resolvedEndCol;
    }
    return true;
}

bool TimelineQuickModel::rebuildFromLineTexts(
    const QVector<QString>& lines,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    clear();
    if (lines.isEmpty()) {
        return true;
    }

    lines_.reserve(lines.size());
    ParseState state;
    state.second = firstSeconds;
    state.bpm = kDefaultBpm;
    state.beats = kDefaultBeats;
    state.subdivisionIndex = 0;
    applyInitialTimingMetadata(&state.meterNumerator, &state.meterDenominator, timingMetadata);
    state.currentMeasureStartSecond = firstSeconds;
    int startPosition = 0;
    for (int index = 0; index < lines.size(); ++index) {
        LineState line;
        line.lineId = allocateLineId();
        line.lineNumber = index + 1;
        line.startPosition = startPosition;
        line.text = lines.at(index);
        parseLine(&line, state);
        state = line.endState;
        startPosition += line.text.size() + 1;
        lines_.append(line);
    }

    rebuildSlideDerivedFlags();
    rebuildAnchorLineIndices();
    rebuildFollowSelectionSpans();
    rebuildSnapshotDuration();
    return true;
}

QVector<QString> TimelineQuickModel::collectDocumentLines(const QTextDocument* document) const
{
    if (document == nullptr) {
        return {QString()};
    }
    return collectDocumentLines(document, 0, qMax(0, document->blockCount() - 1));
}

QVector<QString> TimelineQuickModel::collectDocumentLines(
    const QTextDocument* document,
    int startLineIndex,
    int endLineIndex) const
{
    QVector<QString> lines;
    if (document == nullptr) {
        lines.append(QString());
        return lines;
    }

    const int boundedStart = qMax(0, startLineIndex);
    const int boundedEnd = qMax(boundedStart, endLineIndex);
    QTextBlock block = document->findBlockByNumber(boundedStart);
    int currentLine = boundedStart;
    while (block.isValid() && currentLine <= boundedEnd) {
        lines.append(lineTextForBlock(block));
        block = block.next();
        ++currentLine;
    }
    if (lines.isEmpty()) {
        lines.append(QString());
    }
    return lines;
}

int TimelineQuickModel::lineIndexForDocumentPosition(const QTextDocument* document, int position) const
{
    if (document == nullptr) {
        return 0;
    }
    const int boundedPosition = qBound(0, position, document->characterCount());
    const QTextBlock block = document->findBlock(boundedPosition);
    return block.isValid() ? qMax(0, block.blockNumber()) : qMax(0, document->blockCount() - 1);
}

int TimelineQuickModel::lineIndexForStoredPosition(int position) const
{
    if (lines_.isEmpty()) {
        return 0;
    }
    const int boundedPosition = qMax(0, position);
    auto it = std::upper_bound(
        lines_.cbegin(),
        lines_.cend(),
        boundedPosition,
        [](int storedPosition, const LineState& lineState) { return storedPosition < lineState.startPosition; });
    if (it == lines_.cbegin()) {
        return 0;
    }
    return static_cast<int>(std::distance(lines_.cbegin(), std::prev(it)));
}

int TimelineQuickModel::allocateLineId()
{
    return nextLineId_++;
}

bool TimelineQuickModel::parseStatesEqual(const ParseState& a, const ParseState& b)
{
    return qAbs(a.second - b.second) <= kTimelineEpsilon
        && qAbs(a.bpm - b.bpm) <= kTimelineEpsilon
        && a.beats == b.beats
        && a.subdivisionIndex == b.subdivisionIndex
        && a.meterNumerator == b.meterNumerator
        && a.meterDenominator == b.meterDenominator
        && qAbs(a.currentMeasureStartSecond - b.currentMeasureStartSecond) <= kTimelineEpsilon
        && a.initialMeasureLineEmitted == b.initialMeasureLineEmitted;
}
