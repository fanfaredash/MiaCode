#include "preview/scene/PreviewTrackShared.h"

#include <QtMath>

namespace miacode::preview::scene {

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
