// Covers erase-by-area slide/wifi track behavior in PreviewTrackShared.
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

using miacode::preview::scene::PreviewSlideEraseByAreaData;
using miacode::preview::scene::PreviewSlideEraseByAreaSegment;
using miacode::preview::scene::buildPreviewSlideEraseByAreaData;
using miacode::preview::scene::previewSlideStarSegment;
using miacode::preview::scene::previewSlideEraseByAreaHiddenArrowCount;
using miacode::preview::scene::previewWifiEraseByAreaHiddenRowCount;

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
bool verifyEraseByAreaStepsAndLeavesATail(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1-5[8:1],\nE"), &marker, err)) {
        return false;
    }

    const PreviewSlideEraseByAreaData areas = buildPreviewSlideEraseByAreaData(marker);
    if (!require(areas.isValid(), QStringLiteral("1-5 builds a valid erase-by-area data"), err)) {
        return false;
    }
    if (!require(areas.segments.size() == 1, QStringLiteral("1-5 builds one erase-by-area segment"), err)) {
        return false;
    }
    const PreviewSlideEraseByAreaSegment& segment = areas.segments.constFirst();
    if (!require(segment.areaCount() > 1, QStringLiteral("1-5 has more than one hit area"), err)) {
        return false;
    }
    if (!require(
            areas.totalArrowCount == totalTrackArrowCount(marker),
            QStringLiteral("erase-by-area data covers every track arrow"),
            err)) {
        return false;
    }

    int previous = -1;
    int distinctNonZero = 0;
    for (int step = 0; step <= 1000; ++step) {
        const qreal progress = static_cast<qreal>(step) / 1000.0;
        const int hidden = previewSlideEraseByAreaHiddenArrowCount(areas, 0, progress);
        if (!require(hidden >= previous, QStringLiteral("hidden arrow count never decreases"), err)) {
            return false;
        }
        if (hidden != previous && hidden > 0) {
            ++distinctNonZero;
        }
        previous = hidden;
    }

    if (!require(
            previewSlideEraseByAreaHiddenArrowCount(areas, 0, 0.0) == 0,
            QStringLiteral("nothing is erased before the star moves"),
            err)) {
        return false;
    }
    if (!require(
            distinctNonZero <= segment.areaCount() - 1,
            QStringLiteral("the track clears in at most areaCount - 1 steps"),
            err)) {
        return false;
    }
    return require(
        previewSlideEraseByAreaHiddenArrowCount(areas, 0, 1.0) < areas.totalArrowCount,
        QStringLiteral("the trailing arrows are still lit when the star lands"),
        err
    );
}

// The hit-area index advances at even fractions of the trace window rather than
// at the geometric area boundaries, so each plateau is the recorded exit point
// of the area the index has just left.
bool verifyEraseByAreaStepsLandOnEvenWindowFractions(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1-5[8:1],\nE"), &marker, err)) {
        return false;
    }

    const PreviewSlideEraseByAreaData areas = buildPreviewSlideEraseByAreaData(marker);
    if (!require(areas.isValid(), QStringLiteral("1-5 builds a valid erase-by-area data"), err)) {
        return false;
    }
    if (!require(areas.segments.size() == 1, QStringLiteral("1-5 builds one erase-by-area segment"), err)) {
        return false;
    }

    const PreviewSlideEraseByAreaSegment& segment = areas.segments.constFirst();
    const int areaCount = segment.areaCount();
    for (int hitIndex = 0; hitIndex < areaCount; ++hitIndex) {
        const qreal windowStart = static_cast<qreal>(hitIndex) * segment.criticalProportion / areaCount;
        const qreal windowEnd = static_cast<qreal>(hitIndex + 1) * segment.criticalProportion / areaCount;
        const qreal sample = qMin<qreal>(1.0, (windowStart + windowEnd) * 0.5);
        const int expected = hitIndex == 0 ? 0 : segment.hiddenArrowCountAfterArea.at(hitIndex - 1);
        if (!require(
                previewSlideEraseByAreaHiddenArrowCount(areas, 0, sample) == expected,
                QStringLiteral("hit area %1 plateaus at its recorded exit arrow").arg(hitIndex),
                err)) {
            return false;
        }
    }
    return true;
}

