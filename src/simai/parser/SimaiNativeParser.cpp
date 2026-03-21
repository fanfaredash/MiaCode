#include "SimaiNativeParser.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QtMath>

#include <algorithm>

namespace {

constexpr double kDefaultBpm = 120.0;
constexpr int kDefaultBeats = 4;
constexpr double kTouchCanvasCenter = 270.0;
constexpr double kLaneAngleBaseDegrees = -67.5;
constexpr double kLaneAngleStepDegrees = 45.0;
constexpr double kOuterLaneRadius = 430.0;
constexpr double kTouchDistanceA = 540.0 * 410.0 / 1080.0;
constexpr double kTouchDistanceB = 540.0 * 220.0 / 1080.0;
constexpr double kTouchDistanceD = 540.0 * 440.0 / 1080.0;
constexpr double kTouchDistanceE = 540.0 * 310.0 / 1080.0;
constexpr int kPathSampleCount = 33;
constexpr double kJudgeTps = 180.0;
constexpr double kTapOnSlideThresholdSeconds = 1.0 / kJudgeTps;
constexpr double kTouchOnSlideThresholdSeconds = 24.0 / kJudgeTps;

struct ParseState {
    double bpm = kDefaultBpm;
    int beats = kDefaultBeats;
    double second = 0.0;
    int nextEachGroupId = 0;
    int lastBeatSourceLine = -1;
    bool strictMode = false;
    SimaiNativeParseResult result;
};

void loadSamplePath(const QJsonArray& samples, QVector<QPointF>* points, QVector<double>* angles);
QVector<MuriPadTimeEntry> loadPadEnterTimes(const QJsonArray& padEnterTimes);

bool isDigitLane(QChar ch)
{
    return ch >= QChar('1') && ch <= QChar('8');
}

bool isTouchPrefix(const QString& token)
{
    if (!token.isEmpty() && token.at(0).toUpper() == QChar('C')) {
        return true;
    }
    if (token.size() < 2) {
        return false;
    }
    const QChar head = token.at(0).toUpper();
    return (head == QChar('A') || head == QChar('B') || head == QChar('D') || head == QChar('E')) && isDigitLane(token.at(1));
}

QString touchPadForToken(const QString& token)
{
    if (token.isEmpty()) {
        return QString();
    }
    const QChar head = token.at(0).toUpper();
    if (head == QChar('C')) {
        return QStringLiteral("C");
    }
    if (token.size() < 2 || !isDigitLane(token.at(1))) {
        return QString();
    }
    return QString(head) + token.at(1);
}

int touchPrefixLength(const QString& token)
{
    if (token.isEmpty()) {
        return 0;
    }
    const QChar head = token.at(0).toUpper();
    if (head == QChar('C')) {
        return 1;
    }
    if (token.size() >= 2
        && (head == QChar('A') || head == QChar('B') || head == QChar('D') || head == QChar('E'))
        && isDigitLane(token.at(1))) {
        return 2;
    }
    return 0;
}

bool parseTouchSuffix(
    const QString& token,
    QString* durationSignature,
    bool* hasHold,
    bool* hasFirework,
    bool* hasBreak,
    QString* errorMessage)
{
    if (durationSignature == nullptr
        || hasHold == nullptr
        || hasFirework == nullptr
        || hasBreak == nullptr
        || errorMessage == nullptr) {
        return false;
    }
    durationSignature->clear();
    *hasHold = false;
    *hasFirework = false;
    *hasBreak = false;
    errorMessage->clear();

    const int prefixLength = touchPrefixLength(token);
    if (prefixLength <= 0 || prefixLength > token.size()) {
        *errorMessage = QString("Invalid note: %1").arg(token);
        return false;
    }

    const QString suffix = token.mid(prefixLength);
    const int openBracket = suffix.indexOf('[');
    const int closeBracket = suffix.indexOf(']');
    if (openBracket < 0 && closeBracket >= 0) {
        *errorMessage = QString("Invalid touch token: %1").arg(token);
        return false;
    }

    QString modifierPart = suffix;
    if (openBracket >= 0) {
        if (closeBracket <= openBracket) {
            *errorMessage = QString("Invalid touch-hold duration: %1").arg(token);
            return false;
        }
        if (closeBracket != suffix.size() - 1) {
            *errorMessage = QString("Invalid touch token: %1").arg(token);
            return false;
        }
        if (suffix.indexOf('[', openBracket + 1) >= 0 || suffix.indexOf(']', closeBracket + 1) >= 0) {
            *errorMessage = QString("Invalid touch token: %1").arg(token);
            return false;
        }
        modifierPart = suffix.left(openBracket);
        *durationSignature = suffix.mid(openBracket + 1, closeBracket - openBracket - 1);
    }

    for (QChar ch : modifierPart) {
        const QChar lower = ch.toLower();
        if (lower == QChar('h')) {
            *hasHold = true;
        } else if (lower == QChar('f')) {
            *hasFirework = true;
        } else if (lower == QChar('b')) {
            *hasBreak = true;
        } else if (lower == QChar('x')) {
            // Currently accepted for compatibility; touch ex is not bound yet.
        } else if (!ch.isSpace()) {
            *errorMessage = QString("Invalid touch modifier: %1").arg(token);
            return false;
        }
    }

    if (openBracket >= 0 && !*hasHold) {
        *errorMessage = QString("Touch duration requires 'h': %1").arg(token);
        return false;
    }
    if (*hasHold && openBracket < 0) {
        *errorMessage = QString("Invalid touch-hold duration: %1").arg(token);
        return false;
    }
    if (*hasHold && durationSignature->isEmpty()) {
        *errorMessage = QString("Invalid touch-hold duration: %1").arg(token);
        return false;
    }

    return true;
}

double noteStepSeconds(double bpm, int beats)
{
    const double clampedBpm = bpm > 0.0 ? bpm : kDefaultBpm;
    const int clampedBeats = qMax(1, beats);
    return 240.0 / (clampedBpm * clampedBeats);
}

double polylineLength(const QVector<QPointF>& points)
{
    if (points.size() < 2) {
        return 0.0;
    }
    double length = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        length += QLineF(points.at(i - 1), points.at(i)).length();
    }
    return length;
}

