#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "core/chart/document/SimaiDocument.h"
#include "core/chart/document/SimaiTimingMetadata.h"
#include "SimaiNativeParser.h"

namespace {

QJsonObject dumpMessage(const SimaiNativeMessage& message)
{
    QJsonObject item;
    item.insert("line", message.line);
    item.insert("col", message.col);
    item.insert("end_col", message.endCol);
    item.insert("message", message.message);
    return item;
}

QJsonObject dumpBeatMarker(const TimelineBeatMarker& marker)
{
    QJsonObject item;
    item.insert("second", marker.second);
    item.insert("line", marker.sourceLine);
    item.insert("col", marker.sourceCol);
    item.insert("major", marker.major);
    return item;
}

QJsonObject dumpPadEntry(const MuriPadTimeEntry& entry)
{
    QJsonObject item;
    item.insert("pad", entry.pad);
    item.insert("proportion", entry.proportion);
    return item;
}

QJsonArray dumpPadEntryVector(const QVector<MuriPadTimeEntry>& entries)
{
    QJsonArray array;
    for (const MuriPadTimeEntry& entry : entries) {
        array.append(dumpPadEntry(entry));
    }
    return array;
}

QJsonArray dumpPadEntryMatrix(const QVector<QVector<MuriPadTimeEntry>>& entries)
{
    QJsonArray array;
    for (const QVector<MuriPadTimeEntry>& group : entries) {
        array.append(dumpPadEntryVector(group));
    }
    return array;
}

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
    item.insert("is_mine", marker.isMine);
    item.insert("track_mine", marker.trackMine);
    item.insert("head_mine", marker.headMine);
    item.insert("hs_multiplier", marker.hsMultiplier);
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
    item.insert("slide_segment_pad_enter_times", dumpPadEntryMatrix(marker.slideSegmentPadEnterTimes));
    item.insert("wifi_pad_enter_times", dumpPadEntryVector(marker.wifiPadEnterTimes));

    return item;
}

QJsonObject dumpParseResult(const SimaiNativeParseResult& result)
{
    QJsonObject root;
    root.insert("ok", result.ok);
    root.insert("duration_seconds", result.durationSeconds);
    root.insert("error_count", result.errors.size());
    root.insert("warning_count", result.warnings.size());
    root.insert("beat_count", result.beatMarkers.size());
    root.insert("note_count", result.noteMarkers.size());

    QJsonArray errors;
    for (const SimaiNativeMessage& error : result.errors) {
        errors.append(dumpMessage(error));
    }
    root.insert("errors", errors);

    QJsonArray warnings;
    for (const SimaiNativeMessage& warning : result.warnings) {
        warnings.append(dumpMessage(warning));
    }
    root.insert("warnings", warnings);

    QJsonArray beatMarkers;
    for (const TimelineBeatMarker& marker : result.beatMarkers) {
        beatMarkers.append(dumpBeatMarker(marker));
    }
    root.insert("beat_markers", beatMarkers);

    QJsonArray notes;
    for (const TimelineNoteMarker& marker : result.noteMarkers) {
        notes.append(dumpMarker(marker));
    }
    root.insert("note_markers", notes);
    return root;
}

QJsonObject dumpDocumentParse(const QString& sourcePath, const QString& text)
{
    const SimaiDocument document = SimaiDocument::fromText(text);
    const miacode::simai::SimaiTimingMetadata timingMetadata = miacode::simai::buildTimingMetadata(document);

    QJsonObject root;
    root.insert("mode", QStringLiteral("document"));
    root.insert("source_path", sourcePath);
    root.insert("title", document.title);
    root.insert("artist", document.artist);
    root.insert("first", document.first);
    root.insert("designer", document.designer);
    root.insert("difficulty_count", document.difficultyIds().size());

    QJsonObject timingObject;
    timingObject.insert("whole_time_signature_text", timingMetadata.wholeTimeSignatureText);
    timingObject.insert("whole_time_signature_numerator", timingMetadata.wholeTimeSignatureNumerator);
    timingObject.insert("whole_time_signature_denominator", timingMetadata.wholeTimeSignatureDenominator);
    timingObject.insert("whole_time_signature_valid", timingMetadata.wholeTimeSignatureValid);
    root.insert("timing_metadata", timingObject);

    QJsonArray difficulties;
    const QVector<int> ids = document.difficultyIds();
    for (int id : ids) {
        const SimaiDifficultyData* difficulty = document.difficulty(id);
        if (difficulty == nullptr) {
            continue;
        }
        QJsonObject item;
        item.insert("id", difficulty->id);
        item.insert("level", difficulty->level);
        item.insert("designer", difficulty->designer);
        item.insert(
            "chart_parse",
            dumpParseResult(SimaiNativeParser::parseForTimeline(
                difficulty->chart,
                timingMetadata)));
        difficulties.append(item);
    }
    root.insert("difficulties", difficulties);
    return root;
}

QJsonObject dumpLegacySingleChartParse(const QString& text)
{
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
    return root;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    bool documentMode = false;
    QString sourcePath;
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == QStringLiteral("--document")) {
            documentMode = true;
            continue;
        }
        if (sourcePath.isEmpty()) {
            sourcePath = arg;
            continue;
        }
    }

    if (sourcePath.isEmpty()) {
        QTextStream(stderr) << "usage: simai_native_dump [--document] <maidata.txt>\n";
        return 1;
    }

    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "failed to open: " << sourcePath << "\n";
        return 2;
    }

    const QString text = QString::fromUtf8(file.readAll());
    const QJsonObject root = documentMode
        ? dumpDocumentParse(sourcePath, text)
        : dumpLegacySingleChartParse(text);

    QTextStream(stdout) << QJsonDocument(root).toJson(QJsonDocument::Compact);
    return 0;
}
