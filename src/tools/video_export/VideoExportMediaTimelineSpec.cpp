// VideoExportMediaTimelineSpec — background-media (PV) placement on the export
// output timeline. Covers the partial-range pre-roll freeze, the full-range
// negative-origin delay, the positive-origin trim, and the still-image case.

#include "VideoExportMediaTimeline.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cmath>

namespace {

using miacode::video_export::buildMediaTimelineFilters;
using miacode::video_export::MediaTimelinePlan;

bool require(bool condition, const QString& message, QString* error)
{
    if (condition) {
        return true;
    }
    if (error != nullptr && error->isEmpty()) {
        *error = message;
    }
    return false;
}

bool containsFilter(const QStringList& filters, const QString& needle)
{
    for (const QString& filter : filters) {
        if (filter.contains(needle)) {
            return true;
        }
    }
    return false;
}

// A partial-range export from chart second 12.0 with the standard 1.5 s
// preload: the PV must hold the frame at 12.0 for the whole freeze, then run
// forward from that same frame.
bool testPartialRangeFreeze(QString* error)
{
    MediaTimelinePlan plan;
    plan.mediaIsImage = false;
    plan.partialRangeExport = true;
    plan.leadInSeconds = 1.5;
    plan.timelineOriginSecond = 12.0 - 1.5;
    plan.alignedTotalSeconds = 1.5 + 8.0;

    const QStringList filters = buildMediaTimelineFilters(plan);
    return require(
               filters.size() == 4,
               QStringLiteral("partial-range freeze should emit trim + setpts + start pad + stop pad"),
               error)
        && require(
               filters.at(0) == QStringLiteral("trim=start=12.000000:end=20.000000"),
               QStringLiteral("freeze must source the PV from the segment-start second, not the pre-roll start"),
               error)
        && require(
               filters.at(1) == QStringLiteral("setpts=PTS-STARTPTS"),
               QStringLiteral("trimmed media must be rebased to output zero"),
               error)
        && require(
               filters.at(2) == QStringLiteral("tpad=start_mode=clone:start_duration=1.500000"),
               QStringLiteral("freeze window must clone the segment-start frame for the whole lead-in"),
               error)
        && require(
               filters.at(3) == QStringLiteral("tpad=stop_mode=clone:stop_duration=9.500000"),
               QStringLiteral("short media must still hold its last frame to the end"),
               error);
}

// Segments that start inside the lead-in window cannot source negative media
// time; the clone pad still covers the freeze.
bool testPartialRangeFreezeNearZero(QString* error)
{
    MediaTimelinePlan plan;
    plan.partialRangeExport = true;
    plan.leadInSeconds = 1.5;
    plan.timelineOriginSecond = 0.5 - 1.5;
    plan.alignedTotalSeconds = 1.5 + 4.0;

    const QStringList filters = buildMediaTimelineFilters(plan);
    return require(
               filters.at(0) == QStringLiteral("trim=start=0.500000:end=4.500000"),
               QStringLiteral("segment start below the lead-in must clamp at media second 0.5"),
               error)
        && require(
               containsFilter(filters, QStringLiteral("tpad=start_mode=clone:start_duration=1.500000")),
               QStringLiteral("freeze pad must survive a segment that starts inside the lead-in"),
               error);
}

// Full-range exports keep the old behaviour: the 2 s count-down runs live, so
// the PV is delayed into place rather than frozen.
bool testFullRangeNegativeOrigin(QString* error)
{
    MediaTimelinePlan plan;
    plan.partialRangeExport = false;
    plan.leadInSeconds = 2.0;
    plan.timelineOriginSecond = -2.0;
    plan.alignedTotalSeconds = 12.0;

    const QStringList filters = buildMediaTimelineFilters(plan);
    return require(
               !containsFilter(filters, QStringLiteral("start_mode=clone")),
               QStringLiteral("full-range exports must not freeze the PV"),
               error)
        && require(
               filters.at(0) == QStringLiteral("trim=start=0:end=10.000000"),
               QStringLiteral("full-range media should start at media zero"),
               error)
        && require(
               filters.at(1) == QStringLiteral("setpts=PTS-STARTPTS+2.000000/TB"),
               QStringLiteral("full-range media should be delayed by the lead-in"),
               error);
}

// A range that ends early but starts at chart 0 is classified full-range
// upstream; its origin is positive only when the caller skipped the count-in.
bool testPositiveOriginTrim(QString* error)
{
    MediaTimelinePlan plan;
    plan.partialRangeExport = false;
    plan.timelineOriginSecond = 6.0;
    plan.alignedTotalSeconds = 4.0;

    const QStringList filters = buildMediaTimelineFilters(plan);
    return require(
        filters.at(0) == QStringLiteral("trim=start=6.000000:end=10.000000"),
        QStringLiteral("positive origin should trim the media to the exported window"),
        error);
}

bool testStillImageNeedsNoTiming(QString* error)
{
    MediaTimelinePlan plan;
    plan.mediaIsImage = true;
    plan.partialRangeExport = true;
    plan.leadInSeconds = 1.5;
    plan.timelineOriginSecond = 10.5;
    plan.alignedTotalSeconds = 9.5;

    return require(
        buildMediaTimelineFilters(plan).isEmpty(),
        QStringLiteral("looped still images need no trim/pad chain"),
        error);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    struct Case {
        const char* name;
        bool (*run)(QString*);
    };
    const Case cases[] = {
        {"partial_range_freeze", &testPartialRangeFreeze},
        {"partial_range_freeze_near_zero", &testPartialRangeFreezeNearZero},
        {"full_range_negative_origin", &testFullRangeNegativeOrigin},
        {"positive_origin_trim", &testPositiveOriginTrim},
        {"still_image_needs_no_timing", &testStillImageNeedsNoTiming},
    };

    int failures = 0;
    for (const Case& testCase : cases) {
        QString error;
        if (testCase.run(&error)) {
            out << "PASS " << testCase.name << '\n';
        } else {
            out << "FAIL " << testCase.name << " — " << error << '\n';
            ++failures;
        }
    }
    out.flush();
    return failures == 0 ? 0 : 1;
}
