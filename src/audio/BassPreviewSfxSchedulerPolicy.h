#pragma once

#include <cmath>

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
};

inline double mixerSecondForChartSecond(const SfxSchedulerAnchor& anchor, double chartSecond)
{
    const double rate = std::isfinite(anchor.playbackRate) && anchor.playbackRate > 0.0
        ? anchor.playbackRate
        : 1.0;
    return anchor.mixerSecond + (chartSecond - anchor.chartSecond) / rate;
}

inline double chartSecondForMixerSecond(const SfxSchedulerAnchor& anchor, double mixerSecond)
{
    const double rate = std::isfinite(anchor.playbackRate) && anchor.playbackRate > 0.0
        ? anchor.playbackRate
        : 1.0;
    return anchor.chartSecond + (mixerSecond - anchor.mixerSecond) * rate;
}

}  // namespace miacode::preview_audio::bass
