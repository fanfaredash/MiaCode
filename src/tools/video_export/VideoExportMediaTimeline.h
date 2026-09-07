#pragma once

#include <QStringList>

namespace miacode::video_export {

// Where the background media (PV video / 曲绘 image) sits on the export output
// timeline. Output second 0 is the first rendered frame; chart second
// `timelineOriginSecond + outputSecond` is what the playfield renders at that
// frame (see VideoExportAudioRenderPlan).
struct MediaTimelinePlan {
    // Still images need no trim/pad chain at all — they are fed to FFmpeg with
    // `-loop 1` and are static by construction.
    bool mediaIsImage = false;
    // Partial-range exports open with a frozen pre-roll (chart, HUD, audio and
    // — since G3 — the PV all held on the segment-start moment) under the
    // pause glyph drawn by drawLeadInPauseOverlay().
    bool partialRangeExport = false;
    double timelineOriginSecond = 0.0;
    double leadInSeconds = 0.0;
    double alignedTotalSeconds = 0.0;
};

// Builds the FFmpeg filter fragments that place the media on the output
// timeline. They are appended AFTER the scale/setsar/fps/format fragments, so
// the stream is already at the export frame rate when the trim/pad runs.
//
// Three shapes come out of this:
//   * partial-range pre-roll — trim from the segment-start second, then
//     `tpad=start_mode=clone` the first frame across the freeze window, so the
//     PV shows the exact frame the chart is about to resume from and does not
//     drift during the pause.
//   * timeline origin > 0 — trim the media to the exported window.
//   * timeline origin < 0 — start at media 0 and delay it into place.
// All video shapes end with `tpad=stop_mode=clone` so media shorter than the
// export holds its last frame instead of ending the filter graph early.
QStringList buildMediaTimelineFilters(const MediaTimelinePlan& plan);

}  // namespace miacode::video_export
