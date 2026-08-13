#include "core/scene/PreviewSlideMotionLayerState.h"

#include "core/scene/PreviewAnimatedSpriteHelpers.h"
#include "core/scene/PreviewMarkerDrawOrder.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"
#include "core/scene/PreviewSkinSelectors.h"
#include "core/scene/PreviewTrackShared.h"

#include <cmath>

#include <algorithm>

namespace {

qreal mirroredStarAngleDegrees(qreal angleDegrees)
{
    return 360.0 - angleDegrees;
}

QPointF interpolatePoint(const QVector<QPointF>& points, qreal proportion)
{
    if (points.isEmpty()) {
        return QPointF();
    }
    if (points.size() == 1 || proportion <= 0.0) {
        return points.constFirst();
    }
    if (proportion >= 1.0) {
        return points.constLast();
    }

    const qreal scaledIndex = qBound<qreal>(0.0, proportion, 1.0) * (points.size() - 1);
    const int startIndex = qFloor(scaledIndex);
    const int endIndex = qMin(points.size() - 1, startIndex + 1);
    const qreal localT = scaledIndex - startIndex;
    return points[startIndex] * (1.0 - localT) + points[endIndex] * localT;
}

qreal interpolateAngle(const QVector<double>& angles, qreal proportion)
{
    if (angles.isEmpty()) {
        return 0.0;
    }
    if (angles.size() == 1 || proportion <= 0.0) {
        return angles.constFirst();
    }
    if (proportion >= 1.0) {
        return angles.constLast();
    }

    const qreal scaledIndex = qBound<qreal>(0.0, proportion, 1.0) * (angles.size() - 1);
    const int startIndex = qFloor(scaledIndex);
    const int endIndex = qMin(angles.size() - 1, startIndex + 1);
    const qreal localT = scaledIndex - startIndex;
    const qreal startAngle = angles[startIndex];
    const qreal endAngle = angles[endIndex];
    const qreal delta = std::fmod(endAngle - startAngle + 540.0, 360.0) - 180.0;
    return startAngle + delta * localT;
}

}  // namespace

namespace miacode::preview::scene {

PreviewSlideMotionLayerState buildPreviewSlideMotionLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewSlideMotionLayerState layerState;
    layerState.sprites.reserve(markers.size() * 8);

    const bool usesPreparedDrawOrder = markers.usesPreparedLayer() && !markers.preparedDrawOrder().isEmpty();
    QVector<int> orderedPreparedIndices;
    QVector<int> orderedMarkerViewIndices;
    if (usesPreparedDrawOrder) {
        orderedPreparedIndices = markers.activePreparedIndices();
        std::stable_sort(orderedPreparedIndices.begin(), orderedPreparedIndices.end(), [&markers](int a, int b) {
            return markers.preparedDrawRank(a) < markers.preparedDrawRank(b);
        });
    } else {
        orderedMarkerViewIndices.reserve(markers.size());
        for (int markerViewIndex = 0; markerViewIndex < markers.size(); ++markerViewIndex) {
            const TimelineNoteMarker& marker = markers.markerAt(markerViewIndex);
            if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
                orderedMarkerViewIndices.append(markerViewIndex);
            }
        }
        sortPreviewMarkerViewIndicesForDraw(
            markers,
            &orderedMarkerViewIndices,
            state.render.slideEarlierSecondAndTextOnTop
        );
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const auto appendSprite = [&layerState, &playfieldRect, canvasScale](
                                  const QImage* image,
                                  const QPointF& logicalPoint,
                                  qreal angleDegrees,
                                  qreal imageScale,
                                  qreal imageOpacity,
                                  PreviewAnimatedSpriteEffect effect,
                                  bool cacheable
                              ) {
        if (image == nullptr || image->isNull() || imageOpacity <= 0.0 || imageScale <= 0.0) {
            return;
        }
        PreviewSpriteDescriptor sprite;
        sprite.image = image;
        sprite.center = mapLogicalPointToRect(
            QPointF(kLogicalCanvasCenter + logicalPoint.x(), kLogicalCanvasCenter + logicalPoint.y()),
            playfieldRect
        );
        sprite.width = qMax<qreal>(1.0, qRound(image->width() * canvasScale * imageScale));
        sprite.height = qMax<qreal>(1.0, qRound(image->height() * canvasScale * imageScale));
        sprite.rotationDegrees = angleDegrees;
        sprite.opacity = imageOpacity;
        sprite.effect = effect;
        sprite.cacheable = cacheable;
        layerState.sprites.append(sprite);
    };

    const int orderedCount = usesPreparedDrawOrder ? orderedPreparedIndices.size() : orderedMarkerViewIndices.size();
    for (int orderIndex = 0; orderIndex < orderedCount; ++orderIndex) {
        const int preparedIndex = usesPreparedDrawOrder ? orderedPreparedIndices.at(orderIndex) : -1;
        const int markerViewIndex = usesPreparedDrawOrder ? -1 : orderedMarkerViewIndices.at(orderIndex);
        const TimelineNoteMarker& marker = usesPreparedDrawOrder
            ? markers.markerForPreparedIndex(preparedIndex)
            : markers.markerAt(markerViewIndex);
        if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
            continue;
        }
        if (marker.slideTraceSecond <= marker.second) {
            continue;
        }
        const bool slideMarker = marker.type == QLatin1String("slide");
        if ((slideMarker && marker.slideSegmentPoints.isEmpty()) || (!slideMarker && marker.wifiLanePoints.isEmpty())) {
            continue;
        }
        if (state.playheadSeconds < marker.second || state.playheadSeconds > marker.endSecond) {
            continue;
        }

