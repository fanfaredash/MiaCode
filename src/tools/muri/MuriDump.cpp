#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QtMath>

#include <limits>

#include "SimaiDocument.h"
#include "SimaiNativeParser.h"
#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriTypes.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriStaticChecker.h"

namespace {

struct TimelineChartCounts {
    int noteCount = 0;
    int markerCount = 0;
    int tapCount = 0;
    int holdCount = 0;
    int slideCount = 0;
    int touchCount = 0;
    int breakCount = 0;
    int helperHeadTapCount = 0;
    int helperHeadBreakCount = 0;
};

struct StaticReferenceRecord {
    MuriKind kind = MuriKind::Overlap;
    int affectedIndex = -1;
    int causeIndex = -1;
    double deltaSecond = 0.0;
    bool hasDelta = false;
};

constexpr double kStaticOverlayThresholdSeconds = 6.0 / miacode::muri::kJudgeTps;
constexpr double kStaticCollideThresholdSeconds = 36.0 / miacode::muri::kJudgeTps;
constexpr double kStaticCollideExtraDeltaSeconds =
    kStaticCollideThresholdSeconds - miacode::muri::kTapAvailableSeconds;
constexpr double kStaticTimeEpsilonSeconds = 1e-9;

bool isSlideLike(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
}

bool isTapMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("tap");
}

bool isHoldMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("hold");
}

bool isTouchMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("touch");
}

bool isTouchHoldMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("touch_hold");
}

bool isTapLikeMarker(const TimelineNoteMarker& marker)
{
    return isTapMarker(marker) || isTouchMarker(marker);
}

bool isHoldLikeMarker(const TimelineNoteMarker& marker)
{
    return isHoldMarker(marker) || isTouchHoldMarker(marker);
}

bool isNonSlideMarker(const TimelineNoteMarker& marker)
{
    return isTapLikeMarker(marker) || isHoldLikeMarker(marker);
}

QString lanePadToken(int lane)
{
    if (lane < 1 || lane > 8) {
        return QString();
    }
    return QStringLiteral("A%1").arg(lane);
}

QString markerPadToken(const TimelineNoteMarker& marker)
{
    if (isTouchMarker(marker) || isTouchHoldMarker(marker)) {
        return marker.touchPad.trimmed().toUpper();
    }
    if (isTapMarker(marker) || isHoldMarker(marker)) {
        return lanePadToken(marker.lane);
    }
    return QString();
}

bool markerMomentInRange(double momentSecond, double startSecond, double endSecond)
{
    return momentSecond + kStaticTimeEpsilonSeconds >= startSecond
        && momentSecond <= endSecond + kStaticTimeEpsilonSeconds;
}

double slideCriticalSecondForMarker(const TimelineNoteMarker& marker)
{
    if (marker.type == QLatin1String("slide")
        && !marker.slideSegmentShootSeconds.isEmpty()
        && !marker.slideSegmentDurations.isEmpty()
        && !marker.slideSegmentCriticalProportions.isEmpty()) {
        const int index = qMin(
            marker.slideSegmentShootSeconds.size(),
            qMin(marker.slideSegmentDurations.size(), marker.slideSegmentCriticalProportions.size())) - 1;
        if (index >= 0) {
            return marker.slideSegmentShootSeconds.at(index)
                + marker.slideSegmentDurations.at(index) * marker.slideSegmentCriticalProportions.at(index);
        }
    }
    if (marker.type == QLatin1String("wifi")
        && marker.slideTraceSecond >= 0.0
        && marker.endSecond >= marker.slideTraceSecond) {
        return marker.slideTraceSecond
            + (marker.endSecond - marker.slideTraceSecond) * marker.wifiCriticalProportion;
    }
    return -1.0;
}

QVector<TimelineNoteMarker> markersWithStaticHelperHeadTaps(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QVector<TimelineNoteMarker> augmented = noteMarkers;
    augmented.reserve(noteMarkers.size() * 2);

    QSet<QString> emittedKeys;
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!isSlideLike(marker) || !marker.hasHeadStar) {
            continue;
        }

        const int helperCol = qMax(1, marker.sourceCol + 1);
        const QString emitKey = marker.sameHeadSlide
            ? QStringLiteral("same_head_helper|%1|%2|%3|%4|%5|%6")
                  .arg(marker.second, 0, 'f', 6)
                  .arg(marker.sourceLine)
                  .arg(marker.lane)
                  .arg(marker.headBreak ? 1 : 0)
                  .arg(marker.headEx ? 1 : 0)
                  .arg(marker.headEach ? 1 : 0)
            : QStringLiteral("helper|%1|%2|%3|%4|%5")
                  .arg(marker.second, 0, 'f', 6)
                  .arg(marker.sourceLine)
                  .arg(helperCol)
                  .arg(marker.lane)
                  .arg(marker.parseOrder);
        if (emittedKeys.contains(emitKey)) {
            continue;
        }
        emittedKeys.insert(emitKey);

        TimelineNoteMarker helper;
        helper.second = marker.second;
        helper.endSecond = -1.0;
        helper.slideTraceSecond = -1.0;
        helper.availableSecond = -1.0;
        helper.parseOrder = marker.parseOrder;
        helper.eachGroupId = marker.eachGroupId;
        helper.sourceLine = marker.sourceLine;
        helper.sourceCol = helperCol;
        helper.lane = marker.lane;
        helper.endLane = marker.lane;
        helper.type = QStringLiteral("tap");
        helper.isEach = marker.headEach;
        helper.isBreak = marker.headBreak;
        helper.isEx = marker.headEx;
        helper.hasHeadStar = true;
        helper.slideTrackKey = QStringLiteral("__static_helper_head_tap__");
        augmented.append(helper);
    }

    return augmented;
}

QString syntheticSlideHeadStarKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.eachGroupId);
}

TimelineChartCounts countTimelineChartNotes(const QVector<TimelineNoteMarker>& noteMarkers)
{
    TimelineChartCounts counts;
    QSet<QString> helperHeadTapKeys;
    QSet<QString> helperHeadBreakKeys;
    counts.markerCount = noteMarkers.size();
    counts.noteCount = counts.markerCount;
    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString type = marker.type.toLower();
        const bool isTap = (type == QLatin1String("tap"));
        const bool isHold = (type == QLatin1String("hold") || type == QLatin1String("touch_hold"));
        const bool isSlide = isSlideLike(marker);
        const bool isTouch = (type == QLatin1String("touch"));

        if (isTap) {
            if (marker.isBreak) {
                ++counts.breakCount;
            } else {
                ++counts.tapCount;
            }
            continue;
        }

        if (isTouch) {
            if (marker.isBreak) {
                ++counts.breakCount;
            } else {
                ++counts.touchCount;
            }
            continue;
        }

        if (isHold) {
            if (marker.isBreak) {
                ++counts.breakCount;
            } else {
                ++counts.holdCount;
            }
            continue;
        }

        if (!isSlide) {
            continue;
        }

        if (marker.trackBreak) {
            ++counts.breakCount;
        } else {
            ++counts.slideCount;
        }

        if (marker.hasHeadStar) {
            if (marker.headBreak) {
                helperHeadBreakKeys.insert(syntheticSlideHeadStarKey(marker));
            } else {
                helperHeadTapKeys.insert(syntheticSlideHeadStarKey(marker));
            }
        }
    }

    counts.helperHeadTapCount = helperHeadTapKeys.size();
    counts.helperHeadBreakCount = helperHeadBreakKeys.size();
    counts.tapCount += counts.helperHeadTapCount;
    counts.breakCount += counts.helperHeadBreakCount;
    counts.noteCount += counts.helperHeadTapCount + counts.helperHeadBreakCount;
    return counts;
}

QString muriKindKey(MuriKind kind)
{
    switch (kind) {
    case MuriKind::SlideTooFast:
        return QStringLiteral("SlideTooFast");
    case MuriKind::SlideHeadTap:
        return QStringLiteral("SlideHeadTap");
    case MuriKind::TapOnSlide:
        return QStringLiteral("TapOnSlide");
    case MuriKind::Overlap:
        return QStringLiteral("Overlap");
    case MuriKind::MultiTouch:
        return QStringLiteral("MultiTouch");
    }
    return QStringLiteral("Unknown");
}

