#pragma once

#include <QPointF>
#include <QVector>

#include "timeline/TimelineData.h"

namespace miacode::preview::scene {

enum class PreviewSlideTrackTrimMode {
    UniformTime = 0,
    AreaImmediate = 1,
};

inline constexpr PreviewSlideTrackTrimMode kPreviewSlideTrackTrimMode = PreviewSlideTrackTrimMode::UniformTime;

int currentAreaIndexForProportion(const QVector<double>& thresholds, qreal proportion, int areaCount);
int passedCheckpointCount(const QVector<MuriCheckpointState>& checkpoints, double playheadSeconds);
int slideAreaCutForPassedCount(const QVector<int>& cutIndices, int passedCount, int pointCount);
int slideAreaTrimCountForProportion(
    const QVector<QPointF>& areaPoints,
    const QVector<double>& areaThresholds,
    const QVector<double>& areaCheckpoints,
    const QVector<int>& areaCutIndices,
    int areaIndex,
    qreal proportion
);
int totalSlideTrackArrowCount(const QVector<QVector<QVector<QPointF>>>& segmentAreas);
int totalWifiTrackArrowCount(const QVector<QVector<QPointF>>& areas);
int currentWifiLaneAreaIndexAt(
    const QVector<double>& progressSeconds,
    const QVector<QVector<MuriCheckpointState>>& laneAreas,
    double playheadSeconds
);

}  // namespace miacode::preview::scene
