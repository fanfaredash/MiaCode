#pragma once

#include <cmath>
#include <cstdint>

#include "BassPreviewMasterMixerPolicy.h"

namespace miacode::preview_audio::bass {

inline bool shouldLogDisarm(bool wasActive, bool hadSync, int groupIndex) noexcept
{
    return wasActive || hadSync || groupIndex != -1;
}

// The master mixer is intentionally never stopped.  Each live preview session
// therefore records the mixer position that corresponds to its chart-second
// anchor, allowing SFX BASS_SYNC_POS positions to remain meaningful across
// separate play / pause / seek cycles.
struct SfxSchedulerAnchor {
    double chartSecond = 0.0;
    double mixerSecond = 0.0;
    double playbackRate = 1.0;
    double outputBufferSeconds = 0.0;
};

inline double mixerSecondForChartSecond(const SfxSchedulerAnchor& anchor, double chartSecond)
{
    const double rate = std::isfinite(anchor.playbackRate) && anchor.playbackRate > 0.0
        ? anchor.playbackRate
        : 1.0;
    return anchor.mixerSecond + (chartSecond - anchor.chartSecond) / rate
        - validOutputBufferSeconds(anchor.outputBufferSeconds);
}

inline double chartSecondForMixerSecond(const SfxSchedulerAnchor& anchor, double mixerSecond)
{
    const double rate = std::isfinite(anchor.playbackRate) && anchor.playbackRate > 0.0
        ? anchor.playbackRate
        : 1.0;
    return anchor.chartSecond + (mixerSecond - anchor.mixerSecond) * rate;
}

// The scheduler keeps exactly one BASS_SYNC_POS armed and arms the next group from its
// callback, so a sync that never fires silences every later note sound until a pause or
// seek re-anchors. BASS only fires a position sync when the decode cursor crosses its
// target; one armed at or behind the cursor (an anchor read a mix block before the
// SetSync call, or a group armed late from the worker) is simply never delivered. The
// grace covers the mix block in which a correctly armed sync is still being dispatched,
// so a sync is only declared missed once the cursor is unambiguously past it.
inline constexpr double kMissedSyncGraceSeconds = 0.200;

inline bool scheduledSyncWasMissed(
    std::uint64_t targetPosition,
    std::uint64_t decodePosition,
    std::uint64_t graceBytes) noexcept
{
    return decodePosition >= targetPosition && decodePosition - targetPosition >= graceBytes;
}

// A mixer callback that loses tryLock() leaves its already-fired one-shot sync for the
// worker. A group still close to its note time is played when the worker replays it; one
// that waited out a stalled worker is skipped and the scheduler re-anchored at the live
// mixer position instead, so a stall never releases a burst of stale note sounds.
inline constexpr double kDeferredSyncMaxLateSeconds = 0.100;

inline bool shouldReplayDeferredSync(double lateSeconds) noexcept
{
    return std::isfinite(lateSeconds) && lateSeconds <= kDeferredSyncMaxLateSeconds;
}

}  // namespace miacode::preview_audio::bass
