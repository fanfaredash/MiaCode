#include "tools/muri/MuriAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "tools/muri/MuriAnalyzerGeometry.h"
#include "tools/muri/MuriAnalyzerModel.h"
#include "tools/muri/MuriAnalyzerInternal.h"
#include "tools/muri/MuriDiagnosticCollector.h"
#include "tools/muri/MuriDiagnosticLabels.h"
#include "tools/muri/MuriOverlayBuilder.h"
#include "tools/muri/MuriRuntimeModelBuilder.h"
#include "tools/muri/MuriSimpleNoteJudge.h"
#include "tools/muri/MuriSlideReferenceData.h"
#include "tools/muri/MuriSlideWifiJudge.h"

// The analyzer implementation lives in namespace miacode::muri::detail (not an
// anonymous namespace) so the analysis pipeline can be split across translation
// units: each stage .cpp shares these symbols through internal headers while
// keeping them out of the public MuriAnalyzer / MuriTypes surface. Geometry, the
// shared data model, and slide reference data already live in detail (own TUs).
namespace miacode::muri::detail {

constexpr double kPadTimeEpsilon = 1e-6;

// The cross-stage data model (PadWindowIndex, MarkerSourceRef, RuntimePadEvent,
// RuntimeJudgeHit, RuntimeSlideJudgeResult, JudgeableSimpleNote, RuntimeTouchGroup,
// RuntimeTopLevelNote, RuntimeHandAction[Kind], RuntimeTouchPoint, DiagnosticAnchor)
// now lives in src/tools/muri/MuriAnalyzerModel.h (namespace miacode::muri::detail).
// It is available unqualified here via the using-directive above. Stage-local
// runtime-judge structs are still defined further down, next to their stage.

// slideRuntimeRoot() and the load*(...) reference-data accessors now live in
// src/tools/muri/MuriSlideReferenceData.{h,cpp} (namespace miacode::muri::detail),
// reachable unqualified via the using-directive above.

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

QString slideHeadPad(int lane)
{
    if (lane < 1 || lane > 8) {
        return QString();
    }
    return QStringLiteral("A%1").arg(lane);
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

// The runtime-model build stage (buildMarkerSourceRefs, buildJudgeableSimpleNotes,
// buildNoteExpiryBuckets, buildRuntimeTouchGroups, buildRuntimeTopLevelNotes) and
// the MuriRuntimeModel / MuriRuntimeModelBuilder types now live in
// src/tools/muri/MuriRuntimeModelBuilder.{h,cpp}. They remain in namespace
// miacode::muri::detail, so the call sites here resolve through that header.

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
    bool includeSlideLike)
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
    const QSet<quint64>* previousMergedSlidePairs,
    QSet<quint64>* currentMergedSlidePairs)
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

}  // namespace miacode::muri::detail

// Bring the detail-namespace pipeline helpers into scope for the public
// MuriAnalyzer::analyze orchestrator defined below.
using namespace miacode::muri::detail;

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

    MuriDiagnosticCollector collector;
    buildOverlayActions(noteMarkers, &report.padWindows, &report.actionTrails);
    collectSimpleNoteRuntimeDiagnostics(
        noteMarkers,
        renderOptions,
        collector,
        staticTapOnSlideThresholdSeconds);

    const MuriRuntimeModel runtimeModel = MuriRuntimeModelBuilder::build(noteMarkers);
    const QHash<QString, MarkerSourceRef>& markerRefs = runtimeModel.markerRefs;
    const QHash<QString, QString> markerConfigLabels = buildMarkerConfigLabels(noteMarkers);
    const QHash<QString, QString> syntheticSlideHeadOwnerKeys = buildSyntheticSlideHeadOwnerKeys(noteMarkers);
    const QHash<QString, int>& noteIndexByMarkerKey = runtimeModel.noteIndexByMarkerKey;
    const QVector<JudgeableSimpleNote>& runtimeNotes = runtimeModel.notes;
    const QHash<int, int>& touchGroupByChildNoteIndex = runtimeModel.touchGroupByChildNoteIndex;
    const QVector<RuntimeTouchGroup>& runtimeTouchGroups = runtimeModel.touchGroups;
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
                    collector.addDiagnostic(
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
                    collector.appendSlideJudgeSpriteEvent(marker, runtimeResult.judgeSecond);
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
                    collector.addDiagnostic(
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
                    collector.appendSlideJudgeSpriteEvent(marker, lastSegment.completedSecond);
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
                    collector.addDiagnostic(
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
                    collector.appendSlideJudgeSpriteEvent(marker, runtimeResult.judgeSecond);
                }
            } else {
                const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
                const double criticalDeltaSecond = slideCriticalDeltaSecond(
                    (1.0 - marker.wifiCriticalProportion) * durationSecond
                );
                if (wifiJudgeIsBad(state.wifiCompletedSecond, state.wifiCriticalSecond, criticalDeltaSecond)) {
                    collector.addDiagnostic(
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
                    collector.appendSlideJudgeSpriteEvent(marker, state.wifiCompletedSecond);
                }
            }
            report.markerStates.insert(markerKey, state);
        }
    }

    collector.finalize();
    report.diagnostics = std::move(collector.diagnostics);
    report.judgeSpriteEvents = std::move(collector.judgeSpriteEvents);
    return report;
}
