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

const QImage* selectTapImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    const bool slideHeadTapMaterial =
        (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi"))
        && marker.slideHeadUsesTapMaterial;
    const bool isBreak = slideHeadTapMaterial ? marker.headBreak : marker.isBreak;
    const bool isEach = slideHeadTapMaterial ? marker.headEach : marker.isEach;
    const QImage* tapImage = &skin.tapImage;
    if (isBreak && !skin.tapBreakImage.isNull()) {
        tapImage = &skin.tapBreakImage;
    } else if (isEach && !skin.tapEachImage.isNull()) {
        tapImage = &skin.tapEachImage;
    }
    return tapImage->isNull() ? nullptr : tapImage;
}

const QImage* selectHoldImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    const QImage* holdImage = &skin.holdImage;
    if (marker.isBreak && !skin.holdBreakImage.isNull()) {
        holdImage = &skin.holdBreakImage;
    } else if (marker.isEach && !skin.holdEachImage.isNull()) {
        holdImage = &skin.holdEachImage;
    }
    return holdImage->isNull() ? nullptr : holdImage;
}

const QImage* selectSlideStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    const bool slideLike = marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
    const bool useHeadFlags = slideLike && !marker.slideHeadUsesTapMaterial;
    const bool isBreak = useHeadFlags ? marker.headBreak : marker.isBreak;
    const bool isEach = useHeadFlags ? marker.headEach : marker.isEach;
    const bool useDouble = useHeadFlags ? marker.sameHeadSlide : marker.tapStarDouble;
    const QImage* starImage = &skin.starImage;
    if (isBreak) {
        if (useDouble && !skin.starBreakDoubleImage.isNull()) {
            starImage = &skin.starBreakDoubleImage;
        } else if (!skin.starBreakImage.isNull()) {
            starImage = &skin.starBreakImage;
        }
    } else if (isEach) {
        if (useDouble && !skin.starEachDoubleImage.isNull()) {
            starImage = &skin.starEachDoubleImage;
        } else if (!skin.starEachImage.isNull()) {
            starImage = &skin.starEachImage;
        } else if (useDouble && !skin.starDoubleImage.isNull()) {
            starImage = &skin.starDoubleImage;
        }
    } else if (useDouble && !skin.starDoubleImage.isNull()) {
        starImage = &skin.starDoubleImage;
    }
    return starImage->isNull() ? nullptr : starImage;
}

const QImage* selectSlideMovingStarImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    const QImage* starImage = &skin.starImage;
    if (marker.trackBreak && !skin.starBreakImage.isNull()) {
        starImage = &skin.starBreakImage;
    } else if (marker.slideEach && !skin.starEachImage.isNull()) {
        starImage = &skin.starEachImage;
    }
    return starImage->isNull() ? nullptr : starImage;
}

}  // namespace miacode::preview::scene
