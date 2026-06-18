#include "timeline/TimelineQuickModel.h"

#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <limits>
#include <utility>

#include "timeline/TimelineQuickModelPrivate.h"

using namespace miacode::timeline::tqm_detail;

namespace {

void appendDistinctSecond(QVector<double>* seconds, double second)
{
    if (seconds == nullptr) {
        return;
    }
    if (!seconds->isEmpty() && qAbs(seconds->constLast() - second) <= kTimelineEpsilon) {
        return;
    }
    seconds->append(second);
}

}  // namespace

void TimelineQuickModel::shiftLineTiming(LineState* lineState, double deltaSeconds) const
{
    if (lineState == nullptr || qAbs(deltaSeconds) <= kTimelineEpsilon) {
        return;
    }
    lineState->startState.second += deltaSeconds;
    lineState->endState.second += deltaSeconds;
    lineState->startState.currentMeasureStartSecond += deltaSeconds;
    lineState->endState.currentMeasureStartSecond += deltaSeconds;
    lineState->render.startSecond += deltaSeconds;
    lineState->render.endSecond += deltaSeconds;
    // A terminal `E` line carries its own absolute terminalSecond used as the
    // measure-line cutoff in rebuildSnapshotDuration; the pure-shift fast path
    // must keep it in sync or a real measure line gets filtered out.
    if (lineState->isTerminalE && lineState->terminalSecond >= 0.0) {
        lineState->terminalSecond += deltaSeconds;
    }
    if (lineState->hasNotes) {
        lineState->firstNoteSecond += deltaSeconds;
        lineState->lastNoteSecond += deltaSeconds;
    }
}