// The correction for connected slides is segment-local. In particular, the
// final segment must retain exactly the same critical clock and erase curve as
// the same shape rendered independently.
bool verifyConnectedLastSegmentMatchesStandalone(QTextStream& err)
{
    TimelineNoteMarker chainMarker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}8>3q4[8:4],\nE"), &chainMarker, err)) {
        return false;
    }
    if (!require(
            chainMarker.slideTrackAreaPoints.size() == 2,
            QStringLiteral("8>3q4 parses as two chained segments"),
            err)) {
        return false;
    }

    TimelineNoteMarker standaloneMarker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}3q4[8:4],\nE"), &standaloneMarker, err)) {
        return false;
    }

    const PreviewSlideEraseByAreaData chainAreas = buildPreviewSlideEraseByAreaData(chainMarker);
    const PreviewSlideEraseByAreaData standaloneAreas = buildPreviewSlideEraseByAreaData(standaloneMarker);
    if (!require(
            chainAreas.isValid() && chainAreas.segments.size() == 2,
            QStringLiteral("8>3q4 builds two erase-by-area segments"),
            err)) {
        return false;
    }
    if (!require(
            standaloneAreas.isValid() && standaloneAreas.segments.size() == 1,
            QStringLiteral("3q4 builds one standalone erase-by-area segment"),
            err)) {
        return false;
    }

    const PreviewSlideEraseByAreaSegment& chainFirst = chainAreas.segments.at(0);
    const PreviewSlideEraseByAreaSegment& chainLast = chainAreas.segments.at(1);
    const PreviewSlideEraseByAreaSegment& standalone = standaloneAreas.segments.constFirst();
    if (!require(
            qAbs(chainFirst.criticalProportion - 1.0) < kEpsilon,
            QStringLiteral("an intermediate segment uses its complete local duration"),
            err)) {
        return false;
    }
    if (!require(
            chainLast.totalArrowCount == standalone.totalArrowCount
                && chainLast.hiddenArrowCountAfterArea == standalone.hiddenArrowCountAfterArea,
            QStringLiteral("the chain's final 3q4 segment keeps the standalone area cuts"),
            err)) {
        return false;
    }
    if (!require(
            qAbs(chainLast.criticalProportion - standalone.criticalProportion) < kEpsilon,
            QStringLiteral("the chain's final 3q4 segment keeps the standalone critical proportion"),
            err)) {
        return false;
    }

    for (int step = 0; step <= 1000; ++step) {
        const qreal localProgress = static_cast<qreal>(step) / 1000.0;
        const int standaloneHidden = previewSlideEraseByAreaHiddenArrowCount(standaloneAreas, 0, localProgress);
        const int chainLastHidden = previewSlideEraseByAreaHiddenArrowCount(chainAreas, 1, localProgress)
            - chainLast.arrowOffset;
        if (!require(
                chainLastHidden == standaloneHidden,
                QStringLiteral("the chain's final segment matches standalone 3q4 at step %1").arg(step),
                err)) {
            return false;
        }
    }

    if (!require(
            previewSlideEraseByAreaHiddenArrowCount(chainAreas, 1, 0.0) == chainLast.arrowOffset,
            QStringLiteral("the continuation erases none of its own arrows at the join"),
            err)) {
        return false;
    }
    return require(
        previewSlideEraseByAreaHiddenArrowCount(chainAreas, 1, 1.0) < chainAreas.totalArrowCount,
        QStringLiteral("the connected track keeps the final segment's lit tail"),
        err
    );
}

