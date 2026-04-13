#include "preview/scene/PreviewTouchLayerState.h"

#include "preview/scene/PreviewAnimatedSpriteHelpers.h"
#include "preview/scene/PreviewOpacityCurves.h"
#include "preview/scene/PreviewSceneConstants.h"
#include "preview/scene/PreviewSceneMath.h"

#include <QHash>

namespace miacode::preview::scene {

PreviewTouchLayerState buildPreviewTouchLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewTouchLayerState layerState;
    QHash<quint64, int> overlapCounts;
    overlapCounts.reserve(16);

    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        if (marker.type != QLatin1String("touch")) {
            continue;
        }
        const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        if (deltaSeconds <= -kTouchDurationSeconds || deltaSeconds >= 0.0) {
            continue;
        }
        overlapCounts[touchRegionKey(marker)] += 1;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const auto appendSprite = [&layerState](
                                  const QImage* image,
                                  const QPointF& center,
                                  qreal width,
                                  qreal height,
                                  qreal rotation,
                                  qreal opacity,
                                  PreviewAnimatedSpriteEffect effect = PreviewAnimatedSpriteEffect::None,
                                  bool cacheable = true
                              ) {
        if (image == nullptr || image->isNull() || opacity <= 0.0 || width <= 0.0 || height <= 0.0) {
            return;
        }
        PreviewSpriteDescriptor sprite;
        sprite.image = image;
        sprite.center = center;
        sprite.width = width;
        sprite.height = height;
        sprite.rotationDegrees = rotation;
        sprite.opacity = opacity;
        sprite.effect = effect;
        sprite.cacheable = cacheable;
        layerState.sprites.append(sprite);
    };

    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        if (marker.type != QLatin1String("touch")) {
            continue;
        }
        if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
            continue;
        }
        const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        if (deltaSeconds <= -kTouchDurationSeconds || deltaSeconds >= 0.0) {
            continue;
        }

        const QImage& basePointImage =
            (marker.isBreak && !state.skin.touchPointBreakImage.isNull())
                ? state.skin.touchPointBreakImage
                : ((marker.isEach && !state.skin.touchPointEachImage.isNull()) ? state.skin.touchPointEachImage : state.skin.touchPointImage);
        const QImage& baseCornerImage =
            (marker.isBreak && !state.skin.touchCornerBreakImage.isNull())
                ? state.skin.touchCornerBreakImage
                : ((marker.isEach && !state.skin.touchCornerEachImage.isNull()) ? state.skin.touchCornerEachImage : state.skin.touchCornerImage);
        const QImage* border2Image =
            marker.isBreak ? &state.skin.touchBorder2BreakImage : (marker.isEach ? &state.skin.touchBorder2EachImage : &state.skin.touchBorder2Image);
        const QImage* border3Image =
            marker.isBreak ? &state.skin.touchBorder3BreakImage : (marker.isEach ? &state.skin.touchBorder3EachImage : &state.skin.touchBorder3Image);
        if (basePointImage.isNull() || baseCornerImage.isNull()) {
            continue;
        }
        const QImage* cornerImage = &baseCornerImage;
        const PreviewAnimatedSpriteEffect cornerEffect = marker.isBreak
            ? PreviewAnimatedSpriteEffect::BreakAnimate
            : PreviewAnimatedSpriteEffect::None;
        const bool cornerImageCacheable = true;

        const QPointF point = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
        const qreal alpha = touchPreHitAlpha(deltaSeconds, kTouchDurationSeconds, kTouchShowDurationSeconds);
        const qreal logicalOffset = touchLogicalOffsetForDelta(
            deltaSeconds,
            kTouchStartOffset,
            kTouchClosedOffset,
            kTouchDurationSeconds,
            kTouchShowDurationSeconds,
            kTouchCloseDurationSeconds
        );
        const qreal offset = mapLogicalLengthToRect(logicalOffset, playfieldRect);
        const int overlapCount = overlapCounts.value(touchRegionKey(marker), 0);

        const qreal pointWidth = qMax<qreal>(1.0, qRound(basePointImage.width() * kTouchAssetScale * canvasScale));
        const qreal pointHeight = qMax<qreal>(1.0, qRound(basePointImage.height() * kTouchAssetScale * canvasScale));
        const qreal cornerWidth = qMax<qreal>(1.0, qRound(cornerImage->width() * kTouchAssetScale * canvasScale));
        const qreal cornerHeight = qMax<qreal>(1.0, qRound(cornerImage->height() * kTouchAssetScale * canvasScale));

        const struct {
            qreal dx;
            qreal dy;
            int angle;
        } layout[] = {
            {0.0, -offset, kTouchUpAngleDegrees},
            {offset, 0.0, kTouchRightAngleDegrees},
            {0.0, offset, kTouchDownAngleDegrees},
            {-offset, 0.0, kTouchLeftAngleDegrees},
        };
        for (const auto& pieceLayout : layout) {
            appendSprite(
                cornerImage,
                QPointF(point.x() + pieceLayout.dx, point.y() + pieceLayout.dy),
                cornerWidth,
                cornerHeight,
                pieceLayout.angle,
                alpha,
                cornerEffect,
                cornerImageCacheable
            );
        }
        if (overlapCount >= kTouchOverlapBorder2Threshold && border2Image != nullptr && !border2Image->isNull()) {
            appendSprite(
                border2Image,
                point,
                qMax<qreal>(1.0, qRound(border2Image->width() * kTouchAssetScale * canvasScale)),
                qMax<qreal>(1.0, qRound(border2Image->height() * kTouchAssetScale * canvasScale)),
                0.0,
                alpha
            );
        }
        if (overlapCount >= kTouchOverlapBorder3Threshold && border3Image != nullptr && !border3Image->isNull()) {
            appendSprite(
                border3Image,
                point,
                qMax<qreal>(1.0, qRound(border3Image->width() * kTouchAssetScale * canvasScale)),
                qMax<qreal>(1.0, qRound(border3Image->height() * kTouchAssetScale * canvasScale)),
                0.0,
                alpha
            );
        }
        appendSprite(&basePointImage, point, pointWidth, pointHeight, 0.0, alpha);
    }

    return layerState;
}

}  // namespace miacode::preview::scene
