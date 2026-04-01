#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "SimaiNativeParser.h"

namespace {

QJsonObject dumpMarker(const TimelineNoteMarker& marker)
{
    QJsonObject item;
    item.insert("type", marker.type);
    item.insert("line", marker.sourceLine);
    item.insert("col", marker.sourceCol);
    item.insert("lane", marker.lane);
    item.insert("end_lane", marker.endLane);
    item.insert("second", marker.second);
    item.insert("end_second", marker.endSecond);
    item.insert("slide_trace_second", marker.slideTraceSecond);
    item.insert("parse_order", marker.parseOrder);
    item.insert("each_group_id", marker.eachGroupId);
    if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
        item.insert("is_each", marker.isEach);
    }
    item.insert("tap_uses_star_material", marker.tapUsesStarMaterial);
    item.insert("tap_star_double", marker.tapStarDouble);
    item.insert("is_break", marker.isBreak);
    item.insert("is_ex", marker.isEx);
    item.insert("is_firework", marker.isFirework);
    item.insert("head_break", marker.headBreak);
    item.insert("track_break", marker.trackBreak);
    item.insert("head_ex", marker.headEx);
    item.insert("head_each", marker.headEach);
    item.insert("slide_head_uses_tap_material", marker.slideHeadUsesTapMaterial);
    item.insert("slide_each", marker.slideEach);
    item.insert("same_head_slide", marker.sameHeadSlide);
    item.insert("slide_head", marker.slideHead);
    item.insert("tail_on_slide_head", marker.tailOnSlideHead);
    item.insert("on_slide", marker.onSlide);
    item.insert("before_slide", marker.beforeSlide);
    item.insert("after_slide", marker.afterSlide);
    item.insert("has_head_star", marker.hasHeadStar);
    item.insert("headless_immediate", marker.headlessImmediate);
    item.insert("slide_track_key", marker.slideTrackKey);
    item.insert("touch_pad", marker.touchPad);

    QJsonArray segmentKeys;
    for (const QString& key : marker.slideSegmentKeys) {
        segmentKeys.append(key);
    }
    item.insert("segment_keys", segmentKeys);

    QJsonArray segmentShoots;
    for (double value : marker.slideSegmentShootSeconds) {
        segmentShoots.append(value);
    }
    item.insert("segment_shoot_seconds", segmentShoots);

    QJsonArray segmentDurations;
    for (double value : marker.slideSegmentDurations) {
        segmentDurations.append(value);
    }
    item.insert("segment_durations", segmentDurations);

    return item;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        QTextStream(stderr) << "usage: simai_native_dump <maidata.txt>\n";
        return 1;
    }

    QFile file(args.at(1));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "failed to open: " << args.at(1) << "\n";
        return 2;
    }

    const QString text = QString::fromUtf8(file.readAll());
    const SimaiNativeParseResult result = SimaiNativeParser::parseForTimeline(text);

    QJsonObject root;
    root.insert("ok", result.ok);
    root.insert("error_count", result.errors.size());
    root.insert("warning_count", result.warnings.size());
    root.insert("note_count", result.noteMarkers.size());

    QJsonArray errors;
    for (const SimaiNativeMessage& error : result.errors) {
        QJsonObject item;
        item.insert("line", error.line);
        item.insert("col", error.col);
        item.insert("message", error.message);
        errors.append(item);
    }
    root.insert("errors", errors);

    QJsonArray notes;
    for (const TimelineNoteMarker& marker : result.noteMarkers) {
        notes.append(dumpMarker(marker));
    }
    root.insert("notes", notes);

    QTextStream(stdout) << QJsonDocument(root).toJson(QJsonDocument::Compact);
    return 0;
}
