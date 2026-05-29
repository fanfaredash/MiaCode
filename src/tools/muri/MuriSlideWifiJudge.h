#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "tools/muri/MuriAnalyzerModel.h"

struct TimelineNoteMarker;

// Slide / wifi runtime-judge stage of the Muri analyzer (2026-05-29 god-file
// decomposition, stage 3). This is the time-stepped simulation that decides
// whether each slide / wifi trace would be "judged too fast" — i.e. cleared by
// finger traffic from *other* notes crossing its pads before the player could
// have completed it. It replays the chart tick-by-tick:
//   * build a runtime judge model per slide / wifi note (its ordered pad-area
//     judge sequence, loaded from slide_data.json),
//   * reconstruct the hands' pad-press footprint each tick (shared
//     buildRuntimeHandActions / buildRuntimeTouchPoints, see Internal.h),
//   * advance each note's area cursor as its pads go down / release, recording
//     which marker caused each area to clear,
//   * flag the note bad when it finishes outside its critical window AND its
//     final area was cleared by a foreign marker.
// The result feeds back into the per-marker MarkerMuriState (overlay stage) and
// the slide-too-fast diagnostics. Internal to the Muri analyzer — namespace
// miacode::muri::detail.
//
// All the working state (RuntimeSlideNoteState / RuntimeWifiNoteState /
// EarlyJudgeCauseInfo) and the per-tick stepping helpers stay private to the .cpp
// — only the four entry points below are shared with analyze().
namespace miacode::muri::detail {

// Replays every slide / wifi marker against the full hand-action footprint and
// returns, per marker key, whether/when it was judged and the per-area hit trail.
QHash<QString, RuntimeSlideJudgeResult> simulateRuntimeSlideAndWifiJudgments(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex,
    const MuriRenderOptions& renderOptions);

// Folds a simulation result back into the marker's overlay-built MarkerMuriState
// (per-area checkpoint causes + early-clear flash).
void applyRuntimeJudgeResultToState(const RuntimeSlideJudgeResult& result, MarkerMuriState* state);

// Source anchor for a slide-too-fast diagnostic: the foreign marker that
// early-judged this trace if known, else the trace's own marker.
DiagnosticAnchor diagnosticAnchorForSlideJudge(
    const TimelineNoteMarker& marker,
    const MarkerMuriState& state,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys);

// Human-readable detail line for a slide-too-fast diagnostic (target label,
// early-judge cause, and the timing gap in ms).
QString formatSlideTooFastDetail(
    const TimelineNoteMarker& marker,
    const MarkerMuriState& state,
    const QHash<QString, QString>& markerConfigLabels,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys);

}  // namespace miacode::muri::detail
