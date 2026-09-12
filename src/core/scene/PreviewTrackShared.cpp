#include "core/scene/PreviewTrackShared.h"

#include "core/scene/PreviewSceneConstants.h"

#include <QtMath>

#include <cmath>

namespace miacode::preview::scene {
namespace {

// Track proportions are authored to six decimals, so compare with a slack that
// is far below one arrow spacing but above the authoring rounding.
constexpr double kProportionEpsilon = 1e-9;

// Arrow index inside one hit area at which the star leaves that area. This is
// the checkpoint the arcade's GetDeleteArrowDistance stops at; when the shape
// records no crossing strictly inside the area, the star leaves it exactly as
// the next area begins, which puts every arrow of the area behind it.
int hiddenCutWithinArea(
    const TimelineNoteMarker& marker,
    int segmentIndex,
    int areaIndex,
    const QVector<double>& thresholds,
    int areaArrowCount
)
{
    const QVector<double>& checkpoints =
        marker.slideTrackAreaCheckpoints.value(segmentIndex).value(areaIndex);
    const QVector<int>& cutIndices =
        marker.slideTrackAreaCutIndices.value(segmentIndex).value(areaIndex);
    if (checkpoints.isEmpty() || cutIndices.size() != checkpoints.size()) {
        return areaArrowCount;
    }

    const double areaStart = thresholds.value(areaIndex, 0.0);
    const double areaEnd = areaIndex + 1 < thresholds.size() ? thresholds.at(areaIndex + 1) : 1.0;

    int cut = -1;
    for (int checkpointIndex = 0; checkpointIndex < checkpoints.size(); ++checkpointIndex) {
        const double checkpoint = checkpoints.at(checkpointIndex);
        if (checkpoint > areaStart - kProportionEpsilon && checkpoint < areaEnd - kProportionEpsilon) {
            cut = cutIndices.at(checkpointIndex);
        }
    }
    if (cut < 0) {
        return areaArrowCount;
    }
    return qBound(0, cut, areaArrowCount);
}

}  // namespace

int currentAreaIndexForProportion(const QVector<double>& thresholds, qreal proportion, int areaCount)
{
    if (areaCount <= 1) {
        return 0;
    }

    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    if (!thresholds.isEmpty()) {
        const int limit = qMin(areaCount, thresholds.size());
        int currentArea = 0;
        for (int nextArea = 1; nextArea < limit; ++nextArea) {
            if (clamped >= thresholds[nextArea]) {
                currentArea = nextArea;
            } else {
                break;
            }
        }
        return qBound(0, currentArea, areaCount - 1);
    }

    return qBound(0, static_cast<int>(qFloor(clamped * (areaCount - 1))), areaCount - 1);
}

int passedCheckpointCount(const QVector<MuriCheckpointState>& checkpoints, double playheadSeconds)
{
    int passed = 0;
    for (const MuriCheckpointState& checkpoint : checkpoints) {
        if (checkpoint.second >= 0.0 && checkpoint.second <= playheadSeconds + 1e-6) {
            ++passed;
        }
    }
    return passed;
}

int slideAreaCutForPassedCount(const QVector<int>& cutIndices, int passedCount, int pointCount)
{
    if (passedCount <= 0) {
        return 0;
    }
    if (cutIndices.isEmpty()) {
        return qBound(0, passedCount, pointCount);
    }

    const int index = qBound(0, passedCount - 1, cutIndices.size() - 1);
    return qBound(0, cutIndices.value(index), pointCount);
}

int slideAreaTrimCountForProportion(
    const QVector<QPointF>& areaPoints,
    const QVector<double>& areaThresholds,
    const QVector<double>& areaCheckpoints,
    const QVector<int>& areaCutIndices,
    int areaIndex,
    qreal proportion
)
{
    if (areaPoints.isEmpty()) {
        return 0;
    }

    qreal areaStart = 0.0;
    qreal areaEnd = 1.0;
    if (!areaThresholds.isEmpty()) {
        areaStart = qBound<qreal>(0.0, areaThresholds.value(areaIndex, 0.0), 1.0);
        if (areaIndex + 1 < areaThresholds.size()) {
            areaEnd = qBound<qreal>(areaStart, areaThresholds.at(areaIndex + 1), 1.0);
        }
    }

    const qreal clampedProportion = qBound<qreal>(0.0, proportion, 1.0);
    if (!areaCheckpoints.isEmpty()) {
        const int checkpointCount = areaCheckpoints.size();
        const auto cutAt = [&](int index) {
            if (!areaCutIndices.isEmpty()) {
                return qBound(0, areaCutIndices.value(index, 0), areaPoints.size());
            }
            return qBound(
                0,
                qFloor(static_cast<qreal>(index + 1) * areaPoints.size() / checkpointCount),
                areaPoints.size()
            );
        };

        qreal previousThreshold = areaStart;
        int previousCut = 0;
        for (int checkpointIndex = 0; checkpointIndex < checkpointCount; ++checkpointIndex) {
            const qreal checkpointThreshold = qBound<qreal>(
                previousThreshold,
                areaCheckpoints.at(checkpointIndex),
                areaEnd
            );
            const int checkpointCut = cutAt(checkpointIndex);
            if (clampedProportion <= checkpointThreshold) {
                const qreal span = qMax<qreal>(0.001, checkpointThreshold - previousThreshold);
                const qreal localT = qBound<qreal>(0.0, (clampedProportion - previousThreshold) / span, 1.0);
                return qBound(
                    0,
                    qFloor(previousCut + (checkpointCut - previousCut) * localT),
                    areaPoints.size()
                );
            }
            previousThreshold = checkpointThreshold;
            previousCut = checkpointCut;
        }

        if (areaEnd > previousThreshold && previousCut < areaPoints.size()) {
            const qreal span = qMax<qreal>(0.001, areaEnd - previousThreshold);
            const qreal localT = qBound<qreal>(0.0, (clampedProportion - previousThreshold) / span, 1.0);
            return qBound(
                0,
                qFloor(previousCut + (areaPoints.size() - previousCut) * localT),
                areaPoints.size()
            );
        }
        return previousCut;
    }

    const qreal span = qMax<qreal>(0.001, areaEnd - areaStart);
    const qreal localT = qBound<qreal>(0.0, (clampedProportion - areaStart) / span, 1.0);
    return qBound(0, qFloor(localT * areaPoints.size()), areaPoints.size());
}

int totalSlideTrackArrowCount(const QVector<QVector<QVector<QPointF>>>& segmentAreas)
{
    int totalArrowCount = 0;
    for (const QVector<QVector<QPointF>>& areas : segmentAreas) {
        for (const QVector<QPointF>& areaPoints : areas) {
            totalArrowCount += areaPoints.size();
        }
    }
    return totalArrowCount;
}

int totalWifiTrackArrowCount(const QVector<QVector<QPointF>>& areas)
{
    int totalArrowCount = 0;
    for (const QVector<QPointF>& areaPoints : areas) {
        totalArrowCount += areaPoints.size();
    }
    return totalArrowCount;
}

PreviewSlideEraseByAreaData buildPreviewSlideEraseByAreaData(const TimelineNoteMarker& marker)
{
    PreviewSlideEraseByAreaData eraseByAreaData;
    const int segmentCount = marker.slideTrackAreaPoints.size();
    if (segmentCount <= 0) {
        return eraseByAreaData;
    }

    int globalArrowOffset = 0;
    for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        PreviewSlideEraseByAreaSegment segment;
        segment.arrowOffset = globalArrowOffset;
        const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
        const QVector<double>& thresholds = marker.slideTrackAreaThresholds.value(segmentIndex);
        for (int areaIndex = 0; areaIndex < areas.size(); ++areaIndex) {
            const int areaArrowCount = areas[areaIndex].size();
            const int hiddenAt = segment.totalArrowCount
                + hiddenCutWithinArea(marker, segmentIndex, areaIndex, thresholds, areaArrowCount);
            segment.hiddenArrowCountAfterArea.append(hiddenAt);
            segment.totalArrowCount += areaArrowCount;
        }

        // The last segment must have the same erasure curve as the same shape
        // rendered independently. Earlier segments use the complete local
        // duration, then their remaining tail disappears at the join.
        if (segmentIndex == segmentCount - 1
            && segmentIndex < marker.slideSegmentCriticalProportions.size()) {
            segment.criticalProportion = qBound(
                kRenderDurationEpsilon,
                marker.slideSegmentCriticalProportions.at(segmentIndex),
                1.0
            );
        }

        globalArrowOffset += segment.totalArrowCount;
        eraseByAreaData.segments.append(segment);
    }
    eraseByAreaData.totalArrowCount = globalArrowOffset;
    return eraseByAreaData;
}

