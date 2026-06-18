#include "core/scene/PreviewTouchHoldLayerState.h"

#include "core/scene/PreviewAnimatedSpriteHelpers.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"

namespace miacode::preview::scene {

PreviewTouchHoldLayerState buildPreviewTouchHoldLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewTouchHoldLayerState layerState;
    layerState.sprites.reserve(markers.size() * 5);
    layerState.arcs.reserve(markers.size());
    // Per-marker touch-hold timing: hsMultiplier scales touchFlowSpeed.
    // Per-type sign policy: touch-hold takes the HS magnitude (never reverses).
    const auto markerTouchTiming = [&state](double hsMultiplier) {
        return previewTouchTimingForEffectiveFlowSpeed(
            static_cast<qreal>(state.render.touchFlowSpeed * qAbs(hsMultiplier)));
    };

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;

    // Emit-order optimisation: group sprites by family across all hold markers so the
    // run-builder can collapse N holds × 5 pieces into a constant number of batches. Within
    // a single hold the painter's-algorithm order is preserved (fan3 → fan2 → fan1 → fan0 →
    // point); only the cross-marker ordering changes. This is safe because touch-zones
    // (C/A/B/D/E) sit at distinct screen positions, so different touch_hold markers do not
    // overlap geometrically — there is no observer who can tell which one drew first.
    constexpr int kFanBucketCount = 4;  // index 0 = fan3 (back) … 3 = fan0 (front)
    QVector<PreviewSpriteDescriptor> fanBuckets[kFanBucketCount];
    QVector<PreviewSpriteDescriptor> pointBucket;
    fanBuckets[0].reserve(markers.size());
    fanBuckets[1].reserve(markers.size());
    fanBuckets[2].reserve(markers.size());
    fanBuckets[3].reserve(markers.size());
    pointBucket.reserve(markers.size());

    const auto appendToBucket = [](
                                    QVector<PreviewSpriteDescriptor>& bucket,
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
        bucket.append(sprite);
    };

    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        if (marker.type != QLatin1String("touch_hold")) {
            continue;
        }
        if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
            continue;
        }
        // beta51+ — only the truly invalid `endSecond < second` case is
        // dropped here; the zero-duration case (`endSecond == second`,
        // produced by bare `Ch` / `A1h` syntax accepted by the parser
        // in db52893) falls through to the line below where the
        // qMax<qreal>(0.001, …) floor gives the animation a tiny but
        // non-zero window. Before this change `<=` short-circuited the
        // floor, so bare touch-hold markers parsed but never reached
        // any of the sprite layout code — they were invisible on the
        // playfield. Regular hold notes (`1h`) already use `<` in
        // PreviewHeadLayerState / PreviewGuideLayerState so this just
        // brings the touch-hold path into line.
        if (marker.endSecond < marker.second) {
            continue;
        }

        const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        const qreal holdDuration = qMax<qreal>(0.001, static_cast<qreal>(marker.endSecond - marker.second));
        const PreviewTouchTiming touchTiming = markerTouchTiming(marker.hsMultiplier);
        if (deltaSeconds <= -touchTiming.durationSeconds || deltaSeconds >= holdDuration) {
            continue;
        }

        const QImage& pointBase =
            (marker.isMine && !state.skin.touchPointMineImage.isNull())
                ? state.skin.touchPointMineImage
                : (marker.isBreak && !state.skin.touchPointBreakImage.isNull())
                    ? state.skin.touchPointBreakImage
                    : ((marker.isEach && !state.skin.touchPointEachImage.isNull()) ? state.skin.touchPointEachImage : state.skin.touchPointImage);
        const QImage& borderBase =
            (marker.isMine && !state.skin.touchHoldBorderMineImage.isNull())
                ? state.skin.touchHoldBorderMineImage
                : (marker.isBreak && !state.skin.touchHoldBreakBorderImage.isNull())
                    ? state.skin.touchHoldBreakBorderImage
                    : state.skin.touchHoldBorderImage;
        const QImage& fan0Base =
            (marker.isMine && !state.skin.touchHoldMine0Image.isNull()) ? state.skin.touchHoldMine0Image
            : (marker.isBreak && !state.skin.touchHoldBreak0Image.isNull()) ? state.skin.touchHoldBreak0Image : state.skin.touchHold0Image;
        const QImage& fan1Base =
            (marker.isMine && !state.skin.touchHoldMine1Image.isNull()) ? state.skin.touchHoldMine1Image
            : (marker.isBreak && !state.skin.touchHoldBreak1Image.isNull()) ? state.skin.touchHoldBreak1Image : state.skin.touchHold1Image;
        const QImage& fan2Base =
            (marker.isMine && !state.skin.touchHoldMine2Image.isNull()) ? state.skin.touchHoldMine2Image
            : (marker.isBreak && !state.skin.touchHoldBreak2Image.isNull()) ? state.skin.touchHoldBreak2Image : state.skin.touchHold2Image;
        const QImage& fan3Base =
            (marker.isMine && !state.skin.touchHoldMine3Image.isNull()) ? state.skin.touchHoldMine3Image
            : (marker.isBreak && !state.skin.touchHoldBreak3Image.isNull()) ? state.skin.touchHoldBreak3Image : state.skin.touchHold3Image;
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
            alpha = touchPreHitAlpha(deltaSeconds, touchTiming.durationSeconds, touchTiming.showDurationSeconds);
            logicalOffset = touchLogicalOffsetForDelta(
                deltaSeconds,
                kTouchHoldStartOffset,
                kTouchHoldClosedOffset,
                touchTiming.durationSeconds,
                touchTiming.showDurationSeconds,
                touchTiming.closeDurationSeconds
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
            // index=3 → fan3 (back, bucket 0); index=0 → fan0 (front, bucket 3).
            const int bucketIndex = 3 - index;
            appendToBucket(
                fanBuckets[bucketIndex],
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
        appendToBucket(pointBucket, &pointBase, point, pointWidth, pointHeight, 0.0, alpha);

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

    // Painter's-algorithm flush: back-most fans first, then layer toward the front, then the
    // touch-point on top. Concatenating the buckets in this order preserves the per-hold
    // z-stack while letting the run-builder fuse same-family sprites from all holds into one
    // batch each.
    qsizetype emittedSpriteTotal = pointBucket.size();
    for (int bucketIndex = 0; bucketIndex < kFanBucketCount; ++bucketIndex) {
        emittedSpriteTotal += fanBuckets[bucketIndex].size();
    }
    layerState.sprites.reserve(layerState.sprites.size() + emittedSpriteTotal);
    for (int bucketIndex = 0; bucketIndex < kFanBucketCount; ++bucketIndex) {
        layerState.sprites.append(fanBuckets[bucketIndex]);
    }
    layerState.sprites.append(pointBucket);

    return layerState;
}

}  // namespace miacode::preview::scene