QPointF polarPoint(double radius, int lane)
{
    const double angleDeg = kLaneAngleBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
    const double angleRad = qDegreesToRadians(angleDeg);
    return QPointF(std::cos(angleRad) * radius, std::sin(angleRad) * radius);
}

double touchAngleDegrees(QChar ring, int lane)
{
    if (ring == QChar('D') || ring == QChar('E')) {
        return -90.0 + (lane - 1) * kLaneAngleStepDegrees;
    }
    return kLaneAngleBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
}

QPointF touchPointForToken(const QString& token)
{
    if (token.isEmpty()) {
        return QPointF();
    }
    const QChar head = token.at(0).toUpper();
    if (head == QChar('C')) {
        return QPointF(kTouchCanvasCenter, kTouchCanvasCenter);
    }
    if (token.size() < 2 || !isDigitLane(token.at(1))) {
        return QPointF();
    }
    const int lane = token.at(1).digitValue();
    if (lane < 1 || lane > 8) {
        return QPointF();
    }
    double distance = 0.0;
    switch (head.toLatin1()) {
    case 'A':
        distance = kTouchDistanceA;
        break;
    case 'B':
        distance = kTouchDistanceB;
        break;
    case 'D':
        distance = kTouchDistanceD;
        break;
    case 'E':
        distance = kTouchDistanceE;
        break;
    default:
        return QPointF();
    }

    const double angleRad = qDegreesToRadians(touchAngleDegrees(head, lane));
    return QPointF(
        kTouchCanvasCenter + std::cos(angleRad) * distance,
        kTouchCanvasCenter + std::sin(angleRad) * distance
    );
}

double parseHoldDurationSignature(const QString& signature, double bpm, bool* ok)
{
    *ok = false;
    if (signature.isEmpty()) {
        return 0.0;
    }

    const QStringList hashParts = signature.split('#');
    if (hashParts.size() > 2) {
        return 0.0;
    }

    const auto parseBeatFraction = [](const QString& text, double useBpm, bool* localOk) -> double {
        *localOk = false;
        const int colon = text.indexOf(':');
        if (colon < 0) {
            return 0.0;
        }
        bool beatsOk = false;
        bool numOk = false;
        const int beats = text.left(colon).toInt(&beatsOk);
        const int num = text.mid(colon + 1).toInt(&numOk);
        if (!beatsOk || !numOk || beats <= 0 || useBpm <= 0.0) {
            return 0.0;
        }
        *localOk = true;
        return 240.0 * static_cast<double>(num) / (useBpm * static_cast<double>(beats));
    };

    if (hashParts.size() == 2) {
        if (hashParts.at(0).isEmpty()) {
            bool secondsOk = false;
            const double seconds = hashParts.at(1).toDouble(&secondsOk);
            if (!secondsOk) {
                return 0.0;
            }
            *ok = true;
            return qMax(0.0, seconds);
        }

        bool tempBpmOk = false;
        const double tempBpm = hashParts.at(0).toDouble(&tempBpmOk);
        if (!tempBpmOk) {
            return 0.0;
        }

        bool localOk = false;
        const double beatsDuration = parseBeatFraction(hashParts.at(1), tempBpm, &localOk);
        if (localOk) {
            *ok = true;
            return beatsDuration;
        }

        bool secondsOk = false;
        const double seconds = hashParts.at(1).toDouble(&secondsOk);
        if (!secondsOk) {
            return 0.0;
        }
        *ok = true;
        return qMax(0.0, seconds);
    }

    bool localOk = false;
    const double beatsDuration = parseBeatFraction(signature, bpm, &localOk);
    if (localOk) {
        *ok = true;
        return beatsDuration;
    }
    return 0.0;
}

