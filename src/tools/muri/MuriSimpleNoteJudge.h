#pragma once

#include <QVector>

#include "common/MuriRenderOptions.h"
#include "tools/muri/MuriDiagnosticCollector.h"

struct TimelineNoteMarker;

// Simple-note judge stage of the Muri analyzer (2026-05-29 god-file
// decomposition, stage 4). Owns the runtime judging of taps / holds / touches
// (everything that is not a slide / wifi trace) and the diagnostics that fall
// out of it:
//   * a tick-stepped simulation that decides, per simple note, whether it would
//     be judged on time, judged-bad (early/late), or cleared by foreign pad
//     traffic — including each-group "touch cluster" majority judging;
//   * multi-touch detection (>2 simultaneous hands) via per-tick action
//     clustering;
//   * the tap-on-slide / slide-head-tap / same-position-overlap diagnostics and
//     the simple-note judge sprite events.
// Internal to the Muri analyzer — namespace miacode::muri::detail.
//
// Only the orchestration entry point is exposed; the simulation, the multi-touch
// clustering, and the per-diagnostic emission stay private to the .cpp. The label
// / anchor / alert-level formatting it leans on lives in MuriDiagnosticLabels.h;
// emitted diagnostics + sprite events are written into the shared
// MuriDiagnosticCollector.
namespace miacode::muri::detail {

// Builds, judges, and reports the simple-note (tap/hold/touch) layer for one
// chart, writing tap-on-slide / overlap / multi-touch diagnostics and the
// simple-note judge sprite events into the collector. staticTapOnSlideThresholdSeconds
// is the user-tunable tap-on-slide warning cutoff.
void collectSimpleNoteRuntimeDiagnostics(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const MuriRenderOptions& renderOptions,
    MuriDiagnosticCollector& collector,
    double staticTapOnSlideThresholdSeconds);

}  // namespace miacode::muri::detail
