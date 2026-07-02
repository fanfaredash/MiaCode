#include "core/scene/PreviewSkinSelectors.h"

#include "core/scene/PreviewSceneConstants.h"

namespace miacode::preview::scene {

const QImage* selectTapNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    const bool slideLike = marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
    if (slideLike && !marker.hasHeadStar) {
        return nullptr;
    }

    const bool starMaterialHead = slideLike ? !marker.slideHeadUsesTapMaterial : marker.tapUsesStarMaterial;
    const bool isBreak = slideLike ? marker.headBreak : marker.isBreak;
    const bool isEach = slideLike ? marker.headEach : marker.isEach;
    const bool isMine = slideLike ? marker.headMine : marker.isMine;

    // Mine notes use a distinct approach guide (overrides break/each), matching
    // MajdataMine_View where the mine note has its own guide ring.
    if (isMine && !skin.noteGuideMineImage.isNull()) {
        return &skin.noteGuideMineImage;
    }
    if (isBreak && !skin.noteGuideBreakImage.isNull()) {
        return &skin.noteGuideBreakImage;
    }
    if (isEach && !skin.noteGuideEachImage.isNull()) {
        return &skin.noteGuideEachImage;
    }
    if (starMaterialHead) {
        return skin.noteGuideSlideImage.isNull() ? &skin.noteGuideNormalImage : &skin.noteGuideSlideImage;
    }
    return skin.noteGuideNormalImage.isNull() ? nullptr : &skin.noteGuideNormalImage;
}

const QImage* selectHoldEndNoteGuideImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    if (marker.isMine && !skin.noteGuideHoldMineEndImage.isNull()) {
        return &skin.noteGuideHoldMineEndImage;
    }
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
    const bool isMine = slideHeadTapMaterial ? marker.headMine : marker.isMine;
    // Mine overrides break/each (matches MajdataPlay's note sprite swap).
    if (isMine && !skin.tapMineImage.isNull()) {
        return &skin.tapMineImage;
    }
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
    if (marker.isMine && !skin.holdMineImage.isNull()) {
        return &skin.holdMineImage;
    }
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
    const bool isMine = useHeadFlags ? marker.headMine : marker.isMine;
    if (isMine && !skin.starMineImage.isNull()) {
        if (useDouble && !skin.starMineDoubleImage.isNull()) {
            return &skin.starMineDoubleImage;
        }
        return &skin.starMineImage;
    }
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
    if (marker.trackMine && !skin.starMineImage.isNull()) {
        return &skin.starMineImage;
    }
    const QImage* starImage = &skin.starImage;
    if (marker.trackBreak && !skin.starBreakImage.isNull()) {
        starImage = &skin.starBreakImage;
    } else if (marker.slideEach && !skin.starEachImage.isNull()) {
        starImage = &skin.starEachImage;
    }
    return starImage->isNull() ? nullptr : starImage;
}

const QImage* selectSlideTrackImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker)
{
    if (marker.trackMine && !skin.slideTrackMineImage.isNull()) {
        return &skin.slideTrackMineImage;
    }
    const QImage* image = &skin.slideTrackImage;
    if (marker.trackBreak && !skin.slideTrackBreakImage.isNull()) {
        image = &skin.slideTrackBreakImage;
    } else if (marker.slideEach && !skin.slideTrackEachImage.isNull()) {
        image = &skin.slideTrackEachImage;
    }
    return image->isNull() ? nullptr : image;
}

const QImage* selectWifiTrackImage(const PreviewSkinAssets& skin, const TimelineNoteMarker& marker, int sampleIndex, int sampleCount)
{
    const QVector<QImage>* images = &skin.wifiImages;
    if (marker.trackMine && !skin.wifiMineImages.isEmpty()) {
        images = &skin.wifiMineImages;
    } else if (marker.trackBreak && !skin.wifiBreakImages.isEmpty()) {
        images = &skin.wifiBreakImages;
    } else if (marker.slideEach && !skin.wifiEachImages.isEmpty()) {
        images = &skin.wifiEachImages;
    }
    if (images->isEmpty()) {
        return nullptr;
    }

    const int maxIndex = images->size() - 1;
    const int sourceIndex = sampleCount <= 0
        ? qBound(0, sampleIndex, maxIndex)
        : sampleCount <= 1
        ? 0
        : qBound(0, qRound(static_cast<qreal>(sampleIndex) * maxIndex / qMax(1, sampleCount - 1)), maxIndex);
    return &images->at(sourceIndex);
}

qreal slideStartupStarInitialScale(const PreviewSkinAssets& skin, const QImage& starImage)
{
    if (starImage.isNull()) {
        return kStarAssetScale;
    }

    const qreal headWidth =
        (!skin.tapImage.isNull() ? skin.tapImage.width() * kSkinAssetScale : starImage.width() * kStarAssetScale)
        * kSlideSpawnStarRelativeScale;
    return qMax<qreal>(0.01, headWidth / qMax(1, starImage.width()));
}

}  // namespace miacode::preview::scene
