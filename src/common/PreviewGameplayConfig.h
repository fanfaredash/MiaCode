#pragma once

namespace miacode::preview_gameplay {

inline constexpr double kLogicalCanvasSize = 540.0;
inline constexpr double kLogicalDistanceTap = kLogicalCanvasSize * 122.5 / 1080.0;
inline constexpr double kLogicalDistanceEdge = kLogicalCanvasSize * 480.0 / 1080.0;

// Unified preview timing scale:
// T = 5 + 215 / v, where T is measured in 120 FPS frames.
inline constexpr double kPreviewTimingFramesPerSecond = 120.0;
inline constexpr double kPreviewTimingDefaultFlowSpeed = 7.5;
inline constexpr double kPreviewTimingFlowSpeedMin = 4.5;
inline constexpr double kPreviewTimingFlowSpeedMax = 10.0;
inline constexpr double kPreviewTimingFlowSpeedStep = 0.5;
inline constexpr double kPreviewTimingBaseFrames = 5.0;
inline constexpr double kPreviewTimingFlowFramesNumerator = 215.0;
inline constexpr double normalizePreviewTimingFlowSpeed(double flowSpeed)
{
    if (flowSpeed < kPreviewTimingFlowSpeedMin) {
        return kPreviewTimingFlowSpeedMin;
    }
    if (flowSpeed > kPreviewTimingFlowSpeedMax) {
        return kPreviewTimingFlowSpeedMax;
    }
    return flowSpeed;
}
inline constexpr double previewTimingScaleFramesAt120Fps(double flowSpeed)
{
    const double normalizedFlowSpeed = normalizePreviewTimingFlowSpeed(flowSpeed);
    return kPreviewTimingBaseFrames + kPreviewTimingFlowFramesNumerator / normalizedFlowSpeed;
}
inline constexpr double previewTimingSecondsFromFramesAt120Fps(double frames)
{
    return frames / kPreviewTimingFramesPerSecond;
}
inline constexpr double kPreviewTimingScaleFramesAt120Fps =
    previewTimingScaleFramesAt120Fps(kPreviewTimingDefaultFlowSpeed);

inline constexpr double kTapLifecycleFramesAt120Fps = kPreviewTimingScaleFramesAt120Fps * 2.0;
inline constexpr double kTapSpawnFramesAt120Fps = kPreviewTimingScaleFramesAt120Fps;
inline constexpr double kTapFlyFramesAt120Fps = kPreviewTimingScaleFramesAt120Fps;
inline constexpr double kTapLifecycleDurationSeconds = previewTimingSecondsFromFramesAt120Fps(kTapLifecycleFramesAt120Fps);
inline constexpr double kTapSpawnDurationSeconds = previewTimingSecondsFromFramesAt120Fps(kTapSpawnFramesAt120Fps);
inline constexpr double kTapFlyDurationSeconds = previewTimingSecondsFromFramesAt120Fps(kTapFlyFramesAt120Fps);

inline constexpr double kJudgeEffectLaneTriggerVisibleStartFramesAt120Fps = 3.0;
inline constexpr double kJudgeEffectLaneTriggerVisibleStartSeconds =
    previewTimingSecondsFromFramesAt120Fps(kJudgeEffectLaneTriggerVisibleStartFramesAt120Fps);
inline constexpr double kJudgeEffectLaneTriggerVisibleEndFramesAt120Fps = 48.0;
inline constexpr double kJudgeEffectLaneTriggerVisibleEndSeconds =
    previewTimingSecondsFromFramesAt120Fps(kJudgeEffectLaneTriggerVisibleEndFramesAt120Fps);
inline constexpr double kHoldSustainEffectStartOffsetFramesAt120Fps = 9.0;
inline constexpr double kHoldSustainEffectStartOffsetSeconds =
    previewTimingSecondsFromFramesAt120Fps(kHoldSustainEffectStartOffsetFramesAt120Fps);
inline constexpr double kHoldSustainEffectEndOffsetFramesAt120Fps = 3.0;
inline constexpr double kHoldSustainEffectEndOffsetSeconds =
    previewTimingSecondsFromFramesAt120Fps(kHoldSustainEffectEndOffsetFramesAt120Fps);

inline constexpr double kSlideTrackAppearLeadInFramesAt120Fps = kPreviewTimingScaleFramesAt120Fps + 2.0;
inline constexpr double kSlideTrackFullBrightLeadInFramesAt120Fps = 6.0;
inline constexpr double kSlideTrackAppearLeadInSeconds =
    previewTimingSecondsFromFramesAt120Fps(kSlideTrackAppearLeadInFramesAt120Fps);
inline constexpr double kSlideTrackFullBrightLeadInSeconds =
    previewTimingSecondsFromFramesAt120Fps(kSlideTrackFullBrightLeadInFramesAt120Fps);
inline constexpr double kSlideTrackAppearAlphaCap = 0.70;
inline constexpr double kSlideTrackAppearAlphaEaseOutExponent = 2.7;
inline constexpr double kTapUnitsPerSecond =
    (kLogicalDistanceEdge - kLogicalDistanceTap) / kTapFlyDurationSeconds;

inline constexpr double kDistanceToScaleSlope = 0.008;
inline constexpr double kDistanceToScaleOffset = 0.51;

inline constexpr double kPlayfieldInset = 18.0;
inline constexpr double kSlideTrackFadeInSeconds = kSlideTrackAppearLeadInSeconds;
inline constexpr double kTouchDurationSeconds = 0.5;

inline constexpr double kJudgeEffectDurationSeconds = 0.71666664;
inline constexpr double kJudgeEffectTouchDurationSeconds = 0.33333334;
inline constexpr double kJudgeEffectFireworkTouchTriggerDelaySeconds = 0.05;
inline constexpr double kJudgeEffectFireworkDurationSeconds = 1.3333334;

}  // namespace miacode::preview_gameplay
