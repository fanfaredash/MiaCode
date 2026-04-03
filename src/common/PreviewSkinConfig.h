#pragma once

namespace miacode::preview_skin {

inline constexpr double kTapHeadScale = 0.5;
inline constexpr double kHoldWidthRelativeToTap = 60.0 / 61.0;
inline constexpr double kSlideTrackScale = 0.5;
inline constexpr int kHoldCapSliceRatioNumerator = 71;
inline constexpr int kHoldCapSliceRatioDenominator = 200;

inline constexpr double kDefaultTapSourceWidth = 122.0;
inline constexpr double kDefaultSlideTrackSourceHeight = 94.0;
inline constexpr double kDefaultTapTargetWidth = kDefaultTapSourceWidth * kTapHeadScale;
inline constexpr double kDefaultHoldTargetWidth = kDefaultTapTargetWidth * kHoldWidthRelativeToTap;
inline constexpr double kSlideTrackLongSideRelativeToTap =
    (kDefaultSlideTrackSourceHeight * kSlideTrackScale) / kDefaultTapTargetWidth;

}  // namespace miacode::preview_skin
