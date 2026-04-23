#include "tools/muri/MuriAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"

namespace {

constexpr double kPadTimeEpsilon = 1e-6;

struct PadWindowIndex {
    int index = -1;
    double activeSecond = -1.0;
};

struct MarkerSourceRef {
    int order = -1;
    double second = 0.0;
    int line = 1;
    int col = 1;
    QString type;
};

struct PadWindowInterval {
    QString pad;
    int startTick = 0;
    int endTick = -1;
    QString sourceMarkerKey;
    QString sourceType;
    int sourceOrder = -1;
    int line = 1;
    int col = 1;
};

struct RuntimePadEvent {
    QString pad;
    int tick = 0;
    double second = -1.0;
    QString sourceMarkerKey;
    QString sourceType;
    int sourceOrder = -1;
    int line = 1;
    int col = 1;
    bool extraPadDown = false;
};

struct RuntimeJudgeHit {
    bool judged = false;
    bool skipped = false;
    double second = -1.0;
    RuntimePadEvent cause;
};

struct RuntimeSlideJudgeResult {
    bool valid = false;
    bool isWifi = false;
    bool judgeBad = false;
    double judgeSecond = -1.0;
    double criticalSecond = -1.0;
    QVector<double> segmentCompletedSeconds;
    QVector<RuntimeJudgeHit> areaHits;
    QVector<QStringList> judgeSequence;
    QVector<int> segmentEndAreaIndices;
    QVector<QVector<RuntimeJudgeHit>> wifiLaneAreaHits;
    QVector<QVector<QStringList>> wifiLaneJudgeSequence;
    QVector<QVector<double>> wifiLaneProgressSeconds;
    double wifiPadCSecond = -1.0;
};

struct JudgeableSimpleNote {
    const TimelineNoteMarker* marker = nullptr;
    QString markerKey;
    QString type;
    QString pad;
    int order = -1;
    int parseOrder = -1;
    int eachGroupId = -1;
    int line = 1;
    int col = 1;
    double momentSecond = 0.0;
    double pressEndSecond = 0.0;
    double criticalSeconds = 0.0;
    double availableSeconds = 0.0;
    int expiryTick = 0;
    bool slideHead = false;
    bool onSlide = false;
    bool tailOnSlideHead = false;
    bool headStarTapLike = false;
    bool hasProtection = false;
    bool judged = false;
    bool judgeBad = false;
    bool lateBad = false;
    double judgeSecond = -1.0;
    RuntimePadEvent cause;
};

struct RuntimeTouchGroup {
    int eachGroupId = -1;
    int firstParseOrder = -1;
    QString sourceMarkerKey;
    int line = 1;
    int col = 1;
    double momentSecond = 0.0;
    bool allOnSlide = true;
    QVector<int> childNoteIndices;
    double threshold = 0.0;
};

struct RuntimeTopLevelNote {
    int sequenceOrder = -1;
    int simpleNoteIndex = -1;
    int touchGroupIndex = -1;
};

enum class RuntimeHandActionKind {
    Press,
    Slide,
};

struct RuntimeHandAction {
    RuntimeHandActionKind kind = RuntimeHandActionKind::Press;
    QString markerKey;
    QString sourceType;
    QString mergeKey;
    int order = -1;
    int line = 1;
    int col = 1;
    double startSecond = 0.0;
    double endSecond = 0.0;
    double motionDurationSecond = 0.0;
    double radius = 0.0;
    bool requireTwoHands = false;
    QVector<QPointF> points;
};

struct RuntimeTouchPoint {
    int actionIndex = -1;
    QPointF center;
    QPointF tangent;
    double radius = 0.0;
    bool hasTangent = false;
};

struct CoveringCircle {
    QPointF center;
    double radius = 0.0;
    bool valid = false;
};

struct DiagnosticAnchor {
    bool valid = false;
    double second = 0.0;
    int order = std::numeric_limits<int>::max();
    int line = 1;
    int col = 1;
};

QVector<MuriDiagnostic> dedupeDenseOverlapDiagnostics(const QVector<MuriDiagnostic>& diagnostics)
{
    QVector<MuriDiagnostic> deduped;
    deduped.reserve(diagnostics.size());

    QHash<qint64, int> keptOverlapCountBySecond;
    keptOverlapCountBySecond.reserve(diagnostics.size());

    for (const MuriDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind != MuriKind::Overlap) {
            deduped.append(diagnostic);
            continue;
        }

        const qint64 secondKey = static_cast<qint64>(std::llround(diagnostic.second * 1000000.0));
        const int keptCount = keptOverlapCountBySecond.value(secondKey, 0);
        if (keptCount >= 1) {
            continue;
        }
        keptOverlapCountBySecond.insert(secondKey, keptCount + 1);
        deduped.append(diagnostic);
    }

    return deduped;
}

const QJsonObject& slideRuntimeRoot()
{
    static const QJsonObject root = []() {
        QFile file(":/data/slide_data.json");
        if (!file.open(QIODevice::ReadOnly)) {
            return QJsonObject();
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            return QJsonObject();
        }
        return doc.object();
    }();
    return root;
}

QVector<QStringList> loadPadAreaSequence(const QJsonArray& areaArray)
{
    QVector<QStringList> sequence;
    sequence.reserve(areaArray.size());
    for (const QJsonValue& areaValue : areaArray) {
        QStringList pads;
        for (const QJsonValue& padValue : areaValue.toArray()) {
            const QString pad = padValue.toString();
            if (!pad.isEmpty()) {
                pads.append(pad);
            }
        }
        if (!pads.isEmpty()) {
            sequence.append(pads);
        }
    }
    return sequence;
}

QVector<QVector<QStringList>> loadTriPadAreaSequence(const QJsonArray& laneArray)
{
    QVector<QVector<QStringList>> sequence;
    sequence.reserve(laneArray.size());
    for (const QJsonValue& value : laneArray) {
        sequence.append(loadPadAreaSequence(value.toArray()));
    }
    return sequence;
}

QVector<QPointF> loadRuntimeSlideActionPath(const QString& key)
{
    QVector<QPointF> points;
    const QJsonObject entry = slideRuntimeRoot().value(QStringLiteral("slides")).toObject().value(key).toObject();
    if (entry.isEmpty()) {
        return points;
    }

    const QJsonArray sampleArray = entry.value(QStringLiteral("real_path_samples")).toArray();
    points.reserve(sampleArray.size());
    for (const QJsonValue& sampleValue : sampleArray) {
        const QJsonObject sampleObject = sampleValue.toObject();
        points.append(QPointF(
            sampleObject.value(QStringLiteral("x")).toDouble(),
            sampleObject.value(QStringLiteral("y")).toDouble()));
    }
    return points;
}

QVector<QVector<QPointF>> loadRuntimeWifiActionPaths(const QString& key)
{
    QVector<QVector<QPointF>> paths;
    const QJsonObject entry = slideRuntimeRoot().value(QStringLiteral("wifi")).toObject().value(key).toObject();
    if (entry.isEmpty()) {
        return paths;
    }

    const QJsonArray pathArray = entry.value(QStringLiteral("di_real_path_samples")).toArray();
    paths.reserve(pathArray.size());
    for (const QJsonValue& pathValue : pathArray) {
        QVector<QPointF> points;
        const QJsonArray sampleArray = pathValue.toArray();
        points.reserve(sampleArray.size());
        for (const QJsonValue& sampleValue : sampleArray) {
            const QJsonObject sampleObject = sampleValue.toObject();
            points.append(QPointF(
                sampleObject.value(QStringLiteral("x")).toDouble(),
                sampleObject.value(QStringLiteral("y")).toDouble()));
        }
        if (!points.isEmpty()) {
            paths.append(points);
        }
    }
    return paths;
}

bool isSlideLike(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
}

bool hasUsableSlideTraceTiming(const TimelineNoteMarker& marker)
{
    return isSlideLike(marker)
        && marker.slideTraceSecond + kPadTimeEpsilon >= marker.second
        && marker.endSecond + kPadTimeEpsilon >= marker.slideTraceSecond;
}

bool slideKeyUsesCcwJudgeSprite(const QString& key)
{
    if (key.isEmpty()) {
        return false;
    }
    int startLane = key.at(0).digitValue();
    if (startLane < 1 || startLane > 8) {
        startLane = 1;
    }
    const bool outerStart = startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8;
    if (key.contains(QLatin1Char('<'))) {
        return outerStart;
    }
    if (key.contains(QLatin1Char('>'))) {
        return !outerStart;
    }
    return false;
}

bool slideKeyUsesCwJudgeSprite(const QString& key)
{
    if (key.isEmpty()) {
        return false;
    }
    int startLane = key.at(0).digitValue();
    if (startLane < 1 || startLane > 8) {
        startLane = 1;
    }
    const bool outerStart = startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8;
    if (key.contains(QLatin1Char('>'))) {
        return outerStart;
    }
    if (key.contains(QLatin1Char('<'))) {
        return !outerStart;
    }
    return false;
}

QString slideHeadStarMarkerKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.eachGroupId);
}

int slideHeadStarSourceCol(const TimelineNoteMarker& marker)
{
    return qMax(1, marker.sourceCol + 1);
}

QString headStarJudgeEmitKey(const TimelineNoteMarker& marker)
{
    if (marker.sameHeadSlide) {
        return QStringLiteral("same_head_star_note|%1|%2|%3|%4|%5|%6")
            .arg(marker.second, 0, 'f', 6)
            .arg(marker.sourceLine)
            .arg(marker.lane)
            .arg(marker.headBreak ? 1 : 0)
            .arg(marker.headEx ? 1 : 0)
            .arg(marker.headEach ? 1 : 0);
    }

    return QStringLiteral("head_star_note|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.lane)
        .arg(marker.parseOrder);
}

bool shouldCreateHeadStarJudgeNote(const TimelineNoteMarker& marker)
{
    return isSlideLike(marker) && marker.hasHeadStar && marker.lane >= 1 && marker.lane <= 8;
}

QVector<QPointF> centeredPathPoints(const QVector<QPointF>& points)
{
    QVector<QPointF> result;
    result.reserve(points.size());
    for (const QPointF& point : points) {
        result.append(QPointF(
            miacode::muri::kLogicalCanvasCenter + point.x(),
            miacode::muri::kLogicalCanvasCenter + point.y()
        ));
    }
    return result;
}

double pointDistance(const QPointF& a, const QPointF& b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return std::sqrt(dx * dx + dy * dy);
}

QPointF normalizedDirection(const QPointF& vector)
{
    const double length = std::sqrt(vector.x() * vector.x() + vector.y() * vector.y());
    if (length <= kPadTimeEpsilon) {
        return QPointF();
    }
    return QPointF(vector.x() / length, vector.y() / length);
}

double tangentDelta(const QPointF& a, const QPointF& b)
{
    return pointDistance(a, b);
}

CoveringCircle coveringCircleFromTwoPoints(const QPointF& a, const QPointF& b)
{
    CoveringCircle circle;
    circle.center = QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
    circle.radius = pointDistance(a, b) * 0.5;
    circle.valid = true;
    return circle;
}

CoveringCircle coveringCircleFromThreePoints(const QPointF& a, const QPointF& b, const QPointF& c)
{
    const double ax = a.x();
    const double ay = a.y();
    const double bx = b.x();
    const double by = b.y();
    const double cx = c.x();
    const double cy = c.y();
    const double denominator =
        2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (qAbs(denominator) <= kPadTimeEpsilon) {
        return CoveringCircle{};
    }

    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;

    CoveringCircle circle;
    circle.center = QPointF(
        (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / denominator,
        (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / denominator);
    circle.radius = pointDistance(circle.center, a);
    circle.valid = true;
    return circle;
}

bool coveringCircleContainsAll(const CoveringCircle& circle, const QVector<QPointF>& points)
{
    if (!circle.valid) {
        return false;
    }
    for (const QPointF& point : points) {
        if (pointDistance(circle.center, point) > circle.radius + 1e-3) {
            return false;
        }
    }
    return true;
}

CoveringCircle smallestCoveringCircle(const QVector<QPointF>& points)
{
    if (points.isEmpty()) {
        return CoveringCircle{};
    }

    CoveringCircle best;
    best.center = points.constFirst();
    best.radius = 0.0;
    best.valid = true;
    if (coveringCircleContainsAll(best, points)) {
        return best;
    }

    best.radius = std::numeric_limits<double>::infinity();
    for (int i = 0; i < points.size(); ++i) {
        CoveringCircle circle;
        circle.center = points.at(i);
        circle.radius = 0.0;
        circle.valid = true;
        if (coveringCircleContainsAll(circle, points) && circle.radius < best.radius) {
            best = circle;
        }
    }

    for (int i = 0; i < points.size(); ++i) {
        for (int j = i + 1; j < points.size(); ++j) {
            const CoveringCircle circle = coveringCircleFromTwoPoints(points.at(i), points.at(j));
            if (coveringCircleContainsAll(circle, points) && circle.radius < best.radius) {
                best = circle;
            }
        }
    }

    for (int i = 0; i < points.size(); ++i) {
        for (int j = i + 1; j < points.size(); ++j) {
            for (int k = j + 1; k < points.size(); ++k) {
                const CoveringCircle circle =
                    coveringCircleFromThreePoints(points.at(i), points.at(j), points.at(k));
                if (coveringCircleContainsAll(circle, points) && circle.radius < best.radius) {
                    best = circle;
                }
            }
        }
    }

    return best.valid ? best : CoveringCircle{};
}

QString slideHeadPad(int lane)
{
    if (lane < 1 || lane > 8) {
        return QString();
    }
    return QStringLiteral("A%1").arg(lane);
}

QString normalizedPadToken(const QString& pad)
{
    return pad.trimmed().toUpper();
}

bool diagnosticAnchorComesBefore(const DiagnosticAnchor& left, const DiagnosticAnchor& right)
{
    if (!left.valid) {
        return false;
    }
    if (!right.valid) {
        return true;
    }
    if (left.second + kPadTimeEpsilon < right.second) {
        return true;
    }
    if (right.second + kPadTimeEpsilon < left.second) {
        return false;
    }
    if (left.order != right.order) {
        return left.order < right.order;
    }
    if (left.line != right.line) {
        return left.line < right.line;
    }
    return left.col < right.col;
}

DiagnosticAnchor earlierDiagnosticAnchor(const DiagnosticAnchor& first, const DiagnosticAnchor& second)
{
    return diagnosticAnchorComesBefore(second, first) ? second : first;
}

DiagnosticAnchor diagnosticAnchorFromMarkerSourceRef(const MarkerSourceRef& ref)
{
    DiagnosticAnchor anchor;
    anchor.valid = ref.order >= 0;
    anchor.second = ref.second;
    anchor.order = ref.order >= 0 ? ref.order : std::numeric_limits<int>::max();
    anchor.line = ref.line;
    anchor.col = ref.col;
    return anchor;
}

DiagnosticAnchor diagnosticAnchorFromMarker(const TimelineNoteMarker& marker)
{
    DiagnosticAnchor anchor;
    anchor.valid = true;
    anchor.second = marker.second;
    anchor.order = marker.parseOrder >= 0 ? marker.parseOrder : std::numeric_limits<int>::max();
    anchor.line = marker.sourceLine;
    anchor.col = marker.sourceCol;
    return anchor;
}

DiagnosticAnchor diagnosticAnchorFromNote(const JudgeableSimpleNote& note)
{
    DiagnosticAnchor anchor;
    anchor.valid = true;
    anchor.second = note.momentSecond;
    anchor.order = note.order >= 0 ? note.order : std::numeric_limits<int>::max();
    anchor.line = note.line;
    anchor.col = note.col;
    return anchor;
}

DiagnosticAnchor diagnosticAnchorFromAction(const RuntimeHandAction& action)
{
    DiagnosticAnchor anchor;
    anchor.valid = true;
    anchor.second = action.startSecond;
    anchor.order = action.order >= 0 ? action.order : std::numeric_limits<int>::max();
    anchor.line = action.line;
    anchor.col = action.col;
    return anchor;
}

QString slideLikeDisplayKey(const TimelineNoteMarker& marker)
{
    const QString displayKey = marker.slideDisplayKey.trimmed();
    if (!displayKey.isEmpty()) {
        return displayKey;
    }
    const QString trackKey = marker.slideTrackKey.trimmed();
    if (!trackKey.isEmpty()) {
        return trackKey;
    }
    return QStringLiteral("%1->%2").arg(marker.lane).arg(marker.endLane);
}

QString noteTypeDisplayLabel(const QString& type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QLatin1String("tap")) {
        return QStringLiteral("tap");
    }
    if (normalized == QLatin1String("hold")) {
        return QStringLiteral("hold");
    }
    if (normalized == QLatin1String("touch")) {
        return QStringLiteral("touch");
    }
    if (normalized == QLatin1String("touch_hold")) {
        return QStringLiteral("touch-hold");
    }
    if (normalized == QLatin1String("slide")) {
        return QStringLiteral("slide");
    }
    if (normalized == QLatin1String("wifi")) {
        return QStringLiteral("wifi");
    }
    if (normalized == QLatin1String("touch_group")) {
        return QStringLiteral("touch");
    }
    return normalized;
}

QString laneDisplayTokenFromPad(const QString& pad)
{
    const QString normalizedPad = normalizedPadToken(pad);
    if (normalizedPad.size() >= 2
        && normalizedPad.at(1) >= QLatin1Char('1')
        && normalizedPad.at(1) <= QLatin1Char('8')) {
        return normalizedPad.mid(1);
    }
    return QString();
}

QString simpleNoteLaneConfigToken(const QString& laneToken, bool hasProtection, bool isHold)
{
    if (laneToken.isEmpty()) {
        return QString();
    }
    QString token = laneToken;
    if (hasProtection) {
        token += QLatin1Char('x');
    }
    if (isHold) {
        token += QLatin1Char('h');
    }
    return token;
}

QString simpleNoteTargetLabel(const JudgeableSimpleNote& note)
{
    if (note.headStarTapLike) {
        const QString laneToken =
            (note.marker != nullptr && note.marker->lane >= 1 && note.marker->lane <= 8)
            ? QString::number(note.marker->lane)
            : laneDisplayTokenFromPad(note.pad);
        const QString token = simpleNoteLaneConfigToken(laneToken, note.hasProtection, false);
        const QString base = token.isEmpty()
            ? QStringLiteral("star")
            : QStringLiteral("star %1").arg(token);
        return note.hasProtection ? QStringLiteral("protected %1").arg(base) : base;
    }

    const QString normalizedType = note.type.trimmed().toLower();
    if (normalizedType == QLatin1String("tap") || normalizedType == QLatin1String("hold")) {
        const QString laneToken =
            (note.marker != nullptr && note.marker->lane >= 1 && note.marker->lane <= 8)
            ? QString::number(note.marker->lane)
            : laneDisplayTokenFromPad(note.pad);
        const bool isHold = normalizedType == QLatin1String("hold");
        const QString configToken = simpleNoteLaneConfigToken(laneToken, note.hasProtection, isHold);
        const QString typeText = noteTypeDisplayLabel(note.type);
        const QString base = configToken.isEmpty() ? typeText : QStringLiteral("%1 %2").arg(typeText, configToken);
        return note.hasProtection ? QStringLiteral("protected %1").arg(base) : base;
    }

    if (normalizedType == QLatin1String("touch") || normalizedType == QLatin1String("touch_hold")) {
        const QString pad = normalizedPadToken(note.pad);
        const QString typeText = noteTypeDisplayLabel(note.type);
        return pad.isEmpty() ? typeText : QStringLiteral("%1 %2").arg(typeText, pad);
    }

    return noteTypeDisplayLabel(note.type);
}

QString slideHeadConfigLabel(const TimelineNoteMarker& marker)
{
    const QString laneToken =
        (marker.lane >= 1 && marker.lane <= 8) ? QString::number(marker.lane) : QString();
    const QString token = simpleNoteLaneConfigToken(laneToken, marker.headEx, false);
    const QString base = token.isEmpty() ? QStringLiteral("star") : QStringLiteral("star %1").arg(token);
    return marker.headEx ? QStringLiteral("protected %1").arg(base) : base;
}

QString formatMarkerConfigLabel(const TimelineNoteMarker& marker, bool slideHead = false)
{
    if (slideHead && isSlideLike(marker)) {
        return slideHeadConfigLabel(marker);
    }
    if (marker.type == QLatin1String("tap")) {
        return QStringLiteral("tap %1").arg(marker.lane);
    }
    if (marker.type == QLatin1String("hold")) {
        return QStringLiteral("hold %1").arg(marker.lane);
    }
    if (marker.type == QLatin1String("touch")) {
        const QString pad = normalizedPadToken(marker.touchPad);
        return pad.isEmpty() ? QStringLiteral("touch") : QStringLiteral("touch %1").arg(pad);
    }
    if (marker.type == QLatin1String("touch_hold")) {
        const QString pad = normalizedPadToken(marker.touchPad);
        return pad.isEmpty() ? QStringLiteral("touch-hold") : QStringLiteral("touch-hold %1").arg(pad);
    }
    if (marker.type == QLatin1String("slide")) {
        return QStringLiteral("slide %1").arg(slideLikeDisplayKey(marker));
    }
    if (marker.type == QLatin1String("wifi")) {
        return QStringLiteral("wifi %1").arg(slideLikeDisplayKey(marker));
    }
    return marker.type;
}

QHash<QString, const TimelineNoteMarker*> buildMarkerLookup(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, const TimelineNoteMarker*> lookup;
    lookup.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        lookup.insert(makeMarkerAnalysisKey(marker), &marker);
    }
    return lookup;
}

QHash<QString, QString> buildMarkerConfigLabels(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, QString> labels;
    labels.reserve(noteMarkers.size() * 2);

    for (const TimelineNoteMarker& marker : noteMarkers) {
        labels.insert(makeMarkerAnalysisKey(marker), formatMarkerConfigLabel(marker));
        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }
        labels.insert(slideHeadStarMarkerKey(marker), formatMarkerConfigLabel(marker, true));
    }

    return labels;
}

