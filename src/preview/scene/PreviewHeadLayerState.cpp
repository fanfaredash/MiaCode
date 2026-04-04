#include "preview/scene/PreviewHeadLayerState.h"

#include "preview/scene/PreviewAnimatedSpriteHelpers.h"
#include "preview/scene/PreviewOpacityCurves.h"
#include "preview/scene/PreviewSceneConstants.h"
#include "preview/scene/PreviewSceneMath.h"
#include "preview/scene/PreviewSkinSelectors.h"

#include <algorithm>

namespace {

using miacode::preview::scene::PreviewAnimatedSpriteEffect;
using miacode::preview::scene::PreviewHeadLayerState;
using miacode::preview::scene::PreviewSpriteDescriptor;
using miacode::preview::scene::TapApproachSample;

constexpr qreal kTapOverlayBaseMix = 0.82;
constexpr qreal kTapOverlayAlphaMix = 0.18;
constexpr qreal kHoldOverlayBaseMix = 0.85;
constexpr qreal kHoldOverlayAlphaMix = 0.20;
constexpr qreal kHoldTargetWidth = static_cast<qreal>(miacode::preview_skin::kDefaultHoldTargetWidth + 0.5);
constexpr qreal kHoldCapSliceRatioNumerator =
    static_cast<qreal>(miacode::preview_skin::kHoldCapSliceRatioNumerator);
constexpr qreal kHoldCapSliceRatioDenominator =
    static_cast<qreal>(miacode::preview_skin::kHoldCapSliceRatioDenominator);

qreal mirroredStarAngleDegrees(qreal angleDegrees)
{
    return 360.0 - angleDegrees;
}

qreal totalSlideTraceDurationSeconds(const TimelineNoteMarker& marker)
{
    qreal duration = 0.0;
    for (double segmentDuration : marker.slideSegmentDurations) {
        duration += static_cast<qreal>(qMax(0.0, segmentDuration));
    }
    if (duration > 0.0) {
        return duration;
    }
    if (marker.endSecond > marker.slideTraceSecond) {
        return static_cast<qreal>(marker.endSecond - marker.slideTraceSecond);
    }
    return 0.0;
}

qreal slideHeadRotateSpeedDegreesPerSecond(const TimelineNoteMarker& marker)
{
    if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
        return 0.0;
    }
    const qreal totalLen = static_cast<qreal>(marker.slideNativeTrackLength);
    const qreal totalDuration = totalSlideTraceDurationSeconds(marker);
    if (totalLen <= 0.0 || totalDuration <= 0.0) {
        return 0.0;
    }
    return qMax<qreal>(-4.500 * totalLen / totalDuration, -1080.0);
}

qreal slideHeadFallRotationDegrees(
    const TimelineNoteMarker& marker,
    qreal deltaSeconds,
    qreal tapLifecycleDurationSeconds
)
{
    if ((marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi"))
        || deltaSeconds >= 0.0
        || tapLifecycleDurationSeconds <= 0.0) {
        return 0.0;
    }
    const qreal elapsedSeconds =
        qBound<qreal>(0.0, deltaSeconds + tapLifecycleDurationSeconds, tapLifecycleDurationSeconds);
    return slideHeadRotateSpeedDegreesPerSecond(marker) * elapsedSeconds;
}

qreal tapDoubleStarRotationDegrees(qreal deltaSeconds, qreal tapLifecycleDurationSeconds)
{
    if (deltaSeconds >= 0.0 || tapLifecycleDurationSeconds <= 0.0) {
        return 0.0;
    }
    const qreal elapsedSeconds =
        qBound<qreal>(0.0, deltaSeconds + tapLifecycleDurationSeconds, tapLifecycleDurationSeconds);
    return -360.0 * elapsedSeconds;
}

QColor exTintColor(bool isBreak, bool isEach)
{
    if (isBreak) {
        return QColor("#F59E0B");
    }
    if (isEach) {
        return QColor("#FFF05C");
    }
    return QColor("#FF9FD6");
}

QColor exStarTintColor(bool isBreak, bool isEach)
{
    if (isBreak) {
        return QColor("#F59E0B");
    }
    if (isEach) {
        return QColor("#FFF05C");
    }
    return QColor("#6FB6FF");
}

const QImage* appendOwnedImage(PreviewHeadLayerState* state, QImage image)
{
    if (state == nullptr || image.isNull()) {
        return nullptr;
    }
    state->ownedImages.append(QSharedPointer<QImage>(new QImage(std::move(image))));
    return state->ownedImages.last().data();
}

void appendSprite(
    PreviewHeadLayerState* state,
    const QImage* image,
    const QPointF& center,
    qreal width,
    qreal height,
    qreal rotation,
    qreal opacity = 1.0,
    const QRectF& sourceRect = QRectF(),
    bool cacheable = true
)
{
    if (state == nullptr || image == nullptr || image->isNull() || width <= 0.0 || height <= 0.0 || opacity <= 0.0) {
        return;
    }
    PreviewSpriteDescriptor sprite;
    sprite.image = image;
    sprite.center = center;
    sprite.width = width;
    sprite.height = height;
    sprite.rotationDegrees = rotation;
    sprite.opacity = opacity;
    sprite.sourceRect = sourceRect;
    sprite.cacheable = cacheable;
    state->sprites.append(sprite);
}

}  // namespace

