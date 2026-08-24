#pragma once

#include <cmath>

// Background-video EndOfMedia classification.
//
// Two independent field reports converge on this file:
//
//   * docs/audit/PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md — a legitimately
//     0.333 s `pv.mp4` reaches its real end almost immediately. That is a
//     NATURAL end: the PV is subordinate visual media, so it simply stops on
//     its last frame while BGM / chart / timeline keep running.
//   * docs/audit/PREVIEW_FIRST_PLAY_RENDER_STALL_HANDOFF_AUDIT_ZH.md §5.2 — a
//     121 s PV reported EndOfMedia after 1.446 s of playback with its last
//     decoded pts at 1.267 s. That is a STALE end: nothing about the media
//     actually ended, so freezing the PV on frame 38 for the remaining two
//     minutes is a defect, not subordinate-media behaviour.
//
// Both arrive as the same `EndOfMedia` enum, so the enum alone can never be the
// decision. What separates them is the decoded progress measured against the
// media's OWN duration — not against the chart, the song, or a wall clock, and
// not against a "videos shorter than N seconds are suspicious" threshold (that
// would mis-handle any legitimately short PV, which is exactly the trap the
// auto-pause audit warns about).
//
// Kept as a pure header so the decision is covered by
// preview_end_of_media_policy_spec on every platform, including the ones that
// cannot run the Windows QtAVPlayer backend where the stale event was observed.

namespace miacode::preview::video {

// Shortfall floor. A decoder can legitimately stop a little before the
// container duration (last frame's presentation covers the tail, duration
// rounding, a stream that ends slightly before the format-level duration), so a
// small gap must still read as natural.
inline constexpr double kEndOfMediaNaturalSlackSeconds = 0.35;

// Extra slack proportional to the frame interval, for low-frame-rate sources
// where one frame is worth far more than the floor above.
inline constexpr double kEndOfMediaNaturalSlackFrames = 3.0;

// A stale event is only worth recovering from when there is a meaningful amount
// of media left to show. Below this, recovery costs more than it returns.
inline constexpr double kEndOfMediaRecoveryMinRemainingSeconds = 1.0;

enum class EndOfMediaClass {
    // Duration or decode progress unknown — do not recover, just record it.
    // Recovering on unknown risks an endless reload loop on a genuinely broken
    // file, which is strictly worse than the frozen last frame.
    Unknown,
    // The decoder reached (or effectively reached) the media's own duration.
    // Subordinate-media behaviour: keep the last frame, never touch the main
    // transport.
    Natural,
    // EndOfMedia arrived while the clip still had substantial content left.
    Stale,
};

struct EndOfMediaFacts {
    // Container duration in seconds; <= 0 when the backend has not reported one.
    double durationSeconds = 0.0;
    // Media time of the last frame the decoder actually produced; < 0 when no
    // frame has been decoded for this source yet.
    double decodedSeconds = -1.0;
    // Average decoded frame interval in seconds; <= 0 when not yet measured.
    double frameIntervalSeconds = 0.0;
    // Media time the preview transport expects the PV to be showing right now.
    // Used only to decide whether recovery is worth attempting.
    double expectedSeconds = 0.0;
};

inline double endOfMediaNaturalSlackSeconds(const EndOfMediaFacts& facts)
{
    const double frameSlack = facts.frameIntervalSeconds > 0.0
        ? facts.frameIntervalSeconds * kEndOfMediaNaturalSlackFrames
        : 0.0;
    return frameSlack > kEndOfMediaNaturalSlackSeconds
        ? frameSlack
        : kEndOfMediaNaturalSlackSeconds;
}

inline EndOfMediaClass classifyEndOfMedia(const EndOfMediaFacts& facts)
{
    if (!std::isfinite(facts.durationSeconds) || facts.durationSeconds <= 0.0) {
        return EndOfMediaClass::Unknown;
    }
    if (!std::isfinite(facts.decodedSeconds) || facts.decodedSeconds < 0.0) {
        // The stream ended before producing a single frame. That is a load /
        // decode failure, and the existing InvalidMedia + software-fallback path
        // owns it; do not turn it into a reload loop from here.
        return EndOfMediaClass::Unknown;
    }
    const double shortfall = facts.durationSeconds - facts.decodedSeconds;
    return shortfall > endOfMediaNaturalSlackSeconds(facts)
        ? EndOfMediaClass::Stale
        : EndOfMediaClass::Natural;
}

// Recovery is only attempted for a stale event that still has content ahead of
// the position the transport wants. A stale event raised while the timeline has
// already run past the end of the PV needs no recovery — the visual result is
// the same last frame either way.
inline bool endOfMediaShouldRecover(const EndOfMediaFacts& facts)
{
    if (classifyEndOfMedia(facts) != EndOfMediaClass::Stale) {
        return false;
    }
    const double expected = std::isfinite(facts.expectedSeconds) && facts.expectedSeconds > 0.0
        ? facts.expectedSeconds
        : 0.0;
    return facts.durationSeconds - expected > kEndOfMediaRecoveryMinRemainingSeconds;
}

inline const char* endOfMediaClassName(EndOfMediaClass value)
{
    switch (value) {
    case EndOfMediaClass::Natural:
        return "natural";
    case EndOfMediaClass::Stale:
        return "stale";
    case EndOfMediaClass::Unknown:
        break;
    }
    return "unknown";
}

}  // namespace miacode::preview::video
