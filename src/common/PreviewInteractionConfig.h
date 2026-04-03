#pragma once

namespace miacode::preview_interaction {

inline constexpr double kSeekStepFrameRate = 60.0;
inline constexpr double kSeekSingleStepSeconds = 1.0 / kSeekStepFrameRate;
inline constexpr double kSeekHoldAccelerationPerSecond = 1.0;
inline constexpr double kSeekHoldMaxPlaybackRate = 2.0;
inline constexpr int kSeekHoldTickIntervalMs = 16;
inline constexpr double kSeekHoldTickIntervalSeconds = static_cast<double>(kSeekHoldTickIntervalMs) / 1000.0;

inline double heldSeekPlaybackRate(double heldSeconds)
{
    if (heldSeconds <= 0.0) {
        return 1.0;
    }
    const double accelerated = 1.0 + heldSeconds * kSeekHoldAccelerationPerSecond;
    return accelerated > kSeekHoldMaxPlaybackRate ? kSeekHoldMaxPlaybackRate : accelerated;
}

inline double heldSeekStepSeconds(double heldSeconds)
{
    return kSeekHoldTickIntervalSeconds * heldSeekPlaybackRate(heldSeconds);
}

}  // namespace miacode::preview_interaction
