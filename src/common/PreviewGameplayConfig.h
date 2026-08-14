#pragma once

#include <QString>

namespace miacode::preview_gameplay {

enum class CenterDisplayMode {
    Off = 0,
    Combo,
    AchievementDxPlus,
    AchievementDxMinus100,
    AchievementDxMinus101,
    DxScorePlus,
    DxScoreMinus,
    AchievementFinalePlus,
};

inline constexpr CenterDisplayMode kDefaultCenterDisplayMode = CenterDisplayMode::Off;

inline const char* centerDisplayModeToken(CenterDisplayMode mode)
{
    switch (mode) {
    case CenterDisplayMode::Combo:
        return "combo";
    case CenterDisplayMode::AchievementDxPlus:
        return "achievement_dx_plus";
    case CenterDisplayMode::AchievementDxMinus100:
        return "achievement_dx_minus_100";
    case CenterDisplayMode::AchievementDxMinus101:
        return "achievement_dx_minus_101";
    case CenterDisplayMode::DxScorePlus:
        return "dx_score_plus";
    case CenterDisplayMode::DxScoreMinus:
        return "dx_score_minus";
    case CenterDisplayMode::AchievementFinalePlus:
        return "achievement_finale_plus";
    case CenterDisplayMode::Off:
    default:
        return "off";
    }
}

inline CenterDisplayMode centerDisplayModeFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QLatin1String("combo")) {
        return CenterDisplayMode::Combo;
    }
    if (normalized == QLatin1String("achievement_dx_plus")
        || normalized == QLatin1String("achievement_plus")
        || normalized == QLatin1String("achievement(+)")) {
        return CenterDisplayMode::AchievementDxPlus;
    }
    if (normalized == QLatin1String("achievement_dx_minus_100")
        || normalized == QLatin1String("achievement_minus_100")) {
        return CenterDisplayMode::AchievementDxMinus100;
    }
    if (normalized == QLatin1String("achievement_dx_minus_101")
        || normalized == QLatin1String("achievement_minus_101")) {
        return CenterDisplayMode::AchievementDxMinus101;
    }
    if (normalized == QLatin1String("dx_score_plus")) {
        return CenterDisplayMode::DxScorePlus;
    }
    if (normalized == QLatin1String("dx_score_minus")) {
        return CenterDisplayMode::DxScoreMinus;
    }
    if (normalized == QLatin1String("achievement_finale_plus")
        || normalized == QLatin1String("achievement(+) finale")) {
        return CenterDisplayMode::AchievementFinalePlus;
    }
    return CenterDisplayMode::Off;
}

inline constexpr double kLogicalCanvasSize = 540.0;
inline constexpr double kLogicalDistanceTap = kLogicalCanvasSize * 122.5 / 1080.0;
inline constexpr double kLogicalDistanceEdge = kLogicalCanvasSize * 480.0 / 1080.0;

// Unified preview timing scale:
// T = 1 + 240 / v, where T is measured in 120 FPS frames.
// Previous calibration: T = 5 + 215 / v. Keep this here for quick comparison
// and rollback if the new 7.5 -> 66 / 8.0 -> 62 lifecycle calibration regresses.
inline constexpr double kPreviewTimingFramesPerSecond = 120.0;
inline constexpr double kPreviewTimingDefaultFlowSpeed = 7.5;
inline constexpr double kPreviewTimingFlowSpeedMin = 4.5;
inline constexpr double kPreviewTimingFlowSpeedMax = 10.0;
inline constexpr double kPreviewTimingFlowSpeedStep = 0.25;
inline constexpr double kPreviewTimingBaseFrames = 1.0;
inline constexpr double kPreviewTimingFlowFramesNumerator = 240.0;
inline constexpr double kSlideHeadRotationBaseFramesAt120Fps = 5.20;
inline constexpr double kSlideHeadRotationPathFramesNumerator = 194.36;
inline constexpr double kSlideHeadRotationReferenceDurationSeconds = 0.5;
inline constexpr double kSlideHeadRotationReferenceDegrees = 72.0;
inline constexpr double kSlideHeadRotationMaxDegreesPerSecond = 1080.0;
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
inline constexpr double previewSlideHeadRotationFramesAt120Fps(double nativeTrackLength)
{
    const double slideImageCount = nativeTrackLength - 1.0;
    if (slideImageCount <= 0.0) {
        return 0.0;
    }
    return kSlideHeadRotationBaseFramesAt120Fps
        + kSlideHeadRotationPathFramesNumerator / slideImageCount;
}
inline constexpr double previewSlideHeadRotationSpeedDegreesPerSecond(
    double nativeTrackLength,
    double durationSeconds)
{
    const double framesAtReferenceDuration =
        previewSlideHeadRotationFramesAt120Fps(nativeTrackLength);
    if (framesAtReferenceDuration <= 0.0 || durationSeconds <= 0.0) {
        return 0.0;
    }
    const double speed =
        kSlideHeadRotationReferenceDegrees
        * kPreviewTimingFramesPerSecond
        * kSlideHeadRotationReferenceDurationSeconds
        / (framesAtReferenceDuration * durationSeconds);
    return speed < kSlideHeadRotationMaxDegreesPerSecond
        ? speed
        : kSlideHeadRotationMaxDegreesPerSecond;
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
inline constexpr bool kPreviewSlideEarlierSecondAndTextOnTop = true;
inline constexpr double kTapUnitsPerSecond =
    (kLogicalDistanceEdge - kLogicalDistanceTap) / kTapFlyDurationSeconds;

inline constexpr double kDistanceToScaleSlope = 0.008;
inline constexpr double kDistanceToScaleOffset = 0.51;

inline constexpr double kPlayfieldInset = 18.0;
inline constexpr double kSlideTrackFadeInSeconds = kSlideTrackAppearLeadInSeconds;
inline constexpr double kTouchDurationSeconds = 0.5;

inline constexpr double kJudgeEffectDurationSeconds = 0.71666664;
inline constexpr double kJudgeEffectTouchDurationSeconds = 0.25;
inline constexpr double kJudgeEffectFireworkTouchTriggerDelaySeconds = 0.05;
// Tuned firework clip: 30 frames at 30 fps from colour-ball onset
// through the final visible spoke fade.
inline constexpr double kJudgeEffectFireworkDurationSeconds = 1.0;

}  // namespace miacode::preview_gameplay
