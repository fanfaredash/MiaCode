// Covers the vanilla-autoplay slide/wifi track erasure in PreviewTrackShared.
//
// The assertions are deliberately structural — step count, monotonicity, where
// the steps land, and the tail that stays lit — rather than a table of expected
// arrow counts. The shape of the curve is the behaviour being simulated; the
// numbers themselves come from MiaCode's own slide_data.json and would only
// re-state the input.

#include <QCoreApplication>
#include <QTextStream>
#include <QtMath>

#include "core/chart/parser/SimaiNativeParser.h"
#include "core/scene/PreviewTrackShared.h"

#include <cmath>

namespace {

using miacode::preview::scene::PreviewSlideAutoplayAreas;
using miacode::preview::scene::buildPreviewSlideAutoplayAreas;
using miacode::preview::scene::previewSlideStarProgress;
using miacode::preview::scene::previewSlideVanillaHiddenArrowCount;
using miacode::preview::scene::previewWifiVanillaHiddenRowCount;

constexpr qreal kEpsilon = 1e-6;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool parseSingleSlideMarker(const QString& chart, TimelineNoteMarker* outMarker, QTextStream& err)
{
    const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(chart);
    if (!require(parsed.ok, QStringLiteral("chart parses: %1").arg(chart), err)) {
        return false;
    }

    int found = 0;
    for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
        if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
            continue;
        }
        ++found;
        *outMarker = marker;
    }
    return require(found == 1, QStringLiteral("chart emits exactly one slide marker: %1").arg(chart), err);
}

int totalTrackArrowCount(const TimelineNoteMarker& marker)
{
    int total = 0;
    for (const QVector<QVector<QPointF>>& segment : marker.slideTrackAreaPoints) {
        for (const QVector<QPointF>& areaPoints : segment) {
            total += areaPoints.size();
        }
    }
    return total;
}

// The star clears the track in whole hit-area steps and stops before the end:
// the arrows past the last step stay lit until the track itself disappears.
bool verifyVanillaTrimStepsByAreaAndLeavesATail(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1-5[8:1],\nE"), &marker, err)) {
        return false;
    }

    const PreviewSlideAutoplayAreas areas = buildPreviewSlideAutoplayAreas(marker);
    if (!require(areas.isValid(), QStringLiteral("1-5 builds a valid autoplay area list"), err)) {
        return false;
    }
    if (!require(areas.areaCount() > 1, QStringLiteral("1-5 has more than one hit area"), err)) {
        return false;
    }
    if (!require(
            areas.totalArrowCount == totalTrackArrowCount(marker),
            QStringLiteral("autoplay area list covers every track arrow"),
            err)) {
        return false;
    }

    int previous = -1;
    int distinctNonZero = 0;
    for (int step = 0; step <= 1000; ++step) {
        const qreal progress = static_cast<qreal>(step) / 1000.0;
        const int hidden = previewSlideVanillaHiddenArrowCount(areas, progress);
        if (!require(hidden >= previous, QStringLiteral("hidden arrow count never decreases"), err)) {
            return false;
        }
        if (hidden != previous && hidden > 0) {
            ++distinctNonZero;
        }
        previous = hidden;
    }

    if (!require(
            previewSlideVanillaHiddenArrowCount(areas, 0.0) == 0,
            QStringLiteral("nothing is erased before the star moves"),
            err)) {
        return false;
    }
    if (!require(
            distinctNonZero <= areas.areaCount() - 1,
            QStringLiteral("the track clears in at most areaCount - 1 steps"),
            err)) {
        return false;
    }
    return require(
        previewSlideVanillaHiddenArrowCount(areas, 1.0) < areas.totalArrowCount,
        QStringLiteral("the trailing arrows are still lit when the star lands"),
        err
    );
}

// The hit-area index advances at even fractions of the trace window rather than
// at the geometric area boundaries, so each plateau is the recorded exit point
// of the area the index has just left.
bool verifyVanillaTrimStepsLandOnEvenWindowFractions(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1-5[8:1],\nE"), &marker, err)) {
        return false;
    }

    const PreviewSlideAutoplayAreas areas = buildPreviewSlideAutoplayAreas(marker);
    if (!require(areas.isValid(), QStringLiteral("1-5 builds a valid autoplay area list"), err)) {
        return false;
    }

    const int areaCount = areas.areaCount();
    for (int hitIndex = 0; hitIndex < areaCount; ++hitIndex) {
        const qreal windowStart = static_cast<qreal>(hitIndex) * areas.criticalProportion / areaCount;
        const qreal windowEnd = static_cast<qreal>(hitIndex + 1) * areas.criticalProportion / areaCount;
        const qreal sample = qMin<qreal>(1.0, (windowStart + windowEnd) * 0.5);
        const int expected = hitIndex == 0 ? 0 : areas.hiddenArrowCountAfterArea.at(hitIndex - 1);
        if (!require(
                previewSlideVanillaHiddenArrowCount(areas, sample) == expected,
                QStringLiteral("hit area %1 plateaus at its recorded exit arrow").arg(hitIndex),
                err)) {
            return false;
        }
    }
    return true;
}

