#include "preview/scene/PreviewTouchHoldLayerState.h"

#include "preview/scene/PreviewAnimatedSpriteHelpers.h"
#include "preview/scene/PreviewOpacityCurves.h"
#include "preview/scene/PreviewSceneConstants.h"
#include "preview/scene/PreviewSceneMath.h"

namespace miacode::preview::scene {

PreviewTouchHoldLayerState buildPreviewTouchHoldLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
)
{
    PreviewTouchHoldLayerState layerState;
    layerState.sprites.reserve(state.noteMarkers.size() * 5);
    layerState.arcs.reserve(state.noteMarkers.size());

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

    for (const TimelineNoteMarker& marker : state.noteMarkers) {
        if (marker.type != QLatin1String("touch_hold")) {
            continue;
        }
        if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
            continue;
        }
        if (marker.endSecond <= marker.second) {
            continue;
        }

        const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        const qreal holdDuration = qMax<qreal>(0.001, static_cast<qreal>(marker.endSecond - marker.second));
        if (deltaSeconds <= -kTouchDurationSeconds || deltaSeconds >= holdDuration) {
            continue;
        }

        const QImage& pointBase =
            (marker.isBreak && !state.skin.touchPointBreakImage.isNull())
                ? state.skin.touchPointBreakImage
                : ((marker.isEach && !state.skin.touchPointEachImage.isNull()) ? state.skin.touchPointEachImage : state.skin.touchPointImage);
        const QImage& borderBase =
            (marker.isBreak && !state.skin.touchHoldBreakBorderImage.isNull())
                ? state.skin.touchHoldBreakBorderImage
                : state.skin.touchHoldBorderImage;
        const QImage& fan0Base =
            (marker.isBreak && !state.skin.touchHoldBreak0Image.isNull()) ? state.skin.touchHoldBreak0Image : state.skin.touchHold0Image;
        const QImage& fan1Base =
            (marker.isBreak && !state.skin.touchHoldBreak1Image.isNull()) ? state.skin.touchHoldBreak1Image : state.skin.touchHold1Image;
        const QImage& fan2Base =
            (marker.isBreak && !state.skin.touchHoldBreak2Image.isNull()) ? state.skin.touchHoldBreak2Image : state.skin.touchHold2Image;
        const QImage& fan3Base =
            (marker.isBreak && !state.skin.touchHoldBreak3Image.isNull()) ? state.skin.touchHoldBreak3Image : state.skin.touchHold3Image;
        if (pointBase.isNull() || borderBase.isNull() || fan0Base.isNull() || fan1Base.isNull() || fan2Base.isNull() || fan3Base.isNull()) {
            continue;
        }

        const QImage* renderFan0 = &fan0Base;
        const QImage* renderFan1 = &fan1Base;
        const QImage* renderFan2 = &fan2Base;
        const QImage* renderFan3 = &fan3Base;
        const PreviewAnimatedSpriteEffect fansEffect = marker.isBreak
            ? PreviewAnimatedSpriteEffect::BreakAnimate
            : PreviewAnimatedSpriteEffect::None;
        const bool fansCacheable = true;
        if (renderFan0 == nullptr || renderFan1 == nullptr || renderFan2 == nullptr || renderFan3 == nullptr) {
            continue;
        }

        const QPointF point = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
        qreal alpha = 1.0;
        qreal logicalOffset = kTouchHoldClosedOffset;
        if (deltaSeconds < 0.0) {
            alpha = touchPreHitAlpha(deltaSeconds, kTouchDurationSeconds, kTouchShowDurationSeconds);
            logicalOffset = touchLogicalOffsetForDelta(
                deltaSeconds,
                kTouchHoldStartOffset,
                kTouchHoldClosedOffset,
                kTouchDurationSeconds,
                kTouchShowDurationSeconds,
                kTouchCloseDurationSeconds
            );
        }
        const qreal offset = mapLogicalLengthToRect(logicalOffset, playfieldRect);
        const qreal pointWidth = qMax<qreal>(1.0, qRound(pointBase.width() * kTouchAssetScale * canvasScale));
        const qreal pointHeight = qMax<qreal>(1.0, qRound(pointBase.height() * kTouchAssetScale * canvasScale));
        const qreal borderWidth = qMax<qreal>(1.0, qRound(borderBase.width() * kTouchAssetScale * canvasScale));
        const qreal borderHeight = qMax<qreal>(1.0, qRound(borderBase.height() * kTouchAssetScale * canvasScale));

        const struct {
            const QImage* image;
            int angle;
            qreal dx;
            qreal dy;
        } layout[] = {
            {renderFan0, kTouchHoldUpperRightAngleDegrees, offset, -offset},
            {renderFan1, kTouchHoldLowerRightAngleDegrees, offset, offset},
            {renderFan2, kTouchHoldLowerLeftAngleDegrees, -offset, offset},
            {renderFan3, kTouchHoldUpperLeftAngleDegrees, -offset, -offset},
        };
        for (int index = 3; index >= 0; --index) {
            const auto& pieceLayout = layout[index];
            if (pieceLayout.image == nullptr || pieceLayout.image->isNull()) {
                continue;
            }
            appendSprite(
                pieceLayout.image,
                QPointF(point.x() + pieceLayout.dx, point.y() + pieceLayout.dy),
                qMax<qreal>(1.0, qRound(pieceLayout.image->width() * kTouchAssetScale * canvasScale)),
                qMax<qreal>(1.0, qRound(pieceLayout.image->height() * kTouchAssetScale * canvasScale)),
                pieceLayout.angle,
                alpha,
                fansEffect,
                fansCacheable
            );
        }
        appendSprite(&pointBase, point, pointWidth, pointHeight, 0.0, alpha);

        if (deltaSeconds >= 0.0 && !borderBase.isNull()) {
            const qreal progress = qBound<qreal>(0.0, deltaSeconds / holdDuration, 1.0);
            if (progress > 0.0) {
                PreviewArcDescriptor arc;
                arc.image = &borderBase;
                arc.center = point;
                arc.width = borderWidth;
                arc.height = borderHeight;
                arc.startDegrees = 90.0;
                arc.sweepDegrees = -progress * 360.0;
                arc.opacity = 1.0;
                arc.cacheable = true;
                layerState.arcs.append(arc);
            }
        }
    }

    return layerState;
}

}  // namespace miacode::preview::scene
