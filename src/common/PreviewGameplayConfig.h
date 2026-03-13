#pragma once

namespace miacode::preview_gameplay {

inline constexpr double kLogicalCanvasSize = 540.0;
inline constexpr double kLogicalDistanceTap = kLogicalCanvasSize * 122.5 / 1080.0;
inline constexpr double kLogicalDistanceEdge = kLogicalCanvasSize * 480.0 / 1080.0;
inline constexpr double kTapUnitsPerSecond = 540.0;

inline constexpr double kDistanceToScaleSlope = 0.008;
inline constexpr double kDistanceToScaleOffset = 0.51;

inline constexpr double kPlayfieldInset = 18.0;
inline constexpr double kSlideTrackFadeInSeconds = 0.2;
inline constexpr double kTouchDurationSeconds = 0.5;

inline constexpr double kJudgeEffectDurationSeconds = 0.71666664;
inline constexpr double kJudgeEffectTouchDurationSeconds = 0.33333334;
inline constexpr double kJudgeEffectFireworkTouchTriggerDelaySeconds = 0.05;
inline constexpr double kJudgeEffectFireworkDurationSeconds = 1.3333334;

}  // namespace miacode::preview_gameplay
