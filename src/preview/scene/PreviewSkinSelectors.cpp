#include "preview/scene/PreviewSkinSelectors.h"

namespace miacode::preview::scene {

const QImage* selectTapNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    if (marker.isBreak && !skin.noteGuideBreakImage.isNull()) {
        return &skin.noteGuideBreakImage;
    }
    if (marker.isEach && !skin.noteGuideEachImage.isNull()) {
        return &skin.noteGuideEachImage;
    }
    return skin.noteGuideNormalImage.isNull() ? nullptr : &skin.noteGuideNormalImage;
}

const QImage* selectHoldEndNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    if (marker.isBreak && !skin.noteGuideHoldBreakEndImage.isNull()) {
        return &skin.noteGuideHoldBreakEndImage;
    }
    if (marker.isEach && !skin.noteGuideHoldEachEndImage.isNull()) {
        return &skin.noteGuideHoldEachEndImage;
    }
    return skin.noteGuideHoldEndImage.isNull() ? nullptr : &skin.noteGuideHoldEndImage;
}

}  // namespace miacode::preview::scene
