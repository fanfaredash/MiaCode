#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

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
};

struct TimelineRenderBeat {
    double secondOffset = 0.0;
    int sourceCol = 1;
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

struct TimelineRenderLine {
    int lineId = 0;
    int lineNumber = 1;
    int startPosition = 0;
    double startSecond = 0.0;
    double endSecond = 0.0;
    QVector<TimelineRenderBeat> beats;
    QVector<TimelineRenderNote> notes;
};

struct TimelineRenderSnapshot {
    QVector<TimelineRenderLine> lines;
    double durationSeconds = 0.0;
    double minimumSecond = -0.5;
    double maximumSecond = 1.0;
};

inline bool timelineRenderFlagSet(const TimelineRenderNote& note, TimelineRenderNoteFlag flag)
{
    return (note.flags & static_cast<quint32>(flag)) != 0u;
}

inline double timelineRenderAbsoluteSecond(const TimelineRenderLine& line, double secondOffset)
{
    return line.startSecond + secondOffset;
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
