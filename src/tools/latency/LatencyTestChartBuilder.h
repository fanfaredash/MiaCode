#pragma once

#include <QString>

namespace miacode::latency {

// Pure function: generate a synthetic simai chart text consisting of a
// long run of plain tap notes (`1,1,1,1,...,E`) at the given BPM and
// subdivision. Used by the latency-detection sandbox to drive the
// existing timeline + SFX engine without ever modifying the user's real
// document.
//
// `subdivision` must be 4 or 8. Anything else is clamped to 4.
// `durationSeconds` is the desired play length; the generator emits
// enough notes to cover that span plus a small tail margin.
// If `durationSeconds <= 0`, falls back to a 180-second default so the
// test chart still has content when no audio is loaded.
QString buildTestChartText(double bpm, int subdivision, double durationSeconds);

}  // namespace miacode::latency
