#include "ChartBatchTransform.h"

#include <functional>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPointF>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QtMath>

#include "SimaiDocument.h"
#include "SimaiNativeParser.h"
#include "core/chart/transform/ChartNormalization.h"
#include "core/chart/transform/Non384SnapTable.h"

namespace {

constexpr double kFloatEpsilon = 1e-6;

struct TransformCase {
    QString label;
    miacode::chart_transform::ChartTransformOp op = miacode::chart_transform::ChartTransformOp::MirrorLeftRight;
    QString expectedFileName;
};

struct ChartSourceLocation {
    int chartLine = 0;
    int chartColumn = 0;
    int maidataLine = 0;
    int maidataColumn = 0;
    QString maidataLineText;
    bool valid = false;
};

bool nearlyEqual(double a, double b, double epsilon = kFloatEpsilon)
{
    return qAbs(a - b) <= epsilon;
}

void expectEqual(const QString& actual, const QString& expected, const QString& message, int* failed, QTextStream& err)
{
    if (actual == expected) {
        return;
    }
    err << "[FAIL] " << message << '\n';
    err << "  expected: " << expected << '\n';
    err << "  actual:   " << actual << '\n';
    ++(*failed);
}

void expectTrue(bool condition, const QString& message, int* failed, QTextStream& err)
{
    if (condition) {
        return;
    }
    err << "[FAIL] " << message << '\n';
    ++(*failed);
}

bool readUtf8File(const QString& path, QString* text, QString* errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to open %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    if (text != nullptr) {
        *text = QString::fromUtf8(file.readAll());
    }
    return true;
}

QJsonArray dumpStringList(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray dumpDoubleVector(const QVector<double>& values)
{
    QJsonArray array;
    for (double value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray dumpIntVector(const QVector<int>& values)
{
    QJsonArray array;
    for (int value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray dumpPointVector(const QVector<QPointF>& values)
{
    QJsonArray array;
    for (const QPointF& value : values) {
        QJsonArray point;
        point.append(value.x());
        point.append(value.y());
        array.append(point);
    }
    return array;
}

QJsonArray dumpDoubleMatrix(const QVector<QVector<double>>& values)
{
    QJsonArray array;
    for (const QVector<double>& row : values) {
        array.append(dumpDoubleVector(row));
    }
    return array;
}

QJsonArray dumpDoubleCube(const QVector<QVector<QVector<double>>>& values)
{
    QJsonArray array;
    for (const QVector<QVector<double>>& plane : values) {
        array.append(dumpDoubleMatrix(plane));
    }
    return array;
}

QJsonArray dumpIntMatrix(const QVector<QVector<int>>& values)
{
    QJsonArray array;
    for (const QVector<int>& row : values) {
        array.append(dumpIntVector(row));
    }
    return array;
}

QJsonArray dumpIntCube(const QVector<QVector<QVector<int>>>& values)
{
    QJsonArray array;
    for (const QVector<QVector<int>>& plane : values) {
        array.append(dumpIntMatrix(plane));
    }
    return array;
}

QJsonArray dumpPointMatrix(const QVector<QVector<QPointF>>& values)
{
    QJsonArray array;
    for (const QVector<QPointF>& row : values) {
        array.append(dumpPointVector(row));
    }
    return array;
}

QJsonArray dumpPointCube(const QVector<QVector<QVector<QPointF>>>& values)
{
    QJsonArray array;
    for (const QVector<QVector<QPointF>>& plane : values) {
        array.append(dumpPointMatrix(plane));
    }
    return array;
}

QJsonObject dumpPadEntry(const MuriPadTimeEntry& value)
{
    QJsonObject object;
    object.insert(QStringLiteral("pad"), value.pad);
    object.insert(QStringLiteral("proportion"), value.proportion);
    return object;
}

QJsonArray dumpPadEntryVector(const QVector<MuriPadTimeEntry>& values)
{
    QJsonArray array;
    for (const MuriPadTimeEntry& value : values) {
        array.append(dumpPadEntry(value));
    }
    return array;
}

QJsonArray dumpPadEntryMatrix(const QVector<QVector<MuriPadTimeEntry>>& values)
{
    QJsonArray array;
    for (const QVector<MuriPadTimeEntry>& row : values) {
        array.append(dumpPadEntryVector(row));
    }
    return array;
}

QJsonObject dumpMessage(const SimaiNativeMessage& message)
{
    QJsonObject object;
    object.insert(QStringLiteral("source_line"), message.line);
    object.insert(QStringLiteral("source_col"), message.col);
    object.insert(QStringLiteral("source_end_col"), message.endCol);
    object.insert(QStringLiteral("message"), message.message);
    return object;
}

QJsonObject dumpBeatMarker(const TimelineBeatMarker& marker)
{
    QJsonObject object;
    object.insert(QStringLiteral("source_line"), marker.sourceLine);
    object.insert(QStringLiteral("source_col"), marker.sourceCol);
    object.insert(QStringLiteral("second"), marker.second);
    object.insert(QStringLiteral("major"), marker.major);
    return object;
}

QJsonArray dumpMessages(const QVector<SimaiNativeMessage>& messages)
{
    QJsonArray array;
    for (const SimaiNativeMessage& message : messages) {
        array.append(dumpMessage(message));
    }
    return array;
}

QJsonArray dumpBeatMarkers(const QVector<TimelineBeatMarker>& markers)
{
    QJsonArray array;
    for (const TimelineBeatMarker& marker : markers) {
        array.append(dumpBeatMarker(marker));
    }
    return array;
}

QJsonObject dumpNoteMarker(const TimelineNoteMarker& marker)
{
    QJsonObject object;
    object.insert(QStringLiteral("source_line"), marker.sourceLine);
    object.insert(QStringLiteral("source_col"), marker.sourceCol);
    object.insert(QStringLiteral("second"), marker.second);
    object.insert(QStringLiteral("end_second"), marker.endSecond);
    object.insert(QStringLiteral("slide_trace_second"), marker.slideTraceSecond);
    object.insert(QStringLiteral("available_second"), marker.availableSecond);
    object.insert(QStringLiteral("parse_order"), marker.parseOrder);
    object.insert(QStringLiteral("each_group_id"), marker.eachGroupId);
    object.insert(QStringLiteral("lane"), marker.lane);
    object.insert(QStringLiteral("end_lane"), marker.endLane);
    object.insert(QStringLiteral("type"), marker.type);
    object.insert(QStringLiteral("slide_track_key"), marker.slideTrackKey);
    object.insert(QStringLiteral("slide_segment_keys"), dumpStringList(marker.slideSegmentKeys));
    object.insert(QStringLiteral("slide_segment_shoot_seconds"), dumpDoubleVector(marker.slideSegmentShootSeconds));
    object.insert(QStringLiteral("slide_segment_durations"), dumpDoubleVector(marker.slideSegmentDurations));
    object.insert(QStringLiteral("slide_segment_pad_enter_times"), dumpPadEntryMatrix(marker.slideSegmentPadEnterTimes));
    object.insert(QStringLiteral("slide_segment_critical_proportions"), dumpDoubleVector(marker.slideSegmentCriticalProportions));
    object.insert(QStringLiteral("slide_segment_points"), dumpPointMatrix(marker.slideSegmentPoints));
    object.insert(QStringLiteral("slide_segment_angles"), dumpDoubleMatrix(marker.slideSegmentAngles));
    object.insert(QStringLiteral("wifi_lane_points"), dumpPointMatrix(marker.wifiLanePoints));
    object.insert(QStringLiteral("wifi_lane_angles"), dumpDoubleMatrix(marker.wifiLaneAngles));
    object.insert(QStringLiteral("slide_track_area_points"), dumpPointCube(marker.slideTrackAreaPoints));
    object.insert(QStringLiteral("slide_track_area_rotations"), dumpDoubleCube(marker.slideTrackAreaRotations));
    object.insert(QStringLiteral("slide_track_area_thresholds"), dumpDoubleMatrix(marker.slideTrackAreaThresholds));
    object.insert(QStringLiteral("slide_track_area_checkpoints"), dumpDoubleCube(marker.slideTrackAreaCheckpoints));
    object.insert(QStringLiteral("slide_track_area_cut_indices"), dumpIntCube(marker.slideTrackAreaCutIndices));
    object.insert(QStringLiteral("wifi_track_area_points"), dumpPointMatrix(marker.wifiTrackAreaPoints));
    object.insert(QStringLiteral("wifi_track_area_rotations"), dumpDoubleMatrix(marker.wifiTrackAreaRotations));
    object.insert(QStringLiteral("wifi_track_area_image_indices"), dumpIntMatrix(marker.wifiTrackAreaImageIndices));
    object.insert(QStringLiteral("wifi_track_area_thresholds"), dumpDoubleVector(marker.wifiTrackAreaThresholds));
    object.insert(QStringLiteral("wifi_track_area_checkpoints"), dumpDoubleMatrix(marker.wifiTrackAreaCheckpoints));
    object.insert(QStringLiteral("wifi_pad_enter_times"), dumpPadEntryVector(marker.wifiPadEnterTimes));
    object.insert(QStringLiteral("wifi_critical_proportion"), marker.wifiCriticalProportion);
    object.insert(QStringLiteral("slide_native_track_length"), marker.slideNativeTrackLength);
    object.insert(QStringLiteral("slide_runtime_track_length"), marker.slideRuntimeTrackLength);

    QJsonArray touchPoint;
    touchPoint.append(marker.touchPoint.x());
    touchPoint.append(marker.touchPoint.y());
    object.insert(QStringLiteral("touch_point"), touchPoint);

    object.insert(QStringLiteral("touch_pad"), marker.touchPad);
    object.insert(QStringLiteral("tap_uses_star_material"), marker.tapUsesStarMaterial);
    object.insert(QStringLiteral("tap_star_double"), marker.tapStarDouble);
    object.insert(QStringLiteral("is_each"), marker.isEach);
    object.insert(QStringLiteral("is_break"), marker.isBreak);
    object.insert(QStringLiteral("is_ex"), marker.isEx);
    object.insert(QStringLiteral("is_firework"), marker.isFirework);
    object.insert(QStringLiteral("on_slide"), marker.onSlide);
    object.insert(QStringLiteral("slide_head"), marker.slideHead);
    object.insert(QStringLiteral("tail_on_slide_head"), marker.tailOnSlideHead);
    object.insert(QStringLiteral("slide_each"), marker.slideEach);
    object.insert(QStringLiteral("same_head_slide"), marker.sameHeadSlide);
    object.insert(QStringLiteral("before_slide"), marker.beforeSlide);
    object.insert(QStringLiteral("after_slide"), marker.afterSlide);
    object.insert(QStringLiteral("head_each"), marker.headEach);
    object.insert(QStringLiteral("head_break"), marker.headBreak);
    object.insert(QStringLiteral("head_ex"), marker.headEx);
    object.insert(QStringLiteral("slide_head_uses_tap_material"), marker.slideHeadUsesTapMaterial);
    object.insert(QStringLiteral("track_break"), marker.trackBreak);
    object.insert(QStringLiteral("has_head_star"), marker.hasHeadStar);
    object.insert(QStringLiteral("headless_immediate"), marker.headlessImmediate);
    return object;
}

QStringList splitLinesPreserveText(const QString& text)
{
    QStringList lines = text.split('\n', Qt::KeepEmptyParts);
    for (QString& line : lines) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
    }
    return lines;
}

bool findDifficultyChartStartInMaidata(
    const QString& maidataText,
    int difficultyId,
    int* maidataLine,
    int* firstChartColumn)
{
    if (maidataLine == nullptr || firstChartColumn == nullptr) {
        return false;
    }
    const QString header = QStringLiteral("&inote_%1=").arg(difficultyId);
    const QStringList lines = splitLinesPreserveText(maidataText);
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (!line.startsWith(header)) {
            continue;
        }
        *maidataLine = i + 1;
        *firstChartColumn = header.size() + 1;
        return true;
    }
    return false;
}

QString extractDiffPath(const QString& diff)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(\$[^\s]*?)\s+(?:type mismatch|size mismatch|key mismatch|value mismatch):?)")
    );
    const QRegularExpressionMatch match = re.match(diff);
    if (!match.hasMatch()) {
        return QString();
    }
    return match.captured(1);
}

bool extractIndexedPathComponent(const QString& path, const QString& key, int* index)
{
    if (index == nullptr) {
        return false;
    }
    const QRegularExpression re(QStringLiteral(R"((?:^|\.))") + QRegularExpression::escape(key) + QStringLiteral(R"(\[(\d+)\])"));
    const QRegularExpressionMatch match = re.match(path);
    if (!match.hasMatch()) {
        return false;
    }
    bool ok = false;
    const int value = match.captured(1).toInt(&ok);
    if (!ok) {
        return false;
    }
    *index = value;
    return true;
}

ChartSourceLocation locateChartDiffInMaidata(
    const QString& diffPath,
    const QJsonObject& parseDump,
    const QString& maidataText,
    int difficultyId)
{
    ChartSourceLocation location;

    int maidataChartStartLine = 0;
    int firstChartColumn = 0;
    if (!findDifficultyChartStartInMaidata(maidataText, difficultyId, &maidataChartStartLine, &firstChartColumn)) {
        return location;
    }

    const QStringList lines = splitLinesPreserveText(maidataText);

    auto loadObjectLocation = [&](const QJsonArray& array, int index) -> ChartSourceLocation {
        ChartSourceLocation out;
        if (index < 0 || index >= array.size() || !array.at(index).isObject()) {
            return out;
        }
        const QJsonObject object = array.at(index).toObject();
        const int chartLine = object.value(QStringLiteral("source_line")).toInt();
        const int chartColumn = object.value(QStringLiteral("source_col")).toInt();
        if (chartLine <= 0 || chartColumn <= 0) {
            return out;
        }
        out.chartLine = chartLine;
        out.chartColumn = chartColumn;
        out.maidataLine = maidataChartStartLine + chartLine - 1;
        out.maidataColumn = chartLine == 1 ? (firstChartColumn + chartColumn - 1) : chartColumn;
        if (out.maidataLine >= 1 && out.maidataLine <= lines.size()) {
            out.maidataLineText = lines.at(out.maidataLine - 1);
        }
        out.valid = true;
        return out;
    };

    int index = -1;
    if (extractIndexedPathComponent(diffPath, QStringLiteral("note_markers"), &index)) {
        return loadObjectLocation(parseDump.value(QStringLiteral("note_markers")).toArray(), index);
    }
    if (extractIndexedPathComponent(diffPath, QStringLiteral("errors"), &index)) {
        return loadObjectLocation(parseDump.value(QStringLiteral("errors")).toArray(), index);
    }
    if (extractIndexedPathComponent(diffPath, QStringLiteral("warnings"), &index)) {
        return loadObjectLocation(parseDump.value(QStringLiteral("warnings")).toArray(), index);
    }
    if (extractIndexedPathComponent(diffPath, QStringLiteral("beat_markers"), &index)) {
        return loadObjectLocation(parseDump.value(QStringLiteral("beat_markers")).toArray(), index);
    }
    return location;
}

QString enrichChartOnlyDiff(
    const QString& diff,
    const QJsonObject& actualParseDump,
    const QString& maidataText,
    int difficultyId)
{
    const QString diffPath = extractDiffPath(diff);
    if (diffPath.isEmpty()) {
        return diff;
    }

    const ChartSourceLocation location = locateChartDiffInMaidata(diffPath, actualParseDump, maidataText, difficultyId);
    if (!location.valid) {
        return diff;
    }

    QString enriched = QStringLiteral("%1 | maidata.txt:%2:%3 | chart line %4:%5")
                           .arg(diff)
                           .arg(location.maidataLine)
                           .arg(location.maidataColumn)
                           .arg(location.chartLine)
                           .arg(location.chartColumn);
    if (!location.maidataLineText.isEmpty()) {
        enriched += QStringLiteral(" | line: %1").arg(location.maidataLineText);
    }
    return enriched;
}

QJsonArray dumpNoteMarkers(const QVector<TimelineNoteMarker>& markers)
{
    QJsonArray array;
    for (const TimelineNoteMarker& marker : markers) {
        array.append(dumpNoteMarker(marker));
    }
    return array;
}

QJsonObject dumpParseResult(const SimaiNativeParseResult& result)
{
    QJsonObject object;
    object.insert(QStringLiteral("ok"), result.ok);
    object.insert(QStringLiteral("duration_seconds"), result.durationSeconds);
    object.insert(QStringLiteral("errors"), dumpMessages(result.errors));
    object.insert(QStringLiteral("warnings"), dumpMessages(result.warnings));
    object.insert(QStringLiteral("beat_markers"), dumpBeatMarkers(result.beatMarkers));
    object.insert(QStringLiteral("note_markers"), dumpNoteMarkers(result.noteMarkers));
    return object;
}

QJsonArray dumpExtraFields(const QVector<SimaiRawField>& fields)
{
    QJsonArray array;
    for (const SimaiRawField& field : fields) {
        QJsonObject object;
        object.insert(QStringLiteral("key"), field.key);
        object.insert(QStringLiteral("value"), field.value);
        array.append(object);
    }
    return array;
}

QJsonObject dumpDocumentSemantics(const SimaiDocument& document)
{
    QJsonObject object;
    object.insert(QStringLiteral("title"), document.title);
    object.insert(QStringLiteral("artist"), document.artist);
    object.insert(QStringLiteral("first"), document.first);
    object.insert(QStringLiteral("designer"), document.designer);
    object.insert(QStringLiteral("extra_fields"), dumpExtraFields(document.extraFields));

    QJsonArray difficulties;
    const QVector<int> ids = document.difficultyIds();
    for (int id : ids) {
        const SimaiDifficultyData* difficulty = document.difficulty(id);
        if (difficulty == nullptr) {
            continue;
        }
        QJsonObject item;
        item.insert(QStringLiteral("id"), difficulty->id);
        item.insert(QStringLiteral("level"), difficulty->level);
        item.insert(QStringLiteral("designer"), difficulty->designer);
        item.insert(
            QStringLiteral("chart_parse"),
            dumpParseResult(SimaiNativeParser::parseForTimeline(
                difficulty->chart,
                miacode::simai::buildTimingMetadata(document))));
        difficulties.append(item);
    }
    object.insert(QStringLiteral("difficulties"), difficulties);
    return object;
}

bool compareJsonValues(const QJsonValue& actual, const QJsonValue& expected, const QString& path, QString* diff)
{
    if (actual.type() != expected.type()) {
        if (diff != nullptr) {
            *diff = QStringLiteral("%1 type mismatch (%2 != %3)")
                        .arg(path)
                        .arg(static_cast<int>(actual.type()))
                        .arg(static_cast<int>(expected.type()));
        }
        return false;
    }

    switch (actual.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return true;
    case QJsonValue::Bool:
        if (actual.toBool() == expected.toBool()) {
            return true;
        }
        break;
    case QJsonValue::Double:
        if (nearlyEqual(actual.toDouble(), expected.toDouble())) {
            return true;
        }
        break;
    case QJsonValue::String:
        if (actual.toString() == expected.toString()) {
            return true;
        }
        break;
    case QJsonValue::Array: {
        const QJsonArray actualArray = actual.toArray();
        const QJsonArray expectedArray = expected.toArray();
        if (actualArray.size() != expectedArray.size()) {
            if (diff != nullptr) {
                *diff = QStringLiteral("%1 size mismatch (%2 != %3)")
                            .arg(path)
                            .arg(actualArray.size())
                            .arg(expectedArray.size());
            }
            return false;
        }
        for (int i = 0; i < actualArray.size(); ++i) {
            if (!compareJsonValues(actualArray.at(i), expectedArray.at(i), QStringLiteral("%1[%2]").arg(path).arg(i), diff)) {
                return false;
            }
        }
        return true;
    }
    case QJsonValue::Object: {
        const QJsonObject actualObject = actual.toObject();
        const QJsonObject expectedObject = expected.toObject();
        QStringList actualKeys = actualObject.keys();
        QStringList expectedKeys = expectedObject.keys();
        actualKeys.sort();
        expectedKeys.sort();
        if (actualKeys != expectedKeys) {
            if (diff != nullptr) {
                *diff = QStringLiteral("%1 key mismatch").arg(path);
            }
            return false;
        }
        for (const QString& key : actualKeys) {
            if (!compareJsonValues(actualObject.value(key), expectedObject.value(key), path + QLatin1Char('.') + key, diff)) {
                return false;
            }
        }
        return true;
    }
    }

    if (diff != nullptr) {
        QJsonArray actualArray;
        actualArray.append(actual);
        QJsonArray expectedArray;
        expectedArray.append(expected);
        *diff = QStringLiteral("%1 value mismatch: actual=%2 expected=%3")
                    .arg(path)
                    .arg(QString::fromUtf8(QJsonDocument(actualArray).toJson(QJsonDocument::Compact)))
                    .arg(QString::fromUtf8(QJsonDocument(expectedArray).toJson(QJsonDocument::Compact)));
    }
    return false;
}

SimaiDocument transformWholeDocument(const SimaiDocument& source, miacode::chart_transform::ChartTransformOp op, int* totalChangedCount)
{
    SimaiDocument transformed = source;
    int totalChanged = 0;
    const QVector<int> ids = transformed.difficultyIds();
    for (int id : ids) {
        SimaiDifficultyData* difficulty = transformed.difficulty(id);
        if (difficulty == nullptr) {
            continue;
        }
        int changed = 0;
        difficulty->chart = miacode::chart_transform::transformChartText(difficulty->chart, op, &changed);
        totalChanged += changed;
    }
    if (totalChangedCount != nullptr) {
        *totalChangedCount = totalChanged;
    }
    return transformed;
}

bool parseTransformOpToken(const QString& token, miacode::chart_transform::ChartTransformOp* op)
{
    if (op == nullptr) {
        return false;
    }
    const QString normalized = token.trimmed().toLower();
    if (normalized == QLatin1String("lr") || normalized == QLatin1String("mirror-lr")) {
        *op = miacode::chart_transform::ChartTransformOp::MirrorLeftRight;
        return true;
    }
    if (normalized == QLatin1String("ud") || normalized == QLatin1String("mirror-ud")) {
        *op = miacode::chart_transform::ChartTransformOp::MirrorUpDown;
        return true;
    }
    if (normalized == QLatin1String("180") || normalized == QLatin1String("rotate-180")) {
        *op = miacode::chart_transform::ChartTransformOp::Rotate180;
        return true;
    }
    if (normalized == QLatin1String("cw45") || normalized == QLatin1String("rotate-cw-45")) {
        *op = miacode::chart_transform::ChartTransformOp::Rotate45Clockwise;
        return true;
    }
    if (normalized == QLatin1String("ccw45") || normalized == QLatin1String("rotate-ccw-45")) {
        *op = miacode::chart_transform::ChartTransformOp::Rotate45CounterClockwise;
        return true;
    }
    return false;
}

bool runFolderMatchSpec(const QString& inputPath, QTextStream& out, QTextStream& err, int* failed)
{
    const QFileInfo inputInfo(inputPath);
    const QString sourcePath = inputInfo.isDir()
        ? QDir(inputInfo.absoluteFilePath()).filePath(QStringLiteral("maidata.txt"))
        : inputInfo.absoluteFilePath();
    const QString baseDir = QFileInfo(sourcePath).absolutePath();

    QString sourceText;
    QString errorMessage;
    if (!readUtf8File(sourcePath, &sourceText, &errorMessage)) {
        err << "[FAIL] folder spec setup\n";
        err << "  " << errorMessage << '\n';
        ++(*failed);
        return false;
    }

    const SimaiDocument sourceDocument = SimaiDocument::fromText(sourceText);
    const QList<TransformCase> cases = {
        {QStringLiteral("mirror left/right"), miacode::chart_transform::ChartTransformOp::MirrorLeftRight, QStringLiteral("lr-mirror.txt")},
        {QStringLiteral("mirror up/down"), miacode::chart_transform::ChartTransformOp::MirrorUpDown, QStringLiteral("ud-mirror.txt")},
        {QStringLiteral("rotate 180"), miacode::chart_transform::ChartTransformOp::Rotate180, QStringLiteral("180-rotate.txt")},
        {QStringLiteral("rotate +45"), miacode::chart_transform::ChartTransformOp::Rotate45Clockwise, QStringLiteral("clockwise.txt")},
        {QStringLiteral("rotate -45"), miacode::chart_transform::ChartTransformOp::Rotate45CounterClockwise, QStringLiteral("anti-clockwise.txt")},
    };

    bool allPassed = true;
    for (const TransformCase& item : cases) {
        QString expectedText;
        const QString expectedPath = QDir(baseDir).filePath(item.expectedFileName);
        if (!readUtf8File(expectedPath, &expectedText, &errorMessage)) {
            err << "[FAIL] " << item.label << '\n';
            err << "  " << errorMessage << '\n';
            ++(*failed);
            allPassed = false;
            continue;
        }

        int changedCount = 0;
        const SimaiDocument actualDocument = transformWholeDocument(sourceDocument, item.op, &changedCount);
        QString diff;
        bool same = false;
        const bool expectedLooksLikeDocument = !SimaiDocument::parseRawFields(expectedText).isEmpty();
        if (expectedLooksLikeDocument) {
            const SimaiDocument expectedDocument = SimaiDocument::fromText(expectedText);
            same = compareJsonValues(
                dumpDocumentSemantics(actualDocument),
                dumpDocumentSemantics(expectedDocument),
                QStringLiteral("$"),
                &diff
            );
        } else {
            const QVector<int> actualIds = actualDocument.difficultyIds();
            if (actualIds.size() != 1) {
                diff = QStringLiteral("expected chart-only fixture, but transformed document has %1 difficulties").arg(actualIds.size());
            } else {
                const SimaiDifficultyData* actualDifficulty = actualDocument.difficulty(actualIds.constFirst());
                if (actualDifficulty != nullptr) {
                    const QJsonObject actualParseDump = dumpParseResult(SimaiNativeParser::parseForTimeline(
                        actualDifficulty->chart,
                        miacode::simai::buildTimingMetadata(actualDocument)));
                    const QJsonObject expectedParseDump = dumpParseResult(SimaiNativeParser::parseForTimeline(expectedText));
                    same = compareJsonValues(actualParseDump, expectedParseDump, QStringLiteral("$.chart"), &diff);
                    if (!same) {
                        diff = enrichChartOnlyDiff(diff, actualParseDump, sourceText, actualDifficulty->id);
                    }
                }
            }
        }
        if (same) {
            out << "[PASS] " << item.label << " matches " << item.expectedFileName << " (changed=" << changedCount << ")\n";
            continue;
        }

        err << "[FAIL] " << item.label << '\n';
        err << "  source:   " << QDir::toNativeSeparators(sourcePath) << '\n';
        err << "  expected: " << QDir::toNativeSeparators(expectedPath) << '\n';
        err << "  diff:     " << diff << '\n';
        ++(*failed);
        allPassed = false;
    }

    return allPassed;
}

void runInlineSpecs(QTextStream& err, int* failed)
{
    {
        int changed = 0;
        const QString input = QStringLiteral("12 3h[4:1] A1 C1h[4:1] 1-5[8:1]");
        const QString output = miacode::chart_transform::toggleBreakForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1b/2b 3bh[4:1] A1b C1h[4:1] 1b-5b[8:1]"),
            QStringLiteral("toggle break enables break for notes, touch, and slide track but skips touch-hold"),
            failed,
            err
        );
        expectTrue(changed == 6, QStringLiteral("toggle break counts all changed objects"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1b/2b 3bh[4:1] A1b C1h[4:1] 1b-5b[8:1]");
        const QString output = miacode::chart_transform::toggleBreakForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1/2 3h[4:1] A1 C1h[4:1] 1-5[8:1]"),
            QStringLiteral("toggle break clears break when all eligible objects already have it"),
            failed,
            err
        );
        expectTrue(changed == 6, QStringLiteral("toggle break clear counts all cleared objects"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1 2h[4:1] A1 C1h[4:1] 1-5[8:1] 6b");
        const QString output = miacode::chart_transform::toggleExForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1x 2xh[4:1] A1 C1h[4:1] 1x-5[8:1] 6bx"),
            QStringLiteral("toggle EX enables ex for note, hold, and slide head but skips touch and touch-hold"),
            failed,
            err
        );
        expectTrue(changed == 4, QStringLiteral("toggle EX counts eligible note objects that changed"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1x 2xh[4:1] A1 C1h[4:1] 1x-5[8:1] 6bx");
        const QString output = miacode::chart_transform::toggleExForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1 2h[4:1] A1 C1h[4:1] 1-5[8:1] 6b"),
            QStringLiteral("toggle EX clears ex when all eligible objects already have it"),
            failed,
            err
        );
        expectTrue(changed == 4, QStringLiteral("toggle EX clear counts eligible note objects"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("A1 A2h[4:1] C1 1 1-5[8:1]");
        const QString output = miacode::chart_transform::toggleFireworkForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("A1f A2fh[4:1] C1f 1 1-5[8:1]"),
            QStringLiteral("toggle firework enables firework for touch and touch-hold only"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("toggle firework counts only touch objects"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("A1f A2fh[4:1] C1f 1 1-5[8:1]");
        const QString output = miacode::chart_transform::toggleFireworkForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("A1 A2h[4:1] C1 1 1-5[8:1]"),
            QStringLiteral("toggle firework clears firework when all eligible touch objects already have it"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("toggle firework clear counts only touch objects"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1 A1 D1 E1 1p3[8:1] 1<5[8:1]");
        const QString output = miacode::chart_transform::transformChartText(
            input,
            miacode::chart_transform::ChartTransformOp::MirrorLeftRight,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("8 A8 D1 E1 8q6[8:1] 8>4[8:1]"),
            QStringLiteral("mirror left/right remaps note lanes, touch lanes, and mirror-sensitive slide operators"),
            failed,
            err
        );
        expectTrue(changed == 8, QStringLiteral("mirror left/right counts transformed chart atoms"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8 A8 D8 8-4[8:1]");
        const QString output = miacode::chart_transform::transformChartText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate45Clockwise,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("1 A1 D1 1-5[8:1]"),
            QStringLiteral("rotate +45 shifts notes, touch lanes, and slide endpoints clockwise"),
            failed,
            err
        );
        expectTrue(changed == 5, QStringLiteral("rotate +45 counts transformed chart atoms"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8}1");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::MirrorLeftRight,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("8}8"),
            QStringLiteral("mirror left/right keeps timing prefix digits before } unchanged and mirrors the note body after }"),
            failed,
            err
        );
        expectTrue(changed == 1, QStringLiteral("mirror left/right counts only the mirrored note body after }"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8}1");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate45Clockwise,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("8}2"),
            QStringLiteral("rotate +45 keeps timing prefix digits before } unchanged and rotates the note body after }"),
            failed,
            err
        );
        expectTrue(changed == 1, QStringLiteral("rotate +45 counts only the rotated note body after }"), failed, err);
    }

    // Regression: a selection that *starts* with an unmatched closing
    // brace — typical when the user drags from inside a {N}…} subdivision
    // and crosses its trailing '}' — must still rotate every note in the
    // selection. Before the fix, the leading '}' was glued onto the
    // first note (yielding token "}E4" which failed every recognition
    // path in transformToken), so E4 silently survived while E5 and E6
    // rotated. All three should rotate now.
    {
        int changed = 0;
        const QString input = QStringLiteral("}E4,E5,E6");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate180,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("}E8,E1,E2"),
            QStringLiteral("rotate 180 transforms every touch note in a selection that leads with an unmatched }"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("rotate 180 counts all three touch notes after a leading }"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("}E4,E5,E6,");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate180,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("}E8,E1,E2,"),
            QStringLiteral("rotate 180 keeps the trailing comma untouched when the selection ends just after the last note"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("rotate 180 counts all three notes regardless of the trailing comma"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("}1,2,3");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate180,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("}5,6,7"),
            QStringLiteral("rotate 180 transforms plain digit notes in a selection that leads with an unmatched }"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("rotate 180 counts all three plain notes after a leading }"), failed, err);
    }

    // Direct-call variant: transformChartText with a stray leading `}`
    // exercises the close-bracket flush trigger added to its tokenizer
    // (the defense-in-depth half of the fix). Even without the selection
    // edge-split, the closer must not absorb the following note.
    {
        int changed = 0;
        const QString input = QStringLiteral("}E4,E5,E6");
        const QString output = miacode::chart_transform::transformChartText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate180,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("}E8,E1,E2"),
            QStringLiteral("transformChartText flushes stray leading } so the following note still parses"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("transformChartText with a leading } counts all three touches"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8:1)1");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::MirrorLeftRight,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("8:1)8"),
            QStringLiteral("mirror left/right keeps unmatched timing suffix before ) unchanged inside a selection"),
            failed,
            err
        );
        expectTrue(changed == 1, QStringLiteral("mirror left/right counts only the mirrored note body after )"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8:1]1");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate45Clockwise,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("8:1]2"),
            QStringLiteral("rotate +45 keeps unmatched timing suffix before ] unchanged inside a selection"),
            failed,
            err
        );
        expectTrue(changed == 1, QStringLiteral("rotate +45 counts only the rotated note body after ]"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8:1)12");
        const QString output = miacode::chart_transform::toggleBreakForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("8:1)1b/2b"),
            QStringLiteral("toggle break keeps unmatched timing suffix before ) unchanged inside a selection"),
            failed,
            err
        );
        expectTrue(changed == 2, QStringLiteral("toggle break counts only changed notes after )"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8:1]1");
        const QString output = miacode::chart_transform::toggleExForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("8:1]1x"),
            QStringLiteral("toggle EX keeps unmatched timing suffix before ] unchanged inside a selection"),
            failed,
            err
        );
        expectTrue(changed == 1, QStringLiteral("toggle EX counts only changed notes after ]"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1$$x 1@x-5[8:1]");
        const QString output = miacode::chart_transform::toggleBreakForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("1bx$$ 1bx@-5b[8:1]"),
            QStringLiteral("toggle break preserves tap-star and @ slide-head material modifiers"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("toggle break counts star-tap and @ slide as changed"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1$$ 1?-5[8:1] 1!-5[8:1]");
        const QString output = miacode::chart_transform::transformChartText(
            input,
            miacode::chart_transform::ChartTransformOp::MirrorLeftRight,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("8$$ 8?-4[8:1] 8!-4[8:1]"),
            QStringLiteral("mirror keeps $$ / ? / ! modifiers while remapping note lanes"),
            failed,
            err
        );
        expectTrue(changed == 5, QStringLiteral("mirror counts transformed atoms with new material/headless modifiers"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("1[8:1");
        const QString output = miacode::chart_transform::transformChartSelectionText(
            input,
            miacode::chart_transform::ChartTransformOp::MirrorLeftRight,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("8[8:1"),
            QStringLiteral("mirror left/right keeps unmatched timing prefix after [ unchanged inside a selection"),
            failed,
            err
        );
        expectTrue(changed == 1, QStringLiteral("mirror left/right counts only the mirrored note body before unmatched ["), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{8}4^1[4:1],5,6h[4:1],,");
        const QString output = miacode::chart_transform::raiseSubdivisionForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{16}4^1[4:1],,5,,6h[4:1],,,,"),
            QStringLiteral("subdivision +1 doubles the rendered grid and expands comma spacing"),
            failed,
            err
        );
        expectTrue(changed == 5, QStringLiteral("subdivision +1 counts the signature and inserted commas"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1,,2,,3,,");
        const QString output = miacode::chart_transform::lowerSubdivisionForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{8}1,2,3,"),
            QStringLiteral("subdivision -1 halves the rendered grid when all occupied slots are even-aligned"),
            failed,
            err
        );
        expectTrue(changed == 4, QStringLiteral("subdivision -1 counts the signature and removed commas"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16},,7,6,5,4,1,2,3,4,");
        const QString output = miacode::chart_transform::lowerSubdivisionForSelection(input, &changed);
        expectEqual(
            output,
            input,
            QStringLiteral("subdivision -1 leaves text unchanged when odd-grid notes would be lost"),
            failed,
            err
        );
        expectTrue(changed == 0, QStringLiteral("subdivision -1 reports no changes when reduction is impossible"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16},,7,6,5,4,1,2,3,4,\n{8}7^2[4:1],,5,,4h[4:1],,");
        const QString output = miacode::chart_transform::lowerSubdivisionForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{16},,7,6,5,4,1,2,3,4,\n{4}7^2[4:1],5,4h[4:1],"),
            QStringLiteral("subdivision -1 judges each selected line independently"),
            failed,
            err
        );
        expectTrue(changed == 4, QStringLiteral("subdivision -1 counts only the reducible line changes"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16},,7,6,5,4,1,2,3,4,\n{8}7^2[4:1],5,4h[4:1],,");
        const QString output = miacode::chart_transform::lowerSubdivisionForSelection(input, &changed);
        expectEqual(
            output,
            input,
            QStringLiteral("subdivision -1 independently keeps multiple irreducible selected lines unchanged"),
            failed,
            err
        );
        expectTrue(changed == 0, QStringLiteral("subdivision -1 reports no changes when every selected line is irreducible"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1,,\n2/4,,3h[8:1],,5,3,4,2,,6/4,,5,7,,");
        const QString output = miacode::chart_transform::lowerSubdivisionForSelection(input, &changed);
        expectEqual(
            output,
            input,
            QStringLiteral("subdivision -1 refuses a wrapped selection when later notes occupy odd slots"),
            failed,
            err
        );
        expectTrue(changed == 0, QStringLiteral("subdivision -1 reports no changes for irreducible wrapped selection"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1,,\n2/4,,3h[8:1],,5,,7,,");
        const QString output = miacode::chart_transform::lowerSubdivisionForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{8}1,\n2/4,3h[8:1],5,7,"),
            QStringLiteral("subdivision -1 carries slot parity across wrapped lines without explicit subdivision reset"),
            failed,
            err
        );
        expectTrue(changed == 6, QStringLiteral("subdivision -1 counts signature and removed commas across wrapped lines"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1,,1,,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{24}1,,,1,,,"),
            QStringLiteral("subdivision +1/2 raises 16 to 24 and expands two comma slots to three"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("subdivision +1/2 counts the signature and inserted commas"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{32}1,,2,,3,,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{48}1,,,2,,,3,,,"),
            QStringLiteral("subdivision +1/2 raises 32 to 48"),
            failed,
            err
        );
        expectTrue(changed == 4, QStringLiteral("subdivision +1/2 counts one signature and three inserted commas"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1h[16:5],,2-4[32:1],,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{24}1h[16:5],,,2-4[32:1],,,"),
            QStringLiteral("subdivision +1/2 leaves bracketed timing values unchanged"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("subdivision +1/2 still counts signature and inserted commas around bracket syntax"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1,,2,,|| {16}3,,4,,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{24}1,,,2,,,|| {16}3,,4,,"),
            QStringLiteral("subdivision +1/2 skips ordinary comments"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("subdivision +1/2 counts only pre-comment changes"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1,2,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{48}1,,,2,,,"),
            QStringLiteral("subdivision +1/2 triples an even subdivision whose notes sit on odd slots, since a lossless halving is impossible"),
            failed,
            err
        );
        expectTrue(changed == 5, QStringLiteral("subdivision +1/2 counts the tripled signature and four inserted commas for the fallback"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{3}1,1,1,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{9}1,,,1,,,1,,,"),
            QStringLiteral("subdivision +1/2 triples an odd subdivision (3x cannot be halved) while keeping every note aligned"),
            failed,
            err
        );
        expectTrue(changed == 7, QStringLiteral("subdivision +1/2 counts the tripled signature and six inserted commas"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{3}1,,1,,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{9}1,,,,,,1,,,,,,"),
            QStringLiteral("subdivision +1/2 triples odd subdivisions across multi-rest runs without drifting note times"),
            failed,
            err
        );
        expectTrue(changed == 9, QStringLiteral("subdivision +1/2 counts the tripled signature and eight inserted commas"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{16}1,,1,,{3}1,1,1,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{24}1,,,1,,,{9}1,,,1,,,1,,,"),
            QStringLiteral("subdivision +1/2 halves the even-aligned chunk and triples the odd chunk in one pass"),
            failed,
            err
        );
        expectTrue(changed == 10, QStringLiteral("subdivision +1/2 counts both rewritten signatures and every inserted comma across chunks"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{715827883}1,1,");
        const QString output = miacode::chart_transform::raiseSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            input,
            QStringLiteral("subdivision +1/2 leaves an oversized odd subdivision untouched rather than overflowing the tripled denominator"),
            failed,
            err
        );
        expectTrue(changed == 0, QStringLiteral("subdivision +1/2 reports no changes when even a triple would overflow int"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{24}1,,,1,,,");
        const QString output = miacode::chart_transform::lowerSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            QStringLiteral("{16}1,,1,,"),
            QStringLiteral("subdivision -1/2 lowers 24 to 16 and compresses three comma slots to two"),
            failed,
            err
        );
        expectTrue(changed == 3, QStringLiteral("subdivision -1/2 counts the signature and removed commas"), failed, err);
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("{24}1,,2,,");
        const QString output = miacode::chart_transform::lowerSubdivisionHalfStepForSelection(input, &changed);
        expectEqual(
            output,
            input,
            QStringLiteral("subdivision -1/2 leaves non-third-aligned notes unchanged"),
            failed,
            err
        );
        expectTrue(changed == 0, QStringLiteral("subdivision -1/2 reports no changes when reduction is not lossless"), failed, err);
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("A1fh[4:1],,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts a simple valid chart"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}A1hf[4:1],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart leaves a 384-divisor touch-hold duration untouched while still canonicalizing modifier order"),
            failed,
            err
        );
    }

    {
        // Touch notes no longer accept the `x` modifier, so normalization
        // refuses the chart instead of canonicalizing it.
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("A1fxh[4:1],,,,\nE"));
        expectTrue(!normalized.ok, QStringLiteral("normalize whole chart rejects a touch-hold with the x modifier"), failed, err);
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("A1fh[24:6]/1h[8:0]/1-5[24:3]/1-5[120#24:3],,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts mixed 384-divisor and hash durations"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}A1hf[24:6]/1h[8:0]/1-5[24:3]/1-5[120#24:3],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart leaves durations untouched when the original beats is a divisor of 384 or the signature carries '#'"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("{16}2h[2000:1],7h[2000:1],3h[2000:1],6h[2000:1], 1h[2000:1],5h[2000:1],8h[2000:1],4h[2000:1], 7h[2000:1]/3h[2000:1],8h[2000:1]/4h[2000:1],5h[2000:1]/1h[2000:1],2h[2000:1]/6h[2000:1], 3h[2000:1]/7-3[1:3],8h[2000:1]/4h[2000:1],5h[2000:1]/1h[2000:1],6h[2000:1]/2h[2000:1],\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts the reported dense hold fragment"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}2h,7h,3h,6h, 1h,5h,8h,4h, 7h/3h,8h/4h,5h/1h,2h/6h, 3h/7-3[1:3],8h/4h,5h/1h,6h/2h,\nE"),
            QStringLiteral("normalize whole chart collapses non-384-divisor zero-length note holds but keeps a 384-divisor slide duration verbatim"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("1h[500:1]/1-5[500:1]/1-5[2000:1],,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts irreducible hold and slide durations"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1h[384:1]/1-5[384:1]/1-5[384:1],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart keeps local subdivisions driven by note positions even when durations snap to 384"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("{16}1-5[192:1],,2,, 3,,4,, ,,,, ,,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts the reported slide-duration sample"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1-5[192:1],,2,, 3,,4,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart does not let hold or slide durations widen the selected note grid"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("1h[7:1],,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts a non-384-divisor hold duration"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1h[24:3],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart snaps non-384 hold duration via snapXOverY (y=7 -> q=24)"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("1-5[5:1],,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts a non-384-divisor slide duration"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1-5[16:3],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart snaps non-384 slide duration via snapXOverY (y=5 -> q=16)"),
            failed,
            err
        );
    }

    {
        // {28} measure: each moment has denom dividing 28. snapXOverY for
        // each (denom 28, 14, 7, etc.) picks a per-moment q; the segment
        // subdivision is the LCM of those q's, which is 96 for moments
        // whose denom is exactly 28.
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("{28}1,2,3,4,5,6,7,8,1,2,3,4,5,6,7,8,1,2,3,4,5,6,7,8,1,2,3,4,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize accepts a {28} measure"), failed, err);
        expectTrue(
            normalized.text.startsWith(QStringLiteral("{96}")),
            QStringLiteral("normalize routes a {28} measure to {96} via per-moment snapXOverY (no longer falls through to {384})"),
            failed,
            err
        );
    }

    {
        // {7} measure: moments at multiples of 1/7. Per-moment q=24 (max
        // divisor of 384 <= 28). Segment LCM = 24.
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("{7}1,2,3,4,5,6,7,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize accepts a {7} measure"), failed, err);
        expectTrue(
            normalized.text.startsWith(QStringLiteral("{24}")),
            QStringLiteral("normalize routes a {7} measure to {24} via per-moment snapXOverY"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral(",,(180)|| 3 / 4\n,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts BPM plus inline time-signature control syntax"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16},,,, ,,,,\n(180) || 3/4\n{16},,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart splits partial measures and merges adjacent BPM/time-signature lines"),
            failed,
            err
        );
    }

    {
        // User-supplied 变拍 fixture through 谱面整理: a boundary-aligned || 3/4
        // after a (126) whole-note {1} pickup, then two {16} 3/4 measures.
        // Normalization must preserve the inline time-signature change and be
        // idempotent across the 变拍 boundary (the 变拍/变BPM 段相位 reset must not
        // drift the output on a second pass).
        const QString chartText = QStringLiteral(
            "(126)\n"
            "{1},|| 3/4\n"
            "{16}2h[16:3],,,4h[16:3],,,5h[8:1],,6,,47,,\n"
            "{16}7h[16:3],,,5h[16:3],,,4h[8:1],,3,,52,,");
        const miacode::chart_transform::ChartNormalizationResult first =
            miacode::chart_transform::normalizeChartText(chartText);
        expectTrue(first.ok, QStringLiteral("变拍fixture: 谱面整理 accepts the (126)/{1}/|| 3/4 chart"), failed, err);
        expectTrue(first.text.contains(QStringLiteral("|| 3/4")),
                   QStringLiteral("变拍fixture: 谱面整理 preserves the inline || 3/4 change"), failed, err);
        const miacode::chart_transform::ChartNormalizationResult second =
            miacode::chart_transform::normalizeChartText(first.text);
        expectTrue(second.ok, QStringLiteral("变拍fixture: re-normalizing the 变拍 output stays valid"), failed, err);
        expectEqual(second.text, first.text,
                    QStringLiteral("变拍fixture: 谱面整理 is idempotent across the 变拍 boundary"), failed, err);
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("1,2, || hello\n3,4,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart keeps ordinary mid-measure comments"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1,,,, 2,,,,\n|| hello\n{16}3,,,, 4,,,,\nE"),
            QStringLiteral("normalize whole chart splits ordinary mid-measure comments into a standalone line without reordering nearby chart text"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("{24},1,,,,,{16},,,,,,,,,,,,\nE"));
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts mixed local subdivisions within one measure"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{24},1,,,,, {16},,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart minimizes subdivisions per beat instead of promoting the whole measure"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral("@\nE"));
        expectTrue(!normalized.ok, QStringLiteral("normalize whole chart blocks charts with syntax errors"), failed, err);
    }

    {
        const miacode::simai::SimaiTimingMetadata timingMetadata =
            miacode::simai::buildTimingMetadataFromRawText(QStringLiteral("&whole_time_signature=3/4"), true);
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(QStringLiteral(",,,,,,\nE"), timingMetadata);
        expectTrue(normalized.ok, QStringLiteral("normalize whole chart accepts metadata-driven default time signatures"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16},,,, ,,,, ,,,,\n{16},,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize whole chart uses metadata-driven 3/4 measure boundaries"),
            failed,
            err
        );
    }

    {
        const miacode::chart_transform::ChartNormalizationOptions defaults;
        const miacode::chart_transform::ChartNormalizationOptions loaded =
            miacode::chart_transform::chartNormalizationOptionsFromPreferences(QJsonObject(), defaults);
        expectTrue(
            loaded.startAtNewMeasure && loaded.reduceTo384Grid,
            QStringLiteral("chart normalization preferences default both options to enabled"),
            failed,
            err
        );

        QJsonObject preview;
        miacode::chart_transform::saveChartNormalizationOptionsToPreferences(
            &preview,
            miacode::chart_transform::ChartNormalizationOptions{false, false});
        const miacode::chart_transform::ChartNormalizationOptions restored =
            miacode::chart_transform::chartNormalizationOptionsFromPreferences(preview, defaults);
        expectTrue(
            !restored.startAtNewMeasure && !restored.reduceTo384Grid,
            QStringLiteral("chart normalization preferences round-trip through preview json"),
            failed,
            err
        );
    }

    {
        const QString fullText = QStringLiteral("1,2,3,4,\nE");
        const int selectionStart = fullText.indexOf(QStringLiteral("2,3,"));
        const int selectionEnd = selectionStart + QStringLiteral("2,3,").size();
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartSelectionText(
                fullText,
                selectionStart,
                selectionEnd,
                miacode::simai::SimaiTimingMetadata(),
                miacode::chart_transform::ChartNormalizationOptions{true, true});
        expectTrue(normalized.ok, QStringLiteral("selection normalize accepts a mid-measure selection"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("|| 4/4\n{16}2,,,, 3,,,,\n{4}"),
            QStringLiteral("selection normalize injects a restart time signature when starting a new measure mid-line, and appends a standalone trailing {N} so post-selection content keeps the original subdivision"),
            failed,
            err
        );
    }

    {
        const QString fullText = QStringLiteral("{9}1,1,,,,,,,,\nE");
        const int selectionStart = fullText.indexOf(QLatin1Char('1'));
        const int selectionEnd = fullText.indexOf(QLatin1Char('\n'));
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartSelectionText(
                fullText,
                selectionStart,
                selectionEnd,
                miacode::simai::SimaiTimingMetadata(),
                miacode::chart_transform::ChartNormalizationOptions{false, false});
        expectTrue(normalized.ok, QStringLiteral("selection normalize accepts an exact non-384 subdivision selection"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{9}1,1,,,,,,,,"),
            QStringLiteral("selection normalize carries the active subdivision from the selection prefix; no trailing {N} when the last emitted {N} already matches the original final currentBeats"),
            failed,
            err
        );
    }

    {
        const QString input =
            QStringLiteral("{16}1-5[8:1],1%1\nE").arg(QString(15, QLatin1Char(',')));
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                input,
                miacode::simai::SimaiTimingMetadata(),
                miacode::chart_transform::ChartNormalizationOptions{true, false});
        expectTrue(normalized.ok, QStringLiteral("reduce=false on a 384-grid-only measure falls back to approximate"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1-5[8:1],1,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("reduce=false on a measure whose moments are all on the 384 grid renders identically to reduce=true (no chunk collapse)"),
            failed,
            err
        );
    }

    // Regression: chained slides (segments joined with `*`) must rotate
    // every lane digit in every segment, not just the first one.
    // Reported on 8b-3[8:1]*^5[8:1] — before the fix only "8b-3" got
    // rotated and "^5" remained at lane 5.
    {
        int changed = 0;
        const QString input = QStringLiteral("8b-3[8:1]*^5[8:1]");
        const QString output = miacode::chart_transform::transformChartText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate45Clockwise,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("1b-4[8:1]*^6[8:1]"),
            QStringLiteral("rotate +45 rotates every segment of a *-chained slide (8b-3[8:1]*^5[8:1])"),
            failed,
            err
        );
    }

    {
        int changed = 0;
        const QString input = QStringLiteral("8b-3[8:1]*>5[8:1]");
        const QString output = miacode::chart_transform::transformChartText(
            input,
            miacode::chart_transform::ChartTransformOp::Rotate45Clockwise,
            &changed
        );
        expectEqual(
            output,
            QStringLiteral("1b-4[8:1]*>6[8:1]"),
            QStringLiteral("rotate +45 rotates every segment of a *-chained slide with > arc (8b-3[8:1]*>5[8:1])"),
            failed,
            err
        );
    }

    // Snap rule: q = largest divisor of 384 with q <= 4y; p = round(q*x/y).
    // Different x for same y share a q; collisions on adjacent x are
    // accepted (rare and only on dense non-384 charts).
    {
        const miacode::chart_transform::SnapResult r5 =
            miacode::chart_transform::snapXOverY(1, 5);
        expectTrue(
            r5.ok && r5.q == 16 && r5.p == 3,
            QStringLiteral("snap: 1/5 -> 3/16 (y=5: largest divisor of 384 <= 20 is 16; p=round(16/5)=3)"),
            failed,
            err
        );

        const miacode::chart_transform::SnapResult r20 =
            miacode::chart_transform::snapXOverY(4, 20);
        expectTrue(
            r20.ok && r20.q == 64 && r20.p == 13,
            QStringLiteral("snap: 4/20 -> 13/64 (y=20: largest divisor of 384 <= 80 is 64; distinct from 1/5)"),
            failed,
            err
        );

        const miacode::chart_transform::SnapResult r7 =
            miacode::chart_transform::snapXOverY(1, 7);
        expectTrue(
            r7.ok && r7.q == 24 && r7.p == 3,
            QStringLiteral("snap: 1/7 -> 3/24 (y=7: largest divisor of 384 <= 28 is 24; p=round(24/7)=3)"),
            failed,
            err
        );

        const miacode::chart_transform::SnapResult r28 =
            miacode::chart_transform::snapXOverY(1, 28);
        expectTrue(
            r28.ok && r28.q == 96 && r28.p == 3,
            QStringLiteral("snap: 1/28 -> 3/96 (y=28: largest divisor of 384 <= 112 is 96)"),
            failed,
            err
        );

        const miacode::chart_transform::SnapResult rDiv =
            miacode::chart_transform::snapXOverY(3, 16);
        expectTrue(
            rDiv.ok && rDiv.q == 16 && rDiv.p == 3,
            QStringLiteral("snap: y that already divides 384 passes through unchanged"),
            failed,
            err
        );

        const miacode::chart_transform::SnapResult rBig =
            miacode::chart_transform::snapXOverY(1, 2000);
        expectTrue(
            rBig.ok && rBig.q == 384 && rBig.p == 0,
            QStringLiteral("snap: y > 384 falls back to 384-grid round (1/2000 -> 0/384)"),
            failed,
            err
        );

        // x >= y: no carry split; direct round(q*x/y) handles it.
        const miacode::chart_transform::SnapResult rBigX =
            miacode::chart_transform::snapXOverY(11, 5);
        expectTrue(
            rBigX.ok && rBigX.q == 16 && rBigX.p == 35,
            QStringLiteral("snap: x=11, y=5 -> 35/16 (no special carry; just round(16*11/5))"),
            failed,
            err
        );
    }

    // Regression: multi-segment slide via `*` must keep each segment's break
    // 'b' on its own branch. The earlier collapse-to-single-flag rebuild
    // moved a 'b' from the second branch to the first
    // (1-5[8:1]*-4b[8:1] -> 1-5b[8:1]*-4[8:1]), changing semantics.
    {
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("1-5[8:1]*-4b[8:1],,,,\nE"),
                miacode::simai::SimaiTimingMetadata(),
                miacode::chart_transform::ChartNormalizationOptions{true, false});
        expectTrue(normalized.ok, QStringLiteral("normalize accepts a multi-segment slide with per-branch break"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1-5[8:1]*-4b[8:1],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize keeps the break 'b' on the second branch (was moved to the first branch by the old collapse-to-single-flag rebuild)"),
            failed,
            err
        );
    }

    {
        // Inverse case: break on first branch, none on second.
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("1-5b[8:1]*-4[8:1],,,,\nE"),
                miacode::simai::SimaiTimingMetadata(),
                miacode::chart_transform::ChartNormalizationOptions{true, false});
        expectTrue(normalized.ok, QStringLiteral("normalize accepts the inverse multi-segment slide"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1-5b[8:1]*-4[8:1],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize keeps the break 'b' on the first branch"),
            failed,
            err
        );
    }

    {
        // Both branches break.
        const miacode::chart_transform::ChartNormalizationResult normalized =
            miacode::chart_transform::normalizeChartText(
                QStringLiteral("1-5b[8:1]*-4b[8:1],,,,\nE"),
                miacode::simai::SimaiTimingMetadata(),
                miacode::chart_transform::ChartNormalizationOptions{true, false});
        expectTrue(normalized.ok, QStringLiteral("normalize accepts both-branches-break multi-segment slide"), failed, err);
        expectEqual(
            normalized.text,
            QStringLiteral("{16}1-5b[8:1]*-4b[8:1],,,, ,,,, ,,,, ,,,,\nE"),
            QStringLiteral("normalize keeps both 'b' flags on their respective branches"),
            failed,
            err
        );
    }
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    int failed = 0;
    const QStringList args = app.arguments();
    if (args.size() >= 3 && args.at(1) == QLatin1String("--normalize-parse-compare")) {
        const QString path = args.at(2);
        int requestedDiff = -1;
        if (args.size() >= 4) {
            bool idOk = false;
            const int parsed = args.at(3).toInt(&idOk);
            if (idOk) requestedDiff = parsed;
        }
        bool reduce = false;
        if (args.size() >= 5 && args.at(4).compare(QLatin1String("reduce"), Qt::CaseInsensitive) == 0) {
            reduce = true;
        }
        QString sourceText;
        QString errorMessage;
        if (!readUtf8File(path, &sourceText, &errorMessage)) {
            err << errorMessage << '\n';
            return 3;
        }
        const SimaiDocument doc = SimaiDocument::fromText(sourceText);
        int diffId = requestedDiff;
        if (diffId < 0) {
            for (int id : doc.difficultyIds()) {
                const SimaiDifficultyData* d = doc.difficulty(id);
                if (d != nullptr && !d->chart.trimmed().isEmpty() && id > diffId) {
                    diffId = id;
                }
            }
        }
        if (diffId < 0) {
            err << "no usable difficulty in " << QDir::toNativeSeparators(path) << '\n';
            return 4;
        }
        const SimaiDifficultyData* difficulty = doc.difficulty(diffId);
        if (difficulty == nullptr) {
            err << "no difficulty " << diffId << '\n';
            return 4;
        }

        const auto timingMd = miacode::simai::buildTimingMetadata(doc);
        const QString origChart = difficulty->chart;

        // Strip source positions (which shift after normalize) so the parse
        // comparison is purely semantic.
        std::function<QJsonValue(const QJsonValue&)> strip = [&](const QJsonValue& v) -> QJsonValue {
            if (v.isObject()) {
                QJsonObject obj = v.toObject();
                obj.remove(QStringLiteral("source_line"));
                obj.remove(QStringLiteral("source_col"));
                obj.remove(QStringLiteral("source_end_col"));
                obj.remove(QStringLiteral("parse_order"));
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    *it = strip(*it);
                }
                return obj;
            }
            if (v.isArray()) {
                QJsonArray arr = v.toArray();
                QJsonArray result;
                for (const auto& item : arr) {
                    result.append(strip(item));
                }
                return result;
            }
            return v;
        };

        out << "=== " << QDir::toNativeSeparators(path) << " diff=" << diffId
            << " reduce=" << (reduce ? "true" : "false") << " ===\n";

        // Build the semantic comparison view: drop beat_markers (re-emitted
        // subdivisions naturally shift) and warnings (which can change
        // count after normalize). Keep ok / errors / note_markers — those
        // are the user-visible chart semantics.
        const auto buildSemanticView = [&](const SimaiNativeParseResult& parse) {
            QJsonObject obj;
            obj.insert(QStringLiteral("ok"), parse.ok);
            obj.insert(QStringLiteral("errors"), strip(dumpMessages(parse.errors)));
            obj.insert(QStringLiteral("note_markers"), strip(dumpNoteMarkers(parse.noteMarkers)));
            return obj;
        };

        const auto origParse = SimaiNativeParser::parseForTimeline(origChart, timingMd);
        const QJsonObject origSemantic = buildSemanticView(origParse);

        miacode::chart_transform::ChartNormalizationOptions opts;
        opts.reduceTo384Grid = reduce;
        opts.startAtNewMeasure = true;

        // Test 1: whole-chart normalize.
        {
            const auto result = miacode::chart_transform::normalizeChartText(origChart, timingMd, opts);
            if (!result.ok) {
                out << "  WHOLE: normalize FAIL: " << result.errorMessage << '\n';
            } else {
                const auto newParse = SimaiNativeParser::parseForTimeline(result.text, timingMd);
                const QJsonObject newSemantic = buildSemanticView(newParse);
                QString diff;
                const bool same = compareJsonValues(origSemantic, newSemantic, QStringLiteral("$"), &diff);
                out << "  WHOLE: " << (same ? "PASS" : QString("DIFFER: ") + diff) << '\n';
            }
        }

        // Tests 2, 3: two pseudo-random line ranges (deterministic per chart length).
        const QStringList chartLines = origChart.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        const int totalLines = chartLines.size();
        if (totalLines >= 4) {
            // Range A: lines [N/4, N/2). Range B: lines [N/2, 3N/4).
            // Both are deterministic so the tests are repeatable.
            const QVector<QPair<int, int>> ranges = {
                {totalLines / 4, totalLines / 2},
                {totalLines / 2, qMin(totalLines, 3 * totalLines / 4)},
            };
            for (const auto& range : ranges) {
                int startOffset = 0;
                for (int i = 0; i < range.first; ++i) {
                    startOffset += chartLines.at(i).size() + 1;
                }
                int endOffset = startOffset;
                for (int i = range.first; i < range.second; ++i) {
                    endOffset += chartLines.at(i).size() + 1;
                }
                endOffset = qMin(endOffset, origChart.size());
                if (startOffset >= endOffset) continue;

                const auto result = miacode::chart_transform::normalizeChartSelectionText(
                    origChart, startOffset, endOffset, timingMd, opts);
                if (!result.ok) {
                    out << "  SEG[L" << (range.first + 1) << "-L" << range.second
                        << "]: normalize FAIL: " << result.errorMessage << '\n';
                    continue;
                }
                const QString spliced = origChart.left(startOffset) + result.text + origChart.mid(endOffset);
                const auto newParse = SimaiNativeParser::parseForTimeline(spliced, timingMd);
                const QJsonObject newSemantic = buildSemanticView(newParse);
                QString diff;
                const bool same = compareJsonValues(origSemantic, newSemantic, QStringLiteral("$"), &diff);
                out << "  SEG[L" << (range.first + 1) << "-L" << range.second
                    << "]: " << (same ? "PASS" : QString("DIFFER: ") + diff) << '\n';
                if (!same) {
                    out << "    durationSeconds: orig=" << origParse.durationSeconds
                        << " new=" << newParse.durationSeconds << '\n';
                    const QJsonArray origNotes = origSemantic.value(QStringLiteral("note_markers")).toArray();
                    const QJsonArray newNotes = newSemantic.value(QStringLiteral("note_markers")).toArray();
                    const int limit = qMin(origNotes.size(), newNotes.size());
                    for (int idx = 0; idx < limit; ++idx) {
                        QString sub;
                        if (!compareJsonValues(
                                origNotes.at(idx),
                                newNotes.at(idx),
                                QStringLiteral("$.note_markers[%1]").arg(idx),
                                &sub)) {
                            out << "    first diff at idx=" << idx << '\n';
                            break;
                        }
                    }
                }
            }
        }
        return 0;
    }
    if (args.size() >= 3 && args.at(1) == QLatin1String("--normalize-dump")) {
        const QString path = args.at(2);
        int difficultyId = 5;
        if (args.size() >= 4) {
            bool idOk = false;
            const int parsed = args.at(3).toInt(&idOk);
            if (idOk) difficultyId = parsed;
        }
        bool reduce = true;
        if (args.size() >= 5 && args.at(4).compare(QLatin1String("exact"), Qt::CaseInsensitive) == 0) {
            reduce = false;
        }
        QString sourceText;
        QString errorMessage;
        if (!readUtf8File(path, &sourceText, &errorMessage)) {
            err << errorMessage << '\n';
            return 3;
        }
        const SimaiDocument doc = SimaiDocument::fromText(sourceText);
        const SimaiDifficultyData* difficulty = doc.difficulty(difficultyId);
        if (difficulty == nullptr) {
            err << "no difficulty " << difficultyId << " in " << QDir::toNativeSeparators(path) << '\n';
            return 4;
        }
        miacode::chart_transform::ChartNormalizationOptions opts;
        opts.reduceTo384Grid = reduce;
        opts.startAtNewMeasure = true;
        const miacode::chart_transform::ChartNormalizationResult result =
            miacode::chart_transform::normalizeChartText(
                difficulty->chart,
                miacode::simai::buildTimingMetadata(doc),
                opts);
        if (!result.ok) {
            err << "normalize failed: " << result.errorMessage << '\n';
            return 5;
        }
        out << result.text;
        return 0;
    }
    if (args.size() >= 2 && args.at(1) == QLatin1String("--dump-snap-table")) {
        int maxY = miacode::chart_transform::kSnap384Modulus;
        if (args.size() >= 3) {
            bool maxOk = false;
            const int parsed = args.at(2).toInt(&maxOk);
            if (maxOk && parsed > 1) {
                maxY = parsed;
            }
        }
        out << "y\tq\tx\tp\tx/y\tp/q\terr\n";
        for (int y = 2; y <= maxY; ++y) {
            if ((miacode::chart_transform::kSnap384Modulus % y) == 0) continue;
            for (int x = 0; x < y; ++x) {
                const miacode::chart_transform::SnapResult snap =
                    miacode::chart_transform::snapXOverY(x, y);
                if (!snap.ok) continue;
                const double xy = static_cast<double>(x) / static_cast<double>(y);
                const double pq = static_cast<double>(snap.p) / static_cast<double>(snap.q);
                out << y << '\t' << snap.q << '\t' << x << '\t' << snap.p
                    << '\t' << QString::number(xy, 'f', 6)
                    << '\t' << QString::number(pq, 'f', 6)
                    << '\t' << QString::number(qAbs(xy - pq), 'g', 4) << '\n';
            }
        }
        return 0;
    }
    if (args.size() >= 4 && args.at(1) == QLatin1String("--dump")) {
        miacode::chart_transform::ChartTransformOp op = miacode::chart_transform::ChartTransformOp::MirrorLeftRight;
        if (!parseTransformOpToken(args.at(2), &op)) {
            err << "unknown transform op: " << args.at(2) << '\n';
            return 2;
        }

        QString sourceText;
        QString errorMessage;
        const QFileInfo inputInfo(args.at(3));
        const QString sourcePath = inputInfo.isDir()
            ? QDir(inputInfo.absoluteFilePath()).filePath(QStringLiteral("maidata.txt"))
            : inputInfo.absoluteFilePath();
        if (!readUtf8File(sourcePath, &sourceText, &errorMessage)) {
            err << errorMessage << '\n';
            return 3;
        }

        const SimaiDocument sourceDocument = SimaiDocument::fromText(sourceText);
        const SimaiDocument transformed = transformWholeDocument(sourceDocument, op, nullptr);
        const QVector<int> ids = transformed.difficultyIds();
        if (ids.size() == 1) {
            const SimaiDifficultyData* difficulty = transformed.difficulty(ids.constFirst());
            if (difficulty == nullptr) {
                err << "transformed document lost the only difficulty\n";
                return 4;
            }
            out << difficulty->chart;
            return 0;
        }

        out << transformed.toText();
        return 0;
    }

    runInlineSpecs(err, &failed);
    if (args.size() >= 2) {
        runFolderMatchSpec(args.at(1), out, err, &failed);
    }

    if (failed != 0) {
        err << "\nChart batch transform spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "Chart batch transform spec passed.\n";
    return 0;
}
