#pragma once

#include "preview/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

const QImage* selectTapNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectHoldEndNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectTapImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectHoldImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectSlideStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectSlideMovingStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);

}  // namespace miacode::preview::scene
