#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "common/MuriTypes.h"

struct TimelineNoteMarker;

// Overlay-build stage of the Muri analyzer (2026-05-29 god-file decomposition,
// stage 2). Two related jobs that both turn parsed note markers into render-time
// state:
//   * buildOverlayActions — the playfield overlay: a pad-press window + a
//     hand-action trail per note (taps/holds/touches/slides/wifi).
//   * buildSlideState / buildWifiState — the per-marker "Muri state" for one
//     slide / wifi trace: its segment area-checkpoint timeline (resolved against
//     the surrounding pad windows) plus the early-clear flash when the trace
//     would complete too fast. analyze() builds these after the overlay pass so
//     the pad-window index is available. Internal to the Muri analyzer —
//     namespace miacode::muri::detail.
//
// NOT owned here (shared, declared in MuriAnalyzerInternal.h): the pad-window /
// trail primitives addPadWindow / addActionTrail / addSlidePadWindowsAndTrails /
// addWifiPadWindowsAndTrails / notePad (also used by the runtime pad-window
// builder) and the slide-judge timing math slideCriticalDeltaSecond /
// slideJudgeIsBad / wifiJudgeIsBad (also used by the judge stages and analyze()).
namespace miacode::muri::detail {

// Appends a pad-press window and hand-action trail for every note marker. This
// is the report's playfield overlay; pure data → data.
void buildOverlayActions(
    const QVector<TimelineNoteMarker>& noteMarkers,
    QVector<MuriPadWindow>* padWindows,
    QVector<MuriActionTrail>* actionTrails);

// Reconstructs the per-marker Muri state for a slide marker: each segment's area
// checkpoints are resolved against the pad windows in allWindows (indexed by pad
// in windowsByPad), and the marker is flagged early-cleared/flash when its last
// segment would land outside the critical window.
MarkerMuriState buildSlideState(
    const TimelineNoteMarker& marker,
    const QHash<QString, QVector<int>>& windowsByPad,
    const QVector<MuriPadWindow>& allWindows);

// Wifi counterpart of buildSlideState. Wifi traces have no pad-window
// dependency, so this resolves purely from the marker's own enter-time timeline.
MarkerMuriState buildWifiState(const TimelineNoteMarker& marker);

}  // namespace miacode::muri::detail
