#include "timeline/TimelineQuickModel.h"

#include <QCoreApplication>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QtMath>

namespace {

bool nearlyEqual(double a, double b, double epsilon = 1e-6)
{
    return qAbs(a - b) <= epsilon;
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

bool sameLine(const TimelineRenderLine& left, const TimelineRenderLine& right)
{
    if (left.lineNumber != right.lineNumber
        || left.startPosition != right.startPosition
        || !nearlyEqual(left.startSecond, right.startSecond)
        || !nearlyEqual(left.endSecond, right.endSecond)
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

    if (failed > 0) {
        err << "\nTimeline model spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "\nTimeline model spec passed.\n";
    return 0;
}