void TimelineQuickModel::rebuildSnapshotDuration()
{
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
    snapshot_.lines.reserve(lines_.size());
    snapshot_.noteVisualEndPrefixMaxWithSlideTracks.reserve(lines_.size());
    snapshot_.noteVisualEndPrefixMaxWithoutSlideTracks.reserve(lines_.size());

    double minSecond = 0.0;
    double maxSecond = 0.0;
    double noteVisualEndPrefixWithSlideTracks = -std::numeric_limits<double>::infinity();
    double noteVisualEndPrefixWithoutSlideTracks = -std::numeric_limits<double>::infinity();
    bool hasData = false;

    int terminalLineIndex = -1;
    double terminalSecond = 0.0;
    for (int index = 0; index < lines_.size(); ++index) {
        if (!lines_.at(index).isTerminalE) {
            continue;
        }
        terminalLineIndex = index;
        terminalSecond = lines_.at(index).terminalSecond >= 0.0
            ? lines_.at(index).terminalSecond
            : lines_.at(index).render.startSecond;
        break;
    }

    const int anchorSearchEnd = terminalLineIndex >= 0 ? terminalLineIndex : lines_.size();
    int trailingLineIndex = -1;
    bool hasRealComma = false;
    for (int index = 0; index < anchorSearchEnd; ++index) {
        const LineState& lineState = lines_.at(index);
        trailingLineIndex = index;
        hasRealComma = hasRealComma || !lineState.render.beats.isEmpty();
    }
    int trailingMeasureLineIndex = -1;
    for (int index = lines_.size() - 1; index >= 0; --index) {
        const LineState& lineState = lines_.at(index);
        const bool pureTerminalOnly = lineState.isTerminalE
            && lineState.render.beats.isEmpty()
            && lineState.render.notes.isEmpty()
            && lineState.render.measureLineSecondOffsets.isEmpty()
            && qAbs(lineState.render.endSecond - lineState.render.startSecond) <= kTimelineEpsilon
            && qAbs(lineState.terminalSecond - lineState.render.startSecond) <= kTimelineEpsilon;
        if (pureTerminalOnly) {
            continue;
        }
        trailingMeasureLineIndex = index;
        break;
    }

    for (int index = 0; index < lines_.size(); ++index) {
        const LineState& lineState = lines_.at(index);
        TimelineRenderLine renderLine = lineState.render;
        if (index == trailingLineIndex && hasRealComma && terminalLineIndex < 0) {
            TimelineRenderBeat trailingBeat;
            trailingBeat.secondOffset = lineState.endState.second - renderLine.startSecond;
            trailingBeat.sourceCol = qMax(1, lineState.text.size() + 1);
            trailingBeat.subdivisionBeats = qMax(1, lineState.endState.beats);
            trailingBeat.subdivisionIndex = qBound(
                0,
                trailingBeat.subdivisionBeats - 1,
                lineState.endState.subdivisionIndex);
            trailingBeat.major = false;
            renderLine.beats.append(trailingBeat);
        }

        snapshot_.lines.append(renderLine);
        for (int measureIndex = 0; measureIndex < renderLine.measureLineSecondOffsets.size(); ++measureIndex) {
            const double absoluteSecond = renderLine.startSecond
                + renderLine.measureLineSecondOffsets.at(measureIndex);
            if (terminalLineIndex >= 0 && absoluteSecond > terminalSecond + kTimelineEpsilon) {
                continue;
            }
            const int numerator = measureIndex < renderLine.measureLineMeterNumerators.size()
                ? qMax(1, renderLine.measureLineMeterNumerators.at(measureIndex))
                : 4;
            const int denominator = measureIndex < renderLine.measureLineMeterDenominators.size()
                ? qMax(1, renderLine.measureLineMeterDenominators.at(measureIndex))
                : 4;
            const double beatStep = measureIndex < renderLine.measureLineBeatStepSeconds.size()
                ? renderLine.measureLineBeatStepSeconds.at(measureIndex)
                : 0.0;
            if (!snapshot_.measureLineSeconds.isEmpty()
                && qAbs(snapshot_.measureLineSeconds.constLast() - absoluteSecond) <= kTimelineEpsilon) {
                // Boundary-coincident restart spanning a line break: the LATER
                // entry's meter governs the span after the shared line.
                snapshot_.measureLineMeterNumerators.last() = numerator;
                snapshot_.measureLineMeterDenominators.last() = denominator;
                snapshot_.measureLineBeatStepSeconds.last() = beatStep;
                continue;
            }
            snapshot_.measureLineSeconds.append(absoluteSecond);
            snapshot_.measureLineMeterNumerators.append(numerator);
            snapshot_.measureLineMeterDenominators.append(denominator);
            snapshot_.measureLineBeatStepSeconds.append(beatStep);
        }

        minSecond = hasData ? qMin(minSecond, renderLine.startSecond) : renderLine.startSecond;
        maxSecond = hasData ? qMax(maxSecond, renderLine.endSecond) : renderLine.endSecond;
        noteVisualEndPrefixWithSlideTracks = qMax(
            noteVisualEndPrefixWithSlideTracks,
            timelineRenderLineVisualEndSecond(renderLine, true)
        );
        noteVisualEndPrefixWithoutSlideTracks = qMax(
            noteVisualEndPrefixWithoutSlideTracks,
            timelineRenderLineVisualEndSecond(renderLine, false)
        );
        snapshot_.noteVisualEndPrefixMaxWithSlideTracks.append(noteVisualEndPrefixWithSlideTracks);
        snapshot_.noteVisualEndPrefixMaxWithoutSlideTracks.append(noteVisualEndPrefixWithoutSlideTracks);
        hasData = true;
    }

    if (trailingMeasureLineIndex >= 0) {
        const LineState& trailingLine = lines_.at(trailingMeasureLineIndex);
        const double measureDuration = measureDurationSeconds(
            trailingLine.endState.bpm,
            trailingLine.endState.meterNumerator,
            trailingLine.endState.meterDenominator);
        if (qIsFinite(measureDuration) && measureDuration > kTimelineEpsilon) {
            snapshot_.trailingMeasureLineStartSecond = trailingLine.endState.currentMeasureStartSecond;
            snapshot_.trailingMeasureLineStepSeconds = measureDuration;
            snapshot_.trailingMeasureLineMeterNumerator = qMax(1, trailingLine.endState.meterNumerator);
            snapshot_.trailingMeasureLineMeterDenominator = qMax(1, trailingLine.endState.meterDenominator);
            const double nextMeasureSecond = trailingLine.endState.currentMeasureStartSecond + measureDuration;
            if (qIsFinite(nextMeasureSecond)
                && nextMeasureSecond > trailingLine.endState.currentMeasureStartSecond + kTimelineEpsilon) {
                const double trailingBeatStep = noteStepSeconds(
                    trailingLine.endState.bpm,
                    trailingLine.endState.meterDenominator);
                const int sizeBefore = snapshot_.measureLineSeconds.size();
                appendDistinctSecond(&snapshot_.measureLineSeconds, nextMeasureSecond);
                if (snapshot_.measureLineSeconds.size() != sizeBefore) {
                    snapshot_.measureLineMeterNumerators.append(snapshot_.trailingMeasureLineMeterNumerator);
                    snapshot_.measureLineMeterDenominators.append(snapshot_.trailingMeasureLineMeterDenominator);
                    snapshot_.measureLineBeatStepSeconds.append(trailingBeatStep);
                }
                if (hasData) {
                    maxSecond = qMax(maxSecond, nextMeasureSecond);
                    minSecond = qMin(minSecond, nextMeasureSecond);
                } else {
                    minSecond = nextMeasureSecond;
                    maxSecond = nextMeasureSecond;
                    hasData = true;
                }
            }
        }
    }
    if (trailingLineIndex >= 0) {
        const LineState& trailingLine = lines_.at(trailingLineIndex);
        if (hasRealComma && terminalLineIndex < 0) {
            maxSecond = qMax(maxSecond, trailingLine.endState.second);
            minSecond = hasData ? qMin(minSecond, trailingLine.endState.second) : trailingLine.endState.second;
            hasData = true;
        }
    }

    snapshot_.durationSeconds = hasData ? qMax(0.0, maxSecond) : 0.0;
    snapshot_.minimumSecond = hasData ? minSecond : -0.5;
    snapshot_.maximumSecond = hasData ? qMax(maxSecond, snapshot_.durationSeconds) : 1.0;
}

void TimelineQuickModel::resequenceLineMetadata(int startIndex)
{
    int nextStartPosition = 0;
    if (startIndex > 0 && startIndex <= lines_.size()) {
        const LineState& previousLine = lines_.at(startIndex - 1);
        nextStartPosition = previousLine.startPosition + previousLine.text.size() + 1;
    }

    for (int index = startIndex; index < lines_.size(); ++index) {
        LineState& line = lines_[index];
        line.lineNumber = index + 1;
        line.startPosition = nextStartPosition;
        line.render.lineNumber = line.lineNumber;
        line.render.startPosition = line.startPosition;
        nextStartPosition += line.text.size() + 1;
    }
}
