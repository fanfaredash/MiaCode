#pragma once

namespace miacode::layout_ring {

inline constexpr double kOutlineInsetLogical = 25.0;
inline constexpr double kFallbackTextureDiameterRatio = 1.0;
inline constexpr double kFallbackPlayfieldDiameterRatio = 0.9074074074;

inline constexpr int kDetectMinAlpha = 28;
inline constexpr int kDetectMinLuminance = 120;
// Detect ring band in the outer area of the layout texture.
// We then use (inner_edge + outer_edge) / 2 as the effective radius.
inline constexpr double kDetectSearchStartRadiusRatio = 0.88;
inline constexpr double kDetectSearchEndRadiusRatio = 1.00;
inline constexpr double kDetectEdgeThresholdRatio = 0.30;
inline constexpr double kDetectDiameterRatioMin = 0.85;
inline constexpr double kDetectDiameterRatioMax = 1.03;

inline constexpr double kPlayfieldRatioMin = 0.82;
inline constexpr double kPlayfieldRatioMax = 0.95;
inline constexpr double kRenderRatioMax = 0.98;

}  // namespace miacode::layout_ring