QString areaJudgeCauseKey(AreaJudgeCause cause)
{
    switch (cause) {
    case AreaJudgeCause::Self:
        return QStringLiteral("Self");
    case AreaJudgeCause::Other:
        return QStringLiteral("Other");
    case AreaJudgeCause::ExistingHold:
        return QStringLiteral("ExistingHold");
    case AreaJudgeCause::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QJsonObject jsonFromStaticReferenceMarker(const TimelineNoteMarker& marker)
{
    QJsonObject item;
    item.insert(QStringLiteral("marker_key"), makeMarkerAnalysisKey(marker));
    item.insert(QStringLiteral("marker_type"), marker.type);
    item.insert(QStringLiteral("line"), marker.sourceLine);
    item.insert(QStringLiteral("col"), marker.sourceCol);
    item.insert(QStringLiteral("second"), marker.second);
    if (marker.endSecond >= 0.0) {
        item.insert(QStringLiteral("end_second"), marker.endSecond);
    }
    if (marker.slideTraceSecond >= 0.0) {
        item.insert(QStringLiteral("slide_trace_second"), marker.slideTraceSecond);
    }
    const QString pad = markerPadToken(marker);
    if (!pad.isEmpty()) {
        item.insert(QStringLiteral("pad"), pad);
    }
    item.insert(QStringLiteral("lane"), marker.lane);
    item.insert(QStringLiteral("end_lane"), marker.endLane);
    return item;
}

QJsonObject jsonFromStaticReferenceNote(const MuriStaticReferenceNote& note)
{
    QJsonObject item;
    item.insert(QStringLiteral("marker_key"), note.markerKey);
    item.insert(QStringLiteral("marker_type"), note.markerType);
    item.insert(QStringLiteral("line"), note.line);
    item.insert(QStringLiteral("col"), note.col);
    item.insert(QStringLiteral("second"), note.second);
    item.insert(QStringLiteral("lane"), note.lane);
    item.insert(QStringLiteral("end_lane"), note.endLane);
    if (note.endSecond >= 0.0) {
        item.insert(QStringLiteral("end_second"), note.endSecond);
    }
    if (note.slideTraceSecond >= 0.0) {
        item.insert(QStringLiteral("slide_trace_second"), note.slideTraceSecond);
    }
    if (!note.pad.isEmpty()) {
        item.insert(QStringLiteral("pad"), note.pad);
    }
    return item;
}

QJsonObject jsonFromStaticReferenceRecord(const MuriStaticReference& record)
{
    QJsonObject item;
    item.insert(QStringLiteral("kind"), muriKindKey(record.kind));
    item.insert(QStringLiteral("affected"), jsonFromStaticReferenceNote(record.affected));
    item.insert(QStringLiteral("cause"), jsonFromStaticReferenceNote(record.cause));
    if (record.hasDelta) {
        item.insert(QStringLiteral("delta_second"), record.deltaSecond);
        item.insert(QStringLiteral("delta_ms"), record.deltaSecond * 1000.0);
    }
    return item;
}

double shiftedTimelineSecond(double second, double offsetSeconds)
{
    if (!qIsFinite(second) || !qIsFinite(offsetSeconds)) {
        return second;
    }
    return second + offsetSeconds;
}

QVector<TimelineNoteMarker> shiftedNoteMarkers(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double offsetSeconds)
{
    QVector<TimelineNoteMarker> shifted = noteMarkers;
    for (TimelineNoteMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
        if (marker.endSecond >= 0.0) {
            marker.endSecond = shiftedTimelineSecond(marker.endSecond, offsetSeconds);
        }
        if (marker.slideTraceSecond >= 0.0) {
            marker.slideTraceSecond = shiftedTimelineSecond(marker.slideTraceSecond, offsetSeconds);
        }
        if (marker.availableSecond >= 0.0) {
            marker.availableSecond = shiftedTimelineSecond(marker.availableSecond, offsetSeconds);
        }
        for (double& shootSecond : marker.slideSegmentShootSeconds) {
            shootSecond = shiftedTimelineSecond(shootSecond, offsetSeconds);
        }
    }
    return shifted;
}

QJsonArray jsonArrayFromStringList(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray jsonArrayFromPadTimes(const QVector<MuriPadTimeEntry>& entries)
{
    QJsonArray array;
    for (const MuriPadTimeEntry& entry : entries) {
        QJsonObject item;
        item.insert(QStringLiteral("pad"), entry.pad);
        item.insert(QStringLiteral("proportion"), entry.proportion);
        array.append(item);
    }
    return array;
}

QJsonObject jsonFromParseMessage(const SimaiNativeMessage& message)
{
    QJsonObject item;
    item.insert(QStringLiteral("line"), message.line);
    item.insert(QStringLiteral("col"), message.col);
    item.insert(QStringLiteral("end_col"), message.endCol);
    item.insert(QStringLiteral("message"), message.message);
    return item;
}

QJsonObject jsonFromDiagnostic(const MuriDiagnostic& diagnostic)
{
    QJsonObject item;
    item.insert(QStringLiteral("kind"), muriKindKey(diagnostic.kind));
    item.insert(QStringLiteral("second"), diagnostic.second);
    item.insert(QStringLiteral("line"), diagnostic.line);
    item.insert(QStringLiteral("col"), diagnostic.col);
    item.insert(QStringLiteral("marker_key"), diagnostic.markerKey);
    item.insert(QStringLiteral("title"), diagnostic.title);
    item.insert(QStringLiteral("detail"), diagnostic.detail);
    return item;
}

QJsonObject jsonFromCheckpointState(const MuriCheckpointState& checkpoint)
{
    QJsonObject item;
    item.insert(QStringLiteral("proportion"), checkpoint.proportion);
    item.insert(QStringLiteral("second"), checkpoint.second);
    item.insert(QStringLiteral("pads"), jsonArrayFromStringList(checkpoint.pads));
    item.insert(QStringLiteral("cause_marker_key"), checkpoint.causeMarkerKey);
    item.insert(QStringLiteral("cause_type"), checkpoint.causeType);
    item.insert(QStringLiteral("cause"), areaJudgeCauseKey(checkpoint.cause));
    item.insert(QStringLiteral("skipped"), checkpoint.skipped);
    return item;
}

QJsonArray jsonFromCheckpointAreas(const QVector<QVector<MuriCheckpointState>>& areas)
{
    QJsonArray areaArray;
    for (const QVector<MuriCheckpointState>& checkpoints : areas) {
        QJsonArray checkpointArray;
        for (const MuriCheckpointState& checkpoint : checkpoints) {
            checkpointArray.append(jsonFromCheckpointState(checkpoint));
        }
        areaArray.append(checkpointArray);
    }
    return areaArray;
}

QJsonArray jsonFromJudgeAreas(const QVector<QVector<MuriCheckpointState>>& areas)
{
    QJsonArray areaArray;
    for (const QVector<MuriCheckpointState>& checkpoints : areas) {
        QJsonObject item;
        if (checkpoints.isEmpty()) {
            item.insert(QStringLiteral("pads"), QJsonArray());
            item.insert(QStringLiteral("time"), -1.0);
            item.insert(QStringLiteral("skipped"), false);
            item.insert(QStringLiteral("cause"), QStringLiteral("pending"));
            areaArray.append(item);
            continue;
        }

        const MuriCheckpointState& checkpoint = checkpoints.constFirst();
        item.insert(QStringLiteral("pads"), jsonArrayFromStringList(checkpoint.pads));
        item.insert(QStringLiteral("time"), checkpoint.second);
        item.insert(QStringLiteral("skipped"), checkpoint.skipped);
        item.insert(QStringLiteral("cause"), areaJudgeCauseKey(checkpoint.cause));
        item.insert(QStringLiteral("cause_marker_key"), checkpoint.causeMarkerKey);
        item.insert(QStringLiteral("cause_type"), checkpoint.causeType);
        areaArray.append(item);
    }
    return areaArray;
}

QJsonArray jsonFromLaneCheckpointAreas(const QVector<QVector<QVector<MuriCheckpointState>>>& lanes)
{
    QJsonArray laneArray;
    for (const QVector<QVector<MuriCheckpointState>>& lane : lanes) {
        laneArray.append(jsonFromCheckpointAreas(lane));
    }
    return laneArray;
}

QJsonArray jsonFromLaneJudgeAreas(const QVector<QVector<QVector<MuriCheckpointState>>>& lanes)
{
    QJsonArray laneArray;
    for (const QVector<QVector<MuriCheckpointState>>& lane : lanes) {
        laneArray.append(jsonFromJudgeAreas(lane));
    }
    return laneArray;
}

QJsonArray jsonFromLaneProgressSeconds(const QVector<QVector<double>>& lanes)
{
    QJsonArray laneArray;
    for (const QVector<double>& lane : lanes) {
        QJsonArray progressArray;
        for (double second : lane) {
            progressArray.append(second);
        }
        laneArray.append(progressArray);
    }
    return laneArray;
}

QJsonObject jsonFromSegmentState(const MuriSegmentState& segment)
{
    QJsonObject item;
    item.insert(QStringLiteral("completed_second"), segment.completedSecond);
    item.insert(QStringLiteral("expected_completed_second"), segment.expectedCompletedSecond);
    item.insert(QStringLiteral("critical_second"), segment.criticalSecond);
    item.insert(QStringLiteral("area_checkpoints"), jsonFromCheckpointAreas(segment.areaCheckpoints));
    item.insert(QStringLiteral("judge_areas"), jsonFromJudgeAreas(segment.areaCheckpoints));
    return item;
}

QJsonObject jsonFromMarkerState(const MarkerMuriState& state)
{
    QJsonObject item;
    item.insert(QStringLiteral("marker_key"), state.markerKey);
    item.insert(QStringLiteral("marker_type"), state.markerType);
    item.insert(QStringLiteral("line"), state.line);
    item.insert(QStringLiteral("col"), state.col);
    item.insert(QStringLiteral("second"), state.second);
    item.insert(QStringLiteral("end_second"), state.endSecond);
    item.insert(QStringLiteral("early_cleared"), state.earlyCleared);
    item.insert(QStringLiteral("flash_second"), state.flashSecond);

    QJsonArray slideSegments;
    for (const MuriSegmentState& segment : state.slideSegments) {
        slideSegments.append(jsonFromSegmentState(segment));
    }
    item.insert(QStringLiteral("slide_segments"), slideSegments);
    item.insert(QStringLiteral("wifi_lane_areas"), jsonFromLaneCheckpointAreas(state.wifiLaneAreas));
    item.insert(QStringLiteral("wifi_lane_judge_areas"), jsonFromLaneJudgeAreas(state.wifiLaneAreas));
    item.insert(QStringLiteral("wifi_lane_progress_seconds"), jsonFromLaneProgressSeconds(state.wifiLaneProgressSeconds));
    item.insert(QStringLiteral("wifi_completed_second"), state.wifiCompletedSecond);
    item.insert(QStringLiteral("wifi_pad_c_second"), state.wifiPadCSecond);
    item.insert(QStringLiteral("wifi_critical_second"), state.wifiCriticalSecond);
    return item;
}

QJsonObject jsonFromMarker(const TimelineNoteMarker& marker)
{
    QJsonObject item;
    item.insert(QStringLiteral("key"), makeMarkerAnalysisKey(marker));
    item.insert(QStringLiteral("type"), marker.type);
    item.insert(QStringLiteral("line"), marker.sourceLine);
    item.insert(QStringLiteral("col"), marker.sourceCol);
    item.insert(QStringLiteral("second"), marker.second);
    item.insert(QStringLiteral("end_second"), marker.endSecond);
    item.insert(QStringLiteral("slide_trace_second"), marker.slideTraceSecond);
    item.insert(QStringLiteral("available_second"), marker.availableSecond);
    item.insert(QStringLiteral("parse_order"), marker.parseOrder);
    item.insert(QStringLiteral("each_group_id"), marker.eachGroupId);
    item.insert(QStringLiteral("lane"), marker.lane);
    item.insert(QStringLiteral("end_lane"), marker.endLane);
    item.insert(QStringLiteral("touch_pad"), marker.touchPad);
    item.insert(QStringLiteral("slide_track_key"), marker.slideTrackKey);
    item.insert(QStringLiteral("on_slide"), marker.onSlide);
    item.insert(QStringLiteral("slide_head"), marker.slideHead);
    item.insert(QStringLiteral("tail_on_slide_head"), marker.tailOnSlideHead);
    item.insert(QStringLiteral("before_slide"), marker.beforeSlide);
    item.insert(QStringLiteral("after_slide"), marker.afterSlide);
    item.insert(QStringLiteral("slide_each"), marker.slideEach);
    item.insert(QStringLiteral("same_head_slide"), marker.sameHeadSlide);
    item.insert(QStringLiteral("head_each"), marker.headEach);
    item.insert(QStringLiteral("head_break"), marker.headBreak);
    item.insert(QStringLiteral("head_ex"), marker.headEx);
    item.insert(QStringLiteral("has_head_star"), marker.hasHeadStar);

    item.insert(QStringLiteral("segment_keys"), jsonArrayFromStringList(marker.slideSegmentKeys));

    QJsonArray segmentShoots;
    for (double value : marker.slideSegmentShootSeconds) {
        segmentShoots.append(value);
    }
    item.insert(QStringLiteral("segment_shoot_seconds"), segmentShoots);

    QJsonArray segmentDurations;
    for (double value : marker.slideSegmentDurations) {
        segmentDurations.append(value);
    }
    item.insert(QStringLiteral("segment_durations"), segmentDurations);

    QJsonArray segmentPadTimes;
    for (const QVector<MuriPadTimeEntry>& padTimes : marker.slideSegmentPadEnterTimes) {
        segmentPadTimes.append(jsonArrayFromPadTimes(padTimes));
    }
    item.insert(QStringLiteral("segment_pad_enter_times"), segmentPadTimes);

    QJsonArray segmentCriticals;
    for (double value : marker.slideSegmentCriticalProportions) {
        segmentCriticals.append(value);
    }
    item.insert(QStringLiteral("segment_critical_proportions"), segmentCriticals);
    item.insert(QStringLiteral("wifi_pad_enter_times"), jsonArrayFromPadTimes(marker.wifiPadEnterTimes));
    item.insert(QStringLiteral("wifi_critical_proportion"), marker.wifiCriticalProportion);
    return item;
}

QVector<StaticReferenceRecord> buildStaticReferenceRecords(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QVector<int> slideIndices;
    QVector<int> wifiIndices;
    QVector<int> nonSlideIndices;
    slideIndices.reserve(noteMarkers.size());
    wifiIndices.reserve(noteMarkers.size());
    nonSlideIndices.reserve(noteMarkers.size());

    for (int index = 0; index < noteMarkers.size(); ++index) {
        const TimelineNoteMarker& marker = noteMarkers.at(index);
        if (marker.type == QLatin1String("slide")) {
            slideIndices.append(index);
        } else if (marker.type == QLatin1String("wifi")) {
            wifiIndices.append(index);
        } else if (isNonSlideMarker(marker)) {
            nonSlideIndices.append(index);
        }
    }

    QVector<StaticReferenceRecord> records;

    struct CollideEntry {
        QString pad;
        double enterSecond = -1.0;
        double startSecond = -1.0;
        double endSecond = -1.0;
    };

    for (int slideIndex : slideIndices) {
        const TimelineNoteMarker& slide = noteMarkers.at(slideIndex);
        if (slide.slideTraceSecond < 0.0 || slide.endSecond < slide.slideTraceSecond) {
            continue;
        }

        QVector<CollideEntry> collideEntries;
        const int segmentCount = qMin(
            slide.slideSegmentPadEnterTimes.size(),
            qMin(slide.slideSegmentShootSeconds.size(), slide.slideSegmentDurations.size()));
        collideEntries.reserve(segmentCount * 8);
        for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
            const double shootSecond = slide.slideSegmentShootSeconds.at(segmentIndex);
            const double durationSecond = slide.slideSegmentDurations.at(segmentIndex);
            for (const MuriPadTimeEntry& entry : slide.slideSegmentPadEnterTimes.at(segmentIndex)) {
                if (!entry.pad.startsWith(QLatin1Char('A'), Qt::CaseInsensitive)) {
                    continue;
                }
                CollideEntry collide;
                collide.pad = entry.pad.toUpper();
                collide.enterSecond = shootSecond + entry.proportion * durationSecond;
                collide.startSecond = qMax(
                    collide.enterSecond - kStaticCollideExtraDeltaSeconds,
                    slide.slideTraceSecond + miacode::muri::kTapOnSlideThresholdSeconds);
                collide.endSecond = collide.enterSecond + kStaticCollideThresholdSeconds;
                collideEntries.append(collide);
            }
        }

        const QString endPad = lanePadToken(slide.endLane);
        const double criticalSecond = slideCriticalSecondForMarker(slide);
        for (int noteIndex : nonSlideIndices) {
            const TimelineNoteMarker& note = noteMarkers.at(noteIndex);
            if (!isTapMarker(note) && !isHoldMarker(note)) {
                continue;
            }
            if (note.second + kStaticTimeEpsilonSeconds < slide.slideTraceSecond
                || note.second > slide.endSecond + kStaticCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                continue;
            }

            const double headDeltaSecond = note.second - slide.slideTraceSecond;
            if (note.lane == slide.lane
                && headDeltaSecond + kStaticTimeEpsilonSeconds >= miacode::muri::kTapOnSlideThresholdSeconds
                && headDeltaSecond <= kStaticCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                StaticReferenceRecord record;
                record.kind = MuriKind::SlideHeadTap;
                record.affectedIndex = noteIndex;
                record.causeIndex = slideIndex;
                record.deltaSecond = slide.slideTraceSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }

            const QString notePad = markerPadToken(note);
            for (const CollideEntry& collide : collideEntries) {
                if (notePad != collide.pad) {
                    continue;
                }
                if (!markerMomentInRange(note.second, collide.startSecond, collide.endSecond)) {
                    continue;
                }
                StaticReferenceRecord record;
                record.kind = MuriKind::TapOnSlide;
                record.affectedIndex = noteIndex;
                record.causeIndex = slideIndex;
                record.deltaSecond = collide.enterSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }

            if (!endPad.isEmpty()
                && notePad == endPad
                && criticalSecond >= 0.0
                && note.second > criticalSecond + kStaticCollideThresholdSeconds + kStaticTimeEpsilonSeconds
                && note.second <= slide.endSecond + kStaticCollideExtraDeltaSeconds + kStaticTimeEpsilonSeconds) {
                StaticReferenceRecord record;
                record.kind = MuriKind::TapOnSlide;
                record.affectedIndex = noteIndex;
                record.causeIndex = slideIndex;
                record.deltaSecond = criticalSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }
        }
    }

    for (int wifiIndex : wifiIndices) {
        const TimelineNoteMarker& wifi = noteMarkers.at(wifiIndex);
        const double criticalSecond = slideCriticalSecondForMarker(wifi);
        if (wifi.slideTraceSecond < 0.0 || wifi.endSecond < wifi.slideTraceSecond || criticalSecond < 0.0) {
            continue;
        }

        const int endLane = wifi.endLane;
        const QSet<int> endLanes{
            endLane,
            ((endLane + 6) % 8) + 1,
            (endLane % 8) + 1,
        };
        const double startSecond = qMax(
            criticalSecond - kStaticCollideExtraDeltaSeconds,
            wifi.slideTraceSecond + miacode::muri::kTapOnSlideThresholdSeconds);
        const double endSecond = qMax(
            criticalSecond + kStaticCollideThresholdSeconds,
            wifi.endSecond + kStaticCollideExtraDeltaSeconds);

        for (int noteIndex : nonSlideIndices) {
            const TimelineNoteMarker& note = noteMarkers.at(noteIndex);
            if (!isTapMarker(note) && !isHoldMarker(note)) {
                continue;
            }
            if (note.second + kStaticTimeEpsilonSeconds < wifi.slideTraceSecond
                || note.second > wifi.endSecond + kStaticCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                continue;
            }

            const double headDeltaSecond = note.second - wifi.slideTraceSecond;
            if (note.lane == wifi.lane
                && headDeltaSecond + kStaticTimeEpsilonSeconds >= miacode::muri::kTapOnSlideThresholdSeconds
                && headDeltaSecond <= kStaticCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                StaticReferenceRecord record;
                record.kind = MuriKind::SlideHeadTap;
                record.affectedIndex = noteIndex;
                record.causeIndex = wifiIndex;
                record.deltaSecond = wifi.slideTraceSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }

            if (endLanes.contains(note.lane) && markerMomentInRange(note.second, startSecond, endSecond)) {
                StaticReferenceRecord record;
                record.kind = MuriKind::TapOnSlide;
                record.affectedIndex = noteIndex;
                record.causeIndex = wifiIndex;
                record.deltaSecond = criticalSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }
        }
    }

    for (int i = 0; i < nonSlideIndices.size(); ++i) {
        const int noteIndex = nonSlideIndices.at(i);
        const TimelineNoteMarker& note = noteMarkers.at(noteIndex);
        if (note.slideTrackKey == QLatin1String("__static_helper_head_tap__")) {
            continue;
        }
        const QString notePad = markerPadToken(note);
        if (notePad.isEmpty()) {
            continue;
        }

        for (int j = 0; j < nonSlideIndices.size(); ++j) {
            const int note2Index = nonSlideIndices.at(j);
            if (noteIndex == note2Index) {
                continue;
            }
            const TimelineNoteMarker& note2 = noteMarkers.at(note2Index);
            if (note2.slideTrackKey == QLatin1String("__static_helper_head_tap__")) {
                continue;
            }
            if (notePad != markerPadToken(note2)) {
                continue;
            }

            bool overlap = false;
            if (isTapLikeMarker(note)) {
                if (isTapLikeMarker(note2)) {
                    overlap = (i < j)
                        && qAbs(note.second - note2.second) <= kStaticOverlayThresholdSeconds + kStaticTimeEpsilonSeconds;
                } else if (isHoldLikeMarker(note2) && note2.endSecond >= 0.0) {
                    overlap = markerMomentInRange(
                        note.second,
                        note2.second - kStaticOverlayThresholdSeconds,
                        note2.endSecond + kStaticOverlayThresholdSeconds);
                }
            } else if (isHoldLikeMarker(note) && note.endSecond >= 0.0) {
                if (isHoldLikeMarker(note2) && note2.endSecond >= 0.0 && i < j) {
                    overlap =
                        markerMomentInRange(
                            note.second,
                            note2.second - kStaticOverlayThresholdSeconds,
                            note2.endSecond + kStaticOverlayThresholdSeconds)
                        || markerMomentInRange(
                            note2.second,
                            note.second - kStaticOverlayThresholdSeconds,
                            note.endSecond + kStaticOverlayThresholdSeconds);
                }
            }

            if (!overlap) {
                continue;
            }

            StaticReferenceRecord record;
            record.kind = MuriKind::Overlap;
            record.affectedIndex = noteIndex;
            record.causeIndex = note2Index;
            records.append(record);
        }
    }

    std::sort(records.begin(), records.end(), [&noteMarkers](const StaticReferenceRecord& a, const StaticReferenceRecord& b) {
        const TimelineNoteMarker& affectedA = noteMarkers.at(a.affectedIndex);
        const TimelineNoteMarker& affectedB = noteMarkers.at(b.affectedIndex);
        if (!qFuzzyCompare(affectedA.second + 1.0, affectedB.second + 1.0)) {
            return affectedA.second < affectedB.second;
        }
        if (affectedA.sourceLine != affectedB.sourceLine) {
            return affectedA.sourceLine < affectedB.sourceLine;
        }
        if (affectedA.sourceCol != affectedB.sourceCol) {
            return affectedA.sourceCol < affectedB.sourceCol;
        }
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        const TimelineNoteMarker& causeA = noteMarkers.at(a.causeIndex);
        const TimelineNoteMarker& causeB = noteMarkers.at(b.causeIndex);
        if (causeA.sourceLine != causeB.sourceLine) {
            return causeA.sourceLine < causeB.sourceLine;
        }
        return causeA.sourceCol < causeB.sourceCol;
    });

    return records;
}

bool resolveDifficultyId(const QString& token, int* outputId)
{
    if (outputId == nullptr) {
        return false;
    }

    const QString trimmed = token.trimmed();
    bool numericOk = false;
    const int numericId = trimmed.toInt(&numericOk);
    if (numericOk && SimaiDocument::isDifficultyId(numericId)) {
        *outputId = numericId;
        return true;
    }

    for (int id = 1; id <= 7; ++id) {
        if (SimaiDocument::difficultyShortName(id).compare(trimmed, Qt::CaseInsensitive) == 0) {
            *outputId = id;
            return true;
        }
        if (SimaiDocument::difficultyName(id).compare(trimmed, Qt::CaseInsensitive) == 0) {
            *outputId = id;
            return true;
        }
    }
    return false;
}

bool loadChartFromArgs(
    const QCommandLineParser& parser,
    QString* outChartText,
    double* outFirstSeconds,
    QString* outSourceDescription,
    QString* outDifficultyName,
    QString* outErrorMessage)
{
    if (outChartText == nullptr
        || outFirstSeconds == nullptr
        || outSourceDescription == nullptr
        || outDifficultyName == nullptr
        || outErrorMessage == nullptr) {
        return false;
    }

    const QString maidataPath = parser.value(QStringLiteral("maidata")).trimmed();
    const QString filePath = parser.value(QStringLiteral("file")).trimmed();
    if (!maidataPath.isEmpty() && !filePath.isEmpty()) {
        *outErrorMessage = QStringLiteral("Use either --maidata or --file, not both.");
        return false;
    }

    const QString rawFirst = parser.value(QStringLiteral("first")).trimmed();
    const bool firstOverrideSet = !rawFirst.isEmpty();
    bool firstOk = !firstOverrideSet;
    const double firstOverride = firstOverrideSet ? rawFirst.toDouble(&firstOk) : 0.0;
    if (!firstOk) {
        *outErrorMessage = QStringLiteral("--first must be a number.");
        return false;
    }

    if (!maidataPath.isEmpty()) {
        QFile file(maidataPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *outErrorMessage = QStringLiteral("Failed to open maidata: %1").arg(maidataPath);
            return false;
        }

        const SimaiDocument document = SimaiDocument::fromText(QString::fromUtf8(file.readAll()));
        const QString difficultyToken = parser.value(QStringLiteral("difficulty")).trimmed();
        int difficultyId = 0;
        if (!resolveDifficultyId(difficultyToken, &difficultyId)) {
            *outErrorMessage = QStringLiteral("Unknown difficulty: %1").arg(difficultyToken);
            return false;
        }
        const SimaiDifficultyData* difficulty = document.difficulty(difficultyId);
        if (difficulty == nullptr) {
            *outErrorMessage = QStringLiteral("Difficulty %1 not found in maidata.").arg(difficultyToken);
            return false;
        }

        bool parsedFirstOk = true;
        double parsedFirst = 0.0;
        const QString firstText = document.first.trimmed();
        if (!firstText.isEmpty()) {
            parsedFirst = firstText.toDouble(&parsedFirstOk);
        }
        if (!parsedFirstOk) {
            *outErrorMessage = QStringLiteral("Invalid &first value in maidata: %1").arg(document.first);
            return false;
        }

        *outChartText = difficulty->chart;
        *outFirstSeconds = firstOverrideSet ? firstOverride : parsedFirst;
        *outDifficultyName = SimaiDocument::difficultyShortName(difficultyId);
        *outSourceDescription = QStringLiteral("maidata=%1 difficulty=%2")
                                    .arg(maidataPath, *outDifficultyName);
        return true;
    }

    if (!filePath.isEmpty()) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *outErrorMessage = QStringLiteral("Failed to open chart file: %1").arg(filePath);
            return false;
        }
        *outChartText = QString::fromUtf8(file.readAll());
        *outFirstSeconds = firstOverrideSet ? firstOverride : 0.0;
        *outDifficultyName = QStringLiteral("N/A");
        *outSourceDescription = QStringLiteral("file=%1").arg(filePath);
        return true;
    }

    QFile stdinFile;
    if (!stdinFile.open(stdin, QIODevice::ReadOnly)) {
        *outErrorMessage = QStringLiteral("Failed to read chart text from stdin.");
        return false;
    }
    *outChartText = QString::fromUtf8(stdinFile.readAll());
    *outFirstSeconds = firstOverrideSet ? firstOverride : 0.0;
    *outDifficultyName = QStringLiteral("N/A");
    *outSourceDescription = QStringLiteral("stdin");
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Dump MiaCode muri analysis as JSON."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(
        QStringLiteral("maidata"),
        QStringLiteral("Path to maidata.txt."),
        QStringLiteral("path")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("file"),
        QStringLiteral("Path to raw simai chart text."),
        QStringLiteral("path")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("d"), QStringLiteral("difficulty")},
        QStringLiteral("Difficulty short name or id for maidata input."),
        QStringLiteral("difficulty"),
        QStringLiteral("MAS")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("first"),
        QStringLiteral("Override first offset in seconds."),
        QStringLiteral("seconds")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Optional output JSON path."),
        QStringLiteral("path")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("pretty"),
        QStringLiteral("Pretty-print JSON output.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("with-markers"),
        QStringLiteral("Include note marker metadata in output.")
    ));

    if (!parser.parse(app.arguments())) {
        QTextStream(stderr) << parser.errorText() << '\n';
        return 2;
    }

    QString chartText;
    double firstSeconds = 0.0;
    QString sourceDescription;
    QString difficultyName;
    QString loadError;
    if (!loadChartFromArgs(
            parser,
            &chartText,
            &firstSeconds,
            &sourceDescription,
            &difficultyName,
            &loadError)) {
        QTextStream(stderr) << loadError << '\n';
        return 2;
    }

    const SimaiNativeParseResult nativeResult = SimaiNativeParser::parseForTimeline(chartText);
    const QVector<TimelineNoteMarker> shiftedMarkers = shiftedNoteMarkers(nativeResult.noteMarkers, firstSeconds);
    const MuriAnalysisReport report = MuriAnalyzer::analyze(shiftedMarkers);

    QJsonObject parseObject;
    parseObject.insert(QStringLiteral("ok"), nativeResult.ok);
    parseObject.insert(QStringLiteral("error_count"), nativeResult.errors.size());
    parseObject.insert(QStringLiteral("warning_count"), nativeResult.warnings.size());

    QJsonArray parseErrors;
    for (const SimaiNativeMessage& error : nativeResult.errors) {
        parseErrors.append(jsonFromParseMessage(error));
    }
    parseObject.insert(QStringLiteral("errors"), parseErrors);

    QJsonArray parseWarnings;
    for (const SimaiNativeMessage& warning : nativeResult.warnings) {
        parseWarnings.append(jsonFromParseMessage(warning));
    }
    parseObject.insert(QStringLiteral("warnings"), parseWarnings);

    QJsonObject countsByKind;
    int earlyClearedCount = 0;
    for (const MuriDiagnostic& diagnostic : report.diagnostics) {
        const QString kind = muriKindKey(diagnostic.kind);
        countsByKind.insert(kind, countsByKind.value(kind).toInt() + 1);
    }

    QList<MarkerMuriState> markerStates = report.markerStates.values();
    std::sort(markerStates.begin(), markerStates.end(), [](const MarkerMuriState& a, const MarkerMuriState& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second < b.second;
        }
        if (a.line != b.line) {
            return a.line < b.line;
        }
        if (a.col != b.col) {
            return a.col < b.col;
        }
        return a.markerKey < b.markerKey;
    });
    for (const MarkerMuriState& state : markerStates) {
        if (state.earlyCleared) {
            ++earlyClearedCount;
        }
    }

    const TimelineChartCounts chartCounts = countTimelineChartNotes(shiftedMarkers);
    const QVector<MuriStaticReference> staticReferences = miacode::muri::buildStaticMuriReferences(
        shiftedMarkers,
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0);
    QJsonObject staticReferenceCountsByKind;
    for (const MuriStaticReference& record : staticReferences) {
        const QString kind = muriKindKey(record.kind);
        staticReferenceCountsByKind.insert(kind, staticReferenceCountsByKind.value(kind).toInt() + 1);
    }

    QJsonObject summaryObject;
    summaryObject.insert(QStringLiteral("note_count"), chartCounts.noteCount);
    summaryObject.insert(QStringLiteral("marker_count"), chartCounts.markerCount);
    summaryObject.insert(QStringLiteral("tap_count"), chartCounts.tapCount);
    summaryObject.insert(QStringLiteral("hold_count"), chartCounts.holdCount);
    summaryObject.insert(QStringLiteral("slide_count"), chartCounts.slideCount);
    summaryObject.insert(QStringLiteral("touch_count"), chartCounts.touchCount);
    summaryObject.insert(QStringLiteral("break_count"), chartCounts.breakCount);
    summaryObject.insert(QStringLiteral("helper_head_tap_count"), chartCounts.helperHeadTapCount);
    summaryObject.insert(QStringLiteral("helper_head_break_count"), chartCounts.helperHeadBreakCount);
    summaryObject.insert(QStringLiteral("diagnostic_count"), report.diagnostics.size());
    summaryObject.insert(QStringLiteral("marker_state_count"), report.markerStates.size());
    summaryObject.insert(QStringLiteral("early_cleared_count"), earlyClearedCount);
    summaryObject.insert(QStringLiteral("counts_by_kind"), countsByKind);
    summaryObject.insert(QStringLiteral("static_reference_count"), staticReferences.size());
    summaryObject.insert(QStringLiteral("static_reference_counts_by_kind"), staticReferenceCountsByKind);

    QJsonArray diagnosticsArray;
    for (const MuriDiagnostic& diagnostic : report.diagnostics) {
        diagnosticsArray.append(jsonFromDiagnostic(diagnostic));
    }

    QJsonArray staticReferencesArray;
    for (const MuriStaticReference& record : staticReferences) {
        staticReferencesArray.append(jsonFromStaticReferenceRecord(record));
    }

    QJsonArray markerStatesArray;
    for (const MarkerMuriState& state : markerStates) {
        markerStatesArray.append(jsonFromMarkerState(state));
    }

    QJsonObject root;
    root.insert(QStringLiteral("ok"), nativeResult.ok);
    root.insert(QStringLiteral("source"), sourceDescription);
    root.insert(QStringLiteral("difficulty"), difficultyName);
    root.insert(QStringLiteral("first"), firstSeconds);
    root.insert(QStringLiteral("parse"), parseObject);
    root.insert(QStringLiteral("summary"), summaryObject);
    root.insert(QStringLiteral("diagnostics"), diagnosticsArray);
    root.insert(QStringLiteral("static_references"), staticReferencesArray);
    root.insert(QStringLiteral("marker_states"), markerStatesArray);

    if (parser.isSet(QStringLiteral("with-markers"))) {
        QJsonArray markersArray;
        for (const TimelineNoteMarker& marker : shiftedMarkers) {
            markersArray.append(jsonFromMarker(marker));
        }
        root.insert(QStringLiteral("markers"), markersArray);
    }

    const QJsonDocument document(root);
    const QByteArray output = document.toJson(
        parser.isSet(QStringLiteral("pretty")) ? QJsonDocument::Indented : QJsonDocument::Compact
    );

    const QString outputPath = parser.value(QStringLiteral("output")).trimmed();
    if (!outputPath.isEmpty()) {
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream(stderr) << QStringLiteral("Failed to write output file: %1").arg(outputPath) << '\n';
            return 1;
        }
        file.write(output);
        file.close();
        return nativeResult.ok ? 0 : 2;
    }

    QTextStream(stdout) << output;
    return nativeResult.ok ? 0 : 2;
}