void previewSlideStarSegment(
    const TimelineNoteMarker& marker,
    double playheadSeconds,
    int segmentCount,
    int* outSegmentIndex,
    qreal* outSegmentProportion
)
{
    int segmentIndex = 0;
    if (!marker.slideSegmentShootSeconds.isEmpty()
        && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()) {
        for (int index = marker.slideSegmentShootSeconds.size() - 1; index >= 0; --index) {
            if (playheadSeconds >= marker.slideSegmentShootSeconds[index]) {
                segmentIndex = index;
                break;
            }
        }
    }
    if (segmentCount > 0) {
        segmentIndex = qBound(0, segmentIndex, segmentCount - 1);
    }

    qreal proportion = 1.0;
    if (segmentIndex < marker.slideSegmentShootSeconds.size()
        && segmentIndex < marker.slideSegmentDurations.size()) {
        const qreal duration =
            qMax<qreal>(kRenderDurationEpsilon, marker.slideSegmentDurations[segmentIndex]);
        proportion = qBound<qreal>(
            0.0,
            static_cast<qreal>((playheadSeconds - marker.slideSegmentShootSeconds[segmentIndex]) / duration),
            1.0
        );
    }

    if (outSegmentIndex != nullptr) {
        *outSegmentIndex = segmentIndex;
    }
    if (outSegmentProportion != nullptr) {
        *outSegmentProportion = proportion;
    }
}

