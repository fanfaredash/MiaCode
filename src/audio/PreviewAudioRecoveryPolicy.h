#pragma once

#include <cmath>

namespace miacode::preview_audio::recovery {

inline constexpr double kMaxClockDivergenceSeconds = 0.050;

enum class Reason {
    None,
    DefaultOutputChanged,
    DriftExceeded,
};

inline Reason decidePreviewAudioRecovery(
    bool playbackActive,
    bool defaultOutputChanged,
    bool audioClockAvailable,
    double absoluteDeltaSeconds)
{
    if (!playbackActive) {
        return Reason::None;
    }
    if (defaultOutputChanged) {
        return Reason::DefaultOutputChanged;
    }
    if (audioClockAvailable
        && std::isfinite(absoluteDeltaSeconds)
        && absoluteDeltaSeconds >= kMaxClockDivergenceSeconds) {
        return Reason::DriftExceeded;
    }
    return Reason::None;
}

}  // namespace miacode::preview_audio::recovery