bool parseSlideWaitAndDuration(const QString& signature, double bpm, double* waitSecond, double* durationSecond)
{
    if (waitSecond == nullptr || durationSecond == nullptr || signature.isEmpty()) {
        return false;
    }

    *waitSecond = 0.0;
    *durationSecond = 0.0;

    const auto parseFraction = [](const QString& text, double useBpm, double* outSeconds) -> bool {
        if (outSeconds == nullptr || useBpm <= 0.0) {
            return false;
        }
        const int colon = text.indexOf(':');
        if (colon < 0) {
            return false;
        }
        bool beatsOk = false;
        bool numOk = false;
        const int beats = text.left(colon).toInt(&beatsOk);
        const int num = text.mid(colon + 1).toInt(&numOk);
        if (!beatsOk || !numOk || beats <= 0) {
            return false;
        }
        *outSeconds = 240.0 * static_cast<double>(num) / (useBpm * static_cast<double>(beats));
        return true;
    };

    if (signature.contains("###") || signature.count('#') > 3) {
        return false;
    }

    const int doubleHashIndex = signature.indexOf("##");
    if (doubleHashIndex >= 0) {
        bool waitOk = false;
        const double explicitWait = signature.left(doubleHashIndex).toDouble(&waitOk);
        if (!waitOk) {
            return false;
        }
        *waitSecond = qMax(0.0, explicitWait);
        const QString tail = signature.mid(doubleHashIndex + 2);
        const int hashIndex = tail.indexOf('#');
        if (hashIndex >= 0) {
            bool tempBpmOk = false;
            const double tempBpm = tail.left(hashIndex).toDouble(&tempBpmOk);
            if (!tempBpmOk) {
                return false;
            }
            if (parseFraction(tail.mid(hashIndex + 1), tempBpm, durationSecond)) {
                return true;
            }
            return false;
        }
        if (parseFraction(tail, bpm, durationSecond)) {
            return true;
        }
        bool durationOk = false;
        const double seconds = tail.toDouble(&durationOk);
        if (!durationOk) {
            return false;
        }
        *durationSecond = qMax(0.0, seconds);
        return true;
    }

    const int hashIndex = signature.indexOf('#');
    if (hashIndex >= 0) {
        bool tempBpmOk = false;
        const double tempBpm = signature.left(hashIndex).toDouble(&tempBpmOk);
        if (!tempBpmOk || tempBpm <= 0.0) {
            return false;
        }
        *waitSecond = 60.0 / tempBpm;
        const QString tail = signature.mid(hashIndex + 1);
        if (parseFraction(tail, tempBpm, durationSecond)) {
            return true;
        }
        bool durationOk = false;
        const double seconds = tail.toDouble(&durationOk);
        if (!durationOk) {
            return false;
        }
        *durationSecond = qMax(0.0, seconds);
        return true;
    }

    *waitSecond = 60.0 / qMax(1.0, bpm);
    return parseFraction(signature, bpm, durationSecond);
}

double slideAngleDegrees(const QPointF& start, const QPointF& end)
{
    const QPointF delta = end - start;
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())) {
        return 0.0;
    }
    return qRadiansToDegrees(std::atan2(delta.y(), delta.x()));
}

void buildLinearSamples(const QPointF& start, const QPointF& end, QVector<QPointF>* points, QVector<double>* angles)
{
    if (points == nullptr || angles == nullptr) {
        return;
    }
    points->clear();
    angles->clear();
    points->reserve(kPathSampleCount);
    angles->reserve(kPathSampleCount);
    const double angle = slideAngleDegrees(start, end);
    for (int i = 0; i < kPathSampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kPathSampleCount - 1);
        const QPointF point(start.x() + (end.x() - start.x()) * t, start.y() + (end.y() - start.y()) * t);
        points->append(point);
        angles->append(angle);
    }
}