int previewSlideEraseByAreaHiddenArrowCount(
    const PreviewSlideEraseByAreaData& data,
    int segmentIndex,
    qreal segmentProportion
)
{
    if (!data.isValid()) {
        return 0;
    }

    const int clampedSegmentIndex = qBound(0, segmentIndex, data.segments.size() - 1);
    const PreviewSlideEraseByAreaSegment& segment = data.segments.at(clampedSegmentIndex);
    if (!segment.isValid()) {
        return qBound(0, segment.arrowOffset, data.totalArrowCount);
    }

    const int areaCount = segment.areaCount();
    const qreal num6 = qMax<qreal>(0.0, segmentProportion) / segment.criticalProportion;
    const qreal scaled = areaCount * num6;
    const int hitIndex = scaled >= areaCount ? areaCount - 1 : static_cast<int>(qMax<qreal>(0.0, scaled));
    if (hitIndex <= 0) {
        return qBound(0, segment.arrowOffset, data.totalArrowCount);
    }
    return qBound(
        0,
        segment.arrowOffset + segment.hiddenArrowCountAfterArea.at(hitIndex - 1),
        data.totalArrowCount
    );
}

int previewWifiEraseByAreaHiddenRowCount(
    const QVector<int>& areaRowCounts,
    double criticalProportion,
    qreal starProgress
)
{
    const int areaCount = areaRowCounts.size();
    if (areaCount <= 0 || criticalProportion <= 0.0) {
        return 0;
    }

    const qreal num = qMax<qreal>(0.0, starProgress) / criticalProportion;
    const qreal scaled = areaCount * num;
    // Unlike the slide clear, the index is allowed to reach areaCount, so every
    // row is cleared once the star reaches the judge point rather than a few
    // rows stalling for the rest of the note.
    const int hitIndex = scaled >= areaCount ? areaCount : static_cast<int>(qMax<qreal>(0.0, scaled));

    int rows = 0;
    for (int areaIndex = 0; areaIndex < hitIndex; ++areaIndex) {
        rows += qMax(0, areaRowCounts.at(areaIndex));
    }
    return rows;
}

int currentWifiLaneAreaIndexAt(
    const QVector<double>& progressSeconds,
    const QVector<QVector<MuriCheckpointState>>& laneAreas,
    double playheadSeconds
)
{
    if (!progressSeconds.isEmpty()) {
        int areaIndex = 0;
        for (int index = 1; index < progressSeconds.size(); ++index) {
            const double second = progressSeconds.at(index);
            if (second < 0.0 || second > playheadSeconds + 1e-6) {
                break;
            }
            areaIndex = index;
        }
        return areaIndex;
    }

    int areaIndex = 0;
    for (const QVector<MuriCheckpointState>& checkpoints : laneAreas) {
        if (passedCheckpointCount(checkpoints, playheadSeconds) <= 0) {
            break;
        }
        ++areaIndex;
    }
    return areaIndex;
}

}  // namespace miacode::preview::scene