        const QImage* baseImage = selectSlideMovingStarImage(state.skin, marker, state.render.useMineSkin);
        if (baseImage == nullptr || baseImage->isNull()) {
            continue;
        }
        const QImage* renderImage = baseImage;
        const PreviewAnimatedSpriteEffect effect = marker.trackBreak
            ? PreviewAnimatedSpriteEffect::BreakAnimate
            : PreviewAnimatedSpriteEffect::None;
        const bool cacheable = true;

        if (slideMarker) {
            qreal angle = 0.0;
            QPointF logicalPoint;
            qreal imageScale = kStarAssetScale;
            qreal imageOpacity = 1.0;

            if (state.playheadSeconds < marker.slideTraceSecond) {
                const QVector<QPointF>& points = marker.slideSegmentPoints.constFirst();
                const QVector<double>& angles = marker.slideSegmentAngles.constFirst();
                if (points.isEmpty()) {
                    continue;
                }
                logicalPoint = points.constFirst();
                angle = angles.isEmpty() ? 0.0 : angles.constFirst();
                if (!marker.headlessImmediate) {
                    const qreal waitDuration = qMax<qreal>(kRenderDurationEpsilon, static_cast<qreal>(marker.slideTraceSecond - marker.second));
                    const qreal waitT = qBound<qreal>(
                        0.0,
                        static_cast<qreal>((state.playheadSeconds - marker.second) / waitDuration),
                        1.0
                    );
                    const qreal startScale = slideStartupStarInitialScale(state.skin, *renderImage);
                    imageScale = startScale + (kStarAssetScale - startScale) * waitT;
                    imageOpacity = waitingStarOpacity(waitT, kObjectWaitStateStartOpacity, kObjectWaitStateOpacityDelta);
                }
            } else {
                // Shared with the track layer so a track erasure keyed to the
                // star cannot drift away from where the star is drawn.
                int segmentIndex = 0;
                qreal proportion = 1.0;
                previewSlideStarSegment(
                    marker,
                    state.playheadSeconds,
                    marker.slideSegmentPoints.size(),
                    &segmentIndex,
                    &proportion
                );
                const QVector<QPointF>& points = marker.slideSegmentPoints[segmentIndex];
                const QVector<double>& angles = marker.slideSegmentAngles.value(segmentIndex);
                if (points.isEmpty()) {
                    continue;
                }
                logicalPoint = interpolatePoint(points, proportion);
                angle = interpolateAngle(angles, proportion);
            }

            appendSprite(
                renderImage,
                logicalPoint,
                mirroredStarAngleDegrees(angle),
                imageScale,
                imageOpacity,
                effect,
                cacheable
            );
            continue;
        }

        qreal imageScale = kStarAssetScale;
        qreal imageOpacity = 1.0;
        qreal proportion = 0.0;
        const bool waiting = state.playheadSeconds < marker.slideTraceSecond;
        if (waiting) {
            if (!marker.headlessImmediate) {
                const qreal waitDuration = qMax<qreal>(kRenderDurationEpsilon, static_cast<qreal>(marker.slideTraceSecond - marker.second));
                const qreal waitT = qBound<qreal>(
                    0.0,
                    static_cast<qreal>((state.playheadSeconds - marker.second) / waitDuration),
                    1.0
                );
                const qreal startScale = slideStartupStarInitialScale(state.skin, *renderImage);
                imageScale = startScale + (kStarAssetScale - startScale) * waitT;
                imageOpacity = waitingStarOpacity(waitT, kObjectWaitStateStartOpacity, kObjectWaitStateOpacityDelta);
            }
        } else if (!marker.slideSegmentDurations.isEmpty()) {
            const qreal duration = qMax<qreal>(kRenderDurationEpsilon, marker.slideSegmentDurations.constFirst());
            proportion = qBound<qreal>(
                0.0,
                static_cast<qreal>((state.playheadSeconds - marker.slideTraceSecond) / duration),
                1.0
            );
        } else if (marker.endSecond > marker.slideTraceSecond) {
            const qreal duration = qMax<qreal>(kRenderDurationEpsilon, static_cast<qreal>(marker.endSecond - marker.slideTraceSecond));
            proportion = qBound<qreal>(
                0.0,
                static_cast<qreal>((state.playheadSeconds - marker.slideTraceSecond) / duration),
                1.0
            );
        }

        for (int laneIndex = 0; laneIndex < marker.wifiLanePoints.size(); ++laneIndex) {
            const QVector<QPointF>& points = marker.wifiLanePoints[laneIndex];
            const QVector<double>& angles = marker.wifiLaneAngles.value(laneIndex);
            if (points.isEmpty()) {
                continue;
            }
            const QPointF logicalPoint = waiting ? points.constFirst() : interpolatePoint(points, proportion);
            const qreal angle = waiting
                ? (angles.isEmpty() ? 0.0 : angles.constFirst())
                : interpolateAngle(angles, proportion);
            appendSprite(
                renderImage,
                logicalPoint,
                mirroredStarAngleDegrees(angle),
                imageScale,
                imageOpacity,
                effect,
                cacheable
            );
        }
    }

    return layerState;
}

}  // namespace miacode::preview::scene