QString markerConfigLabelForKey(
    const QHash<QString, QString>& markerConfigLabels,
    const QString& markerKey,
    const QString& fallbackType)
{
    const QString label = markerConfigLabels.value(markerKey).trimmed();
    if (!label.isEmpty()) {
        return label;
    }
    return fallbackType.trimmed().isEmpty() ? QStringLiteral("source note") : fallbackType.trimmed();
}

int judgeTickForPadActiveStart(double second);

QString slideStartLookupKey(int lane, double second)
{
    return QStringLiteral("%1|%2").arg(lane).arg(judgeTickForPadActiveStart(second));
}

QHash<QString, QString> buildSyntheticSlideHeadOwnerKeys(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, QString> ownerKeys;
    ownerKeys.reserve(noteMarkers.size() * 2);
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }
        ownerKeys.insert(slideHeadStarMarkerKey(marker), makeMarkerAnalysisKey(marker));
    }
    return ownerKeys;
}

QSet<QString> buildSlideKeysWithTapOnSlideHead(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, QVector<QString>> slideKeysByStart;
    slideKeysByStart.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!hasUsableSlideTraceTiming(marker)) {
            continue;
        }
        slideKeysByStart[slideStartLookupKey(marker.lane, marker.slideTraceSecond)].append(makeMarkerAnalysisKey(marker));
    }

    QSet<QString> slideKeys;
    slideKeys.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (marker.type != QLatin1String("tap") || !marker.slideHead) {
            continue;
        }
        const QVector<QString> matchingSlideKeys = slideKeysByStart.value(slideStartLookupKey(marker.lane, marker.second));
        for (const QString& slideKey : matchingSlideKeys) {
            slideKeys.insert(slideKey);
        }
    }
    return slideKeys;
}

QString slideStartOwnerKeyForSource(
    const QString& sourceMarkerKey,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    return syntheticSlideHeadOwnerKeys.value(sourceMarkerKey, sourceMarkerKey);
}

DiagnosticAnchor diagnosticAnchorForMarkerKey(
    const QString& sourceMarkerKey,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    if (sourceMarkerKey.isEmpty()) {
        return DiagnosticAnchor();
    }

    const QString ownerKey = slideStartOwnerKeyForSource(sourceMarkerKey, syntheticSlideHeadOwnerKeys);
    const auto it = markerRefs.constFind(ownerKey);
    if (it == markerRefs.constEnd()) {
        return DiagnosticAnchor();
    }
    return diagnosticAnchorFromMarkerSourceRef(it.value());
}

DiagnosticAnchor diagnosticAnchorForCause(
    const RuntimePadEvent& cause,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    if (cause.sourceMarkerKey.isEmpty()) {
        return DiagnosticAnchor();
    }

    DiagnosticAnchor anchor =
        diagnosticAnchorForMarkerKey(cause.sourceMarkerKey, markerRefs, syntheticSlideHeadOwnerKeys);
    if (anchor.valid) {
        return anchor;
    }
    anchor.valid = true;
    anchor.second = cause.second;
    anchor.order = cause.sourceOrder >= 0 ? cause.sourceOrder : std::numeric_limits<int>::max();
    anchor.line = cause.line;
    anchor.col = cause.col;
    return anchor;
}

QString markerConfigLabelForSource(
    const QHash<QString, QString>& markerConfigLabels,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys,
    const QString& markerKey,
    const QString& fallbackType)
{
    return markerConfigLabelForKey(
        markerConfigLabels,
        slideStartOwnerKeyForSource(markerKey, syntheticSlideHeadOwnerKeys),
        fallbackType);
}

QString formatMultiTouchActionLabel(
    const RuntimeHandAction& action,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    const QString ownerKey = syntheticSlideHeadOwnerKeys.value(action.markerKey);
    const QString effectiveKey = ownerKey.isEmpty() ? action.markerKey : ownerKey;
    const auto it = markerLookup.constFind(effectiveKey);
    if (it != markerLookup.constEnd() && it.value() != nullptr) {
        const TimelineNoteMarker& marker = *it.value();
        if (!ownerKey.isEmpty()) {
            return formatMarkerConfigLabel(marker, true);
        }
        if (marker.type == QLatin1String("tap")) {
            return QStringLiteral("tap %1").arg(marker.lane);
        }
        return formatMarkerConfigLabel(marker);
    }

    const QString fallbackType = noteTypeDisplayLabel(action.sourceType);
    return fallbackType.isEmpty() ? QStringLiteral("note") : fallbackType;
}

bool sourceTypeIsTouchLike(const QString& sourceType)
{
    const QString normalized = sourceType.trimmed().toLower();
    return normalized == QLatin1String("touch")
        || normalized == QLatin1String("touch_hold")
        || normalized == QLatin1String("touch_group");
}

double slideStartSecondForSource(
    const RuntimePadEvent& cause,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    const QString ownerKey = slideStartOwnerKeyForSource(cause.sourceMarkerKey, syntheticSlideHeadOwnerKeys);
    const auto it = markerLookup.constFind(ownerKey);
    if (it != markerLookup.constEnd()
        && it.value() != nullptr
        && hasUsableSlideTraceTiming(*it.value())) {
        return it.value()->slideTraceSecond;
    }
    return cause.second;
}

MuriAlertLevel slideHeadTapAlertLevel(bool hasTapOnSlideHead, double gapSecond)
{
    if (!hasTapOnSlideHead
        && gapSecond > kPadTimeEpsilon
        && gapSecond <= miacode::muri::kSlideHeadTapNoTapWarningCutoffSeconds + kPadTimeEpsilon) {
        return MuriAlertLevel::Warning;
    }
    if (gapSecond + kPadTimeEpsilon >= miacode::muri::kSlideHeadTapLateWarningCutoffSeconds) {
        return MuriAlertLevel::Warning;
    }
    return MuriAlertLevel::Muri;
}

MuriAlertLevel downgradeProtectedSimpleNoteAlertLevel(MuriAlertLevel alertLevel, bool hasProtection)
{
    if (hasProtection) {
        return MuriAlertLevel::Warning;
    }
    return alertLevel;
}

MuriAlertLevel tapOnSlideAlertLevel(double gapSecond, double thresholdSecond)
{
    if (gapSecond > miacode::muri::kTapOnSlideWarningCutoffSeconds + kPadTimeEpsilon
        && gapSecond <= thresholdSecond + kPadTimeEpsilon) {
        return MuriAlertLevel::Warning;
    }
    return MuriAlertLevel::Muri;
}

QString slideHeadTapDetailText(
    bool hasTapOnSlideHead,
    MuriAlertLevel alertLevel,
    const QString& causeConfig,
    const QString& affectedTarget,
    double gapMs)
{
    const QString gapText = QString::number(gapMs, 'f', 1);
    if (hasTapOnSlideHead) {
        return alertLevel == MuriAlertLevel::Warning
            ? QStringLiteral("%1 start may early-judge %2, gap %3 ms.")
                  .arg(causeConfig, affectedTarget, gapText)
            : QStringLiteral("%1 start will early-judge %2, gap %3 ms.")
                  .arg(causeConfig, affectedTarget, gapText);
    }
    return alertLevel == MuriAlertLevel::Warning
        ? QStringLiteral("%1 jump-start may early-judge %2, gap %3 ms.")
              .arg(causeConfig, affectedTarget, gapText)
        : QStringLiteral("%1 jump-start will early-judge %2, gap %3 ms.")
              .arg(causeConfig, affectedTarget, gapText);
}

QString tapOnSlideDetailText(
    MuriAlertLevel alertLevel,
    const QString& causeConfig,
    const QString& affectedTarget,
    double gapMs)
{
    return alertLevel == MuriAlertLevel::Warning
        ? QStringLiteral("%1 trajectory may collide with %2, gap %3 ms.")
              .arg(causeConfig, affectedTarget, QString::number(gapMs, 'f', 1))
        : QStringLiteral("%1 trajectory will collide with %2, gap %3 ms.")
              .arg(causeConfig, affectedTarget, QString::number(gapMs, 'f', 1));
}

QString notePad(const TimelineNoteMarker& marker)
{
    if (marker.type == QLatin1String("tap") || marker.type == QLatin1String("hold")) {
        return slideHeadPad(marker.lane);
    }
    return marker.touchPad;
}

int padRingGroupIndex(const QString& pad)
{
    if (pad == QLatin1String("C")) {
        return 4;
    }
    if (!miacode::muri::padTokenIsValid(pad)) {
        return -1;
    }
    switch (pad.at(0).toUpper().toLatin1()) {
    case 'A':
        return 0;
    case 'B':
        return 1;
    case 'D':
        return 2;
    case 'E':
        return 3;
    default:
        return -1;
    }
}

int padLaneZeroBased(const QString& pad)
{
    if (!miacode::muri::padTokenIsValid(pad) || pad == QLatin1String("C")) {
        return -1;
    }
    const int lane = pad.mid(1).toInt();
    return lane <= 0 ? -1 : ((lane - 1) & 0x7);
}

bool touchPadsAreAdjacent(const QString& a, const QString& b)
{
    if (a == b) {
        return false;
    }

    int g1 = padRingGroupIndex(a);
    int g2 = padRingGroupIndex(b);
    int i1 = padLaneZeroBased(a);
    int i2 = padLaneZeroBased(b);
    if (g1 < 0 || g2 < 0) {
        return false;
    }
    if (g1 > g2) {
        qSwap(g1, g2);
        qSwap(i1, i2);
    }

    if (g2 == 4) {
        return g1 == 1;
    }

    if ((g1 == 0 && g2 == 0)
        || (g1 == 2 && g2 == 2)
        || (g1 == 3 && g2 == 3)
        || (g1 == 1 && g2 == 2)) {
        return false;
    }

    if (g1 == 1 && g2 == 1) {
        return i2 == ((i1 + 1) & 0x7) || i2 == ((i1 + 7) & 0x7);
    }

    if ((g1 == 0 && g2 == 1) || (g1 == 2 && g2 == 3)) {
        return i1 == i2;
    }

    return i2 == i1 || i2 == ((i1 + 1) & 0x7);
}

double notePressEndSecond(const TimelineNoteMarker& marker)
{
    using namespace miacode::muri;
    if (marker.type == QLatin1String("hold") || marker.type == QLatin1String("touch_hold")) {
        return qMax(marker.second, marker.endSecond)
            + (marker.tailOnSlideHead ? 0.0 : kReleaseDelaySeconds);
    }
    return marker.second + kReleaseDelaySeconds;
}

bool buildSimpleNoteJudgeWindow(
    const TimelineNoteMarker& marker,
    QString* pad,
    double* criticalSeconds,
    double* availableSeconds)
{
    using namespace miacode::muri;

    if (pad == nullptr || criticalSeconds == nullptr || availableSeconds == nullptr) {
        return false;
    }

    if (marker.type == QLatin1String("tap") || marker.type == QLatin1String("hold")) {
        *pad = slideHeadPad(marker.lane);
        *criticalSeconds = kTapCriticalSeconds;
        *availableSeconds = kTapAvailableSeconds;
        return !pad->isEmpty();
    }

    if (marker.type == QLatin1String("touch") || marker.type == QLatin1String("touch_hold")) {
        *pad = marker.touchPad;
        *criticalSeconds = kTouchCriticalSeconds;
        *availableSeconds = kTouchAvailableSeconds;
        return !pad->isEmpty();
    }

    return false;
}

double firstAreaDurationSecond(const TimelineNoteMarker& marker)
{
    if (marker.type == QLatin1String("slide")) {
        if (marker.slideSegmentPadEnterTimes.isEmpty() || marker.slideSegmentDurations.isEmpty()) {
            return miacode::muri::kExtraPadDownDelaySeconds;
        }
        const QVector<MuriPadTimeEntry>& firstPadTimes = marker.slideSegmentPadEnterTimes.constFirst();
        if (firstPadTimes.isEmpty()) {
            return miacode::muri::kExtraPadDownDelaySeconds;
        }
        return qMax(0.0, firstPadTimes.constFirst().proportion * marker.slideSegmentDurations.constFirst());
    }

    if (marker.type == QLatin1String("wifi")) {
        if (marker.wifiPadEnterTimes.isEmpty()) {
            return miacode::muri::kExtraPadDownDelaySeconds;
        }
        const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
        return qMax(0.0, marker.wifiPadEnterTimes.constFirst().proportion * durationSecond);
    }

    return miacode::muri::kExtraPadDownDelaySeconds;
}

double extraPadDownSecond(const TimelineNoteMarker& marker)
{
    if (!hasUsableSlideTraceTiming(marker) || marker.afterSlide) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double firstAreaSecond = firstAreaDurationSecond(marker);
    return marker.slideTraceSecond + qMin(firstAreaSecond, miacode::muri::kExtraPadDownDelaySeconds);
}

int judgeTickForPadActiveStart(double second)
{
    return qCeil(second * miacode::muri::kJudgeTps - kPadTimeEpsilon);
}

int judgeTickForPadActiveEnd(double second)
{
    return qCeil(second * miacode::muri::kJudgeTps - kPadTimeEpsilon) - 1;
}

int judgeTickForExtraPadDown(double second)
{
    return qFloor(second * miacode::muri::kJudgeTps + kPadTimeEpsilon) + 1;
}

int judgeTickForNoteExpiry(double momentSecond, double availableSeconds)
{
    return qFloor((momentSecond + availableSeconds) * miacode::muri::kJudgeTps + kPadTimeEpsilon) + 1;
}

double tickToSecond(int tick)
{
    return tick / miacode::muri::kJudgeTps;
}

void addPadWindow(
    QVector<MuriPadWindow>* windows,
    const QString& pad,
    double startSecond,
    double endSecond,
    const QString& markerKey,
    const QString& type)
{
    if (windows == nullptr || pad.isEmpty() || endSecond + kPadTimeEpsilon < startSecond) {
        return;
    }
    MuriPadWindow window;
    window.pad = pad;
    window.startSecond = startSecond;
    window.endSecond = qMax(startSecond, endSecond);
    window.sourceMarkerKey = markerKey;
    window.sourceType = type;
    windows->append(window);
}

void addActionTrail(
    QVector<MuriActionTrail>* trails,
    const QString& markerKey,
    const QString& type,
    double startSecond,
    double endSecond,
    double radius,
    const QVector<QPointF>& points)
{
    if (trails == nullptr || points.isEmpty() || endSecond + kPadTimeEpsilon < startSecond) {
        return;
    }
    MuriActionTrail trail;
    trail.sourceMarkerKey = markerKey;
    trail.sourceType = type;
    trail.startSecond = startSecond;
    trail.endSecond = qMax(startSecond, endSecond);
    trail.radius = radius;
    trail.points = points;
    trails->append(trail);
}

QVector<QStringList> checkpointPadsForArea(
    const QVector<MuriPadTimeEntry>& padTimes,
    const QVector<double>& checkpointTimes)
{
    QVector<QStringList> result;
    result.reserve(checkpointTimes.size());
    for (double checkpoint : checkpointTimes) {
        QStringList pads;
        for (const MuriPadTimeEntry& entry : padTimes) {
            if (qAbs(entry.proportion - checkpoint) <= kPadTimeEpsilon && !entry.pad.isEmpty()) {
                pads.append(entry.pad);
            }
        }
        pads.removeDuplicates();
        result.append(pads);
    }
    return result;
}

PadWindowIndex earliestPadWindow(
    const QHash<QString, QVector<int>>& windowsByPad,
    const QVector<MuriPadWindow>& windows,
    const QStringList& pads,
    double earliestSecond)
{
    PadWindowIndex best;
    for (const QString& pad : pads) {
        const auto it = windowsByPad.constFind(pad);
        if (it == windowsByPad.constEnd()) {
            continue;
        }
        for (int windowIndex : it.value()) {
            if (windowIndex < 0 || windowIndex >= windows.size()) {
                continue;
            }
            const MuriPadWindow& window = windows.at(windowIndex);
            if (window.endSecond + kPadTimeEpsilon < earliestSecond) {
                continue;
            }
            const double activeSecond = qMax(earliestSecond, window.startSecond);
            if (best.index < 0 || activeSecond + kPadTimeEpsilon < best.activeSecond) {
                best.index = windowIndex;
                best.activeSecond = activeSecond;
            }
        }
    }
    return best;
}

double completionSecondForArea(const QVector<MuriCheckpointState>& checkpoints)
{
    double second = -1.0;
    for (const MuriCheckpointState& checkpoint : checkpoints) {
        second = qMax(second, checkpoint.second);
    }
    return second;
}

QVector<MuriCheckpointState> buildCheckpointStates(
    const QVector<double>& checkpointTimes,
    const QVector<QStringList>& checkpointPads,
    double segmentShootSecond,
    double durationSecond,
    double earliestAllowedSecond,
    const QHash<QString, QVector<int>>& windowsByPad,
    const QVector<MuriPadWindow>& allWindows,
    const QString& selfMarkerKey)
{
    QVector<MuriCheckpointState> states;
    states.reserve(checkpointTimes.size());

    double cursorSecond = earliestAllowedSecond;
    for (int index = 0; index < checkpointTimes.size(); ++index) {
        MuriCheckpointState state;
        state.proportion = checkpointTimes.at(index);
        state.pads = checkpointPads.value(index);
        const PadWindowIndex best = earliestPadWindow(windowsByPad, allWindows, state.pads, earliestAllowedSecond);
        double resolvedSecond = segmentShootSecond + state.proportion * durationSecond;
        if (best.index >= 0) {
            const MuriPadWindow& window = allWindows.at(best.index);
            resolvedSecond = qMin(resolvedSecond, best.activeSecond);
            state.causeMarkerKey = window.sourceMarkerKey;
            state.causeType = window.sourceType;
            state.cause = window.sourceMarkerKey == selfMarkerKey ? AreaJudgeCause::Self : AreaJudgeCause::Other;
        } else {
            state.cause = AreaJudgeCause::Self;
            state.causeMarkerKey = selfMarkerKey;
            state.causeType = QStringLiteral("slide");
        }
        if (resolvedSecond + kPadTimeEpsilon < cursorSecond) {
            resolvedSecond = cursorSecond;
        }
        state.second = resolvedSecond;
        cursorSecond = qMax(cursorSecond, resolvedSecond);
        states.append(state);
    }

    return states;
}

double slideCriticalDeltaSecond(double lastAreaDurationSecond)
{
    using namespace miacode::muri;
    return qMin(kSlideAvailableSeconds, kSlideCriticalSeconds + qMax(0.0, lastAreaDurationSecond) / 4.0);
}

