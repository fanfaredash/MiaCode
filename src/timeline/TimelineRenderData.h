#pragma once

#include <algorithm>
#include <limits>

#include <QString>
#include <QtGlobal>
#include <QVector>

#include "common/PreviewGameplayConfig.h"

enum class TimelineRenderNoteKind {
    Unknown,
    Tap,
    Hold,
    Slide,
    Wifi,
    Touch,
    TouchHold,
};

enum TimelineRenderNoteFlag : quint32 {
    TimelineRenderFlagIsEach = 1u << 0,
    TimelineRenderFlagIsBreak = 1u << 1,
    TimelineRenderFlagIsEx = 1u << 2,
    TimelineRenderFlagIsFirework = 1u << 3,
    TimelineRenderFlagSlideEach = 1u << 4,
    TimelineRenderFlagSameHeadSlide = 1u << 5,
    TimelineRenderFlagHeadEach = 1u << 6,
    TimelineRenderFlagHeadBreak = 1u << 7,
    TimelineRenderFlagHeadEx = 1u << 8,
    TimelineRenderFlagTrackBreak = 1u << 9,
    TimelineRenderFlagHasHeadStar = 1u << 10,
    TimelineRenderFlagTapUsesStarMaterial = 1u << 11,
    TimelineRenderFlagTapStarDouble = 1u << 12,
    TimelineRenderFlagSlideHeadUsesTapMaterial = 1u << 13,
    TimelineRenderFlagHeadlessImmediate = 1u << 14,
    TimelineRenderFlagIsMine = 1u << 15,
    TimelineRenderFlagTrackMine = 1u << 16,
};

struct TimelineRenderBeat {
    double secondOffset = 0.0;
    int sourceCol = 1;
    int subdivisionBeats = 4;
    int subdivisionIndex = 0;
    bool major = false;
};

struct TimelineRenderNote {
    double secondOffset = 0.0;
    double endSecondOffset = -1.0;
    double slideTraceSecondOffset = -1.0;
    int sourceCol = 1;
    int lane = 1;
    int endLane = 1;
    int eachGroupId = -1;
    TimelineRenderNoteKind kind = TimelineRenderNoteKind::Unknown;
    quint32 flags = TimelineRenderFlagHasHeadStar;
};

struct TimelineCursorAnchor {
    int sourceCol = 1;
    int lane = -1;
    double secondOffset = 0.0;
};

struct TimelineVisibleLineRange {
    int begin = 0;
    int end = 0;
};

struct TimelineVisibleNoteRef {
    int lineIndex = 0;
    int noteIndex = 0;
};

struct TimelineRenderLine {
    int lineId = 0;
    int lineNumber = 1;
    int startPosition = 0;
    double startSecond = 0.0;
    double endSecond = 0.0;
    QVector<double> measureLineSecondOffsets;
    QVector<int> measureLineMeterNumerators;
    QVector<int> measureLineMeterDenominators;
    // Real within-measure beat step (seconds) for each measure line, i.e.
    // noteStepSeconds(bpm, denominator). Used to draw subdivision lines on the
    // true beat grid clipped to a 变拍/变BPM boundary, so a truncated measure
    // never spaces its beat lines as if it were a full measure.
    QVector<double> measureLineBeatStepSeconds;
    QVector<TimelineRenderBeat> beats;
    QVector<TimelineRenderNote> notes;
};

struct TimelineRenderSnapshot {
    QVector<TimelineRenderLine> lines;
    QVector<double> measureLineSeconds;
    QVector<int> measureLineMeterNumerators;
    QVector<int> measureLineMeterDenominators;
    // Parallel to measureLineSeconds: real within-measure beat step (seconds)
    // governing the span AFTER each measure line (see TimelineRenderLine).
    QVector<double> measureLineBeatStepSeconds;
    QVector<double> noteVisualEndPrefixMaxWithSlideTracks;
    QVector<double> noteVisualEndPrefixMaxWithoutSlideTracks;
    double trailingMeasureLineStartSecond = 0.0;
    double trailingMeasureLineStepSeconds = 0.0;
    int trailingMeasureLineMeterNumerator = 4;
    int trailingMeasureLineMeterDenominator = 4;
    double durationSeconds = 0.0;
    double minimumSecond = -0.5;
    double maximumSecond = 1.0;
};

inline constexpr double kTimelineFireworkDurationSeconds =
    miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds;

inline bool timelineRenderFlagSet(const TimelineRenderNote& note, TimelineRenderNoteFlag flag)
{
    return (note.flags & static_cast<quint32>(flag)) != 0u;
}

