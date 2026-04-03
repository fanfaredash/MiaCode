#include "timeline/TimelineQuickModel.h"
#include "simai/parser/SimaiNativeParser.h"

#include <QCoreApplication>
#include <QStringList>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QtMath>

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

QVector<double> flattenParserMeasureLines(const SimaiNativeParseResult& parsed)
{
    return parsed.measureLineSeconds;
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

bool snapshotMatchesParser(const QString& chartText, QString* diff = nullptr)
{
    const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(chartText);
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
    if (!model.rebuildFromText(chartText, 0.0)) {
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

bool sameLine(const TimelineRenderLine& left, const TimelineRenderLine& right)
{
    if (left.lineNumber != right.lineNumber
        || left.startPosition != right.startPosition
        || !nearlyEqual(left.startSecond, right.startSecond)
        || !nearlyEqual(left.endSecond, right.endSecond)
        || !sameDoubleVector(left.measureLineSecondOffsets, right.measureLineSecondOffsets)
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

bool sameSnapshot(const TimelineRenderSnapshot& left, const TimelineRenderSnapshot& right)
{
    if (!nearlyEqual(left.durationSeconds, right.durationSeconds)
        || !nearlyEqual(left.minimumSecond, right.minimumSecond)
        || !nearlyEqual(left.maximumSecond, right.maximumSecond)
        || left.lines.size() != right.lines.size()
        || !sameDoubleVector(left.measureLineSeconds, right.measureLineSeconds)
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
    double firstSeconds)
{
    QTextDocument document(originalText);
    TimelineQuickModel incremental;
    TimelineQuickModel rebuilt;
    incremental.rebuildFromDocument(&document, firstSeconds);
    applyDocumentChange(&document, position, charsRemoved, insertedText);
    incremental.applyContentsChange(&document, position, charsRemoved, insertedText.size(), firstSeconds);
    rebuilt.rebuildFromDocument(&document, firstSeconds);
    return sameSnapshot(incremental.snapshot(), rebuilt.snapshot());
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
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
        const TimelineVisibleLineRange inTail = timelineRenderVisibleNoteLineRange(
            lines,
            buildNoteVisualEndPrefixMax(lines, false),
            1.0,
            1.1
        );
        const TimelineVisibleLineRange afterTail = timelineRenderVisibleNoteLineRange(
            lines,
            buildNoteVisualEndPrefixMax(lines, false),
            1.5,
            1.6
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
        int line = 0;
        int col = 0;
        double second = -1.0;
        const bool resolved = model.resolvePreviewFollowCursor(1.0, &line, &col, &second);
        expect(!resolved && line == 1 && col == 1 && nearlyEqual(second, 0.0),
               QStringLiteral("follow cursor falls back to L1 C1 / 0 when chart has no comma anchors"));
    }

    {
        TimelineQuickModel model;
        model.rebuildFromText(QStringLiteral("1,\n2,\nE"), 0.0);
        int line = 0;
        int col = 0;
        double second = -1.0;
        const bool firstResolved = model.resolvePreviewFollowCursor(0.1, &line, &col, &second);
        expect(firstResolved && line == 1 && col == 2 && nearlyEqual(second, 0.0),
               QStringLiteral("follow cursor binds to the latest comma at or before current preview second"));
        const bool secondResolved = model.resolvePreviewFollowCursor(0.6, &line, &col, &second);
        expect(secondResolved && line == 2 && col == 2 && nearlyEqual(second, 0.5),
               QStringLiteral("follow cursor advances to the next past comma while playback progresses"));
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
        expect(quickMeasures.size() == 2, QStringLiteral("quick model builds independent 4/4 measure lines across source lines"));
        if (quickMeasures.size() == 2) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0) && nearlyEqual(quickMeasures.at(1), 2.0),
                   QStringLiteral("measure lines continue by meter time instead of resetting per text line"));
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
        expect(quickMeasures.size() == 2, QStringLiteral("quick model keeps meter-based measure lines across {beats} changes"));
        if (quickMeasures.size() == 2) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0) && nearlyEqual(quickMeasures.at(1), 2.0),
                   QStringLiteral("changing {beats} does not move independent 4/4 measure lines"));
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
        expect(quickMeasures.size() == 3, QStringLiteral("quick model keeps independent measure lines under {3}"));
        if (quickMeasures.size() == 3) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0)
                       && nearlyEqual(quickMeasures.at(1), 2.0)
                       && nearlyEqual(quickMeasures.at(2), 4.0),
                   QStringLiteral("independent 4/4 measure lines keep their own timing under {3}"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("parser and quick model agree on 3-beat measure-line semantics: %1").arg(diff));
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
        expect(quickMeasures.size() == 2, QStringLiteral("quick model keeps independent measure markers across BPM changes"));
        if (quickMeasures.size() == 2) {
            expect(nearlyEqual(quickMeasures.at(0), 0.0) && nearlyEqual(quickMeasures.at(1), 1.0),
                   QStringLiteral("a BPM change resets the independent measure-line timeline at the BPM position"));
        }

        QString diff;
        expect(snapshotMatchesParser(chartText, &diff),
               QStringLiteral("parser and quick model agree on BPM-reset measure-line semantics: %1").arg(diff));
    }

    {
        const QString original = QStringLiteral(",\n,\n,\n,\nE");
        const int position = 0;
        expect(
            incrementalMatchesRebuild(original, position, 1, QStringLiteral("(240),(120){8},{4}"), 0.0),
            QStringLiteral("incremental reparse propagates changed measure-line state even when downstream seconds stay aligned"));
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

    if (failed > 0) {
        err << "\nTimeline model spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "\nTimeline model spec passed.\n";
    return 0;
}
