#pragma once

namespace miacode::preview_interaction {

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