bool slideJudgeIsBad(double judgeSecond, double criticalSecond, double criticalDeltaSecond)
{
    using namespace miacode::muri;
    const double deltaSecond = judgeSecond - criticalSecond;
    if (qAbs(deltaSecond) <= criticalDeltaSecond + kPadTimeEpsilon) {
        return false;
    }
    if (qAbs(deltaSecond + kSlideDeltaShiftSeconds) <= kSlideCriticalSeconds + kPadTimeEpsilon) {
        return false;
    }
    return true;
}

bool wifiJudgeIsBad(double judgeSecond, double criticalSecond, double criticalDeltaSecond)
{
    return qAbs(judgeSecond - criticalSecond) > criticalDeltaSecond + kPadTimeEpsilon;
}

MarkerMuriState buildSlideState(
    const TimelineNoteMarker& marker,
    const QHash<QString, QVector<int>>& windowsByPad,
    const QVector<MuriPadWindow>& allWindows)
{
    MarkerMuriState state;
    state.markerKey = makeMarkerAnalysisKey(marker);
    state.markerType = marker.type;
    state.line = marker.sourceLine;
    state.col = marker.sourceCol;
    state.second = marker.second;
    state.endSecond = marker.endSecond;

    const int segmentCount = qMin(
        qMin(marker.slideTrackAreaCheckpoints.size(), marker.slideSegmentShootSeconds.size()),
        qMin(marker.slideSegmentDurations.size(), marker.slideSegmentPadEnterTimes.size())
    );
    state.slideSegments.reserve(segmentCount);
    double chainCursorSecond = marker.second;
    for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        MuriSegmentState segmentState;
        const double shootSecond = marker.slideSegmentShootSeconds.at(segmentIndex);
        const double durationSecond = qMax(0.0, marker.slideSegmentDurations.at(segmentIndex));
        const QVector<MuriPadTimeEntry>& padTimes = marker.slideSegmentPadEnterTimes.at(segmentIndex);
        const QVector<QVector<double>>& areas = marker.slideTrackAreaCheckpoints.at(segmentIndex);
        segmentState.areaCheckpoints.reserve(areas.size());

        double cursorSecond = chainCursorSecond;
        bool sawCheckpoint = false;
        for (const QVector<double>& area : areas) {
            const QVector<QStringList> pads = checkpointPadsForArea(padTimes, area);
            QVector<MuriCheckpointState> checkpointStates = buildCheckpointStates(
                area,
                pads,
                shootSecond,
                durationSecond,
                cursorSecond,
                windowsByPad,
                allWindows,
                state.markerKey
            );
            if (!checkpointStates.isEmpty()) {
                sawCheckpoint = true;
                const double areaSecond = completionSecondForArea(checkpointStates);
                if (areaSecond + kPadTimeEpsilon < cursorSecond) {
                    for (MuriCheckpointState& checkpointState : checkpointStates) {
                        checkpointState.second = cursorSecond;
                    }
                }
                cursorSecond = qMax(cursorSecond, completionSecondForArea(checkpointStates));
            }
            segmentState.areaCheckpoints.append(checkpointStates);
        }

        double expectedCompleted = shootSecond;
        for (const MuriPadTimeEntry& entry : padTimes) {
            expectedCompleted = qMax(expectedCompleted, shootSecond + entry.proportion * durationSecond);
        }
        segmentState.expectedCompletedSecond = expectedCompleted;
        segmentState.completedSecond = sawCheckpoint ? cursorSecond : expectedCompleted;
        if (segmentIndex < marker.slideSegmentCriticalProportions.size()) {
            segmentState.criticalSecond =
                shootSecond + marker.slideSegmentCriticalProportions.at(segmentIndex) * durationSecond;
        } else {
            segmentState.criticalSecond = expectedCompleted;
        }
        chainCursorSecond = qMax(chainCursorSecond, segmentState.completedSecond);
        state.slideSegments.append(segmentState);
    }

    if (!state.slideSegments.isEmpty()) {
        const MuriSegmentState& lastSegment = state.slideSegments.constLast();
        const int lastSegmentIndex = state.slideSegments.size() - 1;
        const double lastDurationSecond = lastSegmentIndex < marker.slideSegmentDurations.size()
            ? qMax(0.0, marker.slideSegmentDurations.at(lastSegmentIndex))
            : qMax(0.0, marker.endSecond - marker.slideTraceSecond);
        const double criticalProportion = lastSegmentIndex < marker.slideSegmentCriticalProportions.size()
            ? marker.slideSegmentCriticalProportions.at(lastSegmentIndex)
            : 1.0;
        const double criticalDeltaSecond = slideCriticalDeltaSecond((1.0 - criticalProportion) * lastDurationSecond);
        if (slideJudgeIsBad(lastSegment.completedSecond, lastSegment.criticalSecond, criticalDeltaSecond)) {
            state.earlyCleared = true;
            state.flashSecond = lastSegment.completedSecond;
        }
    }

    return state;
}

MarkerMuriState buildWifiState(const TimelineNoteMarker& marker)
{
    MarkerMuriState state;
    state.markerKey = makeMarkerAnalysisKey(marker);
    state.markerType = marker.type;
    state.line = marker.sourceLine;
    state.col = marker.sourceCol;
    state.second = marker.second;
    state.endSecond = marker.endSecond;

    const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
    double expectedCompleted = marker.slideTraceSecond;
    for (const MuriPadTimeEntry& entry : marker.wifiPadEnterTimes) {
        expectedCompleted = qMax(expectedCompleted, marker.slideTraceSecond + entry.proportion * durationSecond);
    }
    state.wifiExpectedCompletedSecond = expectedCompleted;
    state.wifiCompletedSecond = expectedCompleted;
    state.wifiCriticalSecond = marker.slideTraceSecond + marker.wifiCriticalProportion * durationSecond;
    const double criticalDeltaSecond = slideCriticalDeltaSecond(
        (1.0 - marker.wifiCriticalProportion) * durationSecond
    );
    if (wifiJudgeIsBad(state.wifiCompletedSecond, state.wifiCriticalSecond, criticalDeltaSecond)) {
        state.earlyCleared = true;
        state.flashSecond = state.wifiCompletedSecond;
    }
    return state;
}

void addSlidePadWindowsAndTrails(
    const TimelineNoteMarker& marker,
    const QString& markerKey,
    QVector<MuriPadWindow>* padWindows,
    QVector<MuriActionTrail>* actionTrails)
{
    using namespace miacode::muri;
    const int segmentCount = qMin(
        qMin(marker.slideSegmentPadEnterTimes.size(), marker.slideSegmentShootSeconds.size()),
        qMin(marker.slideSegmentDurations.size(), marker.slideSegmentPoints.size())
    );
    for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        const QVector<MuriPadTimeEntry>& padTimes = marker.slideSegmentPadEnterTimes.at(segmentIndex);
        const double shootSecond = marker.slideSegmentShootSeconds.at(segmentIndex);
        const double durationSecond = qMax(0.0, marker.slideSegmentDurations.at(segmentIndex));
        for (int padIndex = 0; padIndex < padTimes.size(); ++padIndex) {
            const double startSecond = shootSecond + padTimes.at(padIndex).proportion * durationSecond;
            const double nextSecond = (padIndex + 1 < padTimes.size())
                ? (shootSecond + padTimes.at(padIndex + 1).proportion * durationSecond)
                : (shootSecond + durationSecond + kReleaseDelaySeconds);
            addPadWindow(
                padWindows,
                padTimes.at(padIndex).pad,
                startSecond,
                nextSecond,
                markerKey,
                marker.type
            );
        }

        const bool isLastSegment = segmentIndex + 1 >= segmentCount;
        const double trailEnd = shootSecond
            + durationSecond
            + ((isLastSegment && marker.beforeSlide) ? 0.0 : kReleaseDelaySeconds);
        addActionTrail(
            actionTrails,
            markerKey,
            marker.type,
            shootSecond,
            trailEnd,
            kHandRadiusNormal,
            centeredPathPoints(marker.slideSegmentPoints.at(segmentIndex))
        );
    }
}

void addWifiPadWindowsAndTrails(
    const TimelineNoteMarker& marker,
    const QString& markerKey,
    QVector<MuriPadWindow>* padWindows,
    QVector<MuriActionTrail>* actionTrails)
{
    using namespace miacode::muri;
    const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
    for (int padIndex = 0; padIndex < marker.wifiPadEnterTimes.size(); ++padIndex) {
        const double startSecond =
            marker.slideTraceSecond + marker.wifiPadEnterTimes.at(padIndex).proportion * durationSecond;
        const double nextSecond = (padIndex + 1 < marker.wifiPadEnterTimes.size())
            ? (marker.slideTraceSecond + marker.wifiPadEnterTimes.at(padIndex + 1).proportion * durationSecond)
            : (marker.slideTraceSecond + durationSecond + kReleaseDelaySeconds);
        addPadWindow(
            padWindows,
            marker.wifiPadEnterTimes.at(padIndex).pad,
            startSecond,
            nextSecond,
            markerKey,
            marker.type
        );
    }

    const QVector<QVector<QPointF>> runtimePaths = loadRuntimeWifiActionPaths(marker.slideTrackKey);
    const QVector<QVector<QPointF>>& actionPaths = runtimePaths.isEmpty() ? marker.wifiLanePoints : runtimePaths;
    if (!actionPaths.isEmpty()) {
        addActionTrail(
            actionTrails,
            markerKey,
            marker.type,
            marker.slideTraceSecond,
            marker.endSecond + kReleaseDelaySeconds,
            kHandRadiusWifi,
            centeredPathPoints(actionPaths.constFirst())
        );
        if (actionPaths.size() > 1) {
            addActionTrail(
                actionTrails,
                markerKey,
                marker.type,
                marker.slideTraceSecond,
                marker.endSecond + kReleaseDelaySeconds,
                kHandRadiusWifi,
                centeredPathPoints(actionPaths.constLast())
            );
        }
    }
}

void buildOverlayActions(
    const QVector<TimelineNoteMarker>& noteMarkers,
    QVector<MuriPadWindow>* padWindows,
    QVector<MuriActionTrail>* actionTrails)
{
    using namespace miacode::muri;
    if (padWindows == nullptr || actionTrails == nullptr) {
        return;
    }

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString markerKey = makeMarkerAnalysisKey(marker);
        const QString pad = notePad(marker);
        if (marker.type == QLatin1String("tap")) {
            if (!marker.slideHead) {
                addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
                addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {miacode::muri::padCenter(pad)});
            }
            continue;
        }
        if (marker.type == QLatin1String("hold")) {
            addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {miacode::muri::padCenter(pad)});
            continue;
        }
        if (marker.type == QLatin1String("touch")) {
            if (!marker.onSlide) {
                addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
                addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {marker.touchPoint});
            }
            continue;
        }
        if (marker.type == QLatin1String("touch_hold")) {
            addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {marker.touchPoint});
            continue;
        }
        if (marker.type == QLatin1String("slide")) {
            addSlidePadWindowsAndTrails(marker, markerKey, padWindows, actionTrails);
            continue;
        }
        if (marker.type == QLatin1String("wifi")) {
            addWifiPadWindowsAndTrails(marker, markerKey, padWindows, actionTrails);
            continue;
        }
    }

}

void addDiagnostic(
    QVector<MuriDiagnostic>* diagnostics,
    MuriKind kind,
    MuriAlertLevel alertLevel,
    double second,
    const TimelineNoteMarker& marker,
    const QString& markerKey,
    const QString& detail,
    const DiagnosticAnchor& anchor = DiagnosticAnchor())
{
    if (diagnostics == nullptr) {
        return;
    }
    MuriDiagnostic diagnostic;
    diagnostic.kind = kind;
    diagnostic.alertLevel = alertLevel;
    diagnostic.second = second;
    diagnostic.anchorSecond = anchor.valid ? anchor.second : diagnostic.second;
    diagnostic.line = anchor.valid ? anchor.line : marker.sourceLine;
    diagnostic.col = anchor.valid ? anchor.col : marker.sourceCol;
    diagnostic.markerKey = markerKey;
    diagnostic.title = muriKindDisplayName(kind, true);
    diagnostic.detail = detail;
    diagnostics->append(diagnostic);
}

void addSimpleNoteDiagnostic(
    QVector<MuriDiagnostic>* diagnostics,
    MuriKind kind,
    MuriAlertLevel alertLevel,
    double second,
    const JudgeableSimpleNote& note,
    const QString& detail,
    const DiagnosticAnchor& anchor = DiagnosticAnchor())
{
    if (diagnostics == nullptr) {
        return;
    }
    MuriDiagnostic diagnostic;
    diagnostic.kind = kind;
    diagnostic.alertLevel = alertLevel;
    diagnostic.second = second;
    diagnostic.anchorSecond = anchor.valid ? anchor.second : diagnostic.second;
    diagnostic.line = anchor.valid ? anchor.line : note.line;
    diagnostic.col = anchor.valid ? anchor.col : note.col;
    diagnostic.markerKey = note.markerKey;
    diagnostic.title = muriKindDisplayName(kind, true);
    diagnostic.detail = detail;
    diagnostics->append(diagnostic);
}

void addSimpleJudgeSpriteEvent(
    QVector<MuriJudgeSpriteEvent>* judgeSpriteEvents,
    const JudgeableSimpleNote& note,
    MuriSimpleJudgeEffect simpleEffect = MuriSimpleJudgeEffect::Good)
{
    if (judgeSpriteEvents == nullptr || !note.judged || !note.judgeBad || note.pad.isEmpty()) {
        return;
    }

    MuriJudgeSpriteEvent event;
    event.kind = MuriJudgeSpriteKind::Simple;
    event.simpleEffect = simpleEffect;
    event.second = note.judgeSecond;
    event.spawnSecond = event.second;
    if (note.marker != nullptr
        && (note.type == QLatin1String("hold") || note.type == QLatin1String("touch_hold"))
        && note.marker->endSecond >= 0.0) {
        event.spawnSecond = qMax(event.second, note.marker->endSecond);
    }
    event.markerKey = note.markerKey;
    event.pad = note.pad;
    judgeSpriteEvents->append(event);
}

bool appendSlideJudgeSpriteEvent(
    QVector<MuriJudgeSpriteEvent>* judgeSpriteEvents,
    const TimelineNoteMarker& marker,
    double judgeSecond)
{
    if (judgeSpriteEvents == nullptr) {
        return false;
    }

    const int lane = qBound(1, marker.endLane, 8);
    if (lane < 1 || lane > 8) {
        return false;
    }

    MuriJudgeSpriteEvent event;
    event.second = judgeSecond;
    event.spawnSecond = event.second;
    event.markerKey = makeMarkerAnalysisKey(marker);
    event.lane = lane;

    if (marker.type == QLatin1String("wifi")) {
        event.kind =
            (lane == 1 || lane == 2 || lane == 7 || lane == 8)
            ? MuriJudgeSpriteKind::WifiUp
            : MuriJudgeSpriteKind::WifiDown;
        judgeSpriteEvents->append(event);
        return true;
    }

    const QString segmentKey = !marker.slideSegmentKeys.isEmpty()
        ? marker.slideSegmentKeys.constLast()
        : marker.slideTrackKey;
    if (slideKeyUsesCcwJudgeSprite(segmentKey)) {
        event.kind = MuriJudgeSpriteKind::SlideCircleCcw;
    } else if (slideKeyUsesCwJudgeSprite(segmentKey)) {
        event.kind = MuriJudgeSpriteKind::SlideCircleCw;
    } else {
        event.kind = MuriJudgeSpriteKind::SlideStraight;
    }

    judgeSpriteEvents->append(event);
    return true;
}

QHash<QString, MarkerSourceRef> buildMarkerSourceRefs(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, MarkerSourceRef> refs;
    refs.reserve(noteMarkers.size() * 2);

    for (int index = 0; index < noteMarkers.size(); ++index) {
        const TimelineNoteMarker& marker = noteMarkers.at(index);
        MarkerSourceRef ref;
        ref.order = marker.parseOrder >= 0 ? marker.parseOrder : index;
        ref.second = marker.second;
        ref.line = marker.sourceLine;
        ref.col = marker.sourceCol;
        ref.type = marker.type;
        refs.insert(makeMarkerAnalysisKey(marker), ref);

        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }

        MarkerSourceRef headStarRef = ref;
        headStarRef.col = slideHeadStarSourceCol(marker);
        headStarRef.type = QStringLiteral("tap");
        refs.insert(slideHeadStarMarkerKey(marker), headStarRef);
    }

    return refs;
}

QVector<JudgeableSimpleNote> buildJudgeableSimpleNotes(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    QHash<QString, int>* noteIndexByMarkerKey)
{
    QVector<JudgeableSimpleNote> notes;
    notes.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        QString pad;
        double criticalSeconds = 0.0;
        double availableSeconds = 0.0;
        if (!buildSimpleNoteJudgeWindow(marker, &pad, &criticalSeconds, &availableSeconds)) {
            continue;
        }

        const QString markerKey = makeMarkerAnalysisKey(marker);
        const MarkerSourceRef ref = markerRefs.value(markerKey);

        JudgeableSimpleNote note;
        note.marker = &marker;
        note.markerKey = markerKey;
        note.type = marker.type;
        note.pad = pad;
        note.order = ref.order;
        note.parseOrder = marker.parseOrder;
        note.eachGroupId = marker.eachGroupId;
        note.line = marker.sourceLine;
        note.col = marker.sourceCol;
        note.momentSecond = marker.second;
        note.pressEndSecond = notePressEndSecond(marker);
        note.criticalSeconds = criticalSeconds;
        note.availableSeconds = availableSeconds;
        note.expiryTick = judgeTickForNoteExpiry(marker.second, availableSeconds);
        note.slideHead = marker.slideHead;
        note.onSlide = marker.onSlide;
        note.tailOnSlideHead = marker.tailOnSlideHead;
        note.hasProtection = marker.isEx;
        const int noteIndex = notes.size();
        notes.append(note);
        if (noteIndexByMarkerKey != nullptr) {
            noteIndexByMarkerKey->insert(markerKey, noteIndex);
        }
    }

    QSet<QString> emittedHeadStarKeys;
    for (int markerIndex = 0; markerIndex < noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers.at(markerIndex);
        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }

        const QString emitKey = headStarJudgeEmitKey(marker);
        if (emittedHeadStarKeys.contains(emitKey)) {
            continue;
        }
        emittedHeadStarKeys.insert(emitKey);

        const QString ownerMarkerKey = makeMarkerAnalysisKey(marker);
        const MarkerSourceRef ref = markerRefs.value(ownerMarkerKey);
        const QString markerKey = slideHeadStarMarkerKey(marker);

        JudgeableSimpleNote note;
        note.marker = &marker;
        note.markerKey = markerKey;
        note.type = QStringLiteral("tap");
        note.pad = slideHeadPad(marker.lane);
        note.order = ref.order;
        note.parseOrder = marker.parseOrder;
        note.eachGroupId = marker.eachGroupId;
        note.line = marker.sourceLine;
        note.col = slideHeadStarSourceCol(marker);
        note.momentSecond = marker.second;
        note.pressEndSecond = marker.second + miacode::muri::kReleaseDelaySeconds;
        note.criticalSeconds = miacode::muri::kTapCriticalSeconds;
        note.availableSeconds = miacode::muri::kTapAvailableSeconds;
        note.expiryTick = judgeTickForNoteExpiry(marker.second, note.availableSeconds);
        // Keep the star identity for labeling and source attribution only.
        note.headStarTapLike = true;
        note.hasProtection = marker.headEx;
        const int noteIndex = notes.size();
        notes.append(note);
        if (noteIndexByMarkerKey != nullptr) {
            noteIndexByMarkerKey->insert(markerKey, noteIndex);
        }
    }

    return notes;
}

QMap<int, QVector<int>> buildNoteExpiryBuckets(const QVector<JudgeableSimpleNote>& notes)
{
    QMap<int, QVector<int>> notesByExpiryTick;
    for (int index = 0; index < notes.size(); ++index) {
        notesByExpiryTick[notes.at(index).expiryTick].append(index);
    }
    return notesByExpiryTick;
}

