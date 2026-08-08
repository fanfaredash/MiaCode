#include "core/scene/PreviewTouchLayerState.h"

#include "core/scene/PreviewAnimatedSpriteHelpers.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"

#include <QHash>

namespace miacode::preview::scene {

PreviewTouchLayerState buildPreviewTouchLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewTouchLayerState layerState;
    QHash<quint64, QVector<int>> sensorMarkers;
    sensorMarkers.reserve(16);
    // Per-marker touch timing: hsMultiplier scales touchFlowSpeed, no
    // upper clamp on the effective speed (Q4).
    // Per-type sign policy: touch takes the HS magnitude (never reverses;
    // only tap/star/each-line honor a negative HS). qAbs makes that explicit.
    const auto markerTouchTiming = [&state](double hsMultiplier) {
        return previewTouchTimingForEffectiveFlowSpeed(
            static_cast<qreal>(state.render.touchFlowSpeed * qAbs(hsMultiplier)));
    };

    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        if (marker.type != QLatin1String("touch")) {
            continue;
        }
        const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
        const PreviewTouchTiming touchTiming = markerTouchTiming(marker.hsMultiplier);
        if (deltaSeconds <= -touchTiming.durationSeconds || deltaSeconds >= 0.0) {
            continue;
        }
        sensorMarkers[touchRegionKey(marker)].append(markerIndex);
    }
    // Order each sensor's marker list by hit time ascending so [0] = front (earliest hit),
    // [1] = next-up (drives border_2 sprite variant), [2] = third (drives border_3 variant).
    for (auto it = sensorMarkers.begin(); it != sensorMarkers.end(); ++it) {
        std::sort(it->begin(), it->end(), [&markers](int a, int b) {
            return markers.markerAt(a).second < markers.markerAt(b).second;
        });
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
        const PreviewTouchTiming touchTiming = markerTouchTiming(marker.hsMultiplier);
        if (deltaSeconds <= -touchTiming.durationSeconds || deltaSeconds >= 0.0) {
            continue;
        }

        const QImage& basePointImage =
            (state.render.useMineSkin && marker.isMine && !state.skin.touchPointMineImage.isNull())
                ? state.skin.touchPointMineImage
                : (marker.isBreak && !state.skin.touchPointBreakImage.isNull())
                    ? state.skin.touchPointBreakImage
                    : ((marker.isEach && !state.skin.touchPointEachImage.isNull()) ? state.skin.touchPointEachImage : state.skin.touchPointImage);
        const QImage& baseCornerImage =
            (state.render.useMineSkin && marker.isMine && !state.skin.touchCornerMineImage.isNull())
                ? state.skin.touchCornerMineImage
                : (marker.isBreak && !state.skin.touchCornerBreakImage.isNull())
                    ? state.skin.touchCornerBreakImage
                    : ((marker.isEach && !state.skin.touchCornerEachImage.isNull()) ? state.skin.touchCornerEachImage : state.skin.touchCornerImage);
        if (basePointImage.isNull() || baseCornerImage.isNull()) {
            continue;
        }
        const QImage* cornerImage = &baseCornerImage;
        const PreviewAnimatedSpriteEffect cornerEffect = marker.isBreak
            ? PreviewAnimatedSpriteEffect::BreakAnimate
            : PreviewAnimatedSpriteEffect::None;
        const bool cornerImageCacheable = true;

        const QPointF point = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
        const qreal alpha =
            touchPreHitAlpha(deltaSeconds, touchTiming.durationSeconds, touchTiming.showDurationSeconds);
        const qreal logicalOffset = touchLogicalOffsetForDelta(
            deltaSeconds,
            kTouchStartOffset,
            kTouchClosedOffset,
            touchTiming.durationSeconds,
            touchTiming.showDurationSeconds,
            touchTiming.closeDurationSeconds
        );
        const qreal offset = mapLogicalLengthToRect(logicalOffset, playfieldRect);
        const QVector<int>& sensorList = sensorMarkers.value(touchRegionKey(marker));
        const bool isFrontOnSensor = !sensorList.isEmpty() && sensorList.first() == markerIndex;

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
        // Per-sensor borders: emitted only on the front (earliest-hit) marker's iteration so each
        // sensor gets at most one border_2 / border_3 (matches MajdataPlay's per-sensor TouchBorder).
        // Variant follows queue[1] / queue[2]'s flags — the next/third upcoming touch — and alpha
        // pops to 1.0 instantly rather than tracking the front touch's pre-hit fade.
        if (isFrontOnSensor && sensorList.size() >= kTouchOverlapBorder2Threshold) {
            const TimelineNoteMarker& secondMarker = markers.markerAt(sensorList.at(1));
            const QImage* secondBorder2 = (state.render.useMineSkin && secondMarker.isMine && !state.skin.touchBorder2MineImage.isNull())
                ? &state.skin.touchBorder2MineImage
                : secondMarker.isBreak
                    ? &state.skin.touchBorder2BreakImage
                    : (secondMarker.isEach
                        ? &state.skin.touchBorder2EachImage
                        : &state.skin.touchBorder2Image);
            if (secondBorder2 != nullptr && !secondBorder2->isNull()) {
                appendSprite(
                    secondBorder2,
                    point,
                    qMax<qreal>(1.0, qRound(secondBorder2->width() * kTouchAssetScale * canvasScale)),
                    qMax<qreal>(1.0, qRound(secondBorder2->height() * kTouchAssetScale * canvasScale)),
                    0.0,
                    1.0
                );
            }
            if (sensorList.size() >= kTouchOverlapBorder3Threshold) {
                const TimelineNoteMarker& thirdMarker = markers.markerAt(sensorList.at(2));
                const QImage* thirdBorder3 = (state.render.useMineSkin && thirdMarker.isMine && !state.skin.touchBorder3MineImage.isNull())
                    ? &state.skin.touchBorder3MineImage
                    : thirdMarker.isBreak
                        ? &state.skin.touchBorder3BreakImage
                        : (thirdMarker.isEach
                            ? &state.skin.touchBorder3EachImage
                            : &state.skin.touchBorder3Image);
                if (thirdBorder3 != nullptr && !thirdBorder3->isNull()) {
                    appendSprite(
                        thirdBorder3,
                        point,
                        qMax<qreal>(1.0, qRound(thirdBorder3->width() * kTouchAssetScale * canvasScale)),
                        qMax<qreal>(1.0, qRound(thirdBorder3->height() * kTouchAssetScale * canvasScale)),
                        0.0,
                        1.0
                    );
                }
            }
        }
        appendSprite(&basePointImage, point, pointWidth, pointHeight, 0.0, alpha);
    }

    return layerState;
}

}  // namespace miacode::preview::scene
