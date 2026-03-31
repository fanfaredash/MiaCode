#include "ChartBatchTransform.h"

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
    object.insert(QStringLiteral("track_break"), marker.trackBreak);
    object.insert(QStringLiteral("has_head_star"), marker.hasHeadStar);
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
        item.insert(QStringLiteral("chart_parse"), dumpParseResult(SimaiNativeParser::parseForTimeline(difficulty->chart)));
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
                    const QJsonObject actualParseDump = dumpParseResult(SimaiNativeParser::parseForTimeline(actualDifficulty->chart));
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
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    int failed = 0;
    const QStringList args = app.arguments();
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