QVector<RuntimeTouchGroup> buildRuntimeTouchGroups(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QHash<QString, int>& noteIndexByMarkerKey,
    QHash<int, int>* touchGroupByChildNoteIndex)
{
    QMap<int, QVector<int>> touchMarkerIndicesByEachGroup;
    for (int markerIndex = 0; markerIndex < noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers.at(markerIndex);
        if (marker.type == QLatin1String("touch") && marker.eachGroupId >= 0 && !marker.touchPad.isEmpty()) {
            touchMarkerIndicesByEachGroup[marker.eachGroupId].append(markerIndex);
        }
    }

    QVector<RuntimeTouchGroup> groups;
    for (auto it = touchMarkerIndicesByEachGroup.begin(); it != touchMarkerIndicesByEachGroup.end(); ++it) {
        QVector<int> markerIndices = it.value();
        std::sort(markerIndices.begin(), markerIndices.end(), [&noteMarkers](int a, int b) {
            const TimelineNoteMarker& left = noteMarkers.at(a);
            const TimelineNoteMarker& right = noteMarkers.at(b);
            const int leftOrder = left.parseOrder >= 0 ? left.parseOrder : a;
            const int rightOrder = right.parseOrder >= 0 ? right.parseOrder : b;
            return leftOrder < rightOrder;
        });

        if (markerIndices.size() <= 1) {
            continue;
        }

        QVector<int> parents(markerIndices.size());
        for (int i = 0; i < parents.size(); ++i) {
            parents[i] = i;
        }

        const auto findRoot = [&parents](int index) {
            int root = index;
            while (parents[root] != root) {
                root = parents[root];
            }
            int cursor = index;
            while (parents[cursor] != root) {
                const int parent = parents[cursor];
                parents[cursor] = root;
                cursor = parent;
            }
            return root;
        };

        for (int left = 0; left < markerIndices.size(); ++left) {
            const TimelineNoteMarker& leftMarker = noteMarkers.at(markerIndices.at(left));
            for (int right = left + 1; right < markerIndices.size(); ++right) {
                const TimelineNoteMarker& rightMarker = noteMarkers.at(markerIndices.at(right));
                if (!touchPadsAreAdjacent(leftMarker.touchPad, rightMarker.touchPad)) {
                    continue;
                }

                const int rootLeft = findRoot(left);
                const int rootRight = findRoot(right);
                if (rootLeft == rootRight) {
                    continue;
                }

                for (int cursor = 0; cursor < parents.size(); ++cursor) {
                    if (parents[cursor] == rootRight) {
                        parents[cursor] = rootLeft;
                    }
                }
            }
        }

        QHash<int, QVector<int>> componentLocals;
        QVector<int> componentOrder;
        for (int localIndex = 0; localIndex < markerIndices.size(); ++localIndex) {
            const int root = findRoot(localIndex);
            if (!componentLocals.contains(root)) {
                componentOrder.append(root);
            }
            componentLocals[root].append(localIndex);
        }

        for (int root : componentOrder) {
            const QVector<int> locals = componentLocals.value(root);
            if (locals.size() <= 1) {
                continue;
            }

            RuntimeTouchGroup group;
            group.eachGroupId = it.key();
            group.allOnSlide = true;
            for (int localIndex : locals) {
                const int markerIndex = markerIndices.at(localIndex);
                const TimelineNoteMarker& marker = noteMarkers.at(markerIndex);
                const QString markerKey = makeMarkerAnalysisKey(marker);
                const int noteIndex = noteIndexByMarkerKey.value(markerKey, -1);
                if (noteIndex < 0) {
                    continue;
                }

                if (group.sourceMarkerKey.isEmpty()) {
                    group.sourceMarkerKey = markerKey;
                    group.line = marker.sourceLine;
                    group.col = marker.sourceCol;
                    group.momentSecond = marker.second;
                }
                if (group.firstParseOrder < 0 || marker.parseOrder < group.firstParseOrder) {
                    group.firstParseOrder = marker.parseOrder;
                }
                if (!marker.onSlide) {
                    group.allOnSlide = false;
                }

                group.childNoteIndices.append(noteIndex);
            }

            if (group.childNoteIndices.size() <= 1) {
                continue;
            }

            group.threshold = group.childNoteIndices.size() * 0.51;
            const int groupIndex = groups.size();
            groups.append(group);
            if (touchGroupByChildNoteIndex != nullptr) {
                for (int childNoteIndex : group.childNoteIndices) {
                    touchGroupByChildNoteIndex->insert(childNoteIndex, groupIndex);
                }
            }
        }
    }

    return groups;
}

QVector<RuntimeTopLevelNote> buildRuntimeTopLevelNotes(
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex)
{
    struct TouchTopLevelEntry {
        int firstParseOrder = -1;
        int simpleNoteIndex = -1;
        int touchGroupIndex = -1;
    };

    QMap<int, QVector<int>> nonTouchNoteIndicesByEachGroup;
    QMap<int, QVector<TouchTopLevelEntry>> touchEntriesByEachGroup;
    QMap<int, int> firstParseOrderByEachGroup;

    for (int noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const JudgeableSimpleNote& note = notes.at(noteIndex);
        const int eachGroupId = note.eachGroupId;
        if (eachGroupId < 0) {
            continue;
        }
        const int parseOrder = note.parseOrder >= 0 ? note.parseOrder : note.order;
        if (!firstParseOrderByEachGroup.contains(eachGroupId) || parseOrder < firstParseOrderByEachGroup.value(eachGroupId)) {
            firstParseOrderByEachGroup[eachGroupId] = parseOrder;
        }

        if (touchGroupByChildNoteIndex.contains(noteIndex)) {
            continue;
        }

        if (note.type == QLatin1String("touch")) {
            TouchTopLevelEntry entry;
            entry.firstParseOrder = parseOrder;
            entry.simpleNoteIndex = noteIndex;
            touchEntriesByEachGroup[eachGroupId].append(entry);
        } else {
            nonTouchNoteIndicesByEachGroup[eachGroupId].append(noteIndex);
        }
    }

    for (int touchGroupIndex = 0; touchGroupIndex < touchGroups.size(); ++touchGroupIndex) {
        const RuntimeTouchGroup& group = touchGroups.at(touchGroupIndex);
        if (group.eachGroupId < 0) {
            continue;
        }
        if (!firstParseOrderByEachGroup.contains(group.eachGroupId)
            || group.firstParseOrder < firstParseOrderByEachGroup.value(group.eachGroupId)) {
            firstParseOrderByEachGroup[group.eachGroupId] = group.firstParseOrder;
        }
        TouchTopLevelEntry entry;
        entry.firstParseOrder = group.firstParseOrder;
        entry.touchGroupIndex = touchGroupIndex;
        touchEntriesByEachGroup[group.eachGroupId].append(entry);
    }

    QVector<int> eachGroupIds = firstParseOrderByEachGroup.keys().toVector();
    std::sort(eachGroupIds.begin(), eachGroupIds.end(), [&firstParseOrderByEachGroup](int a, int b) {
        return firstParseOrderByEachGroup.value(a) < firstParseOrderByEachGroup.value(b);
    });

    QVector<RuntimeTopLevelNote> topLevelNotes;
    int sequenceOrder = 0;
    for (int eachGroupId : eachGroupIds) {
        QVector<int> nonTouchIndices = nonTouchNoteIndicesByEachGroup.value(eachGroupId);
        std::sort(nonTouchIndices.begin(), nonTouchIndices.end(), [&notes](int a, int b) {
            return notes.at(a).parseOrder < notes.at(b).parseOrder;
        });
        for (int noteIndex : nonTouchIndices) {
            RuntimeTopLevelNote topLevel;
            topLevel.sequenceOrder = sequenceOrder++;
            topLevel.simpleNoteIndex = noteIndex;
            topLevelNotes.append(topLevel);
        }

        QVector<TouchTopLevelEntry> touchEntries = touchEntriesByEachGroup.value(eachGroupId);
        std::sort(touchEntries.begin(), touchEntries.end(), [](const TouchTopLevelEntry& a, const TouchTopLevelEntry& b) {
            return a.firstParseOrder < b.firstParseOrder;
        });
        for (const TouchTopLevelEntry& entry : touchEntries) {
            RuntimeTopLevelNote topLevel;
            topLevel.sequenceOrder = sequenceOrder++;
            topLevel.simpleNoteIndex = entry.simpleNoteIndex;
            topLevel.touchGroupIndex = entry.touchGroupIndex;
            topLevelNotes.append(topLevel);
        }
    }

    QVector<int> ungroupedNoteIndices;
    for (int noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const JudgeableSimpleNote& note = notes.at(noteIndex);
        if (note.eachGroupId >= 0) {
            continue;
        }
        if (touchGroupByChildNoteIndex.contains(noteIndex)) {
            continue;
        }
        ungroupedNoteIndices.append(noteIndex);
    }
    std::sort(ungroupedNoteIndices.begin(), ungroupedNoteIndices.end(), [&notes](int a, int b) {
        return notes.at(a).parseOrder < notes.at(b).parseOrder;
    });
    for (int noteIndex : ungroupedNoteIndices) {
        RuntimeTopLevelNote topLevel;
        topLevel.sequenceOrder = sequenceOrder++;
        topLevel.simpleNoteIndex = noteIndex;
        topLevelNotes.append(topLevel);
    }

    return topLevelNotes;
}

