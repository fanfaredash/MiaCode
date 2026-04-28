#pragma once

#include "core/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

const QImage* selectTapNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectHoldEndNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectTapImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectHoldImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectSlideStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectSlideMovingStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectSlideTrackImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectWifiTrackImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, int sampleIndex, int sampleCount);
qreal slideStartupStarInitialScale(const PreviewSkinAssets& skin, const QImage& starImage);

}  // namespace miacode::preview::scene
