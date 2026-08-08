#pragma once

#include "core/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

const QImage* selectTapNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, bool useMineSkin = true);
const QImage* selectHoldEndNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, bool useMineSkin = true);
const QImage* selectTapImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, bool useMineSkin = true);
const QImage* selectHoldImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, bool useMineSkin = true);
const QImage* selectSlideStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, bool useMineSkin = true);
const QImage* selectSlideMovingStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, bool useMineSkin = true);
const QImage* selectSlideTrackImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, bool useMineSkin = true);
const QImage* selectWifiTrackImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, int sampleIndex, int sampleCount, bool useMineSkin = true);
qreal slideStartupStarInitialScale(const PreviewSkinAssets& skin, const QImage& starImage);

}  // namespace miacode::preview::scene