QVector<QPointF> buildTrackArrowPoints(const QPointF& start, const QPointF& end, bool reverseOrder)
{
    const QPointF delta = end - start;
    const double length = std::hypot(delta.x(), delta.y());
    const int arrowCount = qMax(6, static_cast<int>(std::floor(length / 52.0)));
    QVector<QPointF> points;
    points.reserve(arrowCount);
    for (int i = 0; i < arrowCount; ++i) {
        const double t = static_cast<double>(i + 1) / static_cast<double>(arrowCount + 1);
        points.append(QPointF(start.x() + delta.x() * t, start.y() + delta.y() * t));
    }
    if (reverseOrder) {
        std::reverse(points.begin(), points.end());
    }
    return points;
}

void finalizeEachGroup(ParseState* state, const QVector<int>& groupIndices)
{
    if (state == nullptr || groupIndices.isEmpty()) {
        return;
    }
    QVector<TimelineNoteMarker>* noteMarkers = &state->result.noteMarkers;
    const int eachGroupId = state->nextEachGroupId++;
    QVector<int> touchIndices;
    QVector<int> tapIndices;
    QVector<int> holdIndices;
    QVector<int> touchHoldIndices;
    QVector<int> slideIndices;
    QSet<int> headStarLanes;
    int headlessSlideCount = 0;

    for (int index : groupIndices) {
        if (index < 0 || index >= noteMarkers->size()) {
            continue;
        }
        (*noteMarkers)[index].eachGroupId = eachGroupId;
        const TimelineNoteMarker& marker = noteMarkers->at(index);
        if (marker.type == "touch") {
            touchIndices.append(index);
            continue;
        }
        if (marker.type == "tap") {
            tapIndices.append(index);
            continue;
        }
        if (marker.type == "hold") {
            holdIndices.append(index);
            continue;
        }
        if (marker.type == "touch_hold") {
            touchHoldIndices.append(index);
            continue;
        }
        if (marker.type == "slide" || marker.type == "wifi") {
            slideIndices.append(index);
            if (marker.hasHeadStar) {
                headStarLanes.insert(marker.lane);
            } else {
                ++headlessSlideCount;
            }
        }
    }

    const int logicalUnitCount =
        tapIndices.size()
        + holdIndices.size()
        + touchHoldIndices.size()
        + headStarLanes.size()
        + headlessSlideCount;
    const int noteEachGroupCount = touchIndices.size() + logicalUnitCount;

    if (noteEachGroupCount >= 2) {
        for (int index : tapIndices) {
            (*noteMarkers)[index].isEach = true;
        }
        for (int index : holdIndices) {
            (*noteMarkers)[index].isEach = true;
        }
        for (int index : touchHoldIndices) {
            (*noteMarkers)[index].isEach = true;
        }
    }

    if (noteEachGroupCount >= 2) {
        // Slide/wifi keep head-star each state and trace each state separate.
        // The head-star each grouping follows the same synchronous grouping
        // rule as tap/hold/touch, while `sameHeadSlide` remains the separate
        // double-star indicator.
        for (int index : slideIndices) {
            (*noteMarkers)[index].headEach = true;
        }
    }

    if (noteEachGroupCount >= 2) {
        for (int index : touchIndices) {
            (*noteMarkers)[index].isEach = true;
        }
    }

    for (int i = 0; i < groupIndices.size(); ++i) {
        const int aIndex = groupIndices[i];
        if (aIndex < 0 || aIndex >= noteMarkers->size()) {
            continue;
        }
        TimelineNoteMarker& a = (*noteMarkers)[aIndex];
        if (a.type != "slide" && a.type != "wifi") {
            continue;
        }
        for (int j = 0; j < groupIndices.size(); ++j) {
            if (i == j) {
                continue;
            }
            const int bIndex = groupIndices[j];
            if (bIndex < 0 || bIndex >= noteMarkers->size()) {
                continue;
            }
            const TimelineNoteMarker& b = noteMarkers->at(bIndex);
            if ((b.type == "slide" || b.type == "wifi") && b.lane == a.lane) {
                a.sameHeadSlide = true;
                break;
            }
        }
    }

}

QString tokenInsideBrackets(const QString& token)
{
    const int open = token.indexOf('[');
    const int close = token.indexOf(']', open + 1);
    if (open < 0 || close <= open) {
        return QString();
    }
    return token.mid(open + 1, close - open - 1);
}

