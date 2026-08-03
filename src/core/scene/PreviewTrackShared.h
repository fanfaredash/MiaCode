#pragma once

#include <QPointF>
#include <QVector>

#include "timeline/TimelineData.h"

namespace miacode::preview::scene {

enum class PreviewSlideTrackTrimMode {
    UniformTime = 0,
    AreaImmediate = 1,
    VanillaAutoplay = 2,
};

inline constexpr PreviewSlideTrackTrimMode kPreviewSlideTrackTrimMode = PreviewSlideTrackTrimMode::UniformTime;

// ---------------------------------------------------------------------------
// Vanilla autoplay track erasure
// ---------------------------------------------------------------------------
//
// Simulates how the arcade game clears slide-track arrows while autoplaying,
// which differs in shape from both existing trim modes: the arcade advances a
// hit-area index at even fractions of the trace window and erases up to the
// point where the star left that hit area, so the track clears in a few large
// steps and the tail arrows stay lit until the whole track disappears.
//
// Arcade form (SlideRoot.NoteCheck / GetDeleteArrowDistance):
//
//   num6     = (now - launch) / (arrive - launch - lastWaitTime)
//   hitIndex = clamp((int)(areaCount * num6), 0, areaCount - 1)
//   hidden   = (int)(arrowCount * deleteDistance(hitIndex))
//
// Expressed against the star's own progress `p` along the whole slide, this is
// `num6 == p / criticalProportion`, because the arcade star advances linearly
// with time and `criticalProportion == 1 - lastWaitTime / (arrive - launch)`.
// Driving it from `p` rather than from a raw time window is what keeps the
// erasure under the star for connected slides, whose segments MiaCode times
// separately.
//
// `deleteDistance(hitIndex)` reduces to "the position at which the star left
// hit area hitIndex - 1", which `track_checkpoints` / `track_cut_indices`
// already record as an arrow index, so no additional shape data is needed.
struct PreviewSlideAutoplayAreas {
    // Total arrows hidden once the star has moved past merged hit area k.
    QVector<int> hiddenArrowCountAfterArea;
    // 1 - lastWaitTime / traceDuration, weighted across a connected chain.
    double criticalProportion = 1.0;
    int totalArrowCount = 0;

    int areaCount() const { return hiddenArrowCountAfterArea.size(); }
    bool isValid() const { return areaCount() > 0 && criticalProportion > 0.0; }
};

PreviewSlideAutoplayAreas buildPreviewSlideAutoplayAreas(const TimelineNoteMarker& marker);

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

// Star progress over the whole slide, weighting segments by their parsed trace
// durations (arrow counts when durations are unavailable).
qreal previewSlideStarProgress(const TimelineNoteMarker& marker, double playheadSeconds);

int previewSlideVanillaHiddenArrowCount(const PreviewSlideAutoplayAreas& areas, qreal starProgress);

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