// The erasure and the rendered star share this segment locator. It must switch
// at the parser-authored join and expose progress local to that segment.
bool verifyStarSegmentFollowsTheParsedTimingSplit(QTextStream& err)
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
    const double joinSecond = marker.slideSegmentShootSeconds.at(1);
    int segmentIndex = -1;
    qreal segmentProportion = -1.0;
    previewSlideStarSegment(marker, marker.slideTraceSecond, 2, &segmentIndex, &segmentProportion);
    if (!require(
            segmentIndex == 0 && qAbs(segmentProportion) < kEpsilon,
            QStringLiteral("the star starts at segment zero progress zero"),
            err)) {
        return false;
    }
    previewSlideStarSegment(
        marker,
        marker.slideTraceSecond + firstDuration * 0.5,
        2,
        &segmentIndex,
        &segmentProportion
    );
    if (!require(
            segmentIndex == 0 && qAbs(segmentProportion - 0.5) < kEpsilon,
            QStringLiteral("star progress is local and linear inside the first segment"),
            err)) {
        return false;
    }
    previewSlideStarSegment(marker, joinSecond, 2, &segmentIndex, &segmentProportion);
    if (!require(
            segmentIndex == 1 && qAbs(segmentProportion) < kEpsilon,
            QStringLiteral("the star changes to final-segment progress zero at the join"),
            err)) {
        return false;
    }
    previewSlideStarSegment(marker, marker.endSecond, 2, &segmentIndex, &segmentProportion);
    return require(
        segmentIndex == 1 && qAbs(segmentProportion - 1.0) < kEpsilon,
        QStringLiteral("the final segment reaches progress one when the star lands"),
        err
    );
}

// Wifi clears area by area on the same hit-area clock as a slide, and unlike the
// slide clear it runs to completion rather than stalling on a lit tail.
bool verifyWifiClearsAreaByAreaAndCompletes(QTextStream& err)
{
    TimelineNoteMarker marker;
    if (!parseSingleSlideMarker(QStringLiteral("(120){8}1w5[8:1],\nE"), &marker, err)) {
        return false;
    }
    if (!require(marker.type == QLatin1String("wifi"), QStringLiteral("1w5 parses as a wifi marker"), err)) {
        return false;
    }

    QVector<int> areaRowCounts;
    int rowCount = 0;
    for (const QVector<QPointF>& areaPoints : marker.wifiTrackAreaPoints) {
        areaRowCounts.append(areaPoints.size());
        rowCount += areaPoints.size();
    }
    if (!require(areaRowCounts.size() > 1, QStringLiteral("the wifi track has several areas"), err)) {
        return false;
    }
    if (!require(rowCount > 2, QStringLiteral("the wifi track has more than two rows"), err)) {
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
            previewWifiEraseByAreaHiddenRowCount(areaRowCounts, critical, 0.0) == 0,
            QStringLiteral("no wifi row clears before the star moves"),
            err)) {
        return false;
    }

    // Every value the clear takes must be a whole-area boundary, so the draw walk
    // never has to cut inside an area.
    QVector<int> areaBoundaries;
    int cumulative = 0;
    areaBoundaries.append(0);
    for (int areaRows : areaRowCounts) {
        cumulative += areaRows;
        areaBoundaries.append(cumulative);
    }

    int previous = -1;
    for (int step = 0; step <= 1000; ++step) {
        const int hidden = previewWifiEraseByAreaHiddenRowCount(
            areaRowCounts, critical, static_cast<qreal>(step) / 1000.0);
        if (!require(hidden >= previous, QStringLiteral("wifi hidden row count never decreases"), err)) {
            return false;
        }
        if (!require(
                areaBoundaries.contains(hidden),
                QStringLiteral("wifi clears whole areas only"),
                err)) {
            return false;
        }
        previous = hidden;
    }

    if (!require(
            previewWifiEraseByAreaHiddenRowCount(areaRowCounts, critical, 1.0) == rowCount,
            QStringLiteral("every wifi row is cleared by the time the star lands"),
            err)) {
        return false;
    }
    return require(
        previewWifiEraseByAreaHiddenRowCount(areaRowCounts, critical, critical) == rowCount,
        QStringLiteral("the wifi clear completes at the judge point"),
        err
    );
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (!verifyEraseByAreaStepsAndLeavesATail(err)) {
        return 1;
    }
    if (!verifyEraseByAreaStepsLandOnEvenWindowFractions(err)) {
        return 1;
    }
    if (!verifyConnectedLastSegmentMatchesStandalone(err)) {
        return 1;
    }
    if (!verifyStarSegmentFollowsTheParsedTimingSplit(err)) {
        return 1;
    }
    if (!verifyWifiClearsAreaByAreaAndCompletes(err)) {
        return 1;
    }

    out << "preview_slide_erase_by_area_spec ok" << Qt::endl;
    return 0;
}


