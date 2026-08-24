#pragma once

namespace miacode::preview_interaction {

// Touch-pad click authoring parks the playhead this far BEFORE the token it
// wrote to, so the authored touch is still on screen instead of already being
// judged. It is a viewing convention, not a timing correction.
//
// Consequence to keep in mind: the preview-follow binding that owns
// `tokenSecond - kTouchPadAuthoringPreviewLeadSeconds` is the PRECEDING token's,
// for any lead greater than the timeline's 1e-6 resolution — shrinking this
// value does not change which token the playhead resolves to, it only shrinks
// the visual offset. So without a correction the highlight would jump one token
// back the moment you author a touch; `touchPadAuthoringAnchoredSecond()` is
// what maps the parked playhead back to the token that was authored. That
// correction is cosmetic — the insertion point is the text caret, never this.
inline constexpr double kTouchPadAuthoringPreviewLeadSeconds = 1.0 / 60.0;
// How close the playhead has to be to a recorded authoring seek to still count
// as "parked where that click left it".
inline constexpr double kTouchPadAuthoringAnchorToleranceSeconds = 1e-6;

inline constexpr double kSeekStepFrameRate = 120.0;
inline constexpr double kSeekSingleStepSeconds = 1.0 / kSeekStepFrameRate;
inline constexpr double kSeekHoldAccelerationPerSecond = 1.0;
inline constexpr double kSeekHoldMaxPlaybackRate = 3.0;
inline constexpr int kSeekHoldTickIntervalMs = 16;
inline constexpr double kSeekHoldTickIntervalSeconds = static_cast<double>(kSeekHoldTickIntervalMs) / 1000.0;

inline double heldSeekPlaybackRate(double heldSeconds, double maxPlaybackRate = kSeekHoldMaxPlaybackRate)
{
    if (heldSeconds <= 0.0) {
        return 1.0;
    }
    const double accelerated = 1.0 + heldSeconds * kSeekHoldAccelerationPerSecond;
    return accelerated > maxPlaybackRate ? maxPlaybackRate : accelerated;
}

inline double heldSeekStepSeconds(double heldSeconds, double maxPlaybackRate = kSeekHoldMaxPlaybackRate)
{
    return kSeekHoldTickIntervalSeconds * heldSeekPlaybackRate(heldSeconds, maxPlaybackRate);
}

inline double heldSeekStepSecondsForDeltaMs(
    int deltaMs,
    double heldSeconds,
    double maxPlaybackRate = kSeekHoldMaxPlaybackRate)
{
    const int clampedDeltaMs = deltaMs > 0 ? deltaMs : 1;
    return (static_cast<double>(clampedDeltaMs) / 1000.0)
        * heldSeekPlaybackRate(heldSeconds, maxPlaybackRate);
}

}  // namespace miacode::preview_interaction