// A connected slide keeps one merged hit-area list: the incoming segment's
// first area replaces the previous slot instead of adding a step of its own.
bool verifyConnectedSlideMergesJoinAreas(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1-3-5[8:1],\nE"), &marker, err)) {
        return false;
    }
    if (!require(
            marker.slideTrackAreaPoints.size() == 2,
            QStringLiteral("1-3-5 parses as two chained segments"),
            err)) {
        return false;
    }

    int unmergedAreaCount = 0;
    for (const QVector<QVector<QPointF>>& segment : marker.slideTrackAreaPoints) {
        unmergedAreaCount += segment.size();
    }

    const PreviewSlideAutoplayAreas areas = buildPreviewSlideAutoplayAreas(marker);
    if (!require(areas.isValid(), QStringLiteral("1-3-5 builds a valid autoplay area list"), err)) {
        return false;
    }
    if (!require(
            areas.areaCount() == unmergedAreaCount - 1,
            QStringLiteral("the chain join folds one hit area away"),
            err)) {
        return false;
    }

    int previous = -1;
    for (int step = 0; step <= 1000; ++step) {
        const int hidden = previewSlideVanillaHiddenArrowCount(areas, static_cast<qreal>(step) / 1000.0);
        if (!require(hidden >= previous, QStringLiteral("chained hidden count never decreases"), err)) {
            return false;
        }
        previous = hidden;
    }
    return require(
        previewSlideVanillaHiddenArrowCount(areas, 1.0) < areas.totalArrowCount,
        QStringLiteral("a chained track also keeps a lit tail"),
        err
    );
}

// Star progress is what drives the erasure, so it has to follow the segment
// split the parser produced rather than treating a chain's segments as equal.
// `1^3-5` is chosen because its two segments differ in length, which a
// per-segment-uniform implementation would get wrong.
bool verifyStarProgressFollowsTheParsedSegmentSplit(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1^3-5[8:1],\nE"), &marker, err)) {
        return false;
    }
    if (!require(
            marker.slideSegmentShootSeconds.size() == 2 && marker.slideSegmentDurations.size() == 2,
            QStringLiteral("1^3-5 carries two timed segments"),
            err)) {
        return false;
    }

    const double firstDuration = marker.slideSegmentDurations.at(0);
    const double totalDuration = firstDuration + marker.slideSegmentDurations.at(1);
    if (!require(totalDuration > 0.0, QStringLiteral("the chain has a positive trace duration"), err)) {
        return false;
    }
    const qreal durationShare = static_cast<qreal>(firstDuration / totalDuration);
    if (!require(
            qAbs(durationShare - 0.5) > 0.01,
            QStringLiteral("1^3-5 splits unevenly, so the check can discriminate"),
            err)) {
        return false;
    }

    const double joinSecond = marker.slideSegmentShootSeconds.at(1);
    if (!require(
            qAbs(previewSlideStarProgress(marker, marker.slideTraceSecond)) < kEpsilon,
            QStringLiteral("star progress is zero when the trace starts"),
            err)) {
        return false;
    }
    if (!require(
            qAbs(previewSlideStarProgress(marker, joinSecond) - durationShare) < kEpsilon,
            QStringLiteral("star progress at the join equals the first segment's share"),
            err)) {
        return false;
    }
    if (!require(
            qAbs(previewSlideStarProgress(marker, marker.slideTraceSecond + firstDuration * 0.5)
                 - durationShare * 0.5)
                < kEpsilon,
            QStringLiteral("star progress is linear inside a segment"),
            err)) {
        return false;
    }
    return require(
        qAbs(previewSlideStarProgress(marker, marker.endSecond) - 1.0) < kEpsilon,
        QStringLiteral("star progress reaches one when the star lands"),
        err
    );
}

// Wifi reproduces the arcade's fan clear as-is: `num7` is compared against the
// row index without being scaled by the row count, so it barely passes 1 by the
// time the star lands and only the leading rows ever clear.
bool verifyWifiVanillaClearsOnlyTheLeadingRows(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1w5[8:1],\nE"), &marker, err)) {
        return false;
    }
    if (!require(marker.type == QLatin1String("wifi"), QStringLiteral("1w5 parses as a wifi marker"), err)) {
        return false;
    }

    int rowCount = 0;
    for (const QVector<QPointF>& areaPoints : marker.wifiTrackAreaPoints) {
        rowCount += areaPoints.size();
    }
    if (!require(rowCount > 2, QStringLiteral("the wifi lane has more than two rows"), err)) {
        return false;
    }

    const double critical = marker.wifiCriticalProportion;
    if (!require(
            critical > 0.0 && critical < 1.0,
            QStringLiteral("the wifi shape carries a critical proportion"),
            err)) {
        return false;
    }

    if (!require(
            previewWifiVanillaHiddenRowCount(critical, rowCount, 0.0) == 0,
            QStringLiteral("no wifi row clears before the star moves"),
            err)) {
        return false;
    }

    int previous = -1;
    for (int step = 0; step <= 1000; ++step) {
        const int hidden =
            previewWifiVanillaHiddenRowCount(critical, rowCount, static_cast<qreal>(step) / 1000.0);
        if (!require(hidden >= previous, QStringLiteral("wifi hidden row count never decreases"), err)) {
            return false;
        }
        previous = hidden;
    }

    const int hiddenAtLanding = previewWifiVanillaHiddenRowCount(critical, rowCount, 1.0);
    if (!require(
            hiddenAtLanding == static_cast<int>(std::ceil(1.0 / critical)),
            QStringLiteral("the wifi clear stops at ceil(1 / criticalProportion) rows"),
            err)) {
        return false;
    }
    return require(
        hiddenAtLanding < rowCount,
        QStringLiteral("most wifi rows are still lit when the star lands"),
        err
    );
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (!verifyVanillaTrimStepsByAreaAndLeavesATail(err)) {
        return 1;
    }
    if (!verifyVanillaTrimStepsLandOnEvenWindowFractions(err)) {
        return 1;
    }
    if (!verifyConnectedSlideMergesJoinAreas(err)) {
        return 1;
    }
    if (!verifyStarProgressFollowsTheParsedSegmentSplit(err)) {
        return 1;
    }
    if (!verifyWifiVanillaClearsOnlyTheLeadingRows(err)) {
        return 1;
    }

    out << "preview_slide_vanilla_trim_spec ok" << Qt::endl;
    return 0;
}