const QJsonObject& slideDataRoot()
{
    static const QJsonObject root = []() {
        QFile file(":/data/slide_native_data.json");
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

QJsonObject slideLookupEntry(const TimelineNoteMarker& marker)
{
    const QJsonObject root = slideDataRoot();
    if (root.isEmpty() || marker.slideTrackKey.isEmpty()) {
        return QJsonObject();
    }
    if (marker.type == "slide") {
        return root.value("slides").toObject().value(marker.slideTrackKey).toObject();
    }
    if (marker.type == "wifi") {
        return root.value("wifi").toObject().value(marker.slideTrackKey).toObject();
    }
    return QJsonObject();
}

QString canonicalSlideKey(const QString& key)
{
    if (key.size() != 3 || !key.contains(QChar('^'))) {
        return key;
    }

    const QString start = key.left(1);
    const QString end = key.right(1);
    const QJsonObject slides = slideDataRoot().value("slides").toObject();
    const QJsonObject base = slides.value(key).toObject();
    if (base.isEmpty()) {
        return key;
    }

    const QString leftKey = start + QChar('<') + end;
    const QString rightKey = start + QChar('>') + end;
    if (slides.value(leftKey).toObject() == base) {
        return leftKey;
    }
    if (slides.value(rightKey).toObject() == base) {
        return rightKey;
    }
    return key;
}

QString slideHeadPad(int lane)
{
    if (lane < 1 || lane > 8) {
        return QString();
    }
    return QStringLiteral("A%1").arg(lane);
}

bool markerIsSlideLike(const TimelineNoteMarker& marker)
{
    return marker.type == "slide" || marker.type == "wifi";
}

bool touchHitsSlide(const TimelineNoteMarker& touch, const TimelineNoteMarker& slide)
{
    if (touch.type != "touch" || !markerIsSlideLike(slide) || slide.slideTraceSecond < 0.0 || touch.touchPad.isEmpty()) {
        return false;
    }

    if (touch.touchPad == slideHeadPad(slide.lane)) {
        if (slide.type == "slide") {
            if (touch.second > slide.slideTraceSecond - kTapOnSlideThresholdSeconds
                && touch.second < slide.slideTraceSecond + kTouchOnSlideThresholdSeconds) {
                return true;
            }
        } else if (qAbs(touch.second - slide.slideTraceSecond) < kTouchOnSlideThresholdSeconds) {
            return true;
        }
    }

    auto touchHitsEntry = [&touch](const QJsonObject& entry, double shootSecond, double durationSecond) {
        if (entry.isEmpty() || durationSecond <= 0.0) {
            return false;
        }
        const QJsonArray padEnterTimes = entry.value("pad_enter_times").toArray();
        for (const QJsonValue& itemValue : padEnterTimes) {
            if (!itemValue.isObject()) {
                continue;
            }
            const QJsonObject item = itemValue.toObject();
            if (item.value("pad").toString() != touch.touchPad) {
                continue;
            }
            const double enterSecond = shootSecond + item.value("t").toDouble() * durationSecond;
            if (qAbs(touch.second - enterSecond) < kTouchOnSlideThresholdSeconds) {
                return true;
            }
        }
        return false;
    };

    if (!slide.slideSegmentKeys.isEmpty()
        && slide.slideSegmentKeys.size() == slide.slideSegmentShootSeconds.size()
        && slide.slideSegmentKeys.size() == slide.slideSegmentDurations.size()) {
        const QJsonObject slides = slideDataRoot().value("slides").toObject();
        for (int i = 0; i < slide.slideSegmentKeys.size(); ++i) {
            const QJsonObject entry = slides.value(slide.slideSegmentKeys.at(i)).toObject();
            if (touchHitsEntry(entry, slide.slideSegmentShootSeconds.at(i), slide.slideSegmentDurations.at(i))) {
                return true;
            }
        }
        return false;
    }

    const QJsonObject entry = slideLookupEntry(slide);
    const double durationSecond = qMax(0.0, slide.endSecond - slide.slideTraceSecond);
    if (touchHitsEntry(entry, slide.slideTraceSecond, durationSecond)) {
        return true;
    }

    return false;
}

bool parseStandardSlideChain(
    const QString& slideCore,
    bool strictMode,
    double bpm,
    QStringList* shapes,
    double* waitSecond,
    QVector<double>* segmentDurations
)
{
    if (shapes == nullptr || waitSecond == nullptr || segmentDurations == nullptr || slideCore.isEmpty()) {
        return false;
    }

    QStringList parsedShapes;
    QVector<QPair<double, double>> waitAndDurations;
    bool shapeFound = false;
    int signatureState = 0;  // 0 begin, 1 total pending, 2 individual, 3 total confirmed
    QChar lastTarget = slideCore.at(0);

    for (int i = 1; i < slideCore.size(); ++i) {
        const QChar ch = slideCore.at(i);
        if (ch == QChar('[')) {
            if (!shapeFound) {
                return false;
            }

            shapeFound = false;
            if (signatureState == 0) {
                signatureState = 2;
            } else if (signatureState == 1) {
                signatureState = 3;
            } else if (signatureState == 3) {
                return false;
            }

            QString signature;
            ++i;
            while (i < slideCore.size() && slideCore.at(i) != QChar(']')) {
                signature.append(slideCore.at(i));
                ++i;
            }
            if (i >= slideCore.size() || slideCore.at(i) != QChar(']')) {
                return false;
            }

            double wait = 0.0;
            double duration = 0.0;
            if (!parseSlideWaitAndDuration(signature, bpm, &wait, &duration)) {
                return false;
            }
            waitAndDurations.append(qMakePair(wait, duration));
            continue;
        }

        if (!QStringLiteral("-^v<>Vpqsz").contains(ch)) {
            continue;
        }

        if (signatureState == 3) {
            return false;
        }
        if (shapeFound) {
            if (signatureState == 0) {
                signatureState = 1;
            } else if (signatureState == 2) {
                return false;
            }
        }

        shapeFound = true;
        QString shape;
        shape.append(lastTarget);
        shape.append(ch);
        if (ch == QChar('V')) {
            if (i + 2 >= slideCore.size()) {
                return false;
            }
            const QChar mid = slideCore.at(++i);
            const QChar end = slideCore.at(++i);
            shape.append(mid);
            shape.append(end);
            lastTarget = end;
        } else if (ch == QChar('p') || ch == QChar('q')) {
            if (i + 1 >= slideCore.size()) {
                return false;
            }
            const QChar next = slideCore.at(++i);
            shape.append(next);
            if (next == ch) {
                if (i + 1 >= slideCore.size()) {
                    return false;
                }
                const QChar end = slideCore.at(++i);
                shape.append(end);
                lastTarget = end;
            } else {
                lastTarget = next;
            }
        } else {
            if (i + 1 >= slideCore.size()) {
                return false;
            }
            const QChar end = slideCore.at(++i);
            shape.append(end);
            lastTarget = end;
        }
        parsedShapes.append(shape);
    }

    if (parsedShapes.isEmpty() || waitAndDurations.isEmpty()) {
        return false;
    }

    *waitSecond = waitAndDurations.constFirst().first;
    shapes->clear();
    segmentDurations->clear();
    *shapes = parsedShapes;

    auto distributeTotalDurationByShapeLength = [&parsedShapes, &segmentDurations](double totalDuration) -> bool {
        QVector<double> lengths;
        lengths.reserve(parsedShapes.size());
        double totalLength = 0.0;
        // Approximate chain duration splitting by sampled polyline length.
        for (const QString& key : parsedShapes) {
            const QJsonObject entry = slideDataRoot().value("slides").toObject().value(key).toObject();
            QVector<QPointF> points;
            QVector<double> angles;
            loadSamplePath(entry.value("samples").toArray(), &points, &angles);
            const double length = qMax(1.0, polylineLength(points));
            lengths.append(length);
            totalLength += length;
        }
        if (totalLength <= 0.0) {
            return false;
        }
        segmentDurations->reserve(lengths.size());
        for (double length : lengths) {
            segmentDurations->append(totalDuration * length / totalLength);
        }
        return true;
    };

    if (signatureState == 2) {
        if (waitAndDurations.size() != parsedShapes.size()) {
            return false;
        }
        if (parsedShapes.size() > 1) {
            if (strictMode) {
                return false;
            }
            double totalDuration = 0.0;
            for (const auto& item : waitAndDurations) {
                totalDuration += qMax(0.0, item.second);
            }
            return distributeTotalDurationByShapeLength(totalDuration);
        }
        segmentDurations->reserve(waitAndDurations.size());
        for (const auto& item : waitAndDurations) {
            segmentDurations->append(item.second);
        }
        return true;
    }

    if (signatureState == 3) {
        const double totalDuration = waitAndDurations.constFirst().second;
        return distributeTotalDurationByShapeLength(totalDuration);
    }

    return false;
}

QString normalizedSlideLookupKey(const QString& token)
{
    if (token.isEmpty() || !isDigitLane(token.at(0))) {
        return QString();
    }
    QString core = token;
    const int open = core.indexOf('[');
    if (open >= 0) {
        core = core.left(open);
    }
    if (core.isEmpty()) {
        return QString();
    }

    QString key;
    key.reserve(core.size());
    key.append(core.at(0));
    for (int i = 1; i < core.size(); ++i) {
        const QChar ch = core.at(i);
        if (ch == QChar('b') || ch == QChar('x') || ch == QChar('h') || ch == QChar('?') || ch == QChar('!')) {
            continue;
        }
        if (isDigitLane(ch)
            || ch == QChar('-')
            || ch == QChar('<')
            || ch == QChar('>')
            || ch == QChar('^')
            || ch == QChar('v')
            || ch == QChar('V')
            || ch == QChar('p')
            || ch == QChar('q')
            || ch == QChar('s')
            || ch == QChar('z')
            || ch == QChar('w')) {
            key.append(ch);
        }
    }
    return key;
}

void loadSamplePath(const QJsonArray& samples, QVector<QPointF>* points, QVector<double>* angles)
{
    if (points == nullptr || angles == nullptr) {
        return;
    }
    points->clear();
    angles->clear();
    points->reserve(samples.size());
    angles->reserve(samples.size());
    for (const QJsonValue& sampleValue : samples) {
        if (!sampleValue.isObject()) {
            continue;
        }
        const QJsonObject sampleObject = sampleValue.toObject();
        points->append(QPointF(sampleObject.value("x").toDouble(), sampleObject.value("y").toDouble()));
        angles->append(sampleObject.value("angle").toDouble());
    }
}

bool populateSlideFromLookup(const QString& key, TimelineNoteMarker* marker)
{
    if (marker == nullptr || key.isEmpty()) {
        return false;
    }
    const QJsonObject root = slideDataRoot();
    if (root.isEmpty()) {
        return false;
    }

    if (marker->type == "slide") {
        const QJsonObject entry = root.value("slides").toObject().value(key).toObject();
        if (entry.isEmpty()) {
            return false;
        }
        marker->slideTrackKey = key;
        marker->slideSegmentKeys = QStringList{key};
        marker->endLane = qBound(1, entry.value("end").toInt(marker->endLane), 8);
        marker->slideSegmentPadEnterTimes = QVector<QVector<MuriPadTimeEntry>>{
            loadPadEnterTimes(entry.value("pad_enter_times").toArray())
        };
        marker->slideSegmentCriticalProportions = QVector<double>{
            entry.value("critical_proportion").toDouble(1.0)
        };

        QVector<QPointF> points;
        QVector<double> angles;
        loadSamplePath(entry.value("samples").toArray(), &points, &angles);
        if (!points.isEmpty()) {
            marker->slideSegmentPoints = QVector<QVector<QPointF>>{points};
            marker->slideSegmentAngles = QVector<QVector<double>>{angles};
        }

        QVector<QVector<QPointF>> segmentAreas;
        QVector<QVector<double>> segmentRotations;
        const QJsonArray areaArray = entry.value("track_arrows").toArray();
        segmentAreas.reserve(areaArray.size());
        segmentRotations.reserve(areaArray.size());
        for (const QJsonValue& areaValue : areaArray) {
            if (!areaValue.isArray()) {
                continue;
            }
            QVector<QPointF> areaPoints;
            QVector<double> areaRotations;
            const QJsonArray arrowArray = areaValue.toArray();
            areaPoints.reserve(arrowArray.size());
            areaRotations.reserve(arrowArray.size());
            for (const QJsonValue& arrowValue : arrowArray) {
                if (!arrowValue.isObject()) {
                    continue;
                }
                const QJsonObject arrowObject = arrowValue.toObject();
                areaPoints.append(QPointF(arrowObject.value("x").toDouble(), arrowObject.value("y").toDouble()));
                areaRotations.append(arrowObject.value("rotation").toDouble());
            }
            segmentAreas.append(areaPoints);
            segmentRotations.append(areaRotations);
        }
        marker->slideTrackAreaPoints = QVector<QVector<QVector<QPointF>>>{segmentAreas};
        marker->slideTrackAreaRotations = QVector<QVector<QVector<double>>>{segmentRotations};

        QVector<double> thresholds;
        const QJsonArray thresholdArray = entry.value("track_thresholds").toArray();
        thresholds.reserve(thresholdArray.size());
        for (const QJsonValue& thresholdValue : thresholdArray) {
            thresholds.append(thresholdValue.toDouble());
        }
        marker->slideTrackAreaThresholds = QVector<QVector<double>>{thresholds};

        QVector<QVector<double>> checkpointGroups;
        const QJsonArray checkpointArray = entry.value("track_checkpoints").toArray();
        checkpointGroups.reserve(checkpointArray.size());
        for (const QJsonValue& areaValue : checkpointArray) {
            QVector<double> areaCheckpoints;
            const QJsonArray values = areaValue.toArray();
            areaCheckpoints.reserve(values.size());
            for (const QJsonValue& value : values) {
                areaCheckpoints.append(value.toDouble());
            }
            checkpointGroups.append(areaCheckpoints);
        }
        marker->slideTrackAreaCheckpoints = QVector<QVector<QVector<double>>>{checkpointGroups};

        QVector<QVector<int>> cutGroups;
        const QJsonArray cutArray = entry.value("track_cut_indices").toArray();
        cutGroups.reserve(cutArray.size());
        for (const QJsonValue& areaValue : cutArray) {
            QVector<int> areaCuts;
            const QJsonArray values = areaValue.toArray();
            areaCuts.reserve(values.size());
            for (const QJsonValue& value : values) {
                areaCuts.append(value.toInt());
            }
            cutGroups.append(areaCuts);
        }
        marker->slideTrackAreaCutIndices = QVector<QVector<QVector<int>>>{cutGroups};
        return true;
    }

    if (marker->type == "wifi") {
        const QJsonObject entry = root.value("wifi").toObject().value(key).toObject();
        if (entry.isEmpty()) {
            return false;
        }
        marker->slideTrackKey = key;
        marker->slideSegmentKeys = QStringList{key};
        marker->endLane = qBound(1, entry.value("end").toInt(marker->endLane), 8);
        marker->wifiPadEnterTimes = loadPadEnterTimes(entry.value("pad_enter_times").toArray());
        marker->wifiCriticalProportion = entry.value("critical_proportion").toDouble(1.0);

        marker->wifiLanePoints.clear();
        marker->wifiLaneAngles.clear();
        const QJsonArray laneArray = entry.value("lane_samples").toArray();
        marker->wifiLanePoints.reserve(laneArray.size());
        marker->wifiLaneAngles.reserve(laneArray.size());
        for (const QJsonValue& laneValue : laneArray) {
            QVector<QPointF> points;
            QVector<double> angles;
            loadSamplePath(laneValue.toArray(), &points, &angles);
            marker->wifiLanePoints.append(points);
            marker->wifiLaneAngles.append(angles);
        }

        marker->wifiTrackAreaPoints.clear();
        marker->wifiTrackAreaRotations.clear();
        marker->wifiTrackAreaImageIndices.clear();
        const QJsonArray areaArray = entry.value("track_arrows").toArray();
        marker->wifiTrackAreaPoints.reserve(areaArray.size());
        marker->wifiTrackAreaRotations.reserve(areaArray.size());
        marker->wifiTrackAreaImageIndices.reserve(areaArray.size());
        for (const QJsonValue& areaValue : areaArray) {
            if (!areaValue.isArray()) {
                continue;
            }
            QVector<QPointF> areaPoints;
            QVector<double> areaRotations;
            QVector<int> imageIndices;
            const QJsonArray arrowArray = areaValue.toArray();
            areaPoints.reserve(arrowArray.size());
            areaRotations.reserve(arrowArray.size());
            imageIndices.reserve(arrowArray.size());
            for (const QJsonValue& arrowValue : arrowArray) {
                if (!arrowValue.isObject()) {
                    continue;
                }
                const QJsonObject arrowObject = arrowValue.toObject();
                areaPoints.append(QPointF(arrowObject.value("x").toDouble(), arrowObject.value("y").toDouble()));
                areaRotations.append(arrowObject.value("rotation").toDouble());
                imageIndices.append(arrowObject.value("image_index").toInt());
            }
            marker->wifiTrackAreaPoints.append(areaPoints);
            marker->wifiTrackAreaRotations.append(areaRotations);
            marker->wifiTrackAreaImageIndices.append(imageIndices);
        }

        marker->wifiTrackAreaThresholds.clear();
        const QJsonArray thresholdArray = entry.value("track_thresholds").toArray();
        marker->wifiTrackAreaThresholds.reserve(thresholdArray.size());
        for (const QJsonValue& thresholdValue : thresholdArray) {
            marker->wifiTrackAreaThresholds.append(thresholdValue.toDouble());
        }

        marker->wifiTrackAreaCheckpoints.clear();
        const QJsonArray checkpointArray = entry.value("track_checkpoints").toArray();
        marker->wifiTrackAreaCheckpoints.reserve(checkpointArray.size());
        for (const QJsonValue& areaValue : checkpointArray) {
            QVector<double> areaCheckpoints;
            const QJsonArray values = areaValue.toArray();
            areaCheckpoints.reserve(values.size());
            for (const QJsonValue& value : values) {
                areaCheckpoints.append(value.toDouble());
            }
            marker->wifiTrackAreaCheckpoints.append(areaCheckpoints);
        }
        return true;
    }

    return false;
}

int inferSlideEndLane(const QString& token, int fallbackLane)
{
    const int open = token.indexOf('[');
    const QString head = open >= 0 ? token.left(open) : token;
    for (int i = head.size() - 1; i >= 0; --i) {
        if (isDigitLane(head.at(i))) {
            return head.at(i).digitValue();
        }
    }
    return fallbackLane;
}


#include "SimaiNativeParser.StrictChecks.cpp"
#include "SimaiNativeParser.Slide.cpp"
#include "SimaiNativeParser.TouchTap.cpp"
#include "SimaiNativeParser.Driver.cpp"
