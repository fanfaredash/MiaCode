#pragma once

#include <QtGlobal>

#include "common/PreviewGameplayConfig.h"
#include "common/PreviewSkinConfig.h"

namespace miacode::preview::scene {

inline constexpr qreal kLogicalCanvasSize = static_cast<qreal>(miacode::preview_gameplay::kLogicalCanvasSize);
inline constexpr qreal kLogicalCanvasCenter = kLogicalCanvasSize / 2.0;
inline constexpr qreal kLogicalDistanceTap = static_cast<qreal>(miacode::preview_gameplay::kLogicalDistanceTap);
inline constexpr qreal kLogicalDistanceEdge = static_cast<qreal>(miacode::preview_gameplay::kLogicalDistanceEdge);
inline constexpr qreal kLaneUnitVectorBaseDegrees = -67.5;
inline constexpr qreal kLaneRotationBaseDegrees = 22.5;
inline constexpr qreal kLaneAngleStepDegrees = 45.0;
inline constexpr qreal kDistanceToScaleSlope = static_cast<qreal>(miacode::preview_gameplay::kDistanceToScaleSlope);
inline constexpr qreal kDistanceToScaleOffset = static_cast<qreal>(miacode::preview_gameplay::kDistanceToScaleOffset);
inline constexpr qreal kSkinAssetScale = static_cast<qreal>(miacode::preview_skin::kTapHeadScale);
inline constexpr qreal kStarAssetScale = 90.0 / 126.0;
inline constexpr qreal kSlideSpawnStarRelativeScale = 1.08;
inline constexpr qreal kNoteGuideSourceRadius = 240.75;
inline constexpr qreal kEachLine1SourceRadius = 240.02;
inline constexpr qreal kEachLine2SourceRadius = 239.89;
inline constexpr qreal kEachLine3SourceRadius = 239.65;
inline constexpr qreal kEachLine4SourceRadius = 239.92;
inline constexpr qreal kTouchPhaseDivisionEpsilonSeconds = 0.001;
inline constexpr qreal kRenderDurationEpsilon = 0.001;
inline constexpr qreal kPreviewPointHashScale = 1000.0;
inline constexpr quint64 kTouchPadRegionHashFlag = 0x8000000000000000ULL;
inline constexpr qreal kTouchPrefabStartRadiusNormalized = 0.626;
inline constexpr qreal kTouchPrefabEndRadiusNormalized = 0.226;
inline constexpr qreal kTouchPrefabMaxCloseAmountNormalized =
    kTouchPrefabStartRadiusNormalized - kTouchPrefabEndRadiusNormalized;
inline constexpr qreal kTouchCloseCurveResidualBias = 0.42;
inline constexpr qreal kTouchCloseCurveProgressBias = 4.05;
inline constexpr qreal kTouchDurationSeconds = static_cast<qreal>(miacode::preview_gameplay::kTouchDurationSeconds);
inline constexpr qreal kTouchShowDurationSeconds = kTouchDurationSeconds / 5.0;
inline constexpr qreal kTouchCloseDurationSeconds = kTouchDurationSeconds - kTouchShowDurationSeconds;
inline constexpr qreal kTouchPrefabStartEndRatio = kTouchPrefabStartRadiusNormalized / kTouchPrefabEndRadiusNormalized;
inline constexpr qreal kTouchClosedOffset = 12.0;
inline constexpr qreal kTouchStartOffset = kTouchClosedOffset * kTouchPrefabStartEndRatio;
inline constexpr qreal kTouchHoldClosedOffset = 8.0;
inline constexpr qreal kTouchHoldStartOffset = kTouchHoldClosedOffset * kTouchPrefabStartEndRatio;
inline constexpr qreal kTouchCloseCurveExponent = 3.2;
inline constexpr int kTouchOverlapBorder2Threshold = 2;
inline constexpr int kTouchOverlapBorder3Threshold = 3;
inline constexpr int kTouchUpAngleDegrees = 180;
inline constexpr int kTouchRightAngleDegrees = -90;
inline constexpr int kTouchDownAngleDegrees = 0;
inline constexpr int kTouchLeftAngleDegrees = 90;
inline constexpr int kTouchHoldUpperRightAngleDegrees = -135;
inline constexpr int kTouchHoldLowerRightAngleDegrees = -45;
inline constexpr int kTouchHoldLowerLeftAngleDegrees = 45;
inline constexpr int kTouchHoldUpperLeftAngleDegrees = 135;
inline constexpr qreal kTouchAssetScale = 0.5;

}  // namespace miacode::preview::scene