QVector<MuriPadWindow> buildRuntimePadWindows(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QHash<QString, int>& noteIndexByMarkerKey,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex)
{
    QVector<MuriPadWindow> windows;
    QVector<bool> emittedTouchGroups(touchGroups.size(), false);
    QVector<MuriActionTrail> ignoredActionTrails;

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString markerKey = makeMarkerAnalysisKey(marker);
        const QString pad = notePad(marker);
        if (marker.type == QLatin1String("tap")) {
            if (!marker.slideHead) {
                addPadWindow(&windows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            }
            continue;
        }
        if (marker.type == QLatin1String("hold")) {
            addPadWindow(&windows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            continue;
        }
        if (marker.type == QLatin1String("touch")) {
            const int noteIndex = noteIndexByMarkerKey.value(markerKey, -1);
            const int touchGroupIndex = touchGroupByChildNoteIndex.value(noteIndex, -1);
            if (touchGroupIndex >= 0) {
                if (touchGroupIndex < 0 || touchGroupIndex >= touchGroups.size() || emittedTouchGroups.value(touchGroupIndex)) {
                    continue;
                }
                emittedTouchGroups[touchGroupIndex] = true;

                const RuntimeTouchGroup& group = touchGroups.at(touchGroupIndex);
                if (group.allOnSlide) {
                    continue;
                }

                for (int childNoteIndex : group.childNoteIndices) {
                    if (childNoteIndex < 0 || childNoteIndex >= notes.size()) {
                        continue;
                    }
                    const JudgeableSimpleNote& child = notes.at(childNoteIndex);
                    addPadWindow(
                        &windows,
                        child.pad,
                        group.momentSecond,
                        group.momentSecond + miacode::muri::kReleaseDelaySeconds,
                        child.markerKey,
                        child.type);
                }
                continue;
            }

            if (!marker.onSlide) {
                addPadWindow(&windows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            }
            continue;
        }
        if (marker.type == QLatin1String("touch_hold")) {
            addPadWindow(&windows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            continue;
        }
        if (marker.type == QLatin1String("slide")) {
            addSlidePadWindowsAndTrails(marker, markerKey, &windows, &ignoredActionTrails);
            continue;
        }
        if (marker.type == QLatin1String("wifi")) {
            addWifiPadWindowsAndTrails(marker, markerKey, &windows, &ignoredActionTrails);
            continue;
        }
    }

    for (const JudgeableSimpleNote& note : notes) {
        if (!note.headStarTapLike) {
            continue;
        }
        addPadWindow(
            &windows,
            note.pad,
            note.momentSecond,
            note.pressEndSecond,
            note.markerKey,
            note.type);
    }

    return windows;
}

void appendPressHandAction(
    QVector<RuntimeHandAction>* actions,
    const QString& markerKey,
    const QString& sourceType,
    int order,
    int line,
    int col,
    double startSecond,
    double endSecond,
    const QPointF& center,
    double radius,
    bool requireTwoHands)
{
    if (actions == nullptr || endSecond + kPadTimeEpsilon < startSecond) {
        return;
    }

    RuntimeHandAction action;
    action.kind = RuntimeHandActionKind::Press;
    action.markerKey = markerKey;
    action.sourceType = sourceType;
    action.order = order;
    action.line = line;
    action.col = col;
    action.startSecond = startSecond;
    action.endSecond = qMax(startSecond, endSecond);
    action.motionDurationSecond = qMax(0.0, endSecond - startSecond);
    action.radius = radius;
    action.requireTwoHands = requireTwoHands;
    action.points.append(center);
    actions->append(action);
}

void appendSlideHandAction(
    QVector<RuntimeHandAction>* actions,
    const QString& markerKey,
    const QString& sourceType,
    const QString& mergeKey,
    int order,
    int line,
    int col,
    double startSecond,
    double motionDurationSecond,
    double endSecond,
    double radius,
    const QVector<QPointF>& points)
{
    if (actions == nullptr || points.isEmpty() || endSecond + kPadTimeEpsilon < startSecond) {
        return;
    }

    RuntimeHandAction action;
    action.kind = RuntimeHandActionKind::Slide;
    action.markerKey = markerKey;
    action.sourceType = sourceType;
    action.mergeKey = mergeKey;
    action.order = order;
    action.line = line;
    action.col = col;
    action.startSecond = startSecond;
    action.endSecond = qMax(startSecond, endSecond);
    action.motionDurationSecond = qMax(0.0, motionDurationSecond);
    action.radius = radius;
    action.points = points;
    actions->append(action);
}

QPointF simpleNoteActionCenter(const JudgeableSimpleNote& note)
{
    if (note.type == QLatin1String("touch") || note.type == QLatin1String("touch_hold")) {
        if (note.marker != nullptr) {
            return note.marker->touchPoint;
        }
        return QPointF();
    }
    return miacode::muri::padCenter(note.pad);
}

QVector<RuntimeHandAction> buildRuntimeHandActions(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex,
    bool includeSlideLike = false)
{
    using namespace miacode::muri;

    QVector<RuntimeHandAction> actions;
    actions.reserve(notes.size() + touchGroups.size() + noteMarkers.size() * 3);

    for (int noteIndex = 0; noteIndex < notes.size(); ++noteIndex) {
        const JudgeableSimpleNote& note = notes.at(noteIndex);
        if (note.type == QLatin1String("tap")) {
            // Synthetic head-stars keep their own labels, but use the same tap
            // action path as ordinary taps after they are modeled.
            if (note.slideHead) {
                continue;
            }
            appendPressHandAction(
                &actions,
                note.markerKey,
                note.type,
                note.parseOrder >= 0 ? note.parseOrder : note.order,
                note.line,
                note.col,
                note.momentSecond,
                note.pressEndSecond,
                simpleNoteActionCenter(note),
                kHandRadiusNormal,
                false);
            continue;
        }

        if (note.type == QLatin1String("hold")) {
            appendPressHandAction(
                &actions,
                note.markerKey,
                note.type,
                note.parseOrder >= 0 ? note.parseOrder : note.order,
                note.line,
                note.col,
                note.momentSecond,
                note.pressEndSecond,
                simpleNoteActionCenter(note),
                kHandRadiusNormal,
                false);
            continue;
        }

        if (note.type == QLatin1String("touch")) {
            if (touchGroupByChildNoteIndex.contains(noteIndex) || note.onSlide) {
                continue;
            }
            appendPressHandAction(
                &actions,
                note.markerKey,
                note.type,
                note.parseOrder >= 0 ? note.parseOrder : note.order,
                note.line,
                note.col,
                note.momentSecond,
                note.pressEndSecond,
                simpleNoteActionCenter(note),
                kHandRadiusNormal,
                false);
            continue;
        }

        if (note.type == QLatin1String("touch_hold")) {
            appendPressHandAction(
                &actions,
                note.markerKey,
                note.type,
                note.parseOrder >= 0 ? note.parseOrder : note.order,
                note.line,
                note.col,
                note.momentSecond,
                note.pressEndSecond,
                simpleNoteActionCenter(note),
                kHandRadiusNormal,
                false);
            continue;
        }
    }

    for (const RuntimeTouchGroup& group : touchGroups) {
        if (group.allOnSlide) {
            continue;
        }

        QVector<QPointF> touchPoints;
        touchPoints.reserve(group.childNoteIndices.size());
        for (int childNoteIndex : group.childNoteIndices) {
            if (childNoteIndex < 0 || childNoteIndex >= notes.size()) {
                continue;
            }
            const JudgeableSimpleNote& note = notes.at(childNoteIndex);
            if (note.marker == nullptr) {
                continue;
            }
            touchPoints.append(note.marker->touchPoint);
        }
        const CoveringCircle circle = smallestCoveringCircle(touchPoints);
        if (!circle.valid) {
            continue;
        }

        appendPressHandAction(
            &actions,
            group.sourceMarkerKey,
            QStringLiteral("touch_group"),
            group.firstParseOrder,
            group.line,
            group.col,
            group.momentSecond,
            group.momentSecond + kReleaseDelaySeconds,
            circle.center,
            circle.radius,
            circle.radius > kHandRadiusMax + kPadTimeEpsilon);
    }

    if (includeSlideLike) {
        for (const TimelineNoteMarker& marker : noteMarkers) {
            const QString markerKey = makeMarkerAnalysisKey(marker);
            const int order = marker.parseOrder >= 0 ? marker.parseOrder : 0;
            if (marker.type == QLatin1String("slide")) {
                const int segmentCount = qMin(
                    qMin(marker.slideSegmentShootSeconds.size(), marker.slideSegmentDurations.size()),
                    marker.slideSegmentPoints.size());
                for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                    const double startSecond = marker.slideSegmentShootSeconds.at(segmentIndex);
                    const double durationSecond = qMax(0.0, marker.slideSegmentDurations.at(segmentIndex));
                    const bool isLastSegment = (segmentIndex + 1 >= segmentCount);
                    const double endSecond = startSecond
                        + durationSecond
                        + ((isLastSegment && !marker.beforeSlide) ? kReleaseDelaySeconds : 0.0);
                    QVector<QPointF> runtimePoints;
                    if (segmentIndex >= 0 && segmentIndex < marker.slideSegmentKeys.size()) {
                        runtimePoints = loadRuntimeSlideActionPath(marker.slideSegmentKeys.at(segmentIndex));
                    }
                    const QVector<QPointF>& actionPoints = runtimePoints.isEmpty()
                        ? marker.slideSegmentPoints.at(segmentIndex)
                        : runtimePoints;
                    appendSlideHandAction(
                        &actions,
                        markerKey,
                        marker.type,
                        QStringLiteral("normal"),
                        order,
                        marker.sourceLine,
                        marker.sourceCol,
                        startSecond,
                        durationSecond,
                        endSecond,
                        kHandRadiusNormal,
                        centeredPathPoints(actionPoints));
                }
                continue;
            }

            if (marker.type == QLatin1String("wifi")) {
                const QVector<QVector<QPointF>> runtimePaths = loadRuntimeWifiActionPaths(marker.slideTrackKey);
                const QVector<QVector<QPointF>>& actionPaths = runtimePaths.isEmpty() ? marker.wifiLanePoints : runtimePaths;
                const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
                if (!actionPaths.isEmpty()) {
                    appendSlideHandAction(
                        &actions,
                        markerKey,
                        marker.type,
                        QStringLiteral("wifi"),
                        order,
                        marker.sourceLine,
                        marker.sourceCol,
                        marker.slideTraceSecond,
                        durationSecond,
                        marker.endSecond,
                        kHandRadiusWifi,
                        centeredPathPoints(actionPaths.constFirst()));
                }
                if (actionPaths.size() > 1) {
                    appendSlideHandAction(
                        &actions,
                        markerKey,
                        marker.type,
                        QStringLiteral("wifi"),
                        order,
                        marker.sourceLine,
                        marker.sourceCol,
                        marker.slideTraceSecond,
                        durationSecond,
                        marker.endSecond,
                        kHandRadiusWifi,
                        centeredPathPoints(actionPaths.constLast()));
                }
            }
        }
    }

    std::stable_sort(actions.begin(), actions.end(), [](const RuntimeHandAction& a, const RuntimeHandAction& b) {
        return a.startSecond < b.startSecond;
    });

    return actions;
}

bool sampleRuntimeHandAction(
    const RuntimeHandAction& action,
    double nowSecond,
    RuntimeTouchPoint* touchPoint)
{
    if (touchPoint == nullptr) {
        return false;
    }
    if (nowSecond + kPadTimeEpsilon < action.startSecond || nowSecond >= action.endSecond - kPadTimeEpsilon) {
        return false;
    }
    if (action.points.isEmpty()) {
        return false;
    }

    touchPoint->center = action.points.constFirst();
    touchPoint->tangent = QPointF();
    touchPoint->radius = action.radius;
    touchPoint->hasTangent = false;

    if (action.kind == RuntimeHandActionKind::Press || action.points.size() <= 1) {
        return true;
    }

    double progress = 1.0;
    if (action.motionDurationSecond > kPadTimeEpsilon) {
        progress = qBound(0.0, (nowSecond - action.startSecond) / action.motionDurationSecond, 1.0);
    }

    const double scaledIndex = progress * (action.points.size() - 1);
    int leftIndex = static_cast<int>(std::floor(scaledIndex));
    leftIndex = qBound(0, leftIndex, action.points.size() - 1);
    const int rightIndex = qMin(leftIndex + 1, action.points.size() - 1);
    const double localT = scaledIndex - leftIndex;
    const QPointF& left = action.points.at(leftIndex);
    const QPointF& right = action.points.at(rightIndex);
    touchPoint->center = QPointF(
        left.x() + (right.x() - left.x()) * localT,
        left.y() + (right.y() - left.y()) * localT);
    const QPointF tangent = normalizedDirection(right - left);
    if (!qFuzzyIsNull(tangent.x()) || !qFuzzyIsNull(tangent.y())) {
        touchPoint->tangent = tangent;
        touchPoint->hasTangent = true;
    }
    return true;
}

QVector<RuntimeTouchPoint> buildRuntimeTouchPoints(
    const QVector<RuntimeHandAction>& actions,
    const QVector<int>& activeActionIndices,
    double nowSecond,
    const QSet<quint64>* previousMergedSlidePairs = nullptr,
    QSet<quint64>* currentMergedSlidePairs = nullptr)
{
    using namespace miacode::muri;

    QVector<RuntimeTouchPoint> touchPoints;
    touchPoints.reserve(activeActionIndices.size());

    for (int actionIndex : activeActionIndices) {
        if (actionIndex < 0 || actionIndex >= actions.size()) {
            continue;
        }

        RuntimeTouchPoint touchPoint;
        touchPoint.actionIndex = actionIndex;
        if (!sampleRuntimeHandAction(actions.at(actionIndex), nowSecond, &touchPoint)) {
            continue;
        }

        const RuntimeHandAction& action = actions.at(actionIndex);
        if (!action.mergeKey.isEmpty()) {
            bool merged = false;
            const auto orderedActionPairKey = [](int left, int right) {
                const quint32 first = static_cast<quint32>(qMin(left, right));
                const quint32 second = static_cast<quint32>(qMax(left, right));
                return (static_cast<quint64>(first) << 32) | static_cast<quint64>(second);
            };
            const auto inReleaseTail = [nowSecond](const RuntimeHandAction& candidateAction) {
                if (candidateAction.kind != RuntimeHandActionKind::Slide) {
                    return false;
                }
                const double motionEndSecond =
                    candidateAction.startSecond + candidateAction.motionDurationSecond;
                return nowSecond + kPadTimeEpsilon >= motionEndSecond
                    && nowSecond < candidateAction.endSecond - kPadTimeEpsilon;
            };
            for (const RuntimeTouchPoint& existing : touchPoints) {
                if (existing.actionIndex < 0 || existing.actionIndex >= actions.size()) {
                    continue;
                }
                const RuntimeHandAction& existingAction = actions.at(existing.actionIndex);
                if (existingAction.mergeKey.isEmpty() || existingAction.mergeKey != action.mergeKey) {
                    continue;
                }
                const quint64 pairKey = orderedActionPairKey(existing.actionIndex, actionIndex);
                const bool regularMerge =
                    existing.hasTangent
                    && touchPoint.hasTangent
                    && pointDistance(existing.center, touchPoint.center) < kSlideMergeDistance
                    && tangentDelta(existing.tangent, touchPoint.tangent) < kSlideMergeTangentDelta;
                const bool tailContinuationMerge =
                    !regularMerge
                    && previousMergedSlidePairs != nullptr
                    && previousMergedSlidePairs->contains(pairKey)
                    && existingAction.kind == RuntimeHandActionKind::Slide
                    && action.kind == RuntimeHandActionKind::Slide
                    && (inReleaseTail(existingAction) || inReleaseTail(action));
                if (regularMerge || tailContinuationMerge) {
                    if (currentMergedSlidePairs != nullptr) {
                        currentMergedSlidePairs->insert(pairKey);
                    }
                    merged = true;
                    break;
                }
            }
            if (merged) {
                continue;
            }
        }

        touchPoints.append(touchPoint);
    }

    return touchPoints;
}

QVector<PadWindowInterval> buildPadWindowIntervals(
    const QVector<MuriPadWindow>& padWindows,
    const QHash<QString, MarkerSourceRef>& markerRefs)
{
    QVector<PadWindowInterval> intervals;
    intervals.reserve(padWindows.size());

    for (const MuriPadWindow& window : padWindows) {
        if (window.pad.isEmpty()) {
            continue;
        }

        PadWindowInterval interval;
        interval.pad = window.pad;
        interval.startTick = judgeTickForPadActiveStart(window.startSecond);
        interval.endTick = judgeTickForPadActiveEnd(window.endSecond);
        if (interval.endTick < interval.startTick) {
            // Keep zero-duration or sub-tick windows alive for one judge tick.
            // Wifi resources can emit multiple pads at the exact same proportion
            // (for example E5/B5/E6), and dropping those windows makes on-slide
            // touches miss the matching pad-down entirely.
            interval.endTick = interval.startTick;
        }

        interval.sourceMarkerKey = window.sourceMarkerKey;
        interval.sourceType = window.sourceType;
        const MarkerSourceRef ref = markerRefs.value(window.sourceMarkerKey);
        interval.sourceOrder = ref.order;
        interval.line = ref.line;
        interval.col = ref.col;
        intervals.append(interval);
    }

    return intervals;
}

QMap<int, QMap<QString, RuntimePadEvent>> buildRuntimePadEvents(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<MuriPadWindow>& padWindows,
    const QHash<QString, MarkerSourceRef>& markerRefs)
{
    QMap<int, QMap<QString, RuntimePadEvent>> eventsByTick;

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString pad = slideHeadPad(marker.lane);
        const double eventSecond = extraPadDownSecond(marker);
        if (pad.isEmpty() || !std::isfinite(eventSecond)) {
            continue;
        }

        const QString markerKey = makeMarkerAnalysisKey(marker);
        const MarkerSourceRef ref = markerRefs.value(markerKey);
        RuntimePadEvent event;
        event.pad = pad;
        event.tick = judgeTickForExtraPadDown(eventSecond);
        event.second = tickToSecond(event.tick);
        event.sourceMarkerKey = markerKey;
        event.sourceType = marker.type;
        event.sourceOrder = ref.order;
        event.line = marker.sourceLine;
        event.col = marker.sourceCol;
        event.extraPadDown = true;
        eventsByTick[event.tick].insert(pad, event);
    }

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!hasUsableSlideTraceTiming(marker)) {
            continue;
        }

        const QString pad = slideHeadPad(marker.lane);
        if (pad.isEmpty()) {
            continue;
        }

        const QString markerKey = makeMarkerAnalysisKey(marker);
        const MarkerSourceRef ref = markerRefs.value(markerKey);
        RuntimePadEvent event;
        event.pad = pad;
        event.tick = judgeTickForPadActiveStart(marker.slideTraceSecond);
        event.second = tickToSecond(event.tick);
        event.sourceMarkerKey = markerKey;
        event.sourceType = marker.type;
        event.sourceOrder = ref.order;
        event.line = marker.sourceLine;
        event.col = marker.sourceCol;
        event.extraPadDown = false;

        QMap<QString, RuntimePadEvent>& tickEvents = eventsByTick[event.tick];
        if (!tickEvents.contains(pad) || tickEvents.value(pad).sourceOrder <= event.sourceOrder) {
            tickEvents.insert(pad, event);
        }
    }

    const QVector<PadWindowInterval> intervals = buildPadWindowIntervals(padWindows, markerRefs);
    QHash<QString, QVector<int>> intervalIndicesByPad;
    for (int index = 0; index < intervals.size(); ++index) {
        const PadWindowInterval& interval = intervals.at(index);
        if (interval.sourceType == QLatin1String("slide") || interval.sourceType == QLatin1String("wifi")) {
            RuntimePadEvent event;
            event.pad = interval.pad;
            event.tick = interval.startTick;
            event.second = tickToSecond(interval.startTick);
            event.sourceMarkerKey = interval.sourceMarkerKey;
            event.sourceType = interval.sourceType;
            event.sourceOrder = interval.sourceOrder;
            event.line = interval.line;
            event.col = interval.col;
            event.extraPadDown = false;

            QMap<QString, RuntimePadEvent>& tickEvents = eventsByTick[event.tick];
            if (!tickEvents.contains(event.pad) || tickEvents.value(event.pad).sourceOrder <= event.sourceOrder) {
                tickEvents.insert(event.pad, event);
            }
            continue;
        }

        intervalIndicesByPad[interval.pad].append(index);
    }

    for (auto it = intervalIndicesByPad.constBegin(); it != intervalIndicesByPad.constEnd(); ++it) {
        QMap<int, QVector<int>> addByTick;
        QMap<int, QVector<int>> removeByTick;
        for (int intervalIndex : it.value()) {
            const PadWindowInterval& interval = intervals.at(intervalIndex);
            addByTick[interval.startTick].append(intervalIndex);
            removeByTick[interval.endTick + 1].append(intervalIndex);
        }

        QMap<int, bool> changeTicks;
        for (auto addIt = addByTick.constBegin(); addIt != addByTick.constEnd(); ++addIt) {
            changeTicks.insert(addIt.key(), true);
        }
        for (auto removeIt = removeByTick.constBegin(); removeIt != removeByTick.constEnd(); ++removeIt) {
            changeTicks.insert(removeIt.key(), true);
        }

        QVector<int> activeIntervals;
        bool hadActive = false;
        for (auto tickIt = changeTicks.constBegin(); tickIt != changeTicks.constEnd(); ++tickIt) {
            const int tick = tickIt.key();
            for (int intervalIndex : removeByTick.value(tick)) {
                activeIntervals.removeAll(intervalIndex);
            }
            for (int intervalIndex : addByTick.value(tick)) {
                activeIntervals.append(intervalIndex);
            }

            const bool hasActive = !activeIntervals.isEmpty();
            if (!hadActive && hasActive) {
                int bestIntervalIndex = activeIntervals.constFirst();
                for (int intervalIndex : activeIntervals) {
                    const PadWindowInterval& current = intervals.at(intervalIndex);
                    const PadWindowInterval& best = intervals.at(bestIntervalIndex);
                    if (current.sourceOrder > best.sourceOrder) {
                        bestIntervalIndex = intervalIndex;
                    }
                }

                const PadWindowInterval& interval = intervals.at(bestIntervalIndex);
                RuntimePadEvent event;
                event.pad = interval.pad;
                event.tick = tick;
                event.second = tickToSecond(tick);
                event.sourceMarkerKey = interval.sourceMarkerKey;
                event.sourceType = interval.sourceType;
                event.sourceOrder = interval.sourceOrder;
                event.line = interval.line;
                event.col = interval.col;
                event.extraPadDown = false;

                // Match MaiMuriDX's per-tick overwrite behavior: actual pad-down wins over extra pad-down.
                eventsByTick[tick].insert(interval.pad, event);
            }

            hadActive = hasActive;
        }
    }

    return eventsByTick;
}

struct RuntimeSlideNoteState {
    const TimelineNoteMarker* marker = nullptr;
    QString markerKey;
    int line = 1;
    int col = 1;
    double availableSecond = 0.0;
    double shootSecond = 0.0;
    double endSecond = 0.0;
    double criticalSecond = 0.0;
    double criticalDeltaSecond = 0.0;
    QVector<QStringList> judgeSequence;
    QVector<bool> partition;
    QVector<int> segmentEndAreaIndices;
    QVector<RuntimeJudgeHit> areaHits;
    int totalAreaNum = 0;
    int curAreaIdx = 0;
    int curSegmentIdx = 0;
    QString pressingPad;
    bool isL = false;
    bool isSpecialL = false;
    bool judged = false;
    bool judgeBad = false;
    double judgeSecond = -1.0;
    RuntimePadEvent judgeAction;
};

struct RuntimeWifiNoteState {
    const TimelineNoteMarker* marker = nullptr;
    QString markerKey;
    int line = 1;
    int col = 1;
    double availableSecond = 0.0;
    double shootSecond = 0.0;
    double endSecond = 0.0;
    double criticalSecond = 0.0;
    double criticalDeltaSecond = 0.0;
    QVector<QVector<QStringList>> laneJudgeSequence;
    QVector<QVector<RuntimeJudgeHit>> areaHits;
    QVector<QVector<double>> laneProgressSeconds;
    QVector<int> curAreaIdxes;
    QVector<QString> pressingPads;
    QVector<bool> laneFinished;
    int totalAreaNum = 0;
    bool padCPassed = true;
    double padCSecond = -1.0;
    bool judged = false;
    bool judgeBad = false;
    double judgeSecond = -1.0;
    RuntimePadEvent judgeAction;
};

bool runtimePadEventIsForeignJudge(
    const RuntimePadEvent& event,
    const QString& ownerMarkerKey,
    const TimelineNoteMarker* ownerMarker = nullptr)
{
    if (event.sourceMarkerKey.isEmpty()) {
        return false;
    }
    if (event.sourceMarkerKey == ownerMarkerKey) {
        return false;
    }
    if (ownerMarker != nullptr && event.sourceMarkerKey == slideHeadStarMarkerKey(*ownerMarker)) {
        return false;
    }
    return true;
}

bool slideFinalAreaWasJudgedByOther(const RuntimeSlideNoteState& note)
{
    if (note.areaHits.isEmpty()) {
        return false;
    }
    const RuntimeJudgeHit& finalHit = note.areaHits.constLast();
    return finalHit.judged && runtimePadEventIsForeignJudge(finalHit.cause, note.markerKey, note.marker);
}

bool wifiFinalJudgeWasByOther(const RuntimeWifiNoteState& note)
{
    return runtimePadEventIsForeignJudge(note.judgeAction, note.markerKey, note.marker);
}

bool padStateForTick(
    const QHash<QString, RuntimePadEvent>& padStates,
    const QString& pad,
    RuntimePadEvent* event)
{
    const auto it = padStates.constFind(pad);
    if (it == padStates.constEnd()) {
        return false;
    }
    if (event != nullptr) {
        *event = it.value();
    }
    return true;
}

bool padReleasedThisTick(
    const QHash<QString, RuntimePadEvent>& padUpThisTick,
    const QString& pad,
    RuntimePadEvent* event)
{
    const auto it = padUpThisTick.constFind(pad);
    if (it == padUpThisTick.constEnd()) {
        return false;
    }
    if (event != nullptr) {
        *event = it.value();
    }
    return true;
}

bool buildRuntimeSlideJudgeSequence(
    const TimelineNoteMarker& marker,
    QVector<QStringList>* judgeSequence,
    QVector<bool>* partition,
    QVector<int>* segmentEndAreaIndices,
    bool* isL,
    bool* isSpecialL)
{
    if (judgeSequence == nullptr || partition == nullptr || segmentEndAreaIndices == nullptr) {
        return false;
    }

    const QJsonObject slides = slideRuntimeRoot().value(QStringLiteral("slides")).toObject();
    if (slides.isEmpty()) {
        return false;
    }

    judgeSequence->clear();
    partition->clear();
    segmentEndAreaIndices->clear();
    if (isL != nullptr) {
        *isL = false;
    }
    if (isSpecialL != nullptr) {
        *isSpecialL = false;
    }

    for (int segmentIndex = 0; segmentIndex < marker.slideSegmentKeys.size(); ++segmentIndex) {
        const QString& segmentKey = marker.slideSegmentKeys.at(segmentIndex);
        const QJsonObject entry = slides.value(segmentKey).toObject();
        if (entry.isEmpty()) {
            return false;
        }

        QVector<QStringList> segmentSequence =
            loadPadAreaSequence(entry.value(QStringLiteral("judge_sequence")).toArray());
        if (segmentSequence.isEmpty()) {
            return false;
        }

        if (segmentIndex == 0) {
            *judgeSequence = segmentSequence;
            *partition = QVector<bool>(segmentSequence.size(), false);
        } else {
            const bool sharedHead =
                !judgeSequence->isEmpty() && !segmentSequence.isEmpty() && judgeSequence->constLast() == segmentSequence.constFirst();
            if (sharedHead && !partition->isEmpty()) {
                (*partition)[partition->size() - 1] = true;
            }

            // Preserve the shared A-head at chained-segment boundaries.
            // MaiMuriDX treats entering the previous segment's terminal A area
            // as also judging the next segment's opening head.
            for (int areaIndex = 0; areaIndex < segmentSequence.size(); ++areaIndex) {
                judgeSequence->append(segmentSequence.at(areaIndex));
                partition->append(false);
            }
        }

        segmentEndAreaIndices->append(judgeSequence->size() - 1);
        if (segmentIndex == 0 && marker.slideSegmentKeys.size() == 1) {
            if (isL != nullptr) {
                *isL = entry.value(QStringLiteral("is_l")).toBool(false);
            }
            if (isSpecialL != nullptr) {
                *isSpecialL = entry.value(QStringLiteral("is_special_l")).toBool(false);
            }
        }
    }

    partition->append(false);
    return !judgeSequence->isEmpty();
}

bool buildRuntimeWifiJudgeSequence(
    const TimelineNoteMarker& marker,
    QVector<QVector<QStringList>>* laneJudgeSequence,
    bool* padCPassed,
    const MuriRenderOptions& renderOptions)
{
    if (laneJudgeSequence == nullptr) {
        return false;
    }

    const QJsonObject root = slideRuntimeRoot();
    const QJsonObject wifi = root.value(QStringLiteral("wifi")).toObject();
    const QJsonObject entry = wifi.value(marker.slideTrackKey).toObject();
    if (entry.isEmpty()) {
        return false;
    }

    *laneJudgeSequence =
        loadTriPadAreaSequence(entry.value(QStringLiteral("tri_judge_sequence")).toArray());
    if (laneJudgeSequence->size() != 3) {
        return false;
    }

    const int totalAreaNum = laneJudgeSequence->value(1).size();
    if (totalAreaNum <= 0) {
        return false;
    }
    for (const QVector<QStringList>& lane : *laneJudgeSequence) {
        if (lane.size() != totalAreaNum) {
            return false;
        }
    }

    if (padCPassed != nullptr) {
        *padCPassed = !renderOptions.wifiNeedC;
    }
    return true;
}

bool buildRuntimeSlideNoteState(const TimelineNoteMarker& marker, RuntimeSlideNoteState* note)
{
    if (note == nullptr || marker.type != QLatin1String("slide")) {
        return false;
    }

    QVector<QStringList> judgeSequence;
    QVector<bool> partition;
    QVector<int> segmentEndAreaIndices;
    bool isL = false;
    bool isSpecialL = false;
    if (!buildRuntimeSlideJudgeSequence(
            marker,
            &judgeSequence,
            &partition,
            &segmentEndAreaIndices,
            &isL,
            &isSpecialL)) {
        return false;
    }

    note->marker = &marker;
    note->markerKey = makeMarkerAnalysisKey(marker);
    note->line = marker.sourceLine;
    note->col = marker.sourceCol;
    note->availableSecond = marker.second - miacode::muri::kSlideLeadingSeconds;
    note->shootSecond = marker.slideTraceSecond;
    note->endSecond = marker.endSecond;
    note->judgeSequence = judgeSequence;
    note->partition = partition;
    note->segmentEndAreaIndices = segmentEndAreaIndices;
    note->areaHits = QVector<RuntimeJudgeHit>(judgeSequence.size());
    note->totalAreaNum = judgeSequence.size();
    note->isL = isL;
    note->isSpecialL = isSpecialL;

    const int lastSegmentIndex = qMin(marker.slideSegmentDurations.size(), marker.slideSegmentCriticalProportions.size()) - 1;
    if (lastSegmentIndex >= 0) {
        const double lastDurationSecond = qMax(0.0, marker.slideSegmentDurations.at(lastSegmentIndex));
        const double criticalProportion = marker.slideSegmentCriticalProportions.at(lastSegmentIndex);
        const double lastAreaDurationSecond = (1.0 - criticalProportion) * lastDurationSecond;
        note->criticalSecond = marker.endSecond - lastAreaDurationSecond;
        note->criticalDeltaSecond = slideCriticalDeltaSecond(lastAreaDurationSecond);
    } else {
        note->criticalSecond = marker.endSecond;
        note->criticalDeltaSecond = slideCriticalDeltaSecond(0.0);
    }

    return true;
}

