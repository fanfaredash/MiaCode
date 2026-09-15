#pragma once

#include <QPointF>
#include <QVector>

#include "timeline/TimelineData.h"

namespace miacode::preview::scene {

enum class PreviewSlideTrackTrimMode {
    UniformTime = 0,
    AreaImmediate = 1,
    EraseByArea = 2,
};

inline constexpr PreviewSlideTrackTrimMode kPreviewSlideTrackTrimMode = PreviewSlideTrackTrimMode::UniformTime;

// ---------------------------------------------------------------------------
// Erase-by-area track erasure
// ---------------------------------------------------------------------------
//
// Clears slide-track arrows in whole hit-area steps. For standalone slides,
// the timing follows the arcade autoplay calculation: a hit-area index advances
// at even fractions of the trace window and erases up to the point where the
// star left that hit area, leaving the tail lit until the track disappears.
//
// Arcade form (SlideRoot.NoteCheck / GetDeleteArrowDistance):
//
//   num6     = (now - launch) / (arrive - launch - lastWaitTime)
//   hitIndex = clamp((int)(areaCount * num6), 0, areaCount - 1)
//   hidden   = (int)(arrowCount * deleteDistance(hitIndex))
//
// Expressed against the star's progress `p` along one segment, this is
// `num6 == p / criticalProportion`, because the arcade star advances linearly
// with time and `criticalProportion == 1 - lastWaitTime / (arrive - launch)`.
// A standalone slide therefore follows the arcade formula exactly. Connected
// slides deliberately restart this clock for each segment: intermediate
// segments use their whole local duration, while the final segment keeps its
// standalone critical proportion. This avoids the arcade's merged-list quirk
// erasing arrows from the next segment before the star reaches the join.
//
// `deleteDistance(hitIndex)` reduces to "the position at which the star left
// hit area hitIndex - 1", which `track_checkpoints` / `track_cut_indices`
// already record as an arrow index, so no additional shape data is needed.
struct PreviewSlideEraseByAreaSegment {
    // Arrows hidden within this segment once the star has moved past area k.
    QVector<int> hiddenArrowCountAfterArea;
    // Global arrow offset of this segment in the rendered connected track.
    int arrowOffset = 0;
    int totalArrowCount = 0;
    // Final segment: the shape's standalone arcade value. Intermediate
    // segments: 1.0, so their equal-area clock spans the whole segment.
    double criticalProportion = 1.0;

    int areaCount() const { return hiddenArrowCountAfterArea.size(); }
    bool isValid() const { return areaCount() > 0 && criticalProportion > 0.0; }
};

struct PreviewSlideEraseByAreaData {
    QVector<PreviewSlideEraseByAreaSegment> segments;
    int totalArrowCount = 0;

    bool isValid() const { return !segments.isEmpty() && totalArrowCount > 0; }
};

PreviewSlideEraseByAreaData buildPreviewSlideEraseByAreaData(const TimelineNoteMarker& marker);

// Locates the star the way PreviewSlideMotionLayerState draws it: the segment
// whose shoot time has passed, plus the proportion travelled within it. Shared
// so the erasure front cannot drift away from the star it is meant to follow.
void previewSlideStarSegment(
    const TimelineNoteMarker& marker,
    double playheadSeconds,
    int segmentCount,
    int* outSegmentIndex,
    qreal* outSegmentProportion
);

// Returns a global hidden-arrow count. `segmentProportion` is local to the
// selected segment; all arrows belonging to earlier segments are considered
// hidden once the star reaches this segment.
int previewSlideEraseByAreaHiddenArrowCount(
    const PreviewSlideEraseByAreaData& data,
    int segmentIndex,
    qreal segmentProportion
);

// The wifi counterpart. `wifiTrackAreaPoints` groups the eleven rows into the
// same four judge areas as `tri_judge_sequence`, so the rows clear area by area
// on the same hit-area clock as a slide.
//
// This deliberately does NOT reproduce the arcade's autoplay fan clear.
// SlideFan.NoteCheck compares a bare `num7` against the row index instead of
// scaling it by the row count, so `num7` barely passes 1 by the time the star
// lands and only one or two of the eleven rows ever clear; real play uses touch
// progress and clears all of them. Reproducing that here would leave a mode
// named "erase by area" barely erasing anything, so the clear runs to
// completion: the hit-area index is allowed to reach `areaCount`, clearing
// every row by the time the note is judged.
int previewWifiEraseByAreaHiddenRowCount(
    const QVector<int>& areaRowCounts,
    double criticalProportion,
    qreal starProgress
);

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