inline double timelineRenderAbsoluteSecond(const TimelineRenderLine& line, double secondOffset)
{
    return line.startSecond + secondOffset;
}

inline double timelineRenderLineVisualEndSecond(const TimelineRenderLine& line, bool includeSlideTracks)
{
    double visualEnd = -std::numeric_limits<double>::infinity();
    for (const TimelineRenderNote& note : line.notes) {
        const double startSecond = timelineRenderAbsoluteSecond(line, note.secondOffset);
        const double endSecond = note.endSecondOffset >= 0.0
            ? timelineRenderAbsoluteSecond(line, note.endSecondOffset)
            : startSecond;
        visualEnd = qMax(visualEnd, startSecond);

        if ((note.kind == TimelineRenderNoteKind::Hold || note.kind == TimelineRenderNoteKind::TouchHold)
            && endSecond > startSecond) {
            visualEnd = qMax(visualEnd, endSecond);
        }
        if (includeSlideTracks && (note.kind == TimelineRenderNoteKind::Slide || note.kind == TimelineRenderNoteKind::Wifi)) {
            visualEnd = qMax(visualEnd, endSecond);
        }
        if (timelineRenderFlagSet(note, TimelineRenderFlagIsFirework)
            && (note.kind == TimelineRenderNoteKind::Touch || note.kind == TimelineRenderNoteKind::TouchHold)) {
            const double triggerSecond = (note.kind == TimelineRenderNoteKind::TouchHold && endSecond > startSecond)
                ? endSecond
                : startSecond;
            visualEnd = qMax(visualEnd, triggerSecond + kTimelineFireworkDurationSeconds);
        }
    }
    return visualEnd;
}

inline TimelineVisibleLineRange timelineRenderVisibleNoteLineRange(
    const QVector<TimelineRenderLine>& lines,
    const QVector<double>& noteVisualEndPrefixMax,
    double startSecond,
    double endSecond)
{
    TimelineVisibleLineRange range;
    if (lines.isEmpty()) {
        return range;
    }

    const double boundedStart = qMin(startSecond, endSecond);
    const double boundedEnd = qMax(startSecond, endSecond);
    const auto endIt = std::upper_bound(
        lines.cbegin(),
        lines.cend(),
        boundedEnd,
        [](double targetSecond, const TimelineRenderLine& line) {
            return targetSecond < line.startSecond;
        });
    range.end = static_cast<int>(std::distance(lines.cbegin(), endIt));
    if (range.end <= 0 || noteVisualEndPrefixMax.size() < range.end) {
        range.begin = range.end;
        return range;
    }

    const auto beginIt = std::lower_bound(
        noteVisualEndPrefixMax.cbegin(),
        noteVisualEndPrefixMax.cbegin() + range.end,
        boundedStart,
        [](double prefixVisualEnd, double targetSecond) {
            return prefixVisualEnd < targetSecond;
        });
    range.begin = static_cast<int>(std::distance(noteVisualEndPrefixMax.cbegin(), beginIt));
    return range;
}

inline QVector<TimelineVisibleNoteRef> timelineRenderVisibleNotePaintOrder(
    const QVector<TimelineRenderLine>& lines,
    const TimelineVisibleLineRange& range)
{
    QVector<TimelineVisibleNoteRef> refs;
    if (lines.isEmpty()) {
        return refs;
    }

    const int begin = qBound(0, range.begin, lines.size());
    const int end = qBound(begin, range.end, lines.size());
    int reserveCount = 0;
    for (int lineIndex = begin; lineIndex < end; ++lineIndex) {
        reserveCount += lines.at(lineIndex).notes.size();
    }
    refs.reserve(reserveCount);

    for (int lineIndex = begin; lineIndex < end; ++lineIndex) {
        const TimelineRenderLine& line = lines.at(lineIndex);
        for (int noteIndex = 0; noteIndex < line.notes.size(); ++noteIndex) {
            refs.append(TimelineVisibleNoteRef{lineIndex, noteIndex});
        }
    }
    return refs;
}

inline quint64 timelineRenderLocationId(int lineNumber, int sourceCol)
{
    const quint32 linePart = static_cast<quint32>(lineNumber > 0 ? lineNumber : 0);
    const quint32 colPart = static_cast<quint32>(sourceCol > 0 ? sourceCol : 0);
    return (static_cast<quint64>(linePart) << 32) | static_cast<quint64>(colPart);
}

inline quint64 timelineRenderLocationId(const TimelineRenderLine& line, const TimelineRenderNote& note)
{
    return timelineRenderLocationId(line.lineNumber, note.sourceCol);
}
