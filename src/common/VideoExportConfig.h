#pragma once

namespace miacode::video_export {

inline constexpr double kLeadInSeconds = 2.0;
// Partial-range exports get a pre-roll where the chart playfield,
// HUD timestamp, and audio are *all* frozen on the segment-start moment
// while a translucent pause glyph (drawLeadInPauseOverlay) sits on top.
// The viewer reads it as "about to start" — nothing animating, nothing
// sounding. After this many seconds the glyph clears and playback begins.
// Bumped from 1.0 s to 1.5 s so the freeze is long enough to register
// against short partial-range segments.
inline constexpr double kPartialRangePreloadSeconds = 1.5;

}  // namespace miacode::video_export
