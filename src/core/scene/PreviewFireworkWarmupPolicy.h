#pragma once

#include "common/PreviewGameplayConfig.h"
#include "timeline/TimelineData.h"

#include <QtGlobal>

// Pure placement/re-centre policy for the firework PSO warm-up synthetic marker.
//
// The warm-up injects one off-screen synthetic firework so Qt RHI compiles the
// firework material pipeline and uploads the colour-ball texture at a benign
// moment instead of on the user's first real firework. Completion is gated on a
// CONFIRMED draw, so the synthetic has to stay inside its lifecycle window until
// the firework layer emits a node — which is why it is re-centred on the live
// playhead while armed.
//
// Owning the geometry here (instead of inline in PreviewRuntime) makes the one
// question that matters testable: how far may the playhead travel before the
// synthetic falls out of its window and the warm-up can no longer confirm?
namespace miacode::preview::scene {

// Identity shared by the runtime injector/remover and the layer builder. The
// marker is intentionally distinguishable from chart-authored touches so the
// builder can allow its negative trigger at chart second zero without changing
// the existing rule for real notes with negative timestamps.
inline constexpr qreal kFireworkWarmupOffscreenCoordinate = -1.0e6;

inline bool isFireworkWarmupMarker(const TimelineNoteMarker& marker)
{
    return marker.isFirework
        && marker.type == QLatin1String("touch")
        && qFuzzyCompare(marker.touchPoint.x(), kFireworkWarmupOffscreenCoordinate)
        && qFuzzyCompare(marker.touchPoint.y(), kFireworkWarmupOffscreenCoordinate);
}

// The synthetic's firework is made to trigger this far BEHIND the live playhead,
// so the effect is already inside its lifecycle window on the very next frame
// rather than starting exactly at its first sample.
inline constexpr double kFireworkWarmupLeadSeconds = 0.15;

// Derived window, expressed as playhead travel from the re-centre point.
//
//   marker.second = playhead - triggerDelay - lead
//   trigger       = marker.second + triggerDelay = playhead - lead
//   active while    playhead' - trigger  in [0, duration]
//   i.e.            playhead' - playhead in [-lead, duration - lead]
//
// So the synthetic tolerates `duration - lead` of forward travel but only `lead`
// of backward travel. The asymmetry is real and must not be flattened into one
// symmetric threshold.
inline constexpr double kFireworkWarmupForwardSlackSeconds =
    miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds - kFireworkWarmupLeadSeconds;
inline constexpr double kFireworkWarmupBackwardSlackSeconds = kFireworkWarmupLeadSeconds;

// Re-centre once half the available slack in the travelled direction is used up.
// Half, not "just before the edge", so a long frame or a coalesced update cannot
// step across the boundary between two checks.
inline constexpr double kFireworkWarmupRecenterSafetyFactor = 0.5;

// The chart-second a synthetic marker must carry to be centred on `playheadSeconds`.
inline double fireworkWarmupMarkerSecond(double playheadSeconds)
{
    return playheadSeconds
        - miacode::preview_gameplay::kJudgeEffectFireworkTouchTriggerDelaySeconds
        - kFireworkWarmupLeadSeconds;
}

// Whether a synthetic centred at `centeredAtSeconds` must be re-placed for the
// playhead to still be able to draw it.
//
// This is the whole point of the policy: the pre-existing implementation
// re-centred on EVERY playhead change, i.e. once per preview frame (60-180 Hz)
// for as long as the warm-up stayed armed. Each re-centre scans and rewrites the
// full marker vector and bumps sceneContentRevision, which invalidates
// PreviewPreparedSceneCache and forces a full ten-layer rebuild — per frame,
// throughout the first playback of a session. Re-centring only when the playhead
// has actually consumed its slack keeps the seek / pre-roll guarantee (any real
// seek is far larger than the slack) at a small fraction of the cost.
inline bool fireworkWarmupNeedsRecenter(double playheadSeconds, double centeredAtSeconds)
{
    const double travel = playheadSeconds - centeredAtSeconds;
    if (travel >= 0.0) {
        return travel > kFireworkWarmupForwardSlackSeconds * kFireworkWarmupRecenterSafetyFactor;
    }
    return -travel > kFireworkWarmupBackwardSlackSeconds * kFireworkWarmupRecenterSafetyFactor;
}

}  // namespace miacode::preview::scene