bool buildRuntimeWifiNoteState(
    const TimelineNoteMarker& marker,
    RuntimeWifiNoteState* note,
    const MuriRenderOptions& renderOptions)
{
    if (note == nullptr || marker.type != QLatin1String("wifi")) {
        return false;
    }

    QVector<QVector<QStringList>> laneJudgeSequence;
    bool padCPassed = true;
    if (!buildRuntimeWifiJudgeSequence(marker, &laneJudgeSequence, &padCPassed, renderOptions)) {
        return false;
    }

    note->marker = &marker;
    note->markerKey = makeMarkerAnalysisKey(marker);
    note->line = marker.sourceLine;
    note->col = marker.sourceCol;
    note->availableSecond = marker.second - miacode::muri::kSlideLeadingSeconds;
    note->shootSecond = marker.slideTraceSecond;
    note->endSecond = marker.endSecond;
    note->laneJudgeSequence = laneJudgeSequence;
    note->totalAreaNum = laneJudgeSequence.value(1).size();
    note->areaHits = QVector<QVector<RuntimeJudgeHit>>(3, QVector<RuntimeJudgeHit>(note->totalAreaNum));
    note->laneProgressSeconds = QVector<QVector<double>>(3, QVector<double>(note->totalAreaNum + 1, -1.0));
    note->curAreaIdxes = QVector<int>(3, 0);
    note->pressingPads = QVector<QString>(3);
    note->laneFinished = QVector<bool>(3, false);
    note->padCPassed = padCPassed;

    const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
    const double lastAreaDurationSecond = (1.0 - marker.wifiCriticalProportion) * durationSecond;
    note->criticalSecond = marker.endSecond - lastAreaDurationSecond;
    note->criticalDeltaSecond = slideCriticalDeltaSecond(lastAreaDurationSecond);
    return true;
}

bool runtimeSlideCanSkipArea(const RuntimeSlideNoteState& note)
{
    if (note.curAreaIdx >= note.totalAreaNum - 1) {
        return false;
    }

    if (!note.pressingPad.isEmpty()) {
        return true;
    }

    if (note.segmentEndAreaIndices.size() == 1) {
        if (note.isSpecialL) {
            return note.curAreaIdx != 1 && note.curAreaIdx != 3;
        }
        if (note.isL) {
            return note.curAreaIdx != 1;
        }
    }

    if (note.totalAreaNum >= 4) {
        return true;
    }

    return note.curAreaIdx != (note.totalAreaNum - 2);
}

bool progressRuntimeSlideOnce(
    RuntimeSlideNoteState* note,
    double nowSecond,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || note->curAreaIdx >= note->totalAreaNum) {
        return false;
    }

    if (note->pressingPad.isEmpty()) {
        for (const QString& pad : note->judgeSequence.at(note->curAreaIdx)) {
            RuntimePadEvent cause;
            if (!padStateForTick(padStates, pad, &cause)) {
                continue;
            }

            note->pressingPad = pad;
            note->areaHits[note->curAreaIdx].judged = true;
            note->areaHits[note->curAreaIdx].second = nowSecond;
            note->areaHits[note->curAreaIdx].cause = cause;
            if (note->curAreaIdx >= note->totalAreaNum - 1) {
                ++note->curAreaIdx;
                note->judgeAction = cause;
            }
            if (note->partition.value(note->curAreaIdx)) {
                ++note->curSegmentIdx;
            }
            return true;
        }
    } else {
        if (!padStates.contains(note->pressingPad)) {
            note->pressingPad.clear();
            ++note->curAreaIdx;
            return true;
        }
    }

    if (runtimeSlideCanSkipArea(*note)) {
        const int skippedAreaIndex = note->curAreaIdx;
        for (const QString& pad : note->judgeSequence.at(note->curAreaIdx + 1)) {
            RuntimePadEvent cause;
            RuntimePadEvent padStateCause;
            const bool down = padStateForTick(padStates, pad, &padStateCause);
            const bool up = padReleasedThisTick(padUpThisTick, pad, &cause);
            if (!down && !up) {
                continue;
            }

            if (!note->areaHits[skippedAreaIndex].judged) {
                note->areaHits[skippedAreaIndex].judged = true;
                note->areaHits[skippedAreaIndex].skipped = true;
                note->areaHits[skippedAreaIndex].second = nowSecond;
            }
            note->pressingPad = pad;
            ++note->curAreaIdx;
            note->areaHits[note->curAreaIdx].judged = true;
            note->areaHits[note->curAreaIdx].second = nowSecond;
            note->areaHits[note->curAreaIdx].cause = down ? padStateCause : cause;
            if (note->curAreaIdx >= note->totalAreaNum - 1) {
                ++note->curAreaIdx;
                note->judgeAction = down ? padStateCause : RuntimePadEvent{};
            }
            if (note->partition.value(note->curAreaIdx)) {
                ++note->curSegmentIdx;
            }
            return true;
        }
    }

    return false;
}

void updateRuntimeSlideNote(
    RuntimeSlideNoteState* note,
    double nowSecond,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || note->judged || nowSecond + kPadTimeEpsilon < note->availableSecond) {
        return;
    }

    while (note->curAreaIdx < note->totalAreaNum) {
        if (!progressRuntimeSlideOnce(note, nowSecond, padStates, padUpThisTick)) {
            break;
        }
    }

    if (note->curAreaIdx >= note->totalAreaNum) {
        note->judged = true;
        note->judgeSecond = nowSecond;
        note->judgeBad =
            slideJudgeIsBad(nowSecond, note->criticalSecond, note->criticalDeltaSecond)
            && slideFinalAreaWasJudgedByOther(*note);
        return;
    }

    if (nowSecond > note->endSecond + miacode::muri::kSlideAvailableSeconds + kPadTimeEpsilon) {
        note->judged = true;
        note->judgeBad = true;
        note->judgeSecond = nowSecond;
    }
}

bool progressRuntimeWifiLaneOnce(
    RuntimeWifiNoteState* note,
    double nowSecond,
    int lane,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || lane < 0 || lane >= note->laneJudgeSequence.size()) {
        return false;
    }
    if (note->curAreaIdxes.value(lane) >= note->totalAreaNum) {
        return false;
    }

    if (note->pressingPads.at(lane).isEmpty()) {
        for (const QString& pad : note->laneJudgeSequence.at(lane).at(note->curAreaIdxes.at(lane))) {
            RuntimePadEvent cause;
            if (!padStateForTick(padStates, pad, &cause)) {
                continue;
            }

            note->pressingPads[lane] = pad;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].judged = true;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].second = nowSecond;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].cause = cause;
            if (note->curAreaIdxes.at(lane) >= note->totalAreaNum - 1) {
                ++note->curAreaIdxes[lane];
                note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
                note->laneFinished[lane] = true;
                note->judgeAction = cause;
            }
            return true;
        }
    } else {
        if (!padStates.contains(note->pressingPads.at(lane))) {
            note->pressingPads[lane].clear();
            ++note->curAreaIdxes[lane];
            note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
            return true;
        }
    }

    if (note->curAreaIdxes.at(lane) < note->totalAreaNum - 1) {
        for (const QString& pad : note->laneJudgeSequence.at(lane).at(note->curAreaIdxes.at(lane) + 1)) {
            RuntimePadEvent cause;
            RuntimePadEvent padStateCause;
            const bool down = padStateForTick(padStates, pad, &padStateCause);
            const bool up = padReleasedThisTick(padUpThisTick, pad, &cause);
            if (!down && !up) {
                continue;
            }

            note->pressingPads[lane] = pad;
            ++note->curAreaIdxes[lane];
            note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].judged = true;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].second = nowSecond;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].cause = down ? padStateCause : cause;
            if (note->curAreaIdxes.at(lane) >= note->totalAreaNum - 1) {
                ++note->curAreaIdxes[lane];
                note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
                note->laneFinished[lane] = true;
                note->judgeAction = down ? padStateCause : RuntimePadEvent{};
            }
            return true;
        }
    }

    return false;
}

void updateRuntimeWifiNote(
    RuntimeWifiNoteState* note,
    double nowSecond,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || note->judged || nowSecond + kPadTimeEpsilon < note->availableSecond) {
        return;
    }

    for (int lane = 0; lane < note->laneJudgeSequence.size(); ++lane) {
        while (note->curAreaIdxes.at(lane) < note->totalAreaNum) {
            if (!progressRuntimeWifiLaneOnce(note, nowSecond, lane, padStates, padUpThisTick)) {
                break;
            }
        }
    }

    if (!note->padCPassed && note->curAreaIdxes.value(1) > 0) {
        RuntimePadEvent cause;
        if (padReleasedThisTick(padUpThisTick, QStringLiteral("C"), &cause)) {
            note->padCPassed = true;
            note->padCSecond = nowSecond;
            if (note->areaHits.size() > 1 && note->areaHits.at(1).size() > 2) {
                note->areaHits[1][2].judged = true;
                note->areaHits[1][2].second = nowSecond;
                note->areaHits[1][2].cause = cause;
            }
        }
    }

    if (std::all_of(note->laneFinished.cbegin(), note->laneFinished.cend(), [](bool finished) { return finished; })
        && note->padCPassed) {
        note->judged = true;
        note->judgeSecond = nowSecond;
        note->judgeBad =
            wifiJudgeIsBad(nowSecond, note->criticalSecond, note->criticalDeltaSecond)
            && wifiFinalJudgeWasByOther(*note);
        return;
    }

    if (nowSecond > note->endSecond + miacode::muri::kSlideAvailableSeconds + kPadTimeEpsilon) {
        note->judged = true;
        note->judgeBad = true;
        note->judgeSecond = nowSecond;
    }
}

RuntimePadEvent runtimePadEventFromInterval(const PadWindowInterval& interval, int tick)
{
    RuntimePadEvent event;
    event.pad = interval.pad;
    event.tick = tick;
    event.second = tickToSecond(tick);
    event.sourceMarkerKey = interval.sourceMarkerKey;
    event.sourceType = interval.sourceType;
    event.sourceOrder = interval.sourceOrder;
    event.line = interval.line;
    event.col = interval.col;
    return event;
}

struct RuntimeTrailActionState {
    MuriActionTrail trail;
    int sourceOrder = -1;
    int line = 1;
    int col = 1;
};

QPointF interpolateTrailPoint(const QVector<QPointF>& points, double proportion)
{
    if (points.isEmpty()) {
        return QPointF();
    }
    if (points.size() == 1) {
        return points.constFirst();
    }

    const double clamped = qBound(0.0, proportion, 1.0);
    const double scaled = clamped * (points.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), points.size() - 2);
    const double t = scaled - index;
    const QPointF& a = points.at(index);
    const QPointF& b = points.at(index + 1);
    return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
}

bool sampleRuntimeActionTrail(const RuntimeTrailActionState& action, double nowSecond, QPointF* center)
{
    if (center == nullptr) {
        return false;
    }
    if (nowSecond + kPadTimeEpsilon < action.trail.startSecond
        || nowSecond > action.trail.endSecond + kPadTimeEpsilon
        || action.trail.points.isEmpty()) {
        return false;
    }

    double proportion = 0.0;
    if (action.trail.endSecond > action.trail.startSecond + kPadTimeEpsilon) {
        proportion =
            (nowSecond - action.trail.startSecond) / (action.trail.endSecond - action.trail.startSecond);
    }
    *center = interpolateTrailPoint(action.trail.points, proportion);
    return true;
}

int bestActiveIntervalIndex(const QVector<int>& activeIntervals, const QVector<PadWindowInterval>& intervals)
{
    int bestIndex = -1;
    for (int intervalIndex : activeIntervals) {
        if (intervalIndex < 0 || intervalIndex >= intervals.size()) {
            continue;
        }
        if (bestIndex < 0 || intervals.at(intervalIndex).sourceOrder > intervals.at(bestIndex).sourceOrder) {
            bestIndex = intervalIndex;
        }
    }
    return bestIndex;
}

QHash<QString, RuntimeSlideJudgeResult> simulateRuntimeSlideAndWifiJudgments(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex,
    const MuriRenderOptions& renderOptions)
{
    QVector<RuntimeSlideNoteState> slideNotes;
    QVector<RuntimeWifiNoteState> wifiNotes;
    slideNotes.reserve(noteMarkers.size());
    wifiNotes.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (marker.type == QLatin1String("slide")) {
            RuntimeSlideNoteState note;
            if (buildRuntimeSlideNoteState(marker, &note)) {
                slideNotes.append(note);
            }
            continue;
        }
        if (marker.type == QLatin1String("wifi")) {
            RuntimeWifiNoteState note;
            if (buildRuntimeWifiNoteState(marker, &note, renderOptions)) {
                wifiNotes.append(note);
            }
        }
    }

    QHash<QString, RuntimeSlideJudgeResult> results;
    if (slideNotes.isEmpty() && wifiNotes.isEmpty()) {
        return results;
    }

    const QVector<RuntimeHandAction> actions =
        buildRuntimeHandActions(
            noteMarkers,
            notes,
            touchGroups,
            touchGroupByChildNoteIndex,
            true);
    int maxActionTick = 0;
    for (const RuntimeHandAction& action : actions) {
        const int actionEndTick = judgeTickForPadActiveEnd(action.endSecond);
        maxActionTick = qMax(maxActionTick, actionEndTick);
    }

    static const QStringList allPads = []() {
        QStringList pads;
        pads.reserve(33);
        for (const QChar ring : {QLatin1Char('A'), QLatin1Char('B'), QLatin1Char('D'), QLatin1Char('E')}) {
            for (int lane = 1; lane <= 8; ++lane) {
                pads.append(QStringLiteral("%1%2").arg(ring).arg(lane));
            }
        }
        pads.append(QStringLiteral("C"));
        return pads;
    }();

    QHash<QString, RuntimePadEvent> previousPadStates;
    QVector<int> activeActionIndices;
    int actionPointer = 0;
    for (int tick = 0; tick <= maxActionTick; ++tick) {
        const double nowSecond = tickToSecond(tick);
        while (actionPointer < actions.size()) {
            const RuntimeHandAction& action = actions.at(actionPointer);
            if (nowSecond + kPadTimeEpsilon < action.startSecond) {
                break;
            }
            activeActionIndices.append(actionPointer);
            ++actionPointer;
        }

        const QVector<RuntimeTouchPoint> touchPoints =
            buildRuntimeTouchPoints(actions, activeActionIndices, nowSecond);
        QHash<QString, RuntimePadEvent> padStates;
        for (const RuntimeTouchPoint& touchPoint : touchPoints) {
            if (touchPoint.actionIndex < 0 || touchPoint.actionIndex >= actions.size()) {
                continue;
            }
            const RuntimeHandAction& action = actions.at(touchPoint.actionIndex);
            RuntimePadEvent event;
            event.tick = tick;
            event.second = nowSecond;
            event.sourceMarkerKey = action.markerKey;
            event.sourceType = action.sourceType;
            event.sourceOrder = action.order;
            event.line = action.line;
            event.col = action.col;

            for (const QString& pad : allPads) {
                if (pointDistance(touchPoint.center, miacode::muri::padCenter(pad))
                    > touchPoint.radius + miacode::muri::padRadius(pad) + kPadTimeEpsilon) {
                    continue;
                }

                event.pad = pad;
                padStates.insert(pad, event);
            }
        }

        QHash<QString, RuntimePadEvent> padUpThisTick;
        for (auto it = previousPadStates.constBegin(); it != previousPadStates.constEnd(); ++it) {
            if (!padStates.contains(it.key())) {
                padUpThisTick.insert(it.key(), it.value());
            }
        }

        for (RuntimeSlideNoteState& note : slideNotes) {
            updateRuntimeSlideNote(&note, nowSecond, padStates, padUpThisTick);
        }
        for (RuntimeWifiNoteState& note : wifiNotes) {
            updateRuntimeWifiNote(&note, nowSecond, padStates, padUpThisTick);
        }

        previousPadStates = padStates;

        QVector<int> finishedActionIndices;
        finishedActionIndices.reserve(activeActionIndices.size());
        for (int actionIndex : activeActionIndices) {
            if (actionIndex < 0 || actionIndex >= actions.size()) {
                continue;
            }
            if (nowSecond + kPadTimeEpsilon >= actions.at(actionIndex).endSecond) {
                finishedActionIndices.append(actionIndex);
            }
        }
        for (int actionIndex : finishedActionIndices) {
            activeActionIndices.removeAll(actionIndex);
        }
    }

    for (RuntimeSlideNoteState& note : slideNotes) {
        if (note.judged) {
            continue;
        }
        note.judged = true;
        note.judgeBad = false;
        note.judgeSecond = qMax(note.endSecond, note.criticalSecond);
    }
    for (RuntimeWifiNoteState& note : wifiNotes) {
        if (note.judged) {
            continue;
        }
        note.judged = true;
        note.judgeBad = false;
        note.judgeSecond = qMax(note.endSecond, note.criticalSecond);
    }

    for (const RuntimeSlideNoteState& note : slideNotes) {
        RuntimeSlideJudgeResult result;
        result.valid = true;
        result.isWifi = false;
        result.judgeBad = note.judgeBad;
        result.judgeSecond = note.judgeSecond;
        result.criticalSecond = note.criticalSecond;
        result.segmentCompletedSeconds.reserve(note.segmentEndAreaIndices.size());
        result.areaHits = note.areaHits;
        result.judgeSequence = note.judgeSequence;
        result.segmentEndAreaIndices = note.segmentEndAreaIndices;
        for (int segmentIndex = 0; segmentIndex < note.segmentEndAreaIndices.size(); ++segmentIndex) {
            const int areaIndex = note.segmentEndAreaIndices.at(segmentIndex);
            double completedSecond = (areaIndex >= 0 && areaIndex < note.areaHits.size())
                ? note.areaHits.at(areaIndex).second
                : -1.0;
            if (completedSecond < 0.0 && segmentIndex + 1 == note.segmentEndAreaIndices.size()) {
                completedSecond = note.judgeSecond;
            }
            result.segmentCompletedSeconds.append(completedSecond);
        }
        results.insert(note.markerKey, result);
    }
    for (const RuntimeWifiNoteState& note : wifiNotes) {
        RuntimeSlideJudgeResult result;
        result.valid = true;
        result.isWifi = true;
        result.judgeBad = note.judgeBad;
        result.judgeSecond = note.judgeSecond;
        result.criticalSecond = note.criticalSecond;
        result.segmentCompletedSeconds.append(note.judgeSecond);
        result.wifiLaneAreaHits = note.areaHits;
        result.wifiLaneJudgeSequence = note.laneJudgeSequence;
        result.wifiLaneProgressSeconds = note.laneProgressSeconds;
        result.wifiPadCSecond = note.padCSecond;
        results.insert(note.markerKey, result);
    }

    return results;
}

