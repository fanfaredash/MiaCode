#pragma once

namespace miacode::content_duration {

// Trailing tail appended AFTER the last chart note when sizing the "content
// duration" of a chart. Gives the playfield a moment to settle once the final
// note resolves instead of cutting to black on the last hit.
inline constexpr double kTrailingTailSeconds = 3.0;

// THE unified content-duration policy, shared by the realtime preview
// (previewDurationSeconds / previewPlaybackEndSeconds) and every full-range
// export path (single, batch, CLI seed). The deliverable / preview runs to:
//
//     max(chartEnd + kTrailingTailSeconds, musicDuration)
//
// i.e. whichever finishes later — the chart plus its settle tail, or the music
// track in full. Zero-guards keep an empty chart (chartEnd <= 0) from emitting a
// spurious tail and ignore a missing/unprobeable track (musicDuration <= 0).
//
// `chartEndSeconds` is each subsystem's own notion of "last note" (the preview
// timeline's durationSeconds; the export's richer lastMarkerEndSecond that also
// folds in slide trace / available / shoot seconds) — only the FORMULA is shared,
// not the input, because preview and export compute the chart end from different
// data sources.
inline double totalContentDurationSeconds(double chartEndSeconds, double musicDurationSeconds)
{
    double duration = 0.0;
    if (chartEndSeconds > 0.0) {
        duration = chartEndSeconds + kTrailingTailSeconds;
    }
    if (musicDurationSeconds > duration) {
        duration = musicDurationSeconds;
    }
    return duration > 0.0 ? duration : 0.0;
}

}  // namespace miacode::content_duration
