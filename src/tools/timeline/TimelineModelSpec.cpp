#include "timeline/TimelineQuickModel.h"
#include "timeline/TimelineQuickModelPrivate.h"
#include "core/chart/document/SimaiTimingMetadata.h"
#include "core/chart/parser/SimaiNativeParser.h"

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QStringList>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QtMath>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined as the absolute repository root"
#endif

namespace {

bool nearlyEqual(double a, double b, double epsilon = 1e-6)
{
    return qAbs(a - b) <= epsilon;
}

struct ComparableNote {
    int line = 1;
    int col = 1;
    double second = 0.0;
    double endSecond = -1.0;
    double slideTraceSecond = -1.0;
    int lane = 1;
    int endLane = 1;
    TimelineRenderNoteKind kind = TimelineRenderNoteKind::Unknown;
    quint32 flags = 0u;
};

struct ComparableBeat {
    int line = 1;
    int col = 1;
    double second = 0.0;
    bool major = false;
};

QString describeSecond(double second)
{
    return QStringLiteral("second=%1").arg(second, 0, 'f', 6);
}

QString describeFollowBinding(const TimelineQuickModel::PreviewFollowBinding& binding)
{
    return QStringLiteral(
               "resolved=%1 start=%2 end=%3 anchor=%4 L%5C%6 span=[%7:%8-%9:%10] cursor=%11:%12 body=%13")
        .arg(binding.resolved ? 1 : 0)
        .arg(binding.startSecond, 0, 'f', 6)
        .arg(qIsInf(binding.endSecondExclusive)
                 ? QStringLiteral("inf")
                 : QString::number(binding.endSecondExclusive, 'f', 6))
        .arg(binding.anchorSecond, 0, 'f', 6)
        .arg(binding.anchorLine)
        .arg(binding.anchorCol)
        .arg(binding.span.startLine)
        .arg(binding.span.startCol)
        .arg(binding.span.endLine)
        .arg(binding.span.endCol)
        .arg(binding.span.cursorLine)
        .arg(binding.span.cursorCol)
        .arg(binding.span.hasVisibleBody ? 1 : 0);
}

bool bindingsEquivalent(
    const TimelineQuickModel::PreviewFollowBinding& left,
    const TimelineQuickModel::PreviewFollowBinding& right)
{
    const bool endEquivalent = (qIsInf(left.endSecondExclusive) && qIsInf(right.endSecondExclusive))
        || nearlyEqual(left.endSecondExclusive, right.endSecondExclusive);
    return left.resolved == right.resolved
        && nearlyEqual(left.startSecond, right.startSecond)
        && endEquivalent
        && nearlyEqual(left.anchorSecond, right.anchorSecond)
        && left.anchorLine == right.anchorLine
        && left.anchorCol == right.anchorCol
        && left.span.startLine == right.span.startLine
        && left.span.startCol == right.span.startCol
        && left.span.endLine == right.span.endLine
        && left.span.endCol == right.span.endCol
        && left.span.cursorLine == right.span.cursorLine
        && left.span.cursorCol == right.span.cursorCol
        && left.span.startPosition == right.span.startPosition
        && left.span.endPositionExclusive == right.span.endPositionExclusive
        && left.span.cursorPosition == right.span.cursorPosition
        && left.span.hasVisibleBody == right.span.hasVisibleBody;
}

QString kindLabel(TimelineRenderNoteKind kind)
{
    switch (kind) {
    case TimelineRenderNoteKind::Tap:
        return QStringLiteral("tap");
    case TimelineRenderNoteKind::Hold:
        return QStringLiteral("hold");
    case TimelineRenderNoteKind::Slide:
        return QStringLiteral("slide");
    case TimelineRenderNoteKind::Wifi:
        return QStringLiteral("wifi");
    case TimelineRenderNoteKind::Touch:
        return QStringLiteral("touch");
    case TimelineRenderNoteKind::TouchHold:
        return QStringLiteral("touch_hold");
    default:
        return QStringLiteral("unknown");
    }
}

QString flagsLabel(quint32 flags)
{
    QStringList parts;
    const auto appendIf = [&parts, flags](TimelineRenderNoteFlag flag, const QString& label) {
        if ((flags & static_cast<quint32>(flag)) != 0u) {
            parts.append(label);
        }
    };
    appendIf(TimelineRenderFlagIsEach, QStringLiteral("is_each"));
    appendIf(TimelineRenderFlagIsBreak, QStringLiteral("is_break"));
    appendIf(TimelineRenderFlagIsEx, QStringLiteral("is_ex"));
    appendIf(TimelineRenderFlagIsFirework, QStringLiteral("is_firework"));
    appendIf(TimelineRenderFlagSlideEach, QStringLiteral("slide_each"));
    appendIf(TimelineRenderFlagSameHeadSlide, QStringLiteral("same_head"));
    appendIf(TimelineRenderFlagHeadEach, QStringLiteral("head_each"));
    appendIf(TimelineRenderFlagHeadBreak, QStringLiteral("head_break"));
    appendIf(TimelineRenderFlagHeadEx, QStringLiteral("head_ex"));
    appendIf(TimelineRenderFlagTrackBreak, QStringLiteral("track_break"));
    appendIf(TimelineRenderFlagHasHeadStar, QStringLiteral("has_head_star"));
    appendIf(TimelineRenderFlagTapUsesStarMaterial, QStringLiteral("tap_star_material"));
    appendIf(TimelineRenderFlagTapStarDouble, QStringLiteral("tap_star_double"));
    appendIf(TimelineRenderFlagSlideHeadUsesTapMaterial, QStringLiteral("slide_head_tap_material"));
    appendIf(TimelineRenderFlagHeadlessImmediate, QStringLiteral("headless_immediate"));
    appendIf(TimelineRenderFlagIsMine, QStringLiteral("is_mine"));
    appendIf(TimelineRenderFlagTrackMine, QStringLiteral("track_mine"));
    return parts.join(QLatin1Char('|'));
}

QString describeComparableNote(const ComparableNote& note)
{
    return QStringLiteral(
               "L%1C%2 %3 lane=%4 end=%5 second=%6 trace=%7 end_second=%8 flags=%9")
        .arg(note.line)
        .arg(note.col)
        .arg(kindLabel(note.kind))
        .arg(note.lane)
        .arg(note.endLane)
        .arg(note.second, 0, 'f', 6)
        .arg(note.slideTraceSecond, 0, 'f', 6)
        .arg(note.endSecond, 0, 'f', 6)
        .arg(flagsLabel(note.flags));
}

QString describeComparableBeat(const ComparableBeat& beat)
{
    return QStringLiteral("L%1C%2 second=%3 major=%4")
        .arg(beat.line)
        .arg(beat.col)
        .arg(beat.second, 0, 'f', 6)
        .arg(beat.major ? QStringLiteral("true") : QStringLiteral("false"));
}

TimelineRenderNoteKind kindForMarkerType(const QString& type)
{
    if (type == QLatin1String("tap")) {
        return TimelineRenderNoteKind::Tap;
    }
    if (type == QLatin1String("hold")) {
        return TimelineRenderNoteKind::Hold;
    }
    if (type == QLatin1String("slide")) {
        return TimelineRenderNoteKind::Slide;
    }
    if (type == QLatin1String("wifi")) {
        return TimelineRenderNoteKind::Wifi;
    }
    if (type == QLatin1String("touch")) {
        return TimelineRenderNoteKind::Touch;
    }
    if (type == QLatin1String("touch_hold")) {
        return TimelineRenderNoteKind::TouchHold;
    }
    return TimelineRenderNoteKind::Unknown;
}

quint32 flagsForMarker(const TimelineNoteMarker& marker)
{
    quint32 flags = 0u;
    if (marker.isEach) {
        flags |= TimelineRenderFlagIsEach;
    }
    if (marker.isBreak) {
        flags |= TimelineRenderFlagIsBreak;
    }
    if (marker.isEx) {
        flags |= TimelineRenderFlagIsEx;
    }
    if (marker.isFirework) {
        flags |= TimelineRenderFlagIsFirework;
    }
    if (marker.slideEach) {
        flags |= TimelineRenderFlagSlideEach;
    }
    if (marker.sameHeadSlide) {
        flags |= TimelineRenderFlagSameHeadSlide;
    }
    if (marker.headEach) {
        flags |= TimelineRenderFlagHeadEach;
    }
    if (marker.headBreak) {
        flags |= TimelineRenderFlagHeadBreak;
    }
    if (marker.headEx) {
        flags |= TimelineRenderFlagHeadEx;
    }
    if (marker.trackBreak) {
        flags |= TimelineRenderFlagTrackBreak;
    }
    if (marker.hasHeadStar && (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi"))) {
        flags |= TimelineRenderFlagHasHeadStar;
    }
    if (marker.tapUsesStarMaterial) {
        flags |= TimelineRenderFlagTapUsesStarMaterial;
    }
    if (marker.tapStarDouble) {
        flags |= TimelineRenderFlagTapStarDouble;
    }
    if (marker.slideHeadUsesTapMaterial) {
        flags |= TimelineRenderFlagSlideHeadUsesTapMaterial;
    }
    if (marker.headlessImmediate) {
        flags |= TimelineRenderFlagHeadlessImmediate;
    }
    if (marker.isMine) {
        flags |= TimelineRenderFlagIsMine;
    }
    if (marker.trackMine) {
        flags |= TimelineRenderFlagTrackMine;
    }
    return flags;
}

QVector<ComparableNote> flattenSnapshotNotes(const TimelineRenderSnapshot& snapshot)
{
    QVector<ComparableNote> notes;
    for (const TimelineRenderLine& line : snapshot.lines) {
        for (const TimelineRenderNote& note : line.notes) {
            ComparableNote item;
            item.line = line.lineNumber;
            item.col = note.sourceCol;
            item.second = timelineRenderAbsoluteSecond(line, note.secondOffset);
            item.endSecond = note.endSecondOffset >= 0.0
                ? timelineRenderAbsoluteSecond(line, note.endSecondOffset)
                : -1.0;
            item.slideTraceSecond = note.slideTraceSecondOffset >= 0.0
                ? timelineRenderAbsoluteSecond(line, note.slideTraceSecondOffset)
                : -1.0;
            item.lane = note.lane;
            item.endLane = note.endLane;
            item.kind = note.kind;
            item.flags = note.flags;
            notes.append(item);
        }
    }
    return notes;
}

QVector<ComparableNote> flattenParserNotes(const SimaiNativeParseResult& parsed)
{
    QVector<ComparableNote> notes;
    notes.reserve(parsed.noteMarkers.size());
    for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
        const TimelineRenderNoteKind kind = kindForMarkerType(marker.type);
        if (kind == TimelineRenderNoteKind::Unknown) {
            continue;
        }
        ComparableNote item;
        item.line = marker.sourceLine;
        item.col = marker.sourceCol;
        item.second = marker.second;
        item.endSecond = marker.endSecond;
        item.slideTraceSecond = marker.slideTraceSecond;
        item.lane = marker.lane;
        item.endLane = marker.endLane;
        item.kind = kind;
        item.flags = flagsForMarker(marker);
        notes.append(item);
    }
    return notes;
}

QVector<ComparableBeat> flattenSnapshotBeats(const TimelineRenderSnapshot& snapshot)
{
    QVector<ComparableBeat> beats;
    for (const TimelineRenderLine& line : snapshot.lines) {
        for (const TimelineRenderBeat& beat : line.beats) {
            ComparableBeat item;
            item.line = line.lineNumber;
            item.col = beat.sourceCol;
            item.second = timelineRenderAbsoluteSecond(line, beat.secondOffset);
            item.major = beat.major;
            beats.append(item);
        }
    }
    return beats;
}

QVector<ComparableBeat> flattenParserBeats(const SimaiNativeParseResult& parsed)
{
    QVector<ComparableBeat> beats;
    beats.reserve(parsed.beatMarkers.size());
    for (const TimelineBeatMarker& marker : parsed.beatMarkers) {
        ComparableBeat item;
        item.line = marker.sourceLine;
        item.col = marker.sourceCol;
        item.second = marker.second;
        item.major = marker.major;
        beats.append(item);
    }
    return beats;
}

QVector<double> flattenSnapshotMeasureLines(const TimelineRenderSnapshot& snapshot)
{
    return snapshot.measureLineSeconds;
}

QVector<double> flattenSnapshotLineStarts(const TimelineRenderSnapshot& snapshot)
{
    QVector<double> starts;
    starts.reserve(snapshot.lines.size());
    for (const TimelineRenderLine& line : snapshot.lines) {
        starts.append(line.startSecond);
    }
    return starts;
}

QVector<double> flattenParserMeasureLines(const SimaiNativeParseResult& parsed)
{
    return parsed.measureLineSeconds;
}

bool isSyntheticTrailingBeat(const ComparableBeat& beat, const QStringList& chartLines)
{
    const int lineIndex = beat.line - 1;
    if (lineIndex < 0 || lineIndex >= chartLines.size()) {
        return false;
    }
    return beat.col == chartLines.at(lineIndex).size() + 1;
}

bool lineStartsNondecreasing(const QVector<double>& starts)
{
    for (int index = 1; index < starts.size(); ++index) {
        if (starts.at(index) + 1e-6 < starts.at(index - 1)) {
            return false;
        }
    }
    return true;
}

void sortComparableNotes(QVector<ComparableNote>* notes)
{
    if (notes == nullptr) {
        return;
    }
    std::sort(notes->begin(), notes->end(), [](const ComparableNote& left, const ComparableNote& right) {
        if (left.line != right.line) {
            return left.line < right.line;
        }
        if (left.col != right.col) {
            return left.col < right.col;
        }
        if (!nearlyEqual(left.second, right.second)) {
            return left.second < right.second;
        }
        if (left.lane != right.lane) {
            return left.lane < right.lane;
        }
        if (left.endLane != right.endLane) {
            return left.endLane < right.endLane;
        }
        if (left.kind != right.kind) {
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        }
        return left.flags < right.flags;
    });
}

void sortComparableBeats(QVector<ComparableBeat>* beats)
{
    if (beats == nullptr) {
        return;
    }
    std::sort(beats->begin(), beats->end(), [](const ComparableBeat& left, const ComparableBeat& right) {
        if (!nearlyEqual(left.second, right.second)) {
            return left.second < right.second;
        }
        if (left.line != right.line) {
            return left.line < right.line;
        }
        if (left.col != right.col) {
            return left.col < right.col;
        }
        return left.major < right.major;
    });
}

QString comparableNotesDiff(const QVector<ComparableNote>& expected, const QVector<ComparableNote>& actual)
{
    QStringList lines;
    lines.append(QStringLiteral("expected=%1 actual=%2").arg(expected.size()).arg(actual.size()));
    const int limit = qMax(expected.size(), actual.size());
    for (int i = 0; i < limit; ++i) {
        const QString left = i < expected.size() ? describeComparableNote(expected.at(i)) : QStringLiteral("<none>");
        const QString right = i < actual.size() ? describeComparableNote(actual.at(i)) : QStringLiteral("<none>");
        if (i < expected.size() && i < actual.size()) {
            const ComparableNote& a = expected.at(i);
            const ComparableNote& b = actual.at(i);
            if (a.line == b.line
                && a.col == b.col
                && nearlyEqual(a.second, b.second)
                && nearlyEqual(a.endSecond, b.endSecond)
                && nearlyEqual(a.slideTraceSecond, b.slideTraceSecond)
                && a.lane == b.lane
                && a.endLane == b.endLane
                && a.kind == b.kind
                && a.flags == b.flags) {
                continue;
            }
        }
        lines.append(QStringLiteral("[%1] parser=%2").arg(i).arg(left));
        lines.append(QStringLiteral("[%1] quick =%2").arg(i).arg(right));
    }
    return lines.join(QLatin1Char('\n'));
}

QString comparableBeatsDiff(const QVector<ComparableBeat>& expected, const QVector<ComparableBeat>& actual)
{
    QStringList lines;
    lines.append(QStringLiteral("expected=%1 actual=%2").arg(expected.size()).arg(actual.size()));
    const int limit = qMax(expected.size(), actual.size());
    for (int i = 0; i < limit; ++i) {
        const QString left = i < expected.size() ? describeComparableBeat(expected.at(i)) : QStringLiteral("<none>");
        const QString right = i < actual.size() ? describeComparableBeat(actual.at(i)) : QStringLiteral("<none>");
        if (i < expected.size() && i < actual.size()) {
            const ComparableBeat& a = expected.at(i);
            const ComparableBeat& b = actual.at(i);
            if (a.line == b.line
                && a.col == b.col
                && nearlyEqual(a.second, b.second)
                && a.major == b.major) {
                continue;
            }
        }
        lines.append(QStringLiteral("[%1] parser=%2").arg(i).arg(left));
        lines.append(QStringLiteral("[%1] quick =%2").arg(i).arg(right));
    }
    return lines.join(QLatin1Char('\n'));
}

QString comparableSecondsDiff(const QVector<double>& expected, const QVector<double>& actual)
{
    QStringList lines;
    lines.append(QStringLiteral("expected=%1 actual=%2").arg(expected.size()).arg(actual.size()));
    const int limit = qMax(expected.size(), actual.size());
    for (int i = 0; i < limit; ++i) {
        const QString left = i < expected.size() ? describeSecond(expected.at(i)) : QStringLiteral("<none>");
        const QString right = i < actual.size() ? describeSecond(actual.at(i)) : QStringLiteral("<none>");
        if (i < expected.size() && i < actual.size() && nearlyEqual(expected.at(i), actual.at(i))) {
            continue;
        }
        lines.append(QStringLiteral("[%1] parser=%2").arg(i).arg(left));
        lines.append(QStringLiteral("[%1] quick =%2").arg(i).arg(right));
    }
    return lines.join(QLatin1Char('\n'));
}

bool snapshotMatchesParser(
    const QString& chartText,
    QString* diff = nullptr,
    const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata())
{
    const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(chartText, timingMetadata);
    if (!parsed.ok) {
        if (diff != nullptr) {
            QStringList messages;
            messages.append(QStringLiteral("parser failed"));
            for (const SimaiNativeMessage& error : parsed.errors) {
                messages.append(QStringLiteral("L%1C%2 %3").arg(error.line).arg(error.col).arg(error.message));
            }
            *diff = messages.join(QLatin1Char('\n'));
        }
        return false;
    }

    TimelineQuickModel model;
    if (!model.rebuildFromText(chartText, 0.0, timingMetadata)) {
        if (diff != nullptr) {
            *diff = QStringLiteral("quick model rebuild failed");
        }
        return false;
    }

    QVector<ComparableNote> parserNotes = flattenParserNotes(parsed);
    QVector<ComparableNote> quickNotes = flattenSnapshotNotes(model.snapshot());
    sortComparableNotes(&parserNotes);
    sortComparableNotes(&quickNotes);
    QVector<ComparableBeat> parserBeats = flattenParserBeats(parsed);
    QVector<ComparableBeat> quickBeats = flattenSnapshotBeats(model.snapshot());
    QVector<double> parserMeasures = flattenParserMeasureLines(parsed);
    QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
    sortComparableBeats(&parserBeats);
    sortComparableBeats(&quickBeats);
    std::sort(parserMeasures.begin(), parserMeasures.end());
    std::sort(quickMeasures.begin(), quickMeasures.end());
    const QStringList chartLines = chartText.split(QLatin1Char('\n'));
    if (quickBeats.size() == parserBeats.size() + 1
        && isSyntheticTrailingBeat(quickBeats.constLast(), chartLines)) {
        quickBeats.removeLast();
    }
    if (quickMeasures.size() == parserMeasures.size() + 1
        && (parserMeasures.isEmpty() || quickMeasures.constLast() > parserMeasures.constLast() + 1e-6)) {
        quickMeasures.removeLast();
    }

    if (parserNotes.size() != quickNotes.size()) {
        if (diff != nullptr) {
            *diff = comparableNotesDiff(parserNotes, quickNotes);
        }
        return false;
    }

    for (int i = 0; i < parserNotes.size(); ++i) {
        const ComparableNote& a = parserNotes.at(i);
        const ComparableNote& b = quickNotes.at(i);
        if (a.line != b.line
            || a.col != b.col
            || !nearlyEqual(a.second, b.second)
            || !nearlyEqual(a.endSecond, b.endSecond)
            || !nearlyEqual(a.slideTraceSecond, b.slideTraceSecond)
            || a.lane != b.lane
            || a.endLane != b.endLane
            || a.kind != b.kind
            || a.flags != b.flags) {
            if (diff != nullptr) {
                *diff = comparableNotesDiff(parserNotes, quickNotes);
            }
            return false;
        }
    }

    if (parserBeats.size() != quickBeats.size()) {
        if (diff != nullptr) {
            *diff = comparableBeatsDiff(parserBeats, quickBeats);
        }
        return false;
    }

    for (int i = 0; i < parserBeats.size(); ++i) {
        const ComparableBeat& a = parserBeats.at(i);
        const ComparableBeat& b = quickBeats.at(i);
        if (a.line != b.line
            || a.col != b.col
            || !nearlyEqual(a.second, b.second)
            || a.major != b.major) {
            if (diff != nullptr) {
                *diff = comparableBeatsDiff(parserBeats, quickBeats);
            }
            return false;
        }
    }

    if (parserMeasures.size() != quickMeasures.size()) {
        if (diff != nullptr) {
            *diff = comparableSecondsDiff(parserMeasures, quickMeasures);
        }
        return false;
    }

    for (int i = 0; i < parserMeasures.size(); ++i) {
        if (!nearlyEqual(parserMeasures.at(i), quickMeasures.at(i))) {
            if (diff != nullptr) {
                *diff = comparableSecondsDiff(parserMeasures, quickMeasures);
            }
            return false;
        }
    }
    return true;
}

QVector<double> buildNoteVisualEndPrefixMax(const QVector<TimelineRenderLine>& lines, bool includeSlideTracks)
{
    QVector<double> prefix;
    prefix.reserve(lines.size());
    double current = -std::numeric_limits<double>::infinity();
    for (const TimelineRenderLine& line : lines) {
        current = qMax(current, timelineRenderLineVisualEndSecond(line, includeSlideTracks));
        prefix.append(current);
    }
    return prefix;
}

bool sameBeat(const TimelineRenderBeat& left, const TimelineRenderBeat& right)
{
    return nearlyEqual(left.secondOffset, right.secondOffset)
        && left.sourceCol == right.sourceCol
        && left.major == right.major;
}

bool sameNote(const TimelineRenderNote& left, const TimelineRenderNote& right)
{
    return nearlyEqual(left.secondOffset, right.secondOffset)
        && nearlyEqual(left.endSecondOffset, right.endSecondOffset)
        && nearlyEqual(left.slideTraceSecondOffset, right.slideTraceSecondOffset)
        && left.sourceCol == right.sourceCol
        && left.lane == right.lane
        && left.endLane == right.endLane
        && left.kind == right.kind
        && left.flags == right.flags;
}

bool sameDoubleVector(const QVector<double>& left, const QVector<double>& right);
bool sameIntVector(const QVector<int>& left, const QVector<int>& right);

bool sameLine(const TimelineRenderLine& left, const TimelineRenderLine& right)
{
    if (left.lineNumber != right.lineNumber
        || left.startPosition != right.startPosition
        || !nearlyEqual(left.startSecond, right.startSecond)
        || !nearlyEqual(left.endSecond, right.endSecond)
        || !sameDoubleVector(left.measureLineSecondOffsets, right.measureLineSecondOffsets)
        || !sameIntVector(left.measureLineMeterNumerators, right.measureLineMeterNumerators)
        || !sameIntVector(left.measureLineMeterDenominators, right.measureLineMeterDenominators)
        || !sameDoubleVector(left.measureLineBeatStepSeconds, right.measureLineBeatStepSeconds)
        || left.beats.size() != right.beats.size()
        || left.notes.size() != right.notes.size()) {
        return false;
    }

    for (int i = 0; i < left.beats.size(); ++i) {
        if (!sameBeat(left.beats.at(i), right.beats.at(i))) {
            return false;
        }
    }
    for (int i = 0; i < left.notes.size(); ++i) {
        if (!sameNote(left.notes.at(i), right.notes.at(i))) {
            return false;
        }
    }
    return true;
}

bool sameDoubleVector(const QVector<double>& left, const QVector<double>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (int i = 0; i < left.size(); ++i) {
        const bool leftFinite = qIsFinite(left.at(i));
        const bool rightFinite = qIsFinite(right.at(i));
        if (leftFinite != rightFinite) {
            return false;
        }
        if (leftFinite && !nearlyEqual(left.at(i), right.at(i))) {
            return false;
        }
    }
    return true;
}

bool sameIntVector(const QVector<int>& left, const QVector<int>& right)
{
    return left == right;
}

bool sameSnapshot(const TimelineRenderSnapshot& left, const TimelineRenderSnapshot& right)
{
    if (!nearlyEqual(left.durationSeconds, right.durationSeconds)
        || !nearlyEqual(left.minimumSecond, right.minimumSecond)
        || !nearlyEqual(left.maximumSecond, right.maximumSecond)
        || !nearlyEqual(left.trailingMeasureLineStartSecond, right.trailingMeasureLineStartSecond)
        || !nearlyEqual(left.trailingMeasureLineStepSeconds, right.trailingMeasureLineStepSeconds)
        || left.trailingMeasureLineMeterNumerator != right.trailingMeasureLineMeterNumerator
        || left.trailingMeasureLineMeterDenominator != right.trailingMeasureLineMeterDenominator
        || left.lines.size() != right.lines.size()
        || !sameDoubleVector(left.measureLineSeconds, right.measureLineSeconds)
        || !sameIntVector(left.measureLineMeterNumerators, right.measureLineMeterNumerators)
        || !sameIntVector(left.measureLineMeterDenominators, right.measureLineMeterDenominators)
        || !sameDoubleVector(left.measureLineBeatStepSeconds, right.measureLineBeatStepSeconds)
        || !sameDoubleVector(left.noteVisualEndPrefixMaxWithSlideTracks, right.noteVisualEndPrefixMaxWithSlideTracks)
        || !sameDoubleVector(left.noteVisualEndPrefixMaxWithoutSlideTracks, right.noteVisualEndPrefixMaxWithoutSlideTracks)) {
        return false;
    }

    for (int i = 0; i < left.lines.size(); ++i) {
        if (!sameLine(left.lines.at(i), right.lines.at(i))) {
            return false;
        }
    }
    return true;
}

void applyDocumentChange(QTextDocument* document, int position, int charsRemoved, const QString& insertedText)
{
    QTextCursor cursor(document);
    cursor.setPosition(position);
    if (charsRemoved > 0) {
        cursor.setPosition(position + charsRemoved, QTextCursor::KeepAnchor);
    }
    cursor.insertText(insertedText);
}

bool incrementalMatchesRebuild(
    const QString& originalText,
    int position,
    int charsRemoved,
    const QString& insertedText,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata())
{
    QTextDocument document(originalText);
    TimelineQuickModel incremental;
    TimelineQuickModel rebuilt;
    incremental.rebuildFromText(document.toPlainText(), firstSeconds, timingMetadata);
    applyDocumentChange(&document, position, charsRemoved, insertedText);
    incremental.applyTextChange(document.toPlainText(), position, charsRemoved, insertedText.size(), firstSeconds, timingMetadata);
    rebuilt.rebuildFromText(document.toPlainText(), firstSeconds, timingMetadata);
    return sameSnapshot(incremental.snapshot(), rebuilt.snapshot());
}

QString qmlSource(const QString& relativePath)
{
    const QDir root(QStringLiteral(MIACODE_SOURCE_ROOT));
    QFile file(root.filePath(relativePath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    int failed = 0;

    const auto expect = [&](bool condition, const QString& message) {
        if (condition) {
            out << "[PASS] " << message << '\n';
            return;
        }
        err << "[FAIL] " << message << '\n';
        ++failed;
    };

    {
        expect(QDir(QStringLiteral(MIACODE_SOURCE_ROOT)).exists(),
               QStringLiteral("timeline source-contract root is injected by CMake and exists"));
        const QString panel = qmlSource(QStringLiteral("src/app/qml_ui/timeline/BottomPanel.qml"));
        const QString tabBar = qmlSource(QStringLiteral("src/app/qml_ui/timeline/BottomTabBar.qml"));
        const QString splitView = qmlSource(QStringLiteral("src/app/qml_ui/layout/MainSplitView.qml"));
        const QString viewState = qmlSource(QStringLiteral("src/app/qml_ui/ViewState.qml"));
        const QString timelineSession = qmlSource(QStringLiteral("src/app/qml_ui/QmlTimelineModel.h"));
        const QString timelineTick = qmlSource(
            QStringLiteral("src/app/runtime/playback/Tick.cpp"));
        const QString editorDisplay = qmlSource(
            QStringLiteral("src/app/runtime/editor/EditorDisplay.cpp"));
        const QString zoomMenu = qmlSource(QStringLiteral("src/app/qml_ui/timeline/TimelineZoomMenu.qml"));
        const QString brightnessMenu = qmlSource(QStringLiteral("src/app/qml_ui/timeline/TimelineBrightnessMenu.qml"));
        expect(!panel.isEmpty() && !tabBar.isEmpty() && !splitView.isEmpty()
                   && !viewState.isEmpty() && !timelineSession.isEmpty() && !timelineTick.isEmpty()
                   && !editorDisplay.isEmpty() && !zoomMenu.isEmpty() && !brightnessMenu.isEmpty(),
               QStringLiteral("v2 timeline control QML sources are available to the developer spec"));
        const QString controls = panel + tabBar;
        expect(zoomMenu.contains(QStringLiteral("applyZoomPreset"))
                   && brightnessMenu.contains(QStringLiteral("waveformBrightness"))
                   && brightnessMenu.contains(QStringLiteral("measureLineBrightness"))
                   && controls.contains(QStringLiteral("followPreviewToggled")),
               QStringLiteral("v2 timeline zoom and brightness menus are QML and follow-code stays on the session"));
        expect(!timelineSession.contains(QStringLiteral("openTimelineZoomMenu"))
                   && !timelineSession.contains(QStringLiteral("openTimelineBrightnessMenu"))
                   && !timelineSession.contains(QStringLiteral("TimelineBrightnessSliderItem")),
               QStringLiteral("v2 timeline zoom and brightness no longer pop QWidget menus"));
        expect(!controls.contains(QStringLiteral("openTimelineFollowSettingsMenu"))
                   && !timelineSession.contains(QStringLiteral("FollowSettingsCheckItem")),
               QStringLiteral("v2 exposes follow-code directly without extra follow settings"));
        expect(tabBar.contains(QStringLiteral("currentTabId"))
                   && tabBar.contains(QStringLiteral("setCurrentTabId"))
                   && !tabBar.contains(QStringLiteral("activeBottomTab"))
                   && !panel.contains(QStringLiteral("activeBottomTab"))
                   && !splitView.contains(QStringLiteral("activeBottomTab"))
                   && !viewState.contains(QStringLiteral("activeBottomTab")),
               QStringLiteral("v2 bottom tabs select through the timeline session instead of ViewState"));
        expect(splitView.contains(QStringLiteral("persistBottomPanelHeightRatio"))
                   && splitView.contains(QStringLiteral("bottomPanelHeightRatio"))
                   && splitView.contains(QStringLiteral("bottomPanelMinimumHeightRatio"))
                   && splitView.contains(QStringLiteral("bottomPanelMaximumHeightRatio"))
                   && splitView.contains(QStringLiteral("onReleased: root.persistBottomPanelHeightRatio()"))
                   && splitView.contains(QStringLiteral("centerSplit.height * root.preferences.bottomPanelHeightRatio"))
                   && !splitView.contains(QStringLiteral("queueBottomTabsHostHeight"))
                   && !splitView.contains(QStringLiteral("bottomTabsHeightFeedback"))
                   && !splitView.contains(QStringLiteral("maximumHeight: root.bottomPanelEffectivelyVisible ? 340 : 0")),
               QStringLiteral("v2 bottom height persists one SplitView ratio after divider release without a fixed 340px cap"));
        expect(panel.contains(QStringLiteral("headerLeftLimit: zoomButton.x + zoomButton.width + 2"))
                   && panel.contains(QStringLiteral("headerRightLimit: Math.max(0, brightnessButton.x - 2)"))
                   && panel.contains(QStringLiteral("headerMarkerLeftLimit: zoomButton.x + zoomButton.width + 2"))
                   && panel.contains(QStringLiteral("headerMarkerRightLimit: Math.max(0, brightnessButton.x - 2)"))
                   && panel.contains(QStringLiteral("timelineItem.y + Math.max(0, (timelineItem.timelineTop - height) / 2)")),
               QStringLiteral("v2 timeline header keeps labels clear of its QML controls"));
        const int playingFlushStart = timelineTick.indexOf(
            QStringLiteral("void miacode::runtime::PlaybackCoordinator::flushQtPreviewTimelinePosition()"));
        const QString playingFlush = playingFlushStart >= 0
            ? timelineTick.mid(playingFlushStart, 2600)
            : QString();
        expect(!playingFlush.contains(QStringLiteral("|| !timelineTabIsForeground()"))
                   && playingFlush.contains(QStringLiteral("syncEditorCursorToPreviewSecond")),
               QStringLiteral("editor follow tick continues while validation or Muri owns the visible bottom tab"));
        expect(editorDisplay.contains(QStringLiteral("hasLegacyBottomPanelHeight"))
                   && editorDisplay.contains(QStringLiteral("legacySettings.remove(QStringLiteral(\"ui/bottomPanelHeight\"))"))
                   && editorDisplay.lastIndexOf(QStringLiteral("legacySettings.remove"))
                       > editorDisplay.indexOf(QStringLiteral("if (ui.value(\"bottom_tabs_content_scale\").isDouble())")),
               QStringLiteral("legacy v2 bottom-panel height is retired even when shell scale already exists"));
        expect(editorDisplay.contains(QStringLiteral("state_.previewViewportLockEnabled_ = true"))
                   && editorDisplay.contains(QStringLiteral("state_.previewProgressFollowEnabled_ = true"))
                   && editorDisplay.contains(QStringLiteral("preview.insert(\"viewport_lock\", true)"))
                   && editorDisplay.contains(QStringLiteral("preview.insert(\"follow_progress\", true)")),
               QStringLiteral("view-lock and progress-follow retain fixed editor behavior"));
    }

    {
        const QString hsDirective = QStringLiteral("<HS*1.5>");
        int leading = 0;
        int trailing = hsDirective.size();
        expect(
            miacode::timeline::tqm_detail::tryConsumeLeadingFollowControlToken(
                hsDirective, hsDirective.size(), &leading)
                && leading == hsDirective.size()
                && miacode::timeline::tqm_detail::tryConsumeTrailingFollowControlToken(
                    hsDirective, 0, &trailing)
                && trailing == 0,
            QStringLiteral("follow control trimming recognizes leading and trailing HS directives"));
    }

    {
        TimelineRenderLine longSlide;
        longSlide.startSecond = 0.0;
        TimelineRenderNote slide;
        slide.kind = TimelineRenderNoteKind::Slide;
        slide.secondOffset = 0.0;
        slide.endSecondOffset = 5.0;
        longSlide.notes.append(slide);

        TimelineRenderLine gapLine;
        gapLine.startSecond = 1.0;
        TimelineRenderNote tapA;
        tapA.kind = TimelineRenderNoteKind::Tap;
        tapA.secondOffset = 0.0;
        gapLine.notes.append(tapA);

        TimelineRenderLine visibleLine;
        visibleLine.startSecond = 4.0;
        TimelineRenderNote tapB;
        tapB.kind = TimelineRenderNoteKind::Tap;
        tapB.secondOffset = 0.0;
        visibleLine.notes.append(tapB);

        const QVector<TimelineRenderLine> lines{longSlide, gapLine, visibleLine};
        const TimelineVisibleLineRange withSlideTracks = timelineRenderVisibleNoteLineRange(
            lines,
            buildNoteVisualEndPrefixMax(lines, true),
            4.0,
            4.5
        );
        const TimelineVisibleLineRange withoutSlideTracks = timelineRenderVisibleNoteLineRange(
            lines,
            buildNoteVisualEndPrefixMax(lines, false),
            4.0,
            4.5
        );
        expect(withSlideTracks.begin == 0 && withSlideTracks.end == 3,
               QStringLiteral("visible note range keeps long slide source line when slide track stays on"));
        expect(withoutSlideTracks.begin == 2 && withoutSlideTracks.end == 3,
               QStringLiteral("visible note range does not overdraw hidden slide tail"));
    }

    {
        TimelineRenderLine fireworkLine;
        fireworkLine.startSecond = 0.0;
        TimelineRenderNote firework;
        firework.kind = TimelineRenderNoteKind::Touch;
        firework.flags = TimelineRenderFlagIsFirework;
        fireworkLine.notes.append(firework);
        const QVector<TimelineRenderLine> lines{fireworkLine};
        // Probe windows are fractions of the rendered firework duration so they
        // follow kTimelineFireworkDurationSeconds instead of going stale every
        // time the effect is retuned.
        const TimelineVisibleLineRange inTail = timelineRenderVisibleNoteLineRange(
            lines,
            buildNoteVisualEndPrefixMax(lines, false),
            kTimelineFireworkDurationSeconds * 0.6,
            kTimelineFireworkDurationSeconds * 0.7
        );
        const TimelineVisibleLineRange afterTail = timelineRenderVisibleNoteLineRange(
            lines,
            buildNoteVisualEndPrefixMax(lines, false),
            kTimelineFireworkDurationSeconds * 1.1,
            kTimelineFireworkDurationSeconds * 1.2
        );
        expect(inTail.begin == 0 && inTail.end == 1,
               QStringLiteral("visible note range keeps firework tail alive inside its rendered duration"));
        expect(afterTail.begin == 1 && afterTail.end == 1,
               QStringLiteral("visible note range drops firework line after tail ends"));
    }

    {
        TimelineRenderLine earlyLine;
        earlyLine.startSecond = 0.0;
        TimelineRenderNote earlySlide;
        earlySlide.kind = TimelineRenderNoteKind::Slide;
        earlySlide.secondOffset = 0.0;
        earlySlide.slideTraceSecondOffset = 0.2;
        earlySlide.endSecondOffset = 2.0;
        earlyLine.notes.append(earlySlide);

        TimelineRenderLine laterLine;
        laterLine.startSecond = 1.0;
        TimelineRenderNote laterSlide;
        laterSlide.kind = TimelineRenderNoteKind::Slide;
        laterSlide.secondOffset = 0.0;
        laterSlide.slideTraceSecondOffset = 0.1;
        laterSlide.endSecondOffset = 0.6;
        laterLine.notes.append(laterSlide);

        const QVector<TimelineRenderLine> lines{earlyLine, laterLine};
        const TimelineVisibleLineRange range = timelineRenderVisibleNoteLineRange(
            lines,
            buildNoteVisualEndPrefixMax(lines, true),
            1.1,
            1.2
        );
        const QVector<TimelineVisibleNoteRef> order = timelineRenderVisibleNotePaintOrder(lines, range);
        expect(order.size() == 2, QStringLiteral("paint-order helper returns both overlapping visible notes"));
        if (order.size() == 2) {
            expect(order.at(0).lineIndex == 0 && order.at(1).lineIndex == 1,
                   QStringLiteral("paint-order helper keeps later source lines on top of earlier long slides"));
        }
    }

    {
        const QString original = QStringLiteral("1,\n2,\nE");
        const int position = original.indexOf(QLatin1Char('\n'));
        expect(
            incrementalMatchesRebuild(original, position, 1, QString(), 0.0),
            QStringLiteral("incremental merge across removed newline matches full rebuild"));
    }

    {
        const QString original = QStringLiteral("1,2,\nE");
        const int position = original.indexOf(QLatin1Char('2'));
        expect(
            incrementalMatchesRebuild(original, position, 0, QStringLiteral("\n"), 0.0),
            QStringLiteral("incremental split at inserted newline matches full rebuild"));
    }

    {
        const QString original = QStringLiteral("1,\n2,\n3,\nE");
        const int position = original.indexOf(QStringLiteral("2,"));
        const int charsRemoved = QStringLiteral("2,\n3,\n").size();
        expect(
            incrementalMatchesRebuild(original, position, charsRemoved, QStringLiteral("4,\n5,\n6,\n"), 0.0),
            QStringLiteral("incremental multiline replace across line boundaries matches full rebuild"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("E"), 0.0);
        double resolvedSecond = -1.0;
        expect(!model.resolveTimelineSecondForCursor(1, 1, &resolvedSecond),
               QStringLiteral("cursor-time resolver distinguishes missing anchor from valid zero"));
        int line = 0;
        int col = 0;
        double second = -1.0;
        const bool resolved = model.resolvePreviewFollowCursor(1.0, &line, &col, &second);
        expect(!resolved && line == 1 && col == 1 && nearlyEqual(second, 0.0),
               QStringLiteral("follow cursor falls back to L1 C1 / 0 when chart has no comma anchors"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("(120){4}1,12,\nE"), 0.0);
        double resolvedSecond = -1.0;
        expect(model.resolveTimelineSecondForCursor(1, 1, &resolvedSecond) && nearlyEqual(resolvedSecond, 0.0),
               QStringLiteral("cursor-time resolver reports a valid zero-second anchor"));
        expect(nearlyEqual(model.timelineSecondForCursor(1, 1), 0.0),
               QStringLiteral("line-start caret stays on the first segment start"));
        expect(nearlyEqual(model.timelineSecondForCursor(1, 7), 0.0),
               QStringLiteral("control-token interior caret stays on the current segment start"));
        expect(nearlyEqual(model.timelineSecondForCursor(1, 10), 0.0),
               QStringLiteral("caret before the first comma stays on the opening segment"));
        expect(nearlyEqual(model.timelineSecondForCursor(1, 11), 0.5),
               QStringLiteral("caret immediately after a comma advances to the next segment start"));
        expect(nearlyEqual(model.timelineSecondForCursor(1, 12), 0.5),
               QStringLiteral("multi-character note interior stays on the current segment start"));
        expect(nearlyEqual(model.timelineSecondForCursor(1, 14), 1.0),
               QStringLiteral("caret after the final comma advances even without another comma"));

        int line = 0;
        int col = 0;
        double second = -1.0;
        const bool firstResolved = model.resolvePreviewFollowCursor(0.1, &line, &col, &second);
        expect(firstResolved && line == 1 && col == 1 && nearlyEqual(second, 0.0),
               QStringLiteral("follow cursor binds to the latest segment-start anchor at or before preview second"));
        const bool secondResolved = model.resolvePreviewFollowCursor(0.6, &line, &col, &second);
        expect(secondResolved && line == 1 && col == 11 && nearlyEqual(second, 0.5),
               QStringLiteral("follow cursor advances to the next segment-start anchor while playback progresses"));

        TimelineQuickModel::PreviewFollowBinding firstBinding;
        TimelineQuickModel::PreviewFollowBinding secondBinding;
        TimelineQuickModel::PreviewFollowBinding thirdBinding;
        const bool firstBindingResolved = model.resolvePreviewFollowBinding(0.1, &firstBinding);
        const bool secondBindingResolved = model.resolvePreviewFollowBinding(0.4, &secondBinding);
        const bool thirdBindingResolved = model.resolvePreviewFollowBinding(0.6, &thirdBinding);
        expect(firstBindingResolved
                   && secondBindingResolved
                   && bindingsEquivalent(firstBinding, secondBinding)
                   && nearlyEqual(firstBinding.startSecond, 0.0)
                   && nearlyEqual(firstBinding.endSecondExclusive, 0.5),
               QStringLiteral("follow binding stays stable for every second inside the same anchor bucket: %1 vs %2")
                   .arg(describeFollowBinding(firstBinding), describeFollowBinding(secondBinding)));
        expect(thirdBindingResolved
                   && !bindingsEquivalent(firstBinding, thirdBinding)
                   && nearlyEqual(thirdBinding.startSecond, 0.5)
                   && nearlyEqual(thirdBinding.anchorSecond, 0.5)
                   && thirdBinding.anchorLine == 1
                   && thirdBinding.anchorCol == 11,
               QStringLiteral("follow binding only changes after crossing the next anchor second: %1")
                   .arg(describeFollowBinding(thirdBinding)));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1,1-4[8:1],\nE"), 0.0);
        int line = 0;
        int startCol = 0;
        int endCol = 0;
        double second = -1.0;
        const bool resolved = model.resolvePreviewFollowSelection(0.75, &line, &startCol, &endCol, &second);
        expect(resolved && line == 1 && startCol == 3 && endCol == 10 && nearlyEqual(second, 0.5),
               QStringLiteral("follow selection keeps the full current segment including slide timing syntax"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("{16} 8b (160){4},\n{16} (160),\nE"), 0.0);
        int startCol = 0;
        int endCol = 0;
        const bool trimmedResolved = model.resolvePreviewFollowSelectionRange(1, 1, &startCol, &endCol);
        expect(trimmedResolved && startCol == 6 && endCol == 7,
               QStringLiteral("follow selection trims leading and trailing control tokens around the active note text"));
        const bool controlOnlyResolved = model.resolvePreviewFollowSelectionRange(2, 1, &startCol, &endCol);
        expect(controlOnlyResolved && startCol == 1 && endCol == 1,
               QStringLiteral("follow selection falls back to the anchor when a segment only contains control tokens and spaces"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1\n2,\nE"), 0.0);
        TimelineQuickModel::PreviewFollowSpan span;
        double second = -1.0;
        const bool resolved = model.resolvePreviewFollowSpan(0.1, &span, &second);
        expect(resolved
                   && span.hasVisibleBody
                   && span.startLine == 1
                   && span.startCol == 1
                   && span.endLine == 2
                   && span.endCol == 1
                   && span.cursorLine == 2
                   && span.cursorCol == 2
                   && nearlyEqual(second, 0.0),
               QStringLiteral("follow span can continuously cross lines and moves the caret to the span end"));

        TimelineQuickModel::PreviewFollowBinding binding;
        const bool bindingResolved = model.resolvePreviewFollowBinding(0.1, &binding);
        expect(bindingResolved
                   && binding.anchorLine == 2
                   && binding.anchorCol == 1
                   && nearlyEqual(binding.startSecond, 0.0)
                   && nearlyEqual(binding.endSecondExclusive, 0.5)
                   && binding.span.startLine == 1
                   && binding.span.startCol == 1
                   && binding.span.endLine == 2
                   && binding.span.endCol == 1
                   && binding.span.cursorLine == 2
                   && binding.span.cursorCol == 2,
               QStringLiteral("same-second anchors collapse into one follow binding that uses the final anchor but shared span: %1")
                   .arg(describeFollowBinding(binding)));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("{16} (160)\n8b,\nE"), 0.0);
        TimelineQuickModel::PreviewFollowSpan span;
        const bool resolved = model.resolvePreviewFollowSpan(0.01, &span);
        expect(resolved
                   && span.hasVisibleBody
                   && span.startLine == 2
                   && span.startCol == 1
                   && span.endLine == 2
                   && span.endCol == 2
                   && span.cursorLine == 2
                   && span.cursorCol == 3,
               QStringLiteral("follow span can trim a control-only first line and advance to later visible content"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1\n2 (160){4},\nE"), 0.0);
        TimelineQuickModel::PreviewFollowSpan span;
        const bool resolved = model.resolvePreviewFollowSpan(0.1, &span);
        expect(resolved
                   && span.hasVisibleBody
                   && span.startLine == 1
                   && span.startCol == 1
                   && span.endLine == 2
                   && span.endCol == 1
                   && span.cursorLine == 2
                   && span.cursorCol == 2,
               QStringLiteral("follow span trims trailing control tokens on the final line and stops at the last logical character"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("{16} (160),\nE"), 0.0);
        TimelineQuickModel::PreviewFollowSpan span;
        const bool resolved = model.resolvePreviewFollowSpan(0.01, &span);
        expect(resolved
                   && !span.hasVisibleBody
                   && span.startLine == 1
                   && span.startCol == 1
                   && span.endLine == 1
                   && span.endCol == 1
                   && span.cursorLine == 1
                   && span.cursorCol == 2,
               QStringLiteral("follow span reports empty-body fallback spans distinctly from visible-body selections"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1/2,\n1,,\nE"), 0.0);
        int startCol = 0;
        int endCol = 0;
        const bool groupedResolved = model.resolvePreviewFollowSelectionRange(1, 1, &startCol, &endCol);
        expect(groupedResolved && startCol == 1 && endCol == 3,
               QStringLiteral("follow selection keeps slash-joined syntax inside the same comma segment"));
        const bool emptyResolved = model.resolvePreviewFollowSelectionRange(2, 3, &startCol, &endCol);
        expect(emptyResolved && startCol == 3 && endCol == 3,
               QStringLiteral("follow selection falls back to the comma column when the beat segment is empty"));
    }

    {
        // Preview-follow highlight must drop general `||` comments, not only inline
        // time-signature hints. Trimming runs at rebuild time, so the per-tick lookup stays
        // comment-free with no added playback cost.
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("(120){4}1,2-4[4:1] ||note comment\nE"), 0.0);
        int startCol = 0;
        int endCol = 0;
        const bool resolved = model.resolvePreviewFollowSelectionRange(1, 11, &startCol, &endCol);
        expect(resolved && startCol == 11 && endCol == 18,
               QStringLiteral("follow selection trims a trailing || comment after the active slide body (startCol=%1 endCol=%2)")
                   .arg(startCol)
                   .arg(endCol));
    }

    {
        // Span path (the one that actually drives the editor decoration): a trailing comment
        // after a beat, before the next beat, must be excluded from the highlighted span while
        // the active note body (here a slide `2-5[4:1]`, cols 3..10) is kept intact.
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1,2-5[4:1], ||measure tag\n3,\nE"), 0.0);
        TimelineQuickModel::PreviewFollowBinding binding;
        const bool resolved = model.resolvePreviewFollowBinding(0.6, &binding);
        expect(resolved
                   && binding.span.hasVisibleBody
                   && binding.span.startLine == 1
                   && binding.span.startCol == 3
                   && binding.span.endLine == 1
                   && binding.span.endCol == 10,
               QStringLiteral("follow span keeps the slide body but stops before a trailing || comment: %1")
                   .arg(describeFollowBinding(binding)));
    }

    {
        // A standalone comment line between two beats must not leak into the span either.
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1,\n|| section\n2,\nE"), 0.0);
        TimelineQuickModel::PreviewFollowBinding binding;
        const bool resolved = model.resolvePreviewFollowBinding(0.1, &binding);
        expect(resolved
                   && binding.span.hasVisibleBody
                   && binding.span.startLine == 1
                   && binding.span.startCol == 1
                   && binding.span.endLine == 1
                   && binding.span.endCol == 1,
               QStringLiteral("follow span excludes a standalone || comment line between beats: %1")
                   .arg(describeFollowBinding(binding)));
    }

    {
        // Leading comment (and blank line) before the first beat must be trimmed too: the
        // first anchor shares second 0.0 with the empty/comment lines, so the span starts at
        // the document top — the leading trim has to skip blank lines, `||` comments and
        // control tokens to reach the note `1` on line 3.
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("\n|| lead-in\n{16} 1,2,\nE"), 0.0);
        TimelineQuickModel::PreviewFollowBinding binding;
        const bool resolved = model.resolvePreviewFollowBinding(0.05, &binding);
        expect(resolved
                   && binding.span.hasVisibleBody
                   && binding.span.startLine == 3
                   && binding.span.startCol == 6
                   && binding.span.endLine == 3
                   && binding.span.endCol == 6,
               QStringLiteral("follow span skips a leading || comment and blank line before the first beat: %1")
                   .arg(describeFollowBinding(binding)));
    }

    {
        // Mid-chart leading comment: a comment between a comma-heavy line and the next note
        // (which shares the trailing comma's second) must not leak into that note's span.
        const QString chartText = QStringLiteral(
            "{16}8s4[8:4],,,, ,,,, 5-1b[8:1],,,, ,,,,\n\n|| end here\n\n{32}4x,3x,2x");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const double fourthSecond = model.timelineSecondForCursor(5, 5);
        TimelineQuickModel::PreviewFollowBinding binding;
        const bool resolved = model.resolvePreviewFollowBinding(fourthSecond, &binding);
        expect(resolved
                   && binding.span.hasVisibleBody
                   && binding.span.startLine == 5
                   && binding.span.startCol == 5
                   && binding.span.endLine == 5
                   && binding.span.endCol == 6,
               QStringLiteral("follow span skips a mid-chart leading comment between comma runs (4x second=%1): %2")
                   .arg(fourthSecond)
                   .arg(describeFollowBinding(binding)));
    }

    {
        TimelineQuickModel incremental;
        TimelineQuickModel rebuilt;
        const QString original = QStringLiteral("1/2,\n1,,\nE");
        QTextDocument document(original);
        incremental.rebuildFromText(original, 0.0);

        const int position = original.indexOf(QStringLiteral("1/2"));
        QTextCursor cursor(&document);
        cursor.setPosition(position);
        cursor.setPosition(position + QStringLiteral("1/2").size(), QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("1-4[8:1]"));

        const QString updated = document.toPlainText();
        const bool incrementalApplied = incremental.applyTextChange(
            document.toPlainText(),
            position,
            QStringLiteral("1/2").size(),
            QStringLiteral("1-4[8:1]").size(),
            0.0);
        rebuilt.rebuildFromText(updated, 0.0);

        int incrementalStartCol = 0;
        int incrementalEndCol = 0;
        int rebuiltStartCol = 0;
        int rebuiltEndCol = 0;
        const bool incrementalResolved = incremental.resolvePreviewFollowSelectionRange(
            1,
            1,
            &incrementalStartCol,
            &incrementalEndCol);
        const bool rebuiltResolved = rebuilt.resolvePreviewFollowSelectionRange(1, 1, &rebuiltStartCol, &rebuiltEndCol);
        expect(incrementalApplied && incrementalResolved && rebuiltResolved
                   && incrementalStartCol == rebuiltStartCol
                   && incrementalEndCol == rebuiltEndCol
                   && incrementalStartCol == 1
                   && incrementalEndCol == 8,
               QStringLiteral("incremental edits rebuild cached follow-selection ranges the same way as full rebuild"));
    }

    {
        TimelineQuickModel incremental;
        TimelineQuickModel rebuilt;
        const QString original = QStringLiteral("1,\nE");
        QTextDocument document(original);
        incremental.rebuildFromText(original, 0.0);

        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(1, QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("{16} 1-4[8:1] (160)"));

        const QString updated = document.toPlainText();
        const bool incrementalApplied = incremental.applyTextChange(
            document.toPlainText(),
            0,
            1,
            QStringLiteral("{16} 1-4[8:1] (160)").size(),
            0.0);
        rebuilt.rebuildFromText(updated, 0.0);

        int incrementalStartCol = 0;
        int incrementalEndCol = 0;
        int rebuiltStartCol = 0;
        int rebuiltEndCol = 0;
        const bool incrementalResolved = incremental.resolvePreviewFollowSelectionRange(
            1,
            1,
            &incrementalStartCol,
            &incrementalEndCol);
        const bool rebuiltResolved = rebuilt.resolvePreviewFollowSelectionRange(1, 1, &rebuiltStartCol, &rebuiltEndCol);
        expect(incrementalApplied && incrementalResolved && rebuiltResolved
                   && incrementalStartCol == rebuiltStartCol
                   && incrementalEndCol == rebuiltEndCol
                   && incrementalStartCol == 6
                   && incrementalEndCol == 13,
               QStringLiteral("incremental edits rebuild trimmed follow-selection ranges with boundary control tokens"));
    }

    {
        TimelineQuickModel incremental;
        TimelineQuickModel rebuilt;
        const QString original = QStringLiteral("1,\nE");
        QTextDocument document(original);
        incremental.rebuildFromText(original, 0.0);

        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(1, QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("{16} (160)\n8b"));

        const QString updated = document.toPlainText();
        const bool incrementalApplied = incremental.applyTextChange(
            document.toPlainText(),
            0,
            1,
            QStringLiteral("{16} (160)\n8b").size(),
            0.0);
        rebuilt.rebuildFromText(updated, 0.0);

        TimelineQuickModel::PreviewFollowSpan incrementalSpan;
        TimelineQuickModel::PreviewFollowSpan rebuiltSpan;
        const bool incrementalResolved = incremental.resolvePreviewFollowSpan(0.01, &incrementalSpan);
        const bool rebuiltResolved = rebuilt.resolvePreviewFollowSpan(0.01, &rebuiltSpan);
        expect(incrementalApplied
                   && incrementalResolved
                   && rebuiltResolved
                   && incrementalSpan.hasVisibleBody == rebuiltSpan.hasVisibleBody
                   && incrementalSpan.startLine == rebuiltSpan.startLine
                   && incrementalSpan.startCol == rebuiltSpan.startCol
                   && incrementalSpan.endLine == rebuiltSpan.endLine
                   && incrementalSpan.endCol == rebuiltSpan.endCol
                   && incrementalSpan.cursorLine == rebuiltSpan.cursorLine
                   && incrementalSpan.cursorCol == rebuiltSpan.cursorCol
                   && incrementalSpan.startLine == 2
                   && incrementalSpan.startCol == 1
                   && incrementalSpan.endLine == 2
                   && incrementalSpan.endCol == 2
                   && incrementalSpan.cursorLine == 2
                   && incrementalSpan.cursorCol == 3
                   && incrementalSpan.hasVisibleBody,
               QStringLiteral("incremental edits rebuild cached follow spans the same way as full rebuild"));

        TimelineQuickModel::PreviewFollowBinding incrementalBinding;
        TimelineQuickModel::PreviewFollowBinding rebuiltBinding;
        const bool incrementalBindingResolved = incremental.resolvePreviewFollowBinding(0.01, &incrementalBinding);
        const bool rebuiltBindingResolved = rebuilt.resolvePreviewFollowBinding(0.01, &rebuiltBinding);
        expect(incrementalBindingResolved
                   && rebuiltBindingResolved
                   && bindingsEquivalent(incrementalBinding, rebuiltBinding),
               QStringLiteral("incremental edits rebuild follow bindings the same way as full rebuild: %1 vs %2")
                   .arg(describeFollowBinding(incrementalBinding), describeFollowBinding(rebuiltBinding)));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1,,\nE"), 0.0);
        int line = 0;
        int col = 0;
        double second = -1.0;
        const bool navigateResolved = model.resolveTimelineNavigateCursor(0.75, &line, &col, &second);
        expect(navigateResolved && line == 1 && col == 3 && nearlyEqual(second, 0.5),
               QStringLiteral("timeline navigate cursor uses the same segment-start anchor rule as preview follow"));
        const bool followResolved = model.resolvePreviewFollowCursor(0.75, &line, &col, &second);
        expect(followResolved && line == 1 && col == 3 && nearlyEqual(second, 0.5),
               QStringLiteral("follow cursor can advance within the same source line to the latest past segment start"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("(120){4}12\nE"), 0.0);
        expect(nearlyEqual(model.timelineSecondForCursor(1, 11), 0.0),
               QStringLiteral("line end does not extrapolate when the chart has no comma anchors"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("(120){4}1,12,"), 0.0);
        QVector<ComparableBeat> quickBeats = flattenSnapshotBeats(model.snapshot());
        sortComparableBeats(&quickBeats);
        expect(quickBeats.size() == 3, QStringLiteral("charts without E append one trailing implicit beat line"));
        if (quickBeats.size() == 3) {
            expect(nearlyEqual(quickBeats.at(0).second, 0.0)
                       && nearlyEqual(quickBeats.at(1).second, 0.5)
                       && nearlyEqual(quickBeats.at(2).second, 1.0),
                   QStringLiteral("the trailing implicit beat line lands on the next segment boundary"));
        }
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("(120){4}1,12,E || terminal comment"), 0.0);
        QVector<ComparableBeat> quickBeats = flattenSnapshotBeats(model.snapshot());
        sortComparableBeats(&quickBeats);
        expect(quickBeats.size() == 2, QStringLiteral("inline terminal marker suppresses the trailing implicit beat even with a comment"));
        expect(nearlyEqual(model.timelineSecondForCursor(1, 14), 1.0),
               QStringLiteral("inline terminal marker keeps caret timing at the segment boundary before E"));
    }

    {
        const QString chartText = QStringLiteral("(150)\n\n|| 3 / 4\n1,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const QVector<double> starts = flattenSnapshotLineStarts(model.snapshot());
        expect(starts.size() == 5, QStringLiteral("quick model keeps line-start timing for empty/control-only lines"));
        expect(lineStartsNondecreasing(starts),
               QStringLiteral("line-start timing stays nondecreasing across empty/control-only lines"));
        if (starts.size() == 5) {
            expect(nearlyEqual(starts.at(0), 0.0)
                       && nearlyEqual(starts.at(1), 0.0)
                       && nearlyEqual(starts.at(2), 0.0)
                       && nearlyEqual(starts.at(3), 0.0)
                       && nearlyEqual(starts.at(4), 0.4),
                   QStringLiteral("control-only and empty lines keep the correct shared line-start timing"));
        }
    }

    {
        const QString chartText = QStringLiteral("(240),\n,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const QVector<double> starts = flattenSnapshotLineStarts(model.snapshot());
        expect(lineStartsNondecreasing(starts),
               QStringLiteral("line-start timing stays nondecreasing after BPM control tokens"));
        if (starts.size() == 3) {
            expect(nearlyEqual(starts.at(0), 0.0)
                       && nearlyEqual(starts.at(1), 0.25)
                       && nearlyEqual(starts.at(2), 0.5),
                   QStringLiteral("BPM control tokens shift downstream line starts correctly"));
        } else {
            expect(false, QStringLiteral("BPM control token chart keeps three rendered lines"));
        }
    }

    {
        const QString chartText = QStringLiteral("{8},\n,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const QVector<double> starts = flattenSnapshotLineStarts(model.snapshot());
        expect(lineStartsNondecreasing(starts),
               QStringLiteral("line-start timing stays nondecreasing after {beats} control tokens"));
        if (starts.size() == 3) {
            expect(nearlyEqual(starts.at(0), 0.0)
                       && nearlyEqual(starts.at(1), 0.25)
                       && nearlyEqual(starts.at(2), 0.5),
                   QStringLiteral("{beats} control tokens shift downstream line starts correctly"));
        } else {
            expect(false, QStringLiteral("{beats} control token chart keeps three rendered lines"));
        }
    }

    {
        const QString originalText = QStringLiteral(",\n,\n,\nE");
        QTextDocument document(originalText);
        TimelineQuickModel incremental;
        TimelineQuickModel rebuilt;
        incremental.rebuildFromText(document.toPlainText(), 0.0);
        applyDocumentChange(&document, 0, 0, QStringLiteral("(240){8}"));
        incremental.applyTextChange(document.toPlainText(), 0, 0, QStringLiteral("(240){8}").size(), 0.0);
        rebuilt.rebuildFromText(document.toPlainText(), 0.0);

        const QVector<double> incrementalStarts = flattenSnapshotLineStarts(incremental.snapshot());
        const QVector<double> rebuiltStarts = flattenSnapshotLineStarts(rebuilt.snapshot());
        expect(
            sameDoubleVector(incrementalStarts, rebuiltStarts),
            QStringLiteral("incremental edits keep per-line startSecond aligned with full rebuild across later lines"));
        if (rebuiltStarts.size() == 4) {
            expect(nearlyEqual(rebuiltStarts.at(0), 0.0)
                       && nearlyEqual(rebuiltStarts.at(1), 0.125)
                       && nearlyEqual(rebuiltStarts.at(2), 0.25)
                       && nearlyEqual(rebuiltStarts.at(3), 0.375),
                   QStringLiteral("combined BPM/{beats} edits propagate the expected line-start timing"));
        } else {
            expect(false, QStringLiteral("combined BPM/{beats} edit keeps four rendered lines"));
        }
    }

    {
        const QString chartText = QStringLiteral("1,\n2,\n3,\n4,\n5,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        QVector<ComparableBeat> quickBeats = flattenSnapshotBeats(model.snapshot());
        QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
        sortComparableBeats(&quickBeats);
        expect(quickBeats.size() == 5, QStringLiteral("quick model keeps all comma beat markers across lines"));
        if (quickBeats.size() == 5) {
            expect(!quickBeats.at(0).major && !quickBeats.at(1).major && !quickBeats.at(2).major
                       && !quickBeats.at(3).major && !quickBeats.at(4).major,
                   QStringLiteral("comma beat markers stay independent from measure-line styling"));
        }
        expect(quickMeasures.size() == 3, QStringLiteral("quick model keeps the trailing virtual-comma measure line across source lines"));
        if (quickMeasures.size() == 3) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0)
                       && nearlyEqual(quickMeasures.at(1), 2.0)
                       && nearlyEqual(quickMeasures.at(2), 4.0),
                   QStringLiteral("measure lines continue by meter time and extend one future line for the implicit trailing comma"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("parser and quick model agree on cross-line measure-line semantics: %1").arg(diff));
    }

    {
        const QString chartText = QStringLiteral("{4},,{8},,,,,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        QVector<ComparableBeat> quickBeats = flattenSnapshotBeats(model.snapshot());
        QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
        sortComparableBeats(&quickBeats);
        expect(quickBeats.size() == 7, QStringLiteral("quick model keeps mixed-subdivision comma markers"));
        if (quickBeats.size() == 7) {
            expect(!quickBeats.at(0).major
                       && !quickBeats.at(1).major
                       && !quickBeats.at(2).major
                       && !quickBeats.at(3).major
                       && !quickBeats.at(4).major
                       && !quickBeats.at(5).major
                       && !quickBeats.at(6).major,
                   QStringLiteral("changing {beats} does not turn comma beat markers into measure lines"));
        }
        expect(quickMeasures.size() == 3, QStringLiteral("quick model keeps meter-based measure lines and the trailing virtual-comma line across {beats} changes"));
        if (quickMeasures.size() == 3) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0)
                       && nearlyEqual(quickMeasures.at(1), 2.0)
                       && nearlyEqual(quickMeasures.at(2), 4.0),
                   QStringLiteral("changing {beats} does not move independent 4/4 measure lines or the trailing extension"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("parser and quick model agree on mixed-subdivision measure-line semantics: %1").arg(diff));
    }

    {
        const QString chartText = QStringLiteral("{3},,,,,,,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        QVector<ComparableBeat> quickBeats = flattenSnapshotBeats(model.snapshot());
        QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
        sortComparableBeats(&quickBeats);
        expect(quickBeats.size() == 7, QStringLiteral("quick model keeps comma markers for 3-beat subdivisions"));
        if (quickBeats.size() == 7) {
            expect(!quickBeats.at(0).major
                       && !quickBeats.at(1).major
                       && !quickBeats.at(2).major
                       && !quickBeats.at(3).major
                       && !quickBeats.at(4).major
                       && !quickBeats.at(5).major
                       && !quickBeats.at(6).major,
                   QStringLiteral("3-beat comma markers remain separate from measure-line styling"));
        }
        expect(quickMeasures.size() == 4, QStringLiteral("quick model keeps independent measure lines and the trailing virtual-comma line under {3}"));
        if (quickMeasures.size() == 4) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0)
                       && nearlyEqual(quickMeasures.at(1), 2.0)
                       && nearlyEqual(quickMeasures.at(2), 4.0)
                       && nearlyEqual(quickMeasures.at(3), 6.0),
                   QStringLiteral("independent 4/4 measure lines keep their own timing under {3}, plus one trailing future line"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("parser and quick model agree on 3-beat measure-line semantics: %1").arg(diff));
    }

    {
        const QString chartText = QStringLiteral("{48},,,,,,\n,,,,,,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(snapshot.lines.size() >= 2, QStringLiteral("quick model builds dense subdivision beat metadata across line breaks"));
        if (snapshot.lines.size() >= 2) {
            const QVector<TimelineRenderBeat>& firstLineBeats = snapshot.lines.at(0).beats;
            const QVector<TimelineRenderBeat>& secondLineBeats = snapshot.lines.at(1).beats;
            expect(firstLineBeats.size() == 6 && secondLineBeats.size() == 6,
                   QStringLiteral("dense subdivision chart keeps all comma markers on both source lines"));
            if (firstLineBeats.size() == 6 && secondLineBeats.size() == 6) {
                bool indicesOk = true;
                for (int index = 0; index < firstLineBeats.size(); ++index) {
                    indicesOk = indicesOk
                        && firstLineBeats.at(index).subdivisionBeats == 48
                        && firstLineBeats.at(index).subdivisionIndex == index;
                }
                for (int index = 0; index < secondLineBeats.size(); ++index) {
                    indicesOk = indicesOk
                        && secondLineBeats.at(index).subdivisionBeats == 48
                        && secondLineBeats.at(index).subdivisionIndex == index + 6;
                }
                expect(indicesOk,
                       QStringLiteral("dense subdivision beat metadata carries forward across line breaks without resetting"));
            }
        }
    }

    {
        const QString chartText = QStringLiteral("{48},,,\n{48},,,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(snapshot.lines.size() >= 2, QStringLiteral("quick model keeps subdivision reset metadata when {beats} restarts"));
        if (snapshot.lines.size() >= 2) {
            const QVector<TimelineRenderBeat>& firstLineBeats = snapshot.lines.at(0).beats;
            const QVector<TimelineRenderBeat>& secondLineBeats = snapshot.lines.at(1).beats;
            expect(firstLineBeats.size() == 3 && secondLineBeats.size() == 3,
                   QStringLiteral("explicit dense {beats} restarts still keep comma markers on each line"));
            if (firstLineBeats.size() == 3 && secondLineBeats.size() == 3) {
                bool resetOk = true;
                for (int index = 0; index < firstLineBeats.size(); ++index) {
                    resetOk = resetOk
                        && firstLineBeats.at(index).subdivisionBeats == 48
                        && firstLineBeats.at(index).subdivisionIndex == index;
                }
                for (int index = 0; index < secondLineBeats.size(); ++index) {
                    resetOk = resetOk
                        && secondLineBeats.at(index).subdivisionBeats == 48
                        && secondLineBeats.at(index).subdivisionIndex == index;
                }
                expect(resetOk,
                       QStringLiteral("explicit {beats} tokens reset dense subdivision metadata for later rendering"));
            }
        }
    }

    {
        const QString chartText = QStringLiteral(",,(150),\n,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        QVector<ComparableBeat> quickBeats = flattenSnapshotBeats(model.snapshot());
        QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
        sortComparableBeats(&quickBeats);
        expect(quickBeats.size() == 4, QStringLiteral("quick model keeps comma markers across BPM changes"));
        if (quickBeats.size() == 4) {
            expect(!quickBeats.at(0).major
                       && !quickBeats.at(1).major
                       && !quickBeats.at(2).major
                       && !quickBeats.at(3).major,
                   QStringLiteral("BPM changes do not promote comma markers into measure lines"));
        }
        expect(quickMeasures.size() == 3, QStringLiteral("quick model keeps independent measure markers and the trailing virtual-comma line across BPM changes"));
        if (quickMeasures.size() == 3) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0)
                       && nearlyEqual(quickMeasures.at(1), 1.0)
                       && nearlyEqual(quickMeasures.at(2), 2.6),
                   QStringLiteral("a BPM change resets the independent measure-line timeline at the BPM position and still extends one future line"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("parser and quick model agree on BPM-reset measure-line semantics: %1").arg(diff));
    }

    {
        // User-supplied 变拍 fixture: a (126) BPM line, a whole-note {1} pickup
        // measure, then a boundary-aligned || 3/4 followed by two {16} 3/4
        // measures. The || 3/4 restart lands EXACTLY on the auto-advanced 4/4
        // measure line, so it must hand the span after it to the NEW 3/4 meter
        // (problem B), and every measure must record the real beat step so the
        // drawing keeps subdivision lines on the true grid (problem A).
        const QString chartText = QStringLiteral(
            "(126)\n"
            "{1},|| 3/4\n"
            "{16}2h[16:3],,,4h[16:3],,,5h[8:1],,6,,47,,\n"
            "{16}7h[16:3],,,5h[16:3],,,4h[8:1],,3,,52,,");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        const double step126q = 10.0 / 21.0;  // noteStepSeconds(126, 4)
        expect(snapshot.measureLineSeconds.size() == 5,
               QStringLiteral("变拍fixture: 4 real measure lines plus one trailing extrapolation"));
        if (snapshot.measureLineSeconds.size() == 5) {
            expect(nearlyEqual(snapshot.measureLineSeconds.at(0), 0.0)
                       && nearlyEqual(snapshot.measureLineSeconds.at(1), 40.0 / 21.0)
                       && nearlyEqual(snapshot.measureLineSeconds.at(2), 70.0 / 21.0)
                       && nearlyEqual(snapshot.measureLineSeconds.at(3), 100.0 / 21.0)
                       && nearlyEqual(snapshot.measureLineSeconds.at(4), 130.0 / 21.0),
                   QStringLiteral("变拍fixture: measure lines follow the 4/4 then 3/4 cadence at 126 BPM"));
        }
        const QVector<int> expectNumerators{4, 3, 3, 3, 3};
        const QVector<int> expectDenominators{4, 4, 4, 4, 4};
        expect(snapshot.measureLineMeterNumerators == expectNumerators
                   && snapshot.measureLineMeterDenominators == expectDenominators,
               QStringLiteral("变拍fixture: the boundary-coincident || 3/4 hands its span to the new meter (B fix)"));
        bool stepsOk = snapshot.measureLineBeatStepSeconds.size() == 5;
        for (const double beatStep : snapshot.measureLineBeatStepSeconds) {
            stepsOk = stepsOk && nearlyEqual(beatStep, step126q);
        }
        expect(stepsOk,
               QStringLiteral("变拍fixture: every measure records the real beat step for true-grid subdivisions (A data)"));

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("变拍fixture: parser and quick model agree on measure-line cadence: %1").arg(diff));
    }

    {
        // Mid-measure 变拍 truncates the running 4/4 measure (problem A): the
        // truncated span keeps the OLD 4/4 meter and the real 0.5s beat step (NOT
        // span/numerator = 0.25), so the drawing clips subdivisions to the 变拍.
        const QString chartText = QStringLiteral(",,|| 3/4\n,,,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(snapshot.measureLineSeconds.size() >= 2
                   && nearlyEqual(snapshot.measureLineSeconds.at(0), 0.0)
                   && nearlyEqual(snapshot.measureLineSeconds.at(1), 1.0),
               QStringLiteral("mid-measure变拍: a truncated measure line lands at the 变拍 instant (1.0s)"));
        expect(snapshot.measureLineMeterNumerators.size() >= 2
                   && snapshot.measureLineMeterNumerators.at(0) == 4
                   && snapshot.measureLineMeterNumerators.at(1) == 3,
               QStringLiteral("mid-measure变拍: truncated span stays 4/4, the next span is 3/4"));
        expect(snapshot.measureLineBeatStepSeconds.size() >= 2
                   && nearlyEqual(snapshot.measureLineBeatStepSeconds.at(0), 0.5),
               QStringLiteral("mid-measure变拍: truncated 4/4 keeps the real 0.5s step, not span/numerator"));
    }

    {
        // Incremental edit on a terminal-E 变拍 chart: changing an upstream BPM
        // must also shift the terminal line's stored cutoff (problem E), or a real
        // measure line gets filtered out and the incremental snapshot diverges
        // from a full rebuild (now compared incl. the per-measure meter arrays).
        const QString original =
            QStringLiteral("(120)1,2,3,4,\n|| 6/8\n1,2,3,\n(180)4,5,6,\nE");
        const int position = original.indexOf(QStringLiteral("120"));
        expect(position >= 0, QStringLiteral("terminal-E变拍: fixture has an editable BPM token"));
        expect(incrementalMatchesRebuild(original, position, 3, QStringLiteral("90"), 0.0),
               QStringLiteral("terminal-E变拍: incremental BPM edit matches full rebuild (terminalSecond shift)"));
    }

    {
        const QString chartText = QStringLiteral(",\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
        std::sort(quickMeasures.begin(), quickMeasures.end());
        expect(quickMeasures.size() == 2, QStringLiteral("quick model keeps the next measure marker even when E appears early"));
        if (quickMeasures.size() == 2) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0) && nearlyEqual(quickMeasures.at(1), 2.0),
                   QStringLiteral("measure markers are no longer capped by terminal E or remaining text"));
        }
        expect(nearlyEqual(model.snapshot().trailingMeasureLineStartSecond, 0.0)
                   && nearlyEqual(model.snapshot().trailingMeasureLineStepSeconds, 2.0),
               QStringLiteral("snapshot exposes trailing measure cadence for view-side extension"));
    }

    {
        const miacode::simai::SimaiTimingMetadata timingMetadata =
            miacode::simai::buildTimingMetadataFromRawText(QStringLiteral("&whole_time_signature=3/4"), true);
        const QString chartText = QStringLiteral(",,,\n,,,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0, timingMetadata);
        QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
        std::sort(quickMeasures.begin(), quickMeasures.end());
        expect(quickMeasures.size() == 4, QStringLiteral("quick model keeps whole_time_signature-driven measure markers plus the trailing virtual-comma line"));
        if (quickMeasures.size() == 4) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0)
                       && nearlyEqual(quickMeasures.at(1), 1.5)
                       && nearlyEqual(quickMeasures.at(2), 3.0)
                       && nearlyEqual(quickMeasures.at(3), 4.5),
                   QStringLiteral("whole_time_signature metadata shifts quick-model measure timing to 3/4 and keeps one future line"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff, timingMetadata),
               QStringLiteral("parser and quick model agree on whole_time_signature semantics: %1").arg(diff));
    }

    {
        const QString chartText = QStringLiteral(",,|| 3 / 4\n,,,\nE");
        TimelineQuickModel model;
        model.rebuildFromText(chartText, 0.0);
        QVector<double> quickMeasures = flattenSnapshotMeasureLines(model.snapshot());
        std::sort(quickMeasures.begin(), quickMeasures.end());
        expect(quickMeasures.size() == 4, QStringLiteral("quick model accepts inline time-signature comments and keeps the trailing virtual-comma line"));
        if (quickMeasures.size() == 4) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0)
                       && nearlyEqual(quickMeasures.at(1), 1.0)
                       && nearlyEqual(quickMeasures.at(2), 2.5)
                       && nearlyEqual(quickMeasures.at(3), 4.0),
                   QStringLiteral("inline time-signature comments truncate and restart quick-model measure timing, then extend one future line"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("parser and quick model agree on inline time-signature semantics: %1").arg(diff));
    }

    {
        const QString original = QStringLiteral(",\n,\n,\n,\nE");
        const int position = 0;
        expect(
            incrementalMatchesRebuild(original, position, 1, QStringLiteral("(240),(120){8},{4}"), 0.0),
            QStringLiteral("incremental reparse propagates changed measure-line state even when downstream seconds stay aligned"));
    }

    {
        const QString original = QStringLiteral(
            "(193){1},\n"
            "{1}1>8[4:7],\n"
            "{1},\n"
            "{1}1>8[4:7],\n"
            "{1},\n"
            "{1}1pp3pp5pp7pp1[4:13],\n"
            "E");
        const int position = QStringLiteral("(193)").size();
        expect(
            incrementalMatchesRebuild(original, position, 0, QStringLiteral("{8},"), 0.0),
            QStringLiteral("incremental reparse keeps downstream measure lines aligned after inserting leading {8},"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1$$bx/1@bx-4[0.5##8:1]/1!-4[0.5##8:1],\nE"), 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for material/delay syntax"));
        if (!snapshot.lines.isEmpty()) {
            const QVector<TimelineRenderNote>& notes = snapshot.lines.constFirst().notes;
            expect(notes.size() == 3, QStringLiteral("quick model keeps tap and both slides"));
            if (notes.size() == 3) {
                const TimelineRenderNote& tap = notes.at(0);
                const TimelineRenderNote& headTapSlide = notes.at(1);
                const TimelineRenderNote& headlessSlide = notes.at(2);

                expect(timelineRenderFlagSet(tap, TimelineRenderFlagTapUsesStarMaterial),
                       QStringLiteral("quick model preserves tap star-material flag"));
                expect(timelineRenderFlagSet(tap, TimelineRenderFlagTapStarDouble),
                       QStringLiteral("quick model preserves tap $$ flag"));
                expect(timelineRenderFlagSet(tap, TimelineRenderFlagIsBreak)
                           && timelineRenderFlagSet(tap, TimelineRenderFlagIsEx),
                       QStringLiteral("quick model preserves tap b/x flags on star-material tap"));

                expect(timelineRenderFlagSet(headTapSlide, TimelineRenderFlagHasHeadStar),
                       QStringLiteral("quick model keeps @ slide head visible"));
                expect(timelineRenderFlagSet(headTapSlide, TimelineRenderFlagSlideHeadUsesTapMaterial),
                       QStringLiteral("quick model preserves @ head tap-material flag"));
                expect(timelineRenderFlagSet(headTapSlide, TimelineRenderFlagHeadBreak)
                           && timelineRenderFlagSet(headTapSlide, TimelineRenderFlagHeadEx),
                       QStringLiteral("quick model preserves @ slide head b/x flags"));
                expect(nearlyEqual(headTapSlide.slideTraceSecondOffset, 0.5)
                           && nearlyEqual(headTapSlide.endSecondOffset, 0.75),
                       QStringLiteral("quick model parses [wait##fraction] timing on @ slide"));

                expect(!timelineRenderFlagSet(headlessSlide, TimelineRenderFlagHasHeadStar),
                       QStringLiteral("quick model clears head icon flag for headless slide"));
                expect(timelineRenderFlagSet(headlessSlide, TimelineRenderFlagHeadlessImmediate),
                       QStringLiteral("quick model preserves ! immediate wait-visual flag"));
                expect(nearlyEqual(headlessSlide.slideTraceSecondOffset, 0.5)
                           && nearlyEqual(headlessSlide.endSecondOffset, 0.75),
                       QStringLiteral("quick model parses [wait##fraction] timing on headless slide"));
            }
        }
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1m/A1m/1-3[2:1]m,\n1M/A1M/1-3[2:1]M,\nE"), 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(snapshot.lines.size() >= 2, QStringLiteral("quick model builds snapshot for mine case-sensitivity repro"));
        if (snapshot.lines.size() >= 2) {
            const QVector<TimelineRenderNote>& lowerNotes = snapshot.lines.at(0).notes;
            expect(lowerNotes.size() == 3, QStringLiteral("quick model keeps lowercase mine tap, touch, and slide"));
            if (lowerNotes.size() == 3) {
                expect(timelineRenderFlagSet(lowerNotes.at(0), TimelineRenderFlagIsMine),
                       QStringLiteral("quick model marks lowercase tap mine"));
                expect(timelineRenderFlagSet(lowerNotes.at(1), TimelineRenderFlagIsMine),
                       QStringLiteral("quick model marks lowercase touch mine"));
                expect(timelineRenderFlagSet(lowerNotes.at(2), TimelineRenderFlagTrackMine),
                       QStringLiteral("quick model marks lowercase slide track mine"));
            }
            expect(snapshot.lines.at(1).notes.isEmpty(),
                   QStringLiteral("quick model rejects uppercase M mine variants"));
        }
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1h,1h[8:0],\nE"), 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for zero-duration holds"));
        if (!snapshot.lines.isEmpty()) {
            const QVector<TimelineRenderNote>& notes = snapshot.lines.constFirst().notes;
            expect(notes.size() == 2, QStringLiteral("quick model keeps both bare and bracketed zero-duration holds"));
            if (notes.size() == 2) {
                const TimelineRenderNote& bareHold = notes.at(0);
                const TimelineRenderNote& bracketedHold = notes.at(1);
                expect(bareHold.kind == TimelineRenderNoteKind::Hold
                           && nearlyEqual(bareHold.endSecondOffset, bareHold.secondOffset),
                       QStringLiteral("quick model treats bare h as a zero-duration hold"));
                expect(bracketedHold.kind == TimelineRenderNoteKind::Hold
                           && nearlyEqual(bracketedHold.endSecondOffset, bracketedHold.secondOffset),
                       QStringLiteral("quick model keeps explicit [8:0] hold at zero duration"));
            }
        }
    }

    {
        const QString chart = QStringLiteral("Ch/A1h/Ch[]/1h,\nE");
        QString diff;
        const bool matches = snapshotMatchesParser(chart, &diff);
        if (!matches) {
            err << diff << '\n';
        }
        expect(matches,
               QStringLiteral("quick model matches parser for bare and empty-bracket zero-duration touch-holds"));

        TimelineQuickModel model;
        model.rebuildFromText(chart, 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for zero-duration touch-holds"));
        if (!snapshot.lines.isEmpty()) {
            const QVector<TimelineRenderNote>& notes = snapshot.lines.constFirst().notes;
            expect(notes.size() == 4, QStringLiteral("quick model keeps Ch, A1h, Ch[], and 1h"));
            if (notes.size() == 4) {
                int touchHoldCount = 0;
                int tapHoldCount = 0;
                for (const TimelineRenderNote& note : notes) {
                    if (note.kind == TimelineRenderNoteKind::TouchHold) {
                        ++touchHoldCount;
                        expect(nearlyEqual(note.endSecondOffset, note.secondOffset),
                               QStringLiteral("zero-duration touch-hold ends on its head timing"));
                    } else if (note.kind == TimelineRenderNoteKind::Hold) {
                        ++tapHoldCount;
                        expect(nearlyEqual(note.endSecondOffset, note.secondOffset),
                               QStringLiteral("bare tap hold remains zero-duration"));
                    }
                }
                expect(touchHoldCount == 3, QStringLiteral("quick model emits three touch-hold notes"));
                expect(tapHoldCount == 1, QStringLiteral("quick model emits one tap hold note"));
            }
        }
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("C[],\nE"), 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for invalid ordinary touch bracket"));
        if (!snapshot.lines.isEmpty()) {
            expect(snapshot.lines.constFirst().notes.isEmpty(),
                   QStringLiteral("quick model rejects bracketed ordinary touch without h"));
        }
    }

    {
        const QString chart = QStringLiteral("1``2,3```4,\nE");
        QString diff;
        const bool matches = snapshotMatchesParser(chart, &diff);
        if (!matches) {
            err << diff << '\n';
        }
        expect(matches,
               QStringLiteral("quick model treats repeated backticks as a single backtick separator"));
        TimelineQuickModel model;
        model.rebuildFromText(chart, 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for repeated backticks"));
        if (!snapshot.lines.isEmpty()) {
            const QVector<TimelineRenderNote>& notes = snapshot.lines.constFirst().notes;
            expect(notes.size() == 4, QStringLiteral("quick model emits repeated-backtick notes"));
            if (notes.size() == 4) {
                expect(nearlyEqual(notes.at(0).secondOffset, notes.at(1).secondOffset),
                       QStringLiteral("double backtick keeps adjacent notes at the same time"));
                expect(nearlyEqual(notes.at(2).secondOffset, notes.at(3).secondOffset),
                       QStringLiteral("triple backtick keeps adjacent notes at the same time"));
            }
        }
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1<5[4:1],8-3[8:1],\nE"), 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for bracketed slides"));
        if (!snapshot.lines.isEmpty()) {
            const QVector<TimelineRenderNote>& notes = snapshot.lines.constFirst().notes;
            expect(notes.size() == 2, QStringLiteral("quick model keeps both bracketed slides"));
            if (notes.size() == 2) {
                expect(notes.at(0).kind == TimelineRenderNoteKind::Slide && notes.at(0).endLane == 5,
                       QStringLiteral("quick model ignores slide timing digits when inferring < slide end lane"));
                expect(notes.at(1).kind == TimelineRenderNoteKind::Slide && notes.at(1).endLane == 3,
                       QStringLiteral("quick model ignores slide timing digits when inferring - slide end lane"));
            }
        }
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("8-3[8:1]*-5[8:1],\nE"), 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for same-head slide branches"));
        if (!snapshot.lines.isEmpty()) {
            const QVector<TimelineRenderNote>& notes = snapshot.lines.constFirst().notes;
            expect(notes.size() == 2, QStringLiteral("quick model splits same-head slide token into two branches"));
            if (notes.size() == 2) {
                const TimelineRenderNote& left = notes.at(0);
                const TimelineRenderNote& right = notes.at(1);
                expect(left.kind == TimelineRenderNoteKind::Slide && right.kind == TimelineRenderNoteKind::Slide,
                       QStringLiteral("quick model keeps same-head branches as slides"));
                expect(left.endLane == 3 && right.endLane == 5,
                       QStringLiteral("quick model preserves per-branch end lanes for same-head slides"));
                expect(nearlyEqual(left.slideTraceSecondOffset, right.slideTraceSecondOffset)
                           && nearlyEqual(left.endSecondOffset, right.endSecondOffset),
                       QStringLiteral("quick model keeps same-head branch timing independent instead of stacking durations"));
                expect(timelineRenderFlagSet(left, TimelineRenderFlagSameHeadSlide)
                           && timelineRenderFlagSet(right, TimelineRenderFlagSameHeadSlide),
                       QStringLiteral("quick model marks same-head slide branches with same-head flag"));
                expect(timelineRenderFlagSet(left, TimelineRenderFlagSlideEach)
                           && timelineRenderFlagSet(right, TimelineRenderFlagSlideEach),
                       QStringLiteral("quick model marks same-head slide branches as shared-track slides"));
                expect(!timelineRenderFlagSet(left, TimelineRenderFlagHeadEach)
                           && !timelineRenderFlagSet(right, TimelineRenderFlagHeadEach),
                       QStringLiteral("quick model does not mistake same-head-only groups for head-each groups"));
            }
        }
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("6<8[160#16:1]/2>4[16:1],\nE"), 0.0);
        const TimelineRenderSnapshot snapshot = model.snapshot();
        expect(!snapshot.lines.isEmpty(), QStringLiteral("quick model builds snapshot for hashed-duration slash slide each repro"));
        if (!snapshot.lines.isEmpty()) {
            const QVector<TimelineRenderNote>& notes = snapshot.lines.constFirst().notes;
            int slideCount = 0;
            int slideEachCount = 0;
            for (const TimelineRenderNote& note : notes) {
                if (note.kind != TimelineRenderNoteKind::Slide) {
                    continue;
                }
                ++slideCount;
                if (timelineRenderFlagSet(note, TimelineRenderFlagSlideEach)) {
                    ++slideEachCount;
                }
            }
            expect(slideCount == 2, QStringLiteral("quick model emits two slides for hashed-duration slash each repro"));
            expect(slideEachCount == 2, QStringLiteral("quick model marks slash-paired slides as yellow tracks despite # timing"));
        }
    }

    {
        const QString chart = QStringLiteral(
            "(264) {80},,,,,,,,8-4[8:1],,,,,,,,,,,,,,,,,,,,8,,,,,,,,,,,,,,,,,,,,3/5,,,,,,,,,,,,,,,,,,,,8>4[4:1],,,,,1>5[4:1],,,,,2>6[4:1],,\n"
            "{80},,,3<7[4:1],,,,,,,,,,,,,,,,,,,,,,,,,1-5[8:1],,,,,,,,,,,,,,,,,,,,1,,,,,,,,,,,,,,,,,,,,3/7,,,,,,,,,,,,\n"
            "{80},,,,,,,,1<5[4:1],,,,,8<4[4:1],,,,,7<3[4:1],,,,,6>2[4:1],,,,,1,,,,,,,,,,,,,,,,,,,,8-3[8:1]*-5[8:1],,,,,,,,,,,,,,,,,,,,8,,,,,,,,,,,,\n"
            "{80},,,,,,,,2/6,,,,,,,,,,,,,,,,,,,,8>4[4:1],,,,,1>5[4:1],,,,,2>6[4:1],,,,,3<7[4:1],,,,,8,,,,,,,,,,,,,,,,,,,,1-4[8:1]*-6[8:1],,,,,,,,,,,,,\n"
            "E");
        QString diff;
        const bool matches = snapshotMatchesParser(chart, &diff);
        if (!matches) {
            err << diff << '\n';
        }
        expect(matches,
               QStringLiteral("quick model matches parser-visible timeline semantics for the Prophesy One UTG slide repro"));
    }

    {
        // Regression (gap fix): absolute-seconds and tempo#seconds hold/touch-hold
        // durations must render on the Timeline exactly like the authoritative
        // SimaiNativeParser. These forms were previously dropped by the quick model.
        struct DurationCase { QString name; QString chart; double expectedHoldSeconds; };
        const QVector<DurationCase> cases = {
            { QStringLiteral("1h[4:3]"),     QStringLiteral("1h[4:3],\nE"),     1.5 },
            { QStringLiteral("1h[#1.5]"),    QStringLiteral("1h[#1.5],\nE"),    1.5 },
            { QStringLiteral("1h[120#1.5]"), QStringLiteral("1h[120#1.5],\nE"), 1.5 },
            { QStringLiteral("1h[120#4:3]"), QStringLiteral("1h[120#4:3],\nE"), 1.5 },
            { QStringLiteral("Ch[#1.0]"),    QStringLiteral("Ch[#1.0],\nE"),    1.0 },
        };
        for (const DurationCase& dc : cases) {
            QString diff;
            const bool matches = snapshotMatchesParser(dc.chart, &diff);
            if (!matches) {
                err << diff << '\n';
            }
            expect(matches,
                   QStringLiteral("quick model matches parser hold duration for %1").arg(dc.name));

            TimelineQuickModel model;
            model.rebuildFromText(dc.chart, 0.0);
            double holdSeconds = -1.0;
            for (const TimelineRenderLine& line : model.snapshot().lines) {
                for (const TimelineRenderNote& note : line.notes) {
                    if (note.kind == TimelineRenderNoteKind::Hold
                        || note.kind == TimelineRenderNoteKind::TouchHold) {
                        holdSeconds = note.endSecondOffset - note.secondOffset;
                    }
                }
            }
            expect(nearlyEqual(holdSeconds, dc.expectedHoldSeconds),
                   QStringLiteral("quick model resolves %1 to %2s").arg(dc.name).arg(dc.expectedHoldSeconds));
        }
    }

    {
        // Note geometry must survive incremental edits identically to a full rebuild.
        // (a) single stray-digit insert inside a hold bracket.
        expect(incrementalMatchesRebuild(
                   QStringLiteral("5h[4:3],\nE"),
                   QStringLiteral("5h[4:3],\nE").indexOf(QStringLiteral("[4:3]")) + 3,
                   0,
                   QStringLiteral("2"),
                   0.0),
               QStringLiteral("incremental insert inside hold duration bracket matches rebuild"));

        // (b) full keystroke sequence on a SINGLE model: type '2' -> delete '2' ->
        // delete ']' (transiently invalid) -> retype ']'. Each step must match a
        // fresh rebuild, and the hold must revert to its original length.
        struct EditStep { int pos; int charsRemoved; QString inserted; QString note; };
        const QString original = QStringLiteral("5h[4:3],\nE");
        const int threeIndex = original.indexOf(QStringLiteral("[4:3]")) + 3;
        const QVector<EditStep> steps = {
            { threeIndex, 0, QStringLiteral("2"), QStringLiteral("insert stray 2 -> [4:23]") },
            { threeIndex, 1, QString(),           QStringLiteral("delete stray 2 -> [4:3]") },
            { threeIndex + 1, 1, QString(),       QStringLiteral("delete ] -> invalid") },
            { threeIndex + 1, 0, QStringLiteral("]"), QStringLiteral("retype ] -> [4:3]") },
        };
        QTextDocument document(original);
        TimelineQuickModel incremental;
        incremental.rebuildFromText(document.toPlainText(), 0.0);
        for (const EditStep& step : steps) {
            applyDocumentChange(&document, step.pos, step.charsRemoved, step.inserted);
            incremental.applyTextChange(
                document.toPlainText(), step.pos, step.charsRemoved, step.inserted.size(), 0.0);
            TimelineQuickModel rebuilt;
            rebuilt.rebuildFromText(document.toPlainText(), 0.0);
            expect(sameSnapshot(incremental.snapshot(), rebuilt.snapshot()),
                   QStringLiteral("incremental hold edit matches rebuild after: %1").arg(step.note));
        }

        double finalHoldSeconds = -1.0;
        for (const TimelineRenderLine& line : incremental.snapshot().lines) {
            for (const TimelineRenderNote& note : line.notes) {
                if (note.kind == TimelineRenderNoteKind::Hold) {
                    finalHoldSeconds = note.endSecondOffset - note.secondOffset;
                }
            }
        }
        expect(nearlyEqual(finalHoldSeconds, 1.5),
               QStringLiteral("hold duration reverts to [4:3] length after stray-digit insert+delete"));
    }

    if (failed > 0) {
        err << "\nTimeline model spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "\nTimeline model spec passed.\n";
    return 0;
}