void applyRuntimeJudgeResultToState(const RuntimeSlideJudgeResult& result, MarkerMuriState* state)
{
    if (state == nullptr || !result.valid) {
        return;
    }

    if (result.isWifi) {
        if (!result.segmentCompletedSeconds.isEmpty()) {
            state->wifiCompletedSecond = result.segmentCompletedSeconds.constFirst();
        }
        QVector<QVector<QVector<MuriCheckpointState>>> runtimeLanes;
        runtimeLanes.reserve(result.wifiLaneAreaHits.size());
        for (int laneIndex = 0; laneIndex < result.wifiLaneAreaHits.size(); ++laneIndex) {
            QVector<QVector<MuriCheckpointState>> laneAreas;
            const QVector<RuntimeJudgeHit>& laneHits = result.wifiLaneAreaHits.at(laneIndex);
            const QVector<QStringList>& lanePads = result.wifiLaneJudgeSequence.value(laneIndex);
            laneAreas.reserve(laneHits.size());
            for (int areaIndex = 0; areaIndex < laneHits.size(); ++areaIndex) {
                QVector<MuriCheckpointState> checkpoints;
                const RuntimeJudgeHit& hit = laneHits.at(areaIndex);
                if (hit.judged) {
                    MuriCheckpointState checkpoint;
                    checkpoint.second = hit.second;
                    checkpoint.pads = lanePads.value(areaIndex);
                    checkpoint.skipped = hit.skipped;
                    checkpoint.causeMarkerKey = hit.cause.sourceMarkerKey;
                    checkpoint.causeType = hit.skipped
                        ? QStringLiteral("skipped")
                        : hit.cause.sourceType;
                    if (hit.cause.sourceMarkerKey.isEmpty()) {
                        checkpoint.cause = AreaJudgeCause::Unknown;
                    } else if (hit.cause.sourceMarkerKey == state->markerKey) {
                        checkpoint.cause = AreaJudgeCause::Self;
                    } else {
                        checkpoint.cause = AreaJudgeCause::Other;
                    }
                    checkpoints.append(checkpoint);
                }
                laneAreas.append(checkpoints);
            }
            runtimeLanes.append(laneAreas);
        }
        if (!runtimeLanes.isEmpty()) {
            state->wifiLaneAreas = runtimeLanes;
        }
        if (!result.wifiLaneProgressSeconds.isEmpty()) {
            state->wifiLaneProgressSeconds = result.wifiLaneProgressSeconds;
        }
        state->wifiPadCSecond = result.wifiPadCSecond;
    } else {
        int areaCursor = 0;
        for (int index = 0; index < result.segmentCompletedSeconds.size() && index < state->slideSegments.size(); ++index) {
            if (result.segmentCompletedSeconds.at(index) >= 0.0) {
                state->slideSegments[index].completedSecond = result.segmentCompletedSeconds.at(index);
            }

            QVector<QVector<MuriCheckpointState>> runtimeAreas;
            const int segmentEndAreaIndex = result.segmentEndAreaIndices.value(index, areaCursor - 1);
            while (areaCursor <= segmentEndAreaIndex && areaCursor < result.areaHits.size()) {
                QVector<MuriCheckpointState> checkpoints;
                const RuntimeJudgeHit& hit = result.areaHits.at(areaCursor);
                if (hit.judged) {
                    MuriCheckpointState checkpoint;
                    checkpoint.second = hit.second;
                    checkpoint.pads = result.judgeSequence.value(areaCursor);
                    checkpoint.skipped = hit.skipped;
                    checkpoint.causeMarkerKey = hit.cause.sourceMarkerKey;
                    checkpoint.causeType = hit.skipped
                        ? QStringLiteral("skipped")
                        : hit.cause.sourceType;
                    if (hit.cause.sourceMarkerKey.isEmpty()) {
                        checkpoint.cause = AreaJudgeCause::Unknown;
                    } else if (hit.cause.sourceMarkerKey == state->markerKey) {
                        checkpoint.cause = AreaJudgeCause::Self;
                    } else {
                        checkpoint.cause = AreaJudgeCause::Other;
                    }
                    checkpoints.append(checkpoint);
                }
                runtimeAreas.append(checkpoints);
                ++areaCursor;
            }
            if (!runtimeAreas.isEmpty()) {
                state->slideSegments[index].areaCheckpoints = runtimeAreas;
            }
        }
    }

    state->earlyCleared = result.judgeBad;
    state->flashSecond = result.judgeBad ? result.judgeSecond : -1.0;
}

struct EarlyJudgeCauseInfo {
    bool valid = false;
    double second = -1.0;
    QString markerKey;
    QString causeType;
};

void updateLatestEarlyJudgeCause(const MuriCheckpointState& checkpoint, EarlyJudgeCauseInfo* latestCause)
{
    if (latestCause == nullptr
        || checkpoint.second < 0.0
        || checkpoint.cause != AreaJudgeCause::Other
        || checkpoint.causeMarkerKey.isEmpty()) {
        return;
    }

    if (!latestCause->valid || checkpoint.second > latestCause->second + kPadTimeEpsilon) {
        latestCause->valid = true;
        latestCause->second = checkpoint.second;
        latestCause->markerKey = checkpoint.causeMarkerKey;
        latestCause->causeType = checkpoint.causeType;
    }
}

EarlyJudgeCauseInfo latestEarlyJudgeCauseForState(const MarkerMuriState& state)
{
    EarlyJudgeCauseInfo latestCause;
    for (const MuriSegmentState& segment : state.slideSegments) {
        for (const QVector<MuriCheckpointState>& area : segment.areaCheckpoints) {
            for (const MuriCheckpointState& checkpoint : area) {
                updateLatestEarlyJudgeCause(checkpoint, &latestCause);
            }
        }
    }
    for (const QVector<QVector<MuriCheckpointState>>& laneAreas : state.wifiLaneAreas) {
        for (const QVector<MuriCheckpointState>& area : laneAreas) {
            for (const MuriCheckpointState& checkpoint : area) {
                updateLatestEarlyJudgeCause(checkpoint, &latestCause);
            }
        }
    }
    return latestCause;
}

DiagnosticAnchor diagnosticAnchorForSlideJudge(
    const TimelineNoteMarker& marker,
    const MarkerMuriState& state,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    const EarlyJudgeCauseInfo latestCause = latestEarlyJudgeCauseForState(state);
    if (latestCause.valid) {
        const DiagnosticAnchor causeAnchor = diagnosticAnchorForMarkerKey(
            latestCause.markerKey,
            markerRefs,
            syntheticSlideHeadOwnerKeys);
        if (causeAnchor.valid) {
            return causeAnchor;
        }
    }
    return diagnosticAnchorFromMarker(marker);
}

QString formatSlideTooFastDetail(
    const TimelineNoteMarker& marker,
    const MarkerMuriState& state,
    const QHash<QString, QString>& markerConfigLabels,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    const EarlyJudgeCauseInfo latestCause = latestEarlyJudgeCauseForState(state);
    const double expectedSecond = marker.type == QLatin1String("wifi")
        ? state.wifiExpectedCompletedSecond
        : (state.slideSegments.isEmpty() ? -1.0 : state.slideSegments.constLast().expectedCompletedSecond);
    const double resolvedSecond = marker.type == QLatin1String("wifi")
        ? state.wifiCompletedSecond
        : (state.slideSegments.isEmpty() ? -1.0 : state.slideSegments.constLast().completedSecond);
    const double normalJudgeSecond = marker.endSecond >= 0.0 ? marker.endSecond : expectedSecond;
    const double gapMs = qMax(0.0, normalJudgeSecond - resolvedSecond) * 1000.0;
    const QString targetLabel = formatMarkerConfigLabel(marker);
    if (latestCause.valid) {
        const QString causeLabel = markerConfigLabelForSource(
            markerConfigLabels,
            syntheticSlideHeadOwnerKeys,
            latestCause.markerKey,
            latestCause.causeType);
        return QStringLiteral("%1 was early-judged by %2, gap %3 ms.")
            .arg(targetLabel, causeLabel, QString::number(gapMs, 'f', 1));
    }
    return QStringLiteral("%1 resolved outside its critical window, gap %2 ms.")
        .arg(targetLabel, QString::number(gapMs, 'f', 1));
}

bool judgeSimpleNoteOnPadDown(
    JudgeableSimpleNote* note,
    double nowSecond,
    const QString& pad,
    const RuntimePadEvent* cause)
{
    if (note == nullptr || note->judged) {
        return false;
    }

    const double deltaSecond = nowSecond - note->momentSecond;
    if (deltaSecond < -note->availableSeconds - kPadTimeEpsilon) {
        return false;
    }
    if (pad != note->pad) {
        return false;
    }

    note->judged = true;
    note->judgeSecond = nowSecond;
    note->judgeBad = qAbs(deltaSecond) > note->criticalSeconds + kPadTimeEpsilon;
    if (cause != nullptr) {
        note->cause = *cause;
    } else {
        note->cause = RuntimePadEvent{};
        note->cause.pad = pad;
        note->cause.second = nowSecond;
    }
    return true;
}

void updateSimpleNoteLateBad(JudgeableSimpleNote* note, double nowSecond)
{
    if (note == nullptr || note->judged) {
        return;
    }
    if (nowSecond - note->momentSecond <= note->availableSeconds + kPadTimeEpsilon) {
        return;
    }

    note->judged = true;
    note->judgeBad = true;
    note->lateBad = true;
    note->judgeSecond = nowSecond;
}

bool consumeRuntimePadEvent(
    QVector<JudgeableSimpleNote>* notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const RuntimeTopLevelNote& topLevel,
    double nowSecond,
    const RuntimePadEvent& event)
{
    if (notes == nullptr) {
        return false;
    }

    if (topLevel.touchGroupIndex >= 0) {
        if (topLevel.touchGroupIndex < 0 || topLevel.touchGroupIndex >= touchGroups.size()) {
            return false;
        }

        const RuntimeTouchGroup& group = touchGroups.at(topLevel.touchGroupIndex);
        for (int childNoteIndex : group.childNoteIndices) {
            if (childNoteIndex < 0 || childNoteIndex >= notes->size()) {
                continue;
            }
            if (judgeSimpleNoteOnPadDown(&(*notes)[childNoteIndex], nowSecond, event.pad, &event)) {
                return true;
            }
        }
        return false;
    }

    if (topLevel.simpleNoteIndex < 0 || topLevel.simpleNoteIndex >= notes->size()) {
        return false;
    }
    return judgeSimpleNoteOnPadDown(&(*notes)[topLevel.simpleNoteIndex], nowSecond, event.pad, &event);
}

void updateRuntimeTopLevelNote(
    QVector<JudgeableSimpleNote>* notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const RuntimeTopLevelNote& topLevel,
    double nowSecond)
{
    if (notes == nullptr) {
        return;
    }

    if (topLevel.touchGroupIndex >= 0) {
        if (topLevel.touchGroupIndex < 0 || topLevel.touchGroupIndex >= touchGroups.size()) {
            return;
        }

        const RuntimeTouchGroup& group = touchGroups.at(topLevel.touchGroupIndex);
        int judgedCount = 0;
        for (int childNoteIndex : group.childNoteIndices) {
            if (childNoteIndex < 0 || childNoteIndex >= notes->size()) {
                continue;
            }
            JudgeableSimpleNote& child = (*notes)[childNoteIndex];
            updateSimpleNoteLateBad(&child, nowSecond);
            if (child.judged) {
                ++judgedCount;
            }
        }

        if (judgedCount + kPadTimeEpsilon >= group.threshold) {
            for (int childNoteIndex : group.childNoteIndices) {
                if (childNoteIndex < 0 || childNoteIndex >= notes->size()) {
                    continue;
                }
                JudgeableSimpleNote& child = (*notes)[childNoteIndex];
                if (child.judged) {
                    continue;
                }
                RuntimePadEvent syntheticCause;
                syntheticCause.pad = child.pad;
                syntheticCause.second = nowSecond;
                syntheticCause.sourceMarkerKey = child.markerKey;
                syntheticCause.sourceType = child.type;
                syntheticCause.sourceOrder = child.order;
                syntheticCause.line = child.line;
                syntheticCause.col = child.col;
                judgeSimpleNoteOnPadDown(&child, nowSecond, child.pad, &syntheticCause);
            }
        }
        return;
    }

    if (topLevel.simpleNoteIndex < 0 || topLevel.simpleNoteIndex >= notes->size()) {
        return;
    }
    updateSimpleNoteLateBad(&(*notes)[topLevel.simpleNoteIndex], nowSecond);
}

void simulateSimpleNoteJudgments(
    QVector<JudgeableSimpleNote>* notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QVector<RuntimeTopLevelNote>& topLevelNotes,
    const QMap<int, QMap<QString, RuntimePadEvent>>& eventsByTick)
{
    if (notes == nullptr) {
        return;
    }

    QMap<int, QVector<int>> expiryBuckets = buildNoteExpiryBuckets(*notes);
    QMap<int, bool> timelineTicks;
    for (auto it = eventsByTick.constBegin(); it != eventsByTick.constEnd(); ++it) {
        timelineTicks.insert(it.key(), true);
    }
    for (auto it = expiryBuckets.constBegin(); it != expiryBuckets.constEnd(); ++it) {
        timelineTicks.insert(it.key(), true);
    }

    for (auto tickIt = timelineTicks.constBegin(); tickIt != timelineTicks.constEnd(); ++tickIt) {
        const int tick = tickIt.key();
        const double nowSecond = tickToSecond(tick);

        const QMap<QString, RuntimePadEvent> events = eventsByTick.value(tick);
        for (auto eventIt = events.constBegin(); eventIt != events.constEnd(); ++eventIt) {
            const RuntimePadEvent& event = eventIt.value();
            for (const RuntimeTopLevelNote& topLevel : topLevelNotes) {
                if (consumeRuntimePadEvent(notes, touchGroups, topLevel, nowSecond, event)) {
                    break;
                }
            }
        }

        for (const RuntimeTopLevelNote& topLevel : topLevelNotes) {
            updateRuntimeTopLevelNote(notes, touchGroups, topLevel, nowSecond);
        }
    }
}

QSet<QString> buildSameMomentPadOverlapKeys(const QVector<JudgeableSimpleNote>& notes)
{
    QHash<QString, QVector<QString>> markerKeysByMomentPad;
    markerKeysByMomentPad.reserve(notes.size());

    for (const JudgeableSimpleNote& note : notes) {
        const QString pad = normalizedPadToken(note.pad);
        if (pad.isEmpty()) {
            continue;
        }
        const QString lookupKey =
            QStringLiteral("%1|%2").arg(pad).arg(qRound64(note.momentSecond * 1000000.0));
        markerKeysByMomentPad[lookupKey].append(note.markerKey);
    }

    QSet<QString> overlapKeys;
    overlapKeys.reserve(notes.size());
    for (auto it = markerKeysByMomentPad.constBegin(); it != markerKeysByMomentPad.constEnd(); ++it) {
        if (it.value().size() <= 1) {
            continue;
        }
        for (const QString& markerKey : it.value()) {
            overlapKeys.insert(markerKey);
        }
    }
    return overlapKeys;
}

constexpr double kMultiTouchOverlapCenterEpsilon = 1e-3;
constexpr double kMultiTouchOverlapRadiusEpsilon = 1e-3;

struct MultiTouchActionCluster {
    RuntimeTouchPoint representativePoint;
    QVector<int> actionIndices;
    int handCount = 0;
};

bool shouldMergeMultiTouchCluster(
    const RuntimeTouchPoint& existingPoint,
    const RuntimeHandAction& existingAction,
    const RuntimeTouchPoint& candidatePoint,
    const RuntimeHandAction& candidateAction)
{
    if (existingAction.kind != RuntimeHandActionKind::Press
        || candidateAction.kind != RuntimeHandActionKind::Press) {
        return false;
    }
    return pointDistance(existingPoint.center, candidatePoint.center) <= kMultiTouchOverlapCenterEpsilon
        && qAbs(existingPoint.radius - candidatePoint.radius) <= kMultiTouchOverlapRadiusEpsilon;
}

QVector<MultiTouchActionCluster> buildMultiTouchActionClusters(
    const QVector<RuntimeTouchPoint>& touchPoints,
    const QVector<RuntimeHandAction>& actions)
{
    QVector<MultiTouchActionCluster> clusters;
    clusters.reserve(touchPoints.size());

    for (const RuntimeTouchPoint& touchPoint : touchPoints) {
        if (touchPoint.actionIndex < 0 || touchPoint.actionIndex >= actions.size()) {
            continue;
        }

        const RuntimeHandAction& action = actions.at(touchPoint.actionIndex);
        bool merged = false;
        for (MultiTouchActionCluster& cluster : clusters) {
            if (cluster.representativePoint.actionIndex < 0
                || cluster.representativePoint.actionIndex >= actions.size()) {
                continue;
            }
            const RuntimeHandAction& representativeAction =
                actions.at(cluster.representativePoint.actionIndex);
            if (!shouldMergeMultiTouchCluster(
                    cluster.representativePoint,
                    representativeAction,
                    touchPoint,
                    action)) {
                continue;
            }

            cluster.actionIndices.append(touchPoint.actionIndex);
            cluster.handCount = qMax(cluster.handCount, action.requireTwoHands ? 2 : 1);
            merged = true;
            break;
        }

        if (merged) {
            continue;
        }

        MultiTouchActionCluster cluster;
        cluster.representativePoint = touchPoint;
        cluster.actionIndices.append(touchPoint.actionIndex);
        cluster.handCount = action.requireTwoHands ? 2 : 1;
        clusters.append(cluster);
    }

    return clusters;
}

void collectSimpleNoteMultiTouchDiagnostics(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys,
    const MuriRenderOptions& renderOptions,
    QVector<MuriDiagnostic>* diagnostics,
    QSet<QString>* multiTouchMarkerKeys)
{
    if (diagnostics == nullptr) {
        return;
    }

    QVector<RuntimeHandAction> actions =
        buildRuntimeHandActions(
            noteMarkers,
            notes,
            touchGroups,
            touchGroupByChildNoteIndex,
            true);
    if (renderOptions.excludeTouchFromMultiTouch) {
        actions.erase(
            std::remove_if(actions.begin(), actions.end(), [](const RuntimeHandAction& action) {
                return sourceTypeIsTouchLike(action.sourceType);
            }),
            actions.end());
    }
    if (actions.isEmpty()) {
        return;
    }

    int maxTick = 0;
    for (const RuntimeHandAction& action : actions) {
        maxTick = qMax(maxTick, judgeTickForPadActiveEnd(action.endSecond));
    }
    QVector<int> activeActionIndices;
    QSet<QString> seenSignatures;
    QSet<quint64> previousMergedSlidePairs;
    int actionPointer = 0;
    for (int tick = 0; tick <= maxTick; ++tick) {
        const double nowSecond = tickToSecond(tick);
        while (actionPointer < actions.size()) {
            const RuntimeHandAction& action = actions.at(actionPointer);
            if (nowSecond + kPadTimeEpsilon < action.startSecond) {
                break;
            }
            activeActionIndices.append(actionPointer);
            ++actionPointer;
        }

        QSet<quint64> currentMergedSlidePairs;
        const QVector<RuntimeTouchPoint> touchPoints =
            buildRuntimeTouchPoints(
                actions,
                activeActionIndices,
                nowSecond,
                &previousMergedSlidePairs,
                &currentMergedSlidePairs);
        const QVector<MultiTouchActionCluster> touchClusters =
            buildMultiTouchActionClusters(touchPoints, actions);
        int handCount = 0;
        QVector<int> touchPointActionIndices;
        touchPointActionIndices.reserve(touchClusters.size());
        int nonTouchHandCount = 0;
        bool involvesTouch = false;
        for (const MultiTouchActionCluster& cluster : touchClusters) {
            if (cluster.representativePoint.actionIndex < 0
                || cluster.representativePoint.actionIndex >= actions.size()) {
                continue;
            }

            const int clusterHandCount = qMax(1, cluster.handCount);
            handCount += clusterHandCount;
            bool clusterHasTouchLike = false;
            bool clusterHasNonTouch = false;
            for (int actionIndex : cluster.actionIndices) {
                if (actionIndex < 0 || actionIndex >= actions.size()) {
                    continue;
                }
                if (sourceTypeIsTouchLike(actions.at(actionIndex).sourceType)) {
                    clusterHasTouchLike = true;
                } else {
                    clusterHasNonTouch = true;
                }
            }
            if (clusterHasTouchLike) {
                involvesTouch = true;
            }
            if (clusterHasNonTouch) {
                nonTouchHandCount += clusterHandCount;
            }
            touchPointActionIndices.append(cluster.representativePoint.actionIndex);
        }
        if (handCount > 2 && !touchPointActionIndices.isEmpty()) {
            std::sort(touchPointActionIndices.begin(), touchPointActionIndices.end(), [&actions](int a, int b) {
                const RuntimeHandAction& left = actions.at(a);
                const RuntimeHandAction& right = actions.at(b);
                if (left.order != right.order) {
                    return left.order < right.order;
                }
                if (left.line != right.line) {
                    return left.line < right.line;
                }
                return left.col < right.col;
            });

            QSet<QString> uniqueSources;
            QVector<int> causeActionIndices;
            causeActionIndices.reserve(touchPointActionIndices.size());
            for (int actionIndex : touchPointActionIndices) {
                const RuntimeHandAction& action = actions.at(actionIndex);
                if (uniqueSources.contains(action.markerKey)) {
                    continue;
                }
                uniqueSources.insert(action.markerKey);
                causeActionIndices.append(actionIndex);
                if (multiTouchMarkerKeys != nullptr) {
                    multiTouchMarkerKeys->insert(action.markerKey);
                }
            }

            QStringList signatureParts;
            QStringList detailParts;
            signatureParts.reserve(causeActionIndices.size());
            detailParts.reserve(causeActionIndices.size());
            for (int actionIndex : causeActionIndices) {
                const RuntimeHandAction& action = actions.at(actionIndex);
                signatureParts.append(action.markerKey);
                detailParts.append(
                    formatMultiTouchActionLabel(action, markerLookup, syntheticSlideHeadOwnerKeys));
            }
            const QString signature = signatureParts.join(QLatin1Char('|'));
            if (!seenSignatures.contains(signature)) {
                seenSignatures.insert(signature);

                int anchorActionIndex = causeActionIndices.constFirst();
                DiagnosticAnchor anchorInfo = diagnosticAnchorFromAction(actions.at(anchorActionIndex));
                for (int actionIndex : causeActionIndices) {
                    const DiagnosticAnchor candidateAnchor = diagnosticAnchorFromAction(actions.at(actionIndex));
                    if (diagnosticAnchorComesBefore(candidateAnchor, anchorInfo)) {
                        anchorInfo = candidateAnchor;
                        anchorActionIndex = actionIndex;
                    }
                }

                MuriDiagnostic diagnostic;
                diagnostic.kind = MuriKind::MultiTouch;
                diagnostic.second = nowSecond;
                diagnostic.anchorSecond = anchorInfo.valid ? anchorInfo.second : diagnostic.second;
                diagnostic.line = anchorInfo.valid ? anchorInfo.line : 1;
                diagnostic.col = anchorInfo.valid ? anchorInfo.col : 1;
                diagnostic.markerKey = actions.at(anchorActionIndex).markerKey;
                diagnostic.title = muriKindDisplayName(MuriKind::MultiTouch, true);
                diagnostic.alertLevel = (involvesTouch && nonTouchHandCount <= 2)
                    ? MuriAlertLevel::Warning
                    : MuriAlertLevel::Muri;
                diagnostic.detail =
                    QStringLiteral("Multi-touch formed by %1.").arg(detailParts.join(QStringLiteral(", ")));
                diagnostics->append(diagnostic);
            }
        }

        QVector<int> finishedActionIndices;
        finishedActionIndices.reserve(activeActionIndices.size());
        for (int actionIndex : activeActionIndices) {
            if (actionIndex < 0 || actionIndex >= actions.size()) {
                continue;
            }
            if (nowSecond + kPadTimeEpsilon >= actions.at(actionIndex).endSecond) {
                finishedActionIndices.append(actionIndex);
            }
        }
        for (int actionIndex : finishedActionIndices) {
            activeActionIndices.removeAll(actionIndex);
        }
        previousMergedSlidePairs.swap(currentMergedSlidePairs);
    }
}

