#pragma once

#include "preview/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

const QImage* selectTapNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);
const QImage* selectHoldEndNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker);

}  // namespace miacode::preview::scene