namespace miacode::preview::scene {

PreviewHeadLayerState buildPreviewHeadLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
)
{
    PreviewHeadLayerState layerState;
    layerState.ownedImages.reserve(state.noteMarkers.size() * 6);

    QVector<qsizetype> layerOrder;
    layerOrder.reserve(state.noteMarkers.size());
    for (qsizetype markerIndex = 0; markerIndex < state.noteMarkers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = state.noteMarkers[markerIndex];
        if (marker.type == QLatin1String("hold")
            || marker.type == QLatin1String("tap")
            || marker.type == QLatin1String("slide")
            || marker.type == QLatin1String("wifi")) {
            layerOrder.append(markerIndex);
        }
    }
    std::sort(layerOrder.begin(), layerOrder.end(), [&state](qsizetype a, qsizetype b) {
        const TimelineNoteMarker& markerA = state.noteMarkers[a];
        const TimelineNoteMarker& markerB = state.noteMarkers[b];
        if (!qFuzzyCompare(1.0 + markerA.second, 1.0 + markerB.second)) {
            return markerA.second > markerB.second;
        }
        return a > b;
    });

    const PreviewTapTiming tapTiming = previewTapTimingForFlowSpeed(static_cast<qreal>(state.render.noteFlowSpeed));

    const auto tapApproachFor = [tapTiming](qreal deltaSeconds) {
        return sampleTapApproach(
            deltaSeconds,
            tapTiming.lifecycleDurationSeconds,
            tapTiming.spawnDurationSeconds,
            tapTiming.flyDurationSeconds,
            tapTiming.unitsPerSecond,
            kLogicalDistanceTap,
            kLogicalDistanceEdge
        );
    };

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;

    for (qsizetype markerIndex : layerOrder) {
        const TimelineNoteMarker& marker = state.noteMarkers[markerIndex];
        if (marker.type == QLatin1String("hold")) {
            const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
            const qreal deltaEndSeconds = static_cast<qreal>(state.playheadSeconds - marker.endSecond);
            if (marker.endSecond < marker.second || deltaEndSeconds > 0.0) {
                continue;
            }
            const TapApproachSample headApproach = tapApproachFor(deltaSeconds);
            if (headApproach.scale <= 0.0) {
                continue;
            }
            const QPointF unit = laneUnitVector(marker.lane);
            const bool holdActive = deltaSeconds >= 0.0;
            const QImage* holdImage = nullptr;
            if (marker.isBreak) {
                holdImage = (holdActive && !state.skin.holdBreakOnImage.isNull()) ? &state.skin.holdBreakOnImage : &state.skin.holdBreakImage;
            } else if (marker.isEach) {
                holdImage = (holdActive && !state.skin.holdEachOnImage.isNull()) ? &state.skin.holdEachOnImage : &state.skin.holdEachImage;
            } else {
                holdImage = (holdActive && !state.skin.holdOnImage.isNull()) ? &state.skin.holdOnImage : &state.skin.holdImage;
            }
            if (holdImage == nullptr || holdImage->isNull()) {
                holdImage = selectHoldImage(state.skin, marker);
            }
            if (holdImage == nullptr || holdImage->isNull()) {
                continue;
            }
            const PreviewAnimatedSpriteEffect holdEffect = marker.isBreak
                ? PreviewAnimatedSpriteEffect::BreakAnimate
                : (holdActive ? PreviewAnimatedSpriteEffect::HoldShine : PreviewAnimatedSpriteEffect::None);
            QImage holdCapImage = *holdImage;
            bool ownsHoldImage = false;
            if (marker.isEx && !state.skin.holdExImage.isNull()) {
                const QColor tint = exTintColor(marker.isBreak, marker.isEach);
                holdCapImage = composeOverlayImage(holdCapImage, state.skin.holdExImage, kHoldOverlayBaseMix, kHoldOverlayAlphaMix, &tint);
                ownsHoldImage = true;
            }
            if (holdEffect != PreviewAnimatedSpriteEffect::None) {
                holdCapImage = buildAnimatedSpriteImage(holdCapImage, holdEffect, state.playheadSeconds);
                ownsHoldImage = true;
            }

            const QImage* renderHoldImage = holdImage;
            bool renderHoldImageCacheable = true;
            if (ownsHoldImage) {
                renderHoldImage = appendOwnedImage(&layerState, std::move(holdCapImage));
                renderHoldImageCacheable = false;
            }
            if (renderHoldImage == nullptr || renderHoldImage->isNull()) {
                continue;
            }

            const auto appendHoldStripSlices = [&](const QPointF& center, int bodyLogicalLength, qreal scale) {
                const int srcW = qMax(1, renderHoldImage->width());
                const int srcH = qMax(1, renderHoldImage->height());
                const int capRaw = qMax(1, qMin(srcH / 2, qRound(srcH * kHoldCapSliceRatioNumerator / kHoldCapSliceRatioDenominator)));
                const int midY = qBound(0, srcH / 2, srcH - 1);
                const qreal targetWidth = qMax<qreal>(1.0, qRound(kHoldTargetWidth * canvasScale * scale));
                const qreal targetCapHeight = qMax<qreal>(
                    1.0,
                    qRound(static_cast<qreal>(qRound(static_cast<qreal>(capRaw) * kHoldTargetWidth / srcW)) * canvasScale * scale)
                );
                const qreal targetBodyHeight = qMax<qreal>(0.0, qRound(bodyLogicalLength * canvasScale * scale));
                const qreal capOffset = (targetBodyHeight + targetCapHeight) / 2.0;
                const qreal angle = laneRotationDegrees(marker.lane);
                const QPointF headCenter = center + unit * capOffset;
                const QPointF tailCenter = center - unit * capOffset;
                if (targetBodyHeight > 0.0) {
                    appendSprite(
                        &layerState,
                        renderHoldImage,
                        center,
                        targetWidth,
                        targetBodyHeight,
                        angle,
                        1.0,
                        QRectF(0.0, midY, srcW, 1.0),
                        renderHoldImageCacheable
                    );
                }
                appendSprite(
                    &layerState,
                    renderHoldImage,
                    headCenter,
                    targetWidth,
                    targetCapHeight,
                    angle,
                    1.0,
                    QRectF(0.0, 0.0, srcW, capRaw),
                    renderHoldImageCacheable
                );
                appendSprite(
                    &layerState,
                    renderHoldImage,
                    tailCenter,
                    targetWidth,
                    targetCapHeight,
                    angle,
                    1.0,
                    QRectF(0.0, srcH - capRaw, srcW, capRaw),
                    renderHoldImageCacheable
                );
            };

            if (deltaSeconds < -tapTiming.flyDurationSeconds) {
                const QPointF logicalPoint(
                    kLogicalCanvasCenter + unit.x() * kLogicalDistanceTap,
                    kLogicalCanvasCenter + unit.y() * kLogicalDistanceTap
                );
                const QPointF point = mapLogicalPointToRect(logicalPoint, playfieldRect);
                appendHoldStripSlices(point, 0, headApproach.scale);
                continue;
            }

            const qreal distance = headApproach.distance;
            const qreal distanceEnd = tapApproachFor(deltaEndSeconds).distance;
            const QPointF logicalHead(
                kLogicalCanvasCenter + unit.x() * distance,
                kLogicalCanvasCenter + unit.y() * distance
            );
            const QPointF logicalTail(
                kLogicalCanvasCenter + unit.x() * distanceEnd,
                kLogicalCanvasCenter + unit.y() * distanceEnd
            );
            const QPointF logicalCenter = (logicalHead + logicalTail) * 0.5;
            const QPointF centerPoint = mapLogicalPointToRect(logicalCenter, playfieldRect);
            const int lineLength = qMax(0, qRound(distance - distanceEnd));
            appendHoldStripSlices(centerPoint, lineLength, 1.0);
            continue;
        }

        const bool slideLike = marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
        if (slideLike && !marker.hasHeadStar) {
            continue;
        }
        const bool starMaterialHead = slideLike ? !marker.slideHeadUsesTapMaterial : marker.tapUsesStarMaterial;
        const bool headBreak = slideLike ? marker.headBreak : marker.isBreak;
        const bool headEach = slideLike ? marker.headEach : marker.isEach;
        const bool headEx = slideLike ? marker.headEx : marker.isEx;
        const bool doubleStarHead = starMaterialHead && (slideLike ? marker.sameHeadSlide : marker.tapStarDouble);

        const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        if (slideLike ? deltaSeconds >= 0.0 : deltaSeconds > 0.0) {
            continue;
        }
        const TapApproachSample approach = tapApproachFor(deltaSeconds);
        if (approach.scale <= 0.0) {
            continue;
        }

        const QPointF unit = laneUnitVector(marker.lane);
        const QPointF logicalPoint(
            kLogicalCanvasCenter + unit.x() * approach.distance,
            kLogicalCanvasCenter + unit.y() * approach.distance
        );
        const QPointF point = mapLogicalPointToRect(logicalPoint, playfieldRect);
        const QImage* baseImage = starMaterialHead ? selectSlideStarImage(state.skin, marker) : selectTapImage(state.skin, marker);
        if (baseImage == nullptr || baseImage->isNull()) {
            continue;
        }
        QImage renderImage = *baseImage;
        bool ownsHeadImage = false;
        if (starMaterialHead && headEx) {
            const QImage& overlay = (doubleStarHead && !state.skin.starExDoubleImage.isNull())
                ? state.skin.starExDoubleImage
                : state.skin.starExImage;
            if (!overlay.isNull()) {
                const QColor tint = exStarTintColor(headBreak, headEach);
                renderImage = composeOverlayImage(renderImage, overlay, kTapOverlayBaseMix, kTapOverlayAlphaMix, &tint);
                ownsHeadImage = true;
            }
        } else if (!starMaterialHead && headEx && !state.skin.tapExImage.isNull()) {
            const QColor tint = exTintColor(headBreak, headEach);
            renderImage = composeOverlayImage(renderImage, state.skin.tapExImage, kTapOverlayBaseMix, kTapOverlayAlphaMix, &tint);
            ownsHeadImage = true;
        }
        if (headBreak) {
            renderImage = buildAnimatedSpriteImage(renderImage, PreviewAnimatedSpriteEffect::BreakAnimate, state.playheadSeconds);
            ownsHeadImage = true;
        }
        const QImage* headImage = baseImage;
        bool headImageCacheable = true;
        if (ownsHeadImage) {
            headImage = appendOwnedImage(&layerState, std::move(renderImage));
            headImageCacheable = false;
        }
        if (headImage == nullptr || headImage->isNull()) {
            continue;
        }

        qreal targetWidth = 0.0;
        qreal targetHeight = 0.0;
        qreal rotation = laneRotationDegrees(marker.lane);
        if (starMaterialHead) {
            const qreal baseWidth = (!state.skin.tapImage.isNull() ? state.skin.tapImage.width() * kSkinAssetScale : headImage->width() * kStarAssetScale)
                * kSlideSpawnStarRelativeScale;
            const qreal baseHeight = (!state.skin.tapImage.isNull() ? state.skin.tapImage.height() * kSkinAssetScale : headImage->height() * kStarAssetScale)
                * kSlideSpawnStarRelativeScale;
            targetWidth = qMax<qreal>(1.0, qRound(baseWidth * canvasScale * approach.scale));
            targetHeight = qMax<qreal>(1.0, qRound(baseHeight * canvasScale * approach.scale));
            rotation = mirroredStarAngleDegrees(
                laneRotationDegrees(marker.lane)
                + (slideLike
                    ? slideHeadFallRotationDegrees(marker, deltaSeconds, tapTiming.lifecycleDurationSeconds)
                    : (marker.tapStarDouble ? tapDoubleStarRotationDegrees(deltaSeconds, tapTiming.lifecycleDurationSeconds) : 0.0))
            );
        } else {
            const qreal imageScale = canvasScale * approach.scale * kSkinAssetScale;
            targetWidth = qMax<qreal>(1.0, qRound(headImage->width() * imageScale));
            targetHeight = qMax<qreal>(1.0, qRound(headImage->height() * imageScale));
        }

        appendSprite(&layerState, headImage, point, targetWidth, targetHeight, rotation, 1.0, QRectF(), headImageCacheable);
    }

    return layerState;
}

}  // namespace miacode::preview::scene