bool runtimeCauseIsSlideHeadTap(
    const JudgeableSimpleNote& note,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup)
{
    if (note.cause.extraPadDown || note.cause.sourceMarkerKey.startsWith(QStringLiteral("slide_head_star|"))) {
        return true;
    }
    if (note.pad.isEmpty()) {
        return false;
    }
    const auto it = markerLookup.constFind(note.cause.sourceMarkerKey);
    if (it == markerLookup.constEnd() || it.value() == nullptr || !isSlideLike(*it.value())) {
        return false;
    }
    return normalizedPadToken(note.pad) == slideHeadPad(it.value()->lane);
}

void collectSimpleNoteRuntimeDiagnostics(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const MuriRenderOptions& renderOptions,
    QVector<MuriDiagnostic>* diagnostics,
    QVector<MuriJudgeSpriteEvent>* judgeSpriteEvents,
    double staticTapOnSlideThresholdSeconds)
{
    if (diagnostics == nullptr) {
        return;
    }

    const double normalizedStaticTapOnSlideThresholdSeconds = qBound(
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdMinMs) / 1000.0,
        staticTapOnSlideThresholdSeconds,
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdMaxMs) / 1000.0);
    const QHash<QString, MarkerSourceRef> markerRefs = buildMarkerSourceRefs(noteMarkers);
    const QHash<QString, const TimelineNoteMarker*> markerLookup = buildMarkerLookup(noteMarkers);
    const QHash<QString, QString> markerConfigLabels = buildMarkerConfigLabels(noteMarkers);
    const QHash<QString, QString> syntheticSlideHeadOwnerKeys = buildSyntheticSlideHeadOwnerKeys(noteMarkers);
    const QSet<QString> slideKeysWithTapOnSlideHead = buildSlideKeysWithTapOnSlideHead(noteMarkers);
    QHash<QString, int> noteIndexByMarkerKey;
    QVector<JudgeableSimpleNote> notes = buildJudgeableSimpleNotes(noteMarkers, markerRefs, &noteIndexByMarkerKey);
    QHash<int, int> touchGroupByChildNoteIndex;
    const QVector<RuntimeTouchGroup> touchGroups =
        buildRuntimeTouchGroups(noteMarkers, noteIndexByMarkerKey, &touchGroupByChildNoteIndex);
    const QVector<RuntimeTopLevelNote> topLevelNotes =
        buildRuntimeTopLevelNotes(notes, touchGroups, touchGroupByChildNoteIndex);
    const QVector<MuriPadWindow> runtimePadWindows =
        buildRuntimePadWindows(noteMarkers, notes, noteIndexByMarkerKey, touchGroups, touchGroupByChildNoteIndex);
    QSet<QString> multiTouchMarkerKeys;
    collectSimpleNoteMultiTouchDiagnostics(
        noteMarkers,
        notes,
        touchGroups,
        touchGroupByChildNoteIndex,
        markerLookup,
        syntheticSlideHeadOwnerKeys,
        renderOptions,
        diagnostics,
        &multiTouchMarkerKeys);
    simulateSimpleNoteJudgments(
        &notes,
        touchGroups,
        topLevelNotes,
        buildRuntimePadEvents(noteMarkers, runtimePadWindows, markerRefs));
    const QSet<QString> overlapRenderKeys = buildSameMomentPadOverlapKeys(notes);
    QHash<QString, MuriSimpleJudgeEffect> simpleJudgeEffects;
    simpleJudgeEffects.reserve(notes.size());
    QSet<QString> suppressJudgeSpriteKeys;
    QSet<QString> forceRenderJudgeSpriteKeys;
    suppressJudgeSpriteKeys.reserve(multiTouchMarkerKeys.size());
    forceRenderJudgeSpriteKeys.reserve(notes.size());
    for (const QString& markerKey : multiTouchMarkerKeys) {
        if (!overlapRenderKeys.contains(markerKey)) {
            suppressJudgeSpriteKeys.insert(markerKey);
        }
    }

    for (const RuntimeTopLevelNote& topLevel : topLevelNotes) {
        if (topLevel.touchGroupIndex >= 0) {
            continue;
        }
        if (topLevel.simpleNoteIndex < 0 || topLevel.simpleNoteIndex >= notes.size()) {
            continue;
        }

        const JudgeableSimpleNote& note = notes.at(topLevel.simpleNoteIndex);
        if (!note.judged || !note.judgeBad || note.marker == nullptr) {
            if (!note.judged || !note.judgeBad) {
                continue;
            }
        }

        if (note.judgeSecond + kPadTimeEpsilon < note.momentSecond && !note.cause.sourceMarkerKey.isEmpty()) {
            const bool slideHeadTap = runtimeCauseIsSlideHeadTap(note, markerLookup);
            const QString sourceOwnerKey =
                slideStartOwnerKeyForSource(note.cause.sourceMarkerKey, syntheticSlideHeadOwnerKeys);
            const bool hasTapOnSlideHead = slideKeysWithTapOnSlideHead.contains(sourceOwnerKey);
            const QString causeConfig = markerConfigLabelForSource(
                markerConfigLabels,
                syntheticSlideHeadOwnerKeys,
                note.cause.sourceMarkerKey,
                note.cause.sourceType);
            const double gapSecond = slideHeadTap
                ? qMax(
                      0.0,
                      note.momentSecond
                          - slideStartSecondForSource(
                              note.cause,
                              markerLookup,
                              syntheticSlideHeadOwnerKeys))
                : qMax(0.0, note.momentSecond - note.judgeSecond);
            const double gapMs = gapSecond * 1000.0;
            const MuriAlertLevel baseAlertLevel = slideHeadTap
                ? slideHeadTapAlertLevel(hasTapOnSlideHead, gapSecond)
                : tapOnSlideAlertLevel(gapSecond, normalizedStaticTapOnSlideThresholdSeconds);
            const MuriAlertLevel alertLevel =
                downgradeProtectedSimpleNoteAlertLevel(baseAlertLevel, note.hasProtection);
            const QString affectedTarget = simpleNoteTargetLabel(note);
            const QString detail = slideHeadTap
                ? slideHeadTapDetailText(
                      hasTapOnSlideHead, alertLevel, causeConfig, affectedTarget, gapMs)
                : tapOnSlideDetailText(alertLevel, causeConfig, affectedTarget, gapMs);
            addSimpleNoteDiagnostic(
                diagnostics,
                slideHeadTap ? MuriKind::SlideHeadTap : MuriKind::TapOnSlide,
                alertLevel,
                note.judgeSecond,
                note,
                detail,
                diagnosticAnchorFromNote(note));
            forceRenderJudgeSpriteKeys.insert(note.markerKey);
            if (alertLevel == MuriAlertLevel::Warning) {
                simpleJudgeEffects.insert(note.markerKey, MuriSimpleJudgeEffect::Perfect);
            }
            continue;
        }

        // Real tap-slide pairs are judged off the slide start in MaiMuriDX.
        // Keep them out of late-overlap reporting until slide runtime is fully ported.
        if (note.slideHead) {
            continue;
        }

        const QString affectedConfig = markerConfigLabelForKey(markerConfigLabels, note.markerKey, note.type);
        QString overlapDetail = QStringLiteral("%1 formed overlap at the same position.").arg(affectedConfig);
        if (!note.cause.sourceMarkerKey.isEmpty()) {
            const QString causeConfig = markerConfigLabelForSource(
                markerConfigLabels,
                syntheticSlideHeadOwnerKeys,
                note.cause.sourceMarkerKey,
                note.cause.sourceType);
            if (!causeConfig.isEmpty() && causeConfig != affectedConfig) {
                overlapDetail =
                    QStringLiteral("%1 and same-position %2 formed overlap.").arg(affectedConfig, causeConfig);
            }
        }
        const DiagnosticAnchor anchor = earlierDiagnosticAnchor(
            diagnosticAnchorFromNote(note),
            diagnosticAnchorForCause(note.cause, markerRefs, syntheticSlideHeadOwnerKeys));
        addSimpleNoteDiagnostic(
            diagnostics,
            MuriKind::Overlap,
            MuriAlertLevel::Muri,
            note.judgeSecond,
            note,
            overlapDetail,
            anchor);
        forceRenderJudgeSpriteKeys.insert(note.markerKey);
    }

    if (judgeSpriteEvents != nullptr) {
        judgeSpriteEvents->reserve(judgeSpriteEvents->size() + notes.size());
        for (const JudgeableSimpleNote& note : notes) {
            if (suppressJudgeSpriteKeys.contains(note.markerKey)
                && !forceRenderJudgeSpriteKeys.contains(note.markerKey)) {
                continue;
            }
            addSimpleJudgeSpriteEvent(
                judgeSpriteEvents,
                note,
                simpleJudgeEffects.value(note.markerKey, MuriSimpleJudgeEffect::Good));
        }
    }
}

}  // namespace

MuriAnalysisReport MuriAnalyzer::analyze(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const MuriRenderOptions& renderOptions)
{
    return analyze(
        noteMarkers,
        renderOptions,
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0);
}

MuriAnalysisReport MuriAnalyzer::analyze(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const MuriRenderOptions& renderOptions,
    double staticTapOnSlideThresholdSeconds)
{
    MuriAnalysisReport report;
    QStringList signatureParts;
    signatureParts.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        signatureParts.append(makeMarkerAnalysisKey(marker));
    }
    report.sourceSignature = signatureParts.join(QLatin1Char(';'));

    buildOverlayActions(noteMarkers, &report.padWindows, &report.actionTrails);
    collectSimpleNoteRuntimeDiagnostics(
        noteMarkers,
        renderOptions,
        &report.diagnostics,
        &report.judgeSpriteEvents,
        staticTapOnSlideThresholdSeconds);

    const QHash<QString, MarkerSourceRef> markerRefs = buildMarkerSourceRefs(noteMarkers);
    const QHash<QString, QString> markerConfigLabels = buildMarkerConfigLabels(noteMarkers);
    const QHash<QString, QString> syntheticSlideHeadOwnerKeys = buildSyntheticSlideHeadOwnerKeys(noteMarkers);
    QHash<QString, int> noteIndexByMarkerKey;
    const QVector<JudgeableSimpleNote> runtimeNotes =
        buildJudgeableSimpleNotes(noteMarkers, markerRefs, &noteIndexByMarkerKey);
    QHash<int, int> touchGroupByChildNoteIndex;
    const QVector<RuntimeTouchGroup> runtimeTouchGroups =
        buildRuntimeTouchGroups(noteMarkers, noteIndexByMarkerKey, &touchGroupByChildNoteIndex);
    const QHash<QString, RuntimeSlideJudgeResult> runtimeSlideResults =
        simulateRuntimeSlideAndWifiJudgments(
            noteMarkers,
            runtimeNotes,
            runtimeTouchGroups,
            touchGroupByChildNoteIndex,
            renderOptions);

    std::sort(report.padWindows.begin(), report.padWindows.end(), [](const MuriPadWindow& a, const MuriPadWindow& b) {
        if (!qFuzzyCompare(a.startSecond + 1.0, b.startSecond + 1.0)) {
            return a.startSecond < b.startSecond;
        }
        if (!qFuzzyCompare(a.endSecond + 1.0, b.endSecond + 1.0)) {
            return a.endSecond < b.endSecond;
        }
        if (a.pad != b.pad) {
            return a.pad < b.pad;
        }
        return a.sourceMarkerKey < b.sourceMarkerKey;
    });

    QHash<QString, QVector<int>> windowsByPad;
    for (int index = 0; index < report.padWindows.size(); ++index) {
        windowsByPad[report.padWindows.at(index).pad].append(index);
    }

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString markerKey = makeMarkerAnalysisKey(marker);
        if (marker.type == QLatin1String("slide")) {
            MarkerMuriState state = buildSlideState(marker, windowsByPad, report.padWindows);
            const RuntimeSlideJudgeResult runtimeResult = runtimeSlideResults.value(markerKey);
            applyRuntimeJudgeResultToState(runtimeResult, &state);
            if (runtimeResult.valid) {
                if (runtimeResult.judgeBad) {
                    addDiagnostic(
                        &report.diagnostics,
                        MuriKind::SlideTooFast,
                        MuriAlertLevel::Muri,
                        runtimeResult.judgeSecond,
                        marker,
                        markerKey,
                        formatSlideTooFastDetail(
                            marker,
                            state,
                            markerConfigLabels,
                            syntheticSlideHeadOwnerKeys),
                        diagnosticAnchorForSlideJudge(
                            marker,
                            state,
                            markerRefs,
                            syntheticSlideHeadOwnerKeys));
                    appendSlideJudgeSpriteEvent(&report.judgeSpriteEvents, marker, runtimeResult.judgeSecond);
                }
            } else if (!state.slideSegments.isEmpty()) {
                const MuriSegmentState& lastSegment = state.slideSegments.constLast();
                const int lastSegmentIndex = state.slideSegments.size() - 1;
                const double lastDurationSecond = lastSegmentIndex < marker.slideSegmentDurations.size()
                    ? qMax(0.0, marker.slideSegmentDurations.at(lastSegmentIndex))
                    : qMax(0.0, marker.endSecond - marker.slideTraceSecond);
                const double criticalProportion = lastSegmentIndex < marker.slideSegmentCriticalProportions.size()
                    ? marker.slideSegmentCriticalProportions.at(lastSegmentIndex)
                    : 1.0;
                const double criticalDeltaSecond = slideCriticalDeltaSecond(
                    (1.0 - criticalProportion) * lastDurationSecond
                );
                if (slideJudgeIsBad(lastSegment.completedSecond, lastSegment.criticalSecond, criticalDeltaSecond)) {
                    addDiagnostic(
                        &report.diagnostics,
                        MuriKind::SlideTooFast,
                        MuriAlertLevel::Muri,
                        lastSegment.completedSecond,
                        marker,
                        markerKey,
                        formatSlideTooFastDetail(
                            marker,
                            state,
                            markerConfigLabels,
                            syntheticSlideHeadOwnerKeys),
                        diagnosticAnchorForSlideJudge(
                            marker,
                            state,
                            markerRefs,
                            syntheticSlideHeadOwnerKeys)
                    );
                    appendSlideJudgeSpriteEvent(&report.judgeSpriteEvents, marker, lastSegment.completedSecond);
                }
            }
            report.markerStates.insert(markerKey, state);
            continue;
        }

        if (marker.type == QLatin1String("wifi")) {
            MarkerMuriState state = buildWifiState(marker);
            const RuntimeSlideJudgeResult runtimeResult = runtimeSlideResults.value(markerKey);
            applyRuntimeJudgeResultToState(runtimeResult, &state);
            if (runtimeResult.valid) {
                if (runtimeResult.judgeBad) {
                    addDiagnostic(
                        &report.diagnostics,
                        MuriKind::SlideTooFast,
                        MuriAlertLevel::Muri,
                        runtimeResult.judgeSecond,
                        marker,
                        markerKey,
                        formatSlideTooFastDetail(
                            marker,
                            state,
                            markerConfigLabels,
                            syntheticSlideHeadOwnerKeys),
                        diagnosticAnchorForSlideJudge(
                            marker,
                            state,
                            markerRefs,
                            syntheticSlideHeadOwnerKeys));
                    appendSlideJudgeSpriteEvent(&report.judgeSpriteEvents, marker, runtimeResult.judgeSecond);
                }
            } else {
                const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
                const double criticalDeltaSecond = slideCriticalDeltaSecond(
                    (1.0 - marker.wifiCriticalProportion) * durationSecond
                );
                if (wifiJudgeIsBad(state.wifiCompletedSecond, state.wifiCriticalSecond, criticalDeltaSecond)) {
                    addDiagnostic(
                        &report.diagnostics,
                        MuriKind::SlideTooFast,
                        MuriAlertLevel::Muri,
                        state.wifiCompletedSecond,
                        marker,
                        markerKey,
                        formatSlideTooFastDetail(
                            marker,
                            state,
                            markerConfigLabels,
                            syntheticSlideHeadOwnerKeys),
                        diagnosticAnchorForSlideJudge(
                            marker,
                            state,
                            markerRefs,
                            syntheticSlideHeadOwnerKeys)
                    );
                    appendSlideJudgeSpriteEvent(&report.judgeSpriteEvents, marker, state.wifiCompletedSecond);
                }
            }
            report.markerStates.insert(markerKey, state);
        }
    }

    std::sort(report.diagnostics.begin(), report.diagnostics.end(), [](const MuriDiagnostic& a, const MuriDiagnostic& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second < b.second;
        }
        if (a.line != b.line) {
            return a.line < b.line;
        }
        return a.col < b.col;
    });
    report.diagnostics = dedupeDenseOverlapDiagnostics(report.diagnostics);
    std::sort(report.judgeSpriteEvents.begin(), report.judgeSpriteEvents.end(), [](const MuriJudgeSpriteEvent& a, const MuriJudgeSpriteEvent& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second < b.second;
        }
        if (!qFuzzyCompare(a.spawnSecond + 1.0, b.spawnSecond + 1.0)) {
            return a.spawnSecond < b.spawnSecond;
        }
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        if (a.pad != b.pad) {
            return a.pad < b.pad;
        }
        return a.markerKey < b.markerKey;
    });
    return report;
}
