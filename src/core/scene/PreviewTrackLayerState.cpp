#include "core/scene/PreviewTrackLayerState.h"

#include "core/scene/PreviewAnimatedSpriteHelpers.h"
#include "core/scene/PreviewMarkerDrawOrder.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"
#include "core/scene/PreviewSkinSelectors.h"
#include "core/scene/PreviewTrackShared.h"

#include <QHash>

#include <algorithm>

namespace {

using MarkerMuriStateLookup = QHash<QString, const MarkerMuriState*>;

QString markerAnalysisBaseKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(marker.type)
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.endLane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.slideTrackKey);
}

QString markerAnalysisBaseKeyFromStateKey(const QString& key)
{
    int separatorCount = 0;
    for (int index = 0; index < key.size(); ++index) {
        if (key.at(index) != QLatin1Char('|')) {
            continue;
        }
        separatorCount += 1;
        if (separatorCount == 7) {
            return key.left(index);
        }
    }
    return key;
}

const MarkerMuriState* findMarkerMuriState(
    const MarkerMuriStateLookup& exactStateByKey,
    const MarkerMuriStateLookup& slideFallbackStateByBaseKey,
    const TimelineNoteMarker& marker
)
{
    const QString exactKey = makeMarkerAnalysisKey(marker);
    if (const auto it = exactStateByKey.constFind(exactKey); it != exactStateByKey.constEnd()) {
        return it.value();
    }

    if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
        const QString baseKey = markerAnalysisBaseKey(marker);
        if (const auto it = slideFallbackStateByBaseKey.constFind(baseKey); it != slideFallbackStateByBaseKey.constEnd()) {
            return it.value();
        }
    }
    return nullptr;
}

}  // namespace

namespace miacode::preview::scene {

PreviewTrackLayerState buildPreviewTrackLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    PreviewTrackLayerState layerState;
    layerState.sprites.reserve(markers.size() * 32);

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

    // Preview Mode: Erase by Area selects the arcade autoplay clear; otherwise the
    // build's default trim mode applies.
    const PreviewSlideTrackTrimMode trimMode =
        state.muriRenderOptions.renderMode == RenderMode::EraseByArea
        ? PreviewSlideTrackTrimMode::VanillaAutoplay
        : kPreviewSlideTrackTrimMode;

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    // Per-marker slide-track timing: hsMultiplier scales tapFlowSpeed.
    const auto markerTrackTiming = [&state](double hsMultiplier) {
        return previewSlideTrackTimingForEffectiveFlowSpeed(
            static_cast<qreal>(state.render.tapFlowSpeed * hsMultiplier));
    };
    MarkerMuriStateLookup exactStateByKey;
    MarkerMuriStateLookup slideFallbackStateByBaseKey;
    exactStateByKey.reserve(state.muriAnalysisReport.markerStates.size());
    slideFallbackStateByBaseKey.reserve(state.muriAnalysisReport.markerStates.size());
    for (auto stateIt = state.muriAnalysisReport.markerStates.constBegin();
         stateIt != state.muriAnalysisReport.markerStates.constEnd();
         ++stateIt) {
        exactStateByKey.insert(stateIt.key(), &stateIt.value());
        if (stateIt.value().markerType != QLatin1String("slide")
            && stateIt.value().markerType != QLatin1String("wifi")) {
            continue;
        }
        const QString baseKey = markerAnalysisBaseKeyFromStateKey(stateIt.key());
        if (!slideFallbackStateByBaseKey.contains(baseKey)) {
            slideFallbackStateByBaseKey.insert(baseKey, &stateIt.value());
        }
    }

    const auto appendSprite = [&layerState, &playfieldRect, canvasScale](
                                  const QImage* image,
                                  const QPointF& logicalPoint,
                                  qreal rotationDegrees,
                                  qreal opacity,
                                  PreviewAnimatedSpriteEffect effect,
                                  bool cacheable
                              ) {
        if (image == nullptr || image->isNull() || opacity <= 0.0) {
            return;
        }
        PreviewSpriteDescriptor sprite;
        sprite.image = image;
        sprite.center = mapLogicalPointToRect(
            QPointF(kLogicalCanvasCenter + logicalPoint.x(), kLogicalCanvasCenter + logicalPoint.y()),
            playfieldRect
        );
        sprite.width = qMax<qreal>(1.0, qRound(image->width() * canvasScale * kSlideTrackScale));
        sprite.height = qMax<qreal>(1.0, qRound(image->height() * canvasScale * kSlideTrackScale));
        sprite.rotationDegrees = rotationDegrees;
        sprite.opacity = opacity;
        sprite.effect = effect;
        sprite.cacheable = cacheable;
        layerState.sprites.append(sprite);
    };

    // MajdataPlay's slide bar only switches to the break (flash) material once its fade-in
    // completes at FadeInCompletedTiming (== Timing - fullBright); while fading in it uses the
    // plain material. Mirror that: gate BreakAnimate on the full-bright point so the track does
    // not shimmer during fade-in. (The moving star keeps its own break flash in another layer.)
    const auto breakFlashEffect = [&](const TimelineNoteMarker& m) -> PreviewAnimatedSpriteEffect {
        if (!m.trackBreak) {
            return PreviewAnimatedSpriteEffect::None;
        }
        const qreal brightStartSecond =
            static_cast<qreal>(m.second) - markerTrackTiming(m.hsMultiplier).fullBrightLeadInSeconds;
        return state.playheadSeconds >= brightStartSecond
            ? PreviewAnimatedSpriteEffect::BreakAnimate
            : PreviewAnimatedSpriteEffect::None;
    };

    const auto appendSlideArea = [&](const TimelineNoteMarker& marker,
                                     int segmentIndex,
                                     int areaIndex,
                                     int localCut,
                                     const QImage* image,
                                     qreal opacity,
                                     PreviewAnimatedSpriteEffect effect,
                                     bool trimFromTail,
                                     bool cacheable) {
        if (image == nullptr || image->isNull() || opacity <= 0.0) {
            return;
        }
        if (segmentIndex < 0 || segmentIndex >= marker.slideTrackAreaPoints.size()) {
            return;
        }

        const QVector<QVector<QPointF>>& segmentAreas = marker.slideTrackAreaPoints[segmentIndex];
        if (areaIndex < 0 || areaIndex >= segmentAreas.size()) {
            return;
        }
        const QVector<QPointF>& points = segmentAreas[areaIndex];
        if (points.isEmpty()) {
            return;
        }
        const QVector<double>& rotations = marker.slideTrackAreaRotations.value(segmentIndex).value(areaIndex);
        const int clampedCut = qBound(0, localCut, points.size());
        if (clampedCut >= points.size()) {
            return;
        }

        const int startPointIndex = trimFromTail ? 0 : clampedCut;
        const int endPointIndex = trimFromTail ? points.size() - clampedCut : points.size();
        for (int pointIndex = startPointIndex; pointIndex < endPointIndex; ++pointIndex) {
            appendSprite(image, points[pointIndex], -rotations.value(pointIndex), opacity, effect, cacheable);
        }
    };

    const auto appendWifiArea = [&](const TimelineNoteMarker& marker,
                                    int areaIndex,
                                    int localCut,
                                    qreal opacity) {
        if (areaIndex < 0 || areaIndex >= marker.wifiTrackAreaPoints.size() || opacity <= 0.0) {
            return;
        }
        const QVector<QPointF>& points = marker.wifiTrackAreaPoints[areaIndex];
        if (points.isEmpty()) {
            return;
        }
        const QVector<double>& rotations = marker.wifiTrackAreaRotations.value(areaIndex);
        const QVector<int>& imageIndices = marker.wifiTrackAreaImageIndices.value(areaIndex);
        const int clampedCut = qBound(0, localCut, points.size());
        if (clampedCut >= points.size()) {
            return;
        }

        const PreviewAnimatedSpriteEffect effect = breakFlashEffect(marker);
        for (int pointIndex = clampedCut; pointIndex < points.size(); ++pointIndex) {
            const int imageIndex = imageIndices.value(pointIndex, pointIndex);
            const QImage* baseImage = selectWifiTrackImage(state.skin, marker, imageIndex, 0);
            if (baseImage == nullptr || baseImage->isNull()) {
                continue;
            }

            appendSprite(baseImage, points[pointIndex], -rotations.value(pointIndex), opacity, effect, true);
        }
    };

    const int orderedCount = usesPreparedDrawOrder ? orderedPreparedIndices.size() : orderedMarkerViewIndices.size();
    for (int orderIndex = 0; orderIndex < orderedCount; ++orderIndex) {
        const int preparedIndex = usesPreparedDrawOrder ? orderedPreparedIndices.at(orderIndex) : -1;
        const int markerViewIndex = usesPreparedDrawOrder ? -1 : orderedMarkerViewIndices.at(orderIndex);
        const TimelineNoteMarker& marker = usesPreparedDrawOrder
            ? markers.markerForPreparedIndex(preparedIndex)
            : markers.markerAt(markerViewIndex);
        if (marker.type == QLatin1String("slide")) {
            if (!state.muriRenderOptions.showSlideTracks) {
                continue;
            }
            const PreviewSlideTrackTiming trackTiming = markerTrackTiming(marker.hsMultiplier);
            if (marker.availableSecond < 0.0
                || marker.slideTrackAreaPoints.isEmpty()
                || state.playheadSeconds < marker.second - trackTiming.appearLeadInSeconds
                || (marker.endSecond > marker.slideTraceSecond && state.playheadSeconds >= marker.endSecond)) {
                continue;
            }

            const QImage* baseImage = selectSlideTrackImage(state.skin, marker);
            if (baseImage == nullptr || baseImage->isNull()) {
                continue;
            }
            const PreviewAnimatedSpriteEffect effect = breakFlashEffect(marker);
            const QImage* renderImage = baseImage;
            const bool cacheable = true;

            if (state.muriRenderOptions.renderMode == RenderMode::MaimuriDxStyle) {
                const MarkerMuriState* muriState = findMarkerMuriState(exactStateByKey, slideFallbackStateByBaseKey, marker);
                if (muriState != nullptr && !muriState->slideSegments.isEmpty()) {
                    qreal opacity = 1.0;
                    if (state.playheadSeconds < marker.slideTraceSecond) {
                        opacity = sampleSlideTrackPreTraceOpacity(
                            static_cast<qreal>(marker.second),
                            static_cast<qreal>(state.playheadSeconds),
                            trackTiming.appearLeadInSeconds,
                            trackTiming.fullBrightLeadInSeconds
                        );
                        if (opacity < 0.0) {
                            continue;
                        }
                    }

                    const auto appendDxSlideRuntimeAreas = [&](qreal areaOpacity) {
                        if (areaOpacity <= 0.0) {
                            return;
                        }

                        for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex >= 0; --segmentIndex) {
                            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                            const MuriSegmentState& segmentState = muriState->slideSegments.value(segmentIndex);
                            for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                                const QVector<MuriCheckpointState>& checkpoints = segmentState.areaCheckpoints.value(areaIndex);
                                if (checkpoints.isEmpty()) {
                                    if (segmentState.completedSecond >= 0.0
                                        && state.playheadSeconds >= segmentState.completedSecond - 1e-6) {
                                        continue;
                                    }
                                    appendSlideArea(
                                        marker,
                                        segmentIndex,
                                        areaIndex,
                                        0,
                                        renderImage,
                                        areaOpacity,
                                        effect,
                                        false,
                                        cacheable
                                    );
                                    continue;
                                }

                                const int passed = passedCheckpointCount(checkpoints, state.playheadSeconds);
                                if (passed >= checkpoints.size()) {
                                    continue;
                                }
                                const int localCut = slideAreaCutForPassedCount(
                                    marker.slideTrackAreaCutIndices.value(segmentIndex).value(areaIndex),
                                    passed,
                                    areas[areaIndex].size()
                                );
                                appendSlideArea(
                                    marker,
                                    segmentIndex,
                                    areaIndex,
                                    localCut,
                                    renderImage,
                                    areaOpacity,
                                    effect,
                                    false,
                                    cacheable
                                );
                            }
                        }
                    };

                    appendDxSlideRuntimeAreas(opacity);

                    const qreal flashOpacity = muriFlashOpacity(muriState->flashSecond, state.playheadSeconds);
                    if (flashOpacity > 0.0) {
                        appendDxSlideRuntimeAreas(flashOpacity);
                    }
                    continue;
                }
            }

            int startSegment = 0;
            int startAreaIndex = 0;
            int partialTrimCount = 0;
            int removedArrowCount = 0;
            qreal startProportion = 0.0;
            qreal opacity = 1.0;
            if (state.playheadSeconds < marker.slideTraceSecond) {
                opacity = sampleSlideTrackPreTraceOpacity(
                    static_cast<qreal>(marker.second),
                    static_cast<qreal>(state.playheadSeconds),
                    trackTiming.appearLeadInSeconds,
                    trackTiming.fullBrightLeadInSeconds
                );
                if (opacity < 0.0) {
                    continue;
                }
            } else if (trimMode == PreviewSlideTrackTrimMode::AreaImmediate
                       && !marker.slideSegmentShootSeconds.isEmpty()
                       && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()
                       && marker.slideTrackAreaPoints.size() == marker.slideSegmentDurations.size()) {
                for (int i = marker.slideSegmentShootSeconds.size() - 1; i >= 0; --i) {
                    if (state.playheadSeconds >= marker.slideSegmentShootSeconds[i]) {
                        startSegment = i;
                        break;
                    }
                }
                startSegment = qBound(0, startSegment, marker.slideTrackAreaPoints.size() - 1);
                const qreal duration = qMax<qreal>(0.001, marker.slideSegmentDurations.value(startSegment));
                startProportion = qBound<qreal>(
                    0.0,
                    static_cast<qreal>((state.playheadSeconds - marker.slideSegmentShootSeconds[startSegment]) / duration),
                    1.0
                );
                const int areaCount = marker.slideTrackAreaPoints[startSegment].size();
                startAreaIndex = currentAreaIndexForProportion(
                    marker.slideTrackAreaThresholds.value(startSegment),
                    startProportion,
                    areaCount
                );
                if (startAreaIndex >= 0 && startAreaIndex < areaCount) {
                    partialTrimCount = slideAreaTrimCountForProportion(
                        marker.slideTrackAreaPoints[startSegment][startAreaIndex],
                        marker.slideTrackAreaThresholds.value(startSegment),
                        marker.slideTrackAreaCheckpoints.value(startSegment).value(startAreaIndex),
                        marker.slideTrackAreaCutIndices.value(startSegment).value(startAreaIndex),
                        startAreaIndex,
                        startProportion
                    );
                }
            } else if (trimMode == PreviewSlideTrackTrimMode::VanillaAutoplay) {
                removedArrowCount = previewSlideVanillaHiddenArrowCount(
                    buildPreviewSlideAutoplayAreas(marker),
                    previewSlideStarProgress(marker, state.playheadSeconds)
                );
            } else {
                const qreal totalDuration = qMax<qreal>(0.001, static_cast<qreal>(marker.endSecond - marker.slideTraceSecond));
                const qreal totalProportion = qBound<qreal>(
                    0.0,
                    static_cast<qreal>((state.playheadSeconds - marker.slideTraceSecond) / totalDuration),
                    1.0
                );
                const int totalArrowCount = totalSlideTrackArrowCount(marker.slideTrackAreaPoints);
                removedArrowCount = qBound(0, qFloor(totalProportion * totalArrowCount), totalArrowCount);
            }

            if (trimMode == PreviewSlideTrackTrimMode::AreaImmediate) {
                for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex > startSegment; --segmentIndex) {
                    const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                    for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                        appendSlideArea(
                            marker,
                            segmentIndex,
                            areaIndex,
                            0,
                            renderImage,
                            opacity,
                            effect,
                            false,
                            cacheable
                        );
                    }
                }
                if (startSegment >= 0 && startSegment < marker.slideTrackAreaPoints.size()) {
                    const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[startSegment];
                    const int clampedStartArea = qBound(0, startAreaIndex, areas.size());
                    if (clampedStartArea >= 0 && clampedStartArea < areas.size()) {
                        partialTrimCount = slideAreaTrimCountForProportion(
                            areas[clampedStartArea],
                            marker.slideTrackAreaThresholds.value(startSegment),
                            marker.slideTrackAreaCheckpoints.value(startSegment).value(clampedStartArea),
                            marker.slideTrackAreaCutIndices.value(startSegment).value(clampedStartArea),
                            clampedStartArea,
                            startProportion
                        );
                    }
                    for (int areaIndex = areas.size() - 1; areaIndex >= clampedStartArea; --areaIndex) {
                        const int localCut = areaIndex == clampedStartArea ? partialTrimCount : 0;
                        appendSlideArea(
                            marker,
                            startSegment,
                            areaIndex,
                            localCut,
                            renderImage,
                            opacity,
                            effect,
                            false,
                            cacheable
                        );
                    }
                }
            } else {
                int trimSegment = marker.slideTrackAreaPoints.size();
                int trimArea = 0;
                int trimLocalCut = 0;
                int remainingToRemove = removedArrowCount;
                for (int segmentIndex = 0; segmentIndex < marker.slideTrackAreaPoints.size(); ++segmentIndex) {
                    const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                    int segmentArrowCount = 0;
                    for (const QVector<QPointF>& areaPoints : areas) {
                        segmentArrowCount += areaPoints.size();
                    }
                    if (remainingToRemove >= segmentArrowCount) {
                        remainingToRemove -= segmentArrowCount;
                        continue;
                    }
                    trimSegment = segmentIndex;
                    for (int areaIndex = 0; areaIndex < areas.size(); ++areaIndex) {
                        const int areaArrowCount = areas[areaIndex].size();
                        if (remainingToRemove < areaArrowCount) {
                            trimArea = areaIndex;
                            trimLocalCut = qBound(0, remainingToRemove, areaArrowCount);
                            remainingToRemove = 0;
                            break;
                        }
                        remainingToRemove -= areaArrowCount;
                    }
                    break;
                }

                for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex > trimSegment; --segmentIndex) {
                    const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                    for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                        appendSlideArea(
                            marker,
                            segmentIndex,
                            areaIndex,
                            0,
                            renderImage,
                            opacity,
                            effect,
                            false,
                            cacheable
                        );
                    }
                }
                if (trimSegment >= 0 && trimSegment < marker.slideTrackAreaPoints.size()) {
                    const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[trimSegment];
                    for (int areaIndex = areas.size() - 1; areaIndex >= trimArea; --areaIndex) {
                        const int localCut = areaIndex == trimArea ? trimLocalCut : 0;
                        appendSlideArea(
                            marker,
                            trimSegment,
                            areaIndex,
                            localCut,
                            renderImage,
                            opacity,
                            effect,
                            areaIndex == trimArea,
                            cacheable
                        );
                    }
                }
            }
            continue;
        }

        if (marker.type != QLatin1String("wifi")) {
            continue;
        }
        if (!state.muriRenderOptions.showSlideTracks) {
            continue;
        }
        const PreviewSlideTrackTiming trackTiming = markerTrackTiming(marker.hsMultiplier);
        if (marker.availableSecond < 0.0
            || marker.wifiTrackAreaPoints.isEmpty()
            || state.playheadSeconds < marker.second - trackTiming.appearLeadInSeconds
            || (marker.endSecond > marker.slideTraceSecond && state.playheadSeconds >= marker.endSecond)) {
            continue;
        }

        qreal opacity = 1.0;
        int startAreaIndex = 0;
        qreal startProportion = 0.0;
        int removedArrowCount = 0;
                const MarkerMuriState* muriState = findMarkerMuriState(
                    exactStateByKey,
                    slideFallbackStateByBaseKey,
                    marker
                );
        if (state.playheadSeconds < marker.slideTraceSecond) {
            opacity = sampleSlideTrackPreTraceOpacity(
                static_cast<qreal>(marker.second),
                static_cast<qreal>(state.playheadSeconds),
                trackTiming.appearLeadInSeconds,
                trackTiming.fullBrightLeadInSeconds
            );
            if (opacity < 0.0) {
                continue;
            }
        } else {
            const qreal totalDuration = qMax<qreal>(0.001, static_cast<qreal>(marker.endSecond - marker.slideTraceSecond));
            startProportion = qBound<qreal>(
                0.0,
                static_cast<qreal>((state.playheadSeconds - marker.slideTraceSecond) / totalDuration),
                1.0
            );
            if (trimMode == PreviewSlideTrackTrimMode::AreaImmediate) {
                startAreaIndex = currentAreaIndexForProportion(
                    marker.wifiTrackAreaThresholds,
                    startProportion,
                    marker.wifiTrackAreaPoints.size()
                );
            } else if (trimMode == PreviewSlideTrackTrimMode::VanillaAutoplay) {
                // The arcade's wifi clear only reaches one or two of the rows before
                // the note ends; that is the behaviour this mode exists to show.
                removedArrowCount = previewWifiVanillaHiddenRowCount(
                    marker.wifiCriticalProportion,
                    totalWifiTrackArrowCount(marker.wifiTrackAreaPoints),
                    startProportion
                );
            } else {
                const int totalArrowCount = totalWifiTrackArrowCount(marker.wifiTrackAreaPoints);
                removedArrowCount = qBound(0, qFloor(startProportion * totalArrowCount), totalArrowCount);
            }
        }

        if (state.muriRenderOptions.renderMode == RenderMode::MaimuriDxStyle
            && muriState != nullptr
            && (!muriState->wifiLaneProgressSeconds.isEmpty() || !muriState->wifiLaneAreas.isEmpty())) {
            const int totalAreaCount = marker.wifiTrackAreaPoints.size();
            int startRuntimeAreaIndex = totalAreaCount;
            const int laneCount = qMax(muriState->wifiLaneProgressSeconds.size(), muriState->wifiLaneAreas.size());
            for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex) {
                startRuntimeAreaIndex = qMin(
                    startRuntimeAreaIndex,
                    qBound(
                        0,
                        currentWifiLaneAreaIndexAt(
                            muriState->wifiLaneProgressSeconds.value(laneIndex),
                            muriState->wifiLaneAreas.value(laneIndex),
                            state.playheadSeconds
                        ),
                        totalAreaCount
                    )
                );
            }
            const bool padCPassed = !state.muriRenderOptions.wifiNeedC
                || (muriState->wifiPadCSecond >= 0.0 && state.playheadSeconds >= muriState->wifiPadCSecond - 1e-6);
            if (padCPassed
                && muriState->wifiCompletedSecond >= 0.0
                && state.playheadSeconds >= muriState->wifiCompletedSecond - 1e-6) {
                startRuntimeAreaIndex = totalAreaCount;
            }
            if (!padCPassed && totalAreaCount > 0) {
                startRuntimeAreaIndex = qMin(startRuntimeAreaIndex, totalAreaCount - 1);
            }
            for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= startRuntimeAreaIndex; --areaIndex) {
                appendWifiArea(marker, areaIndex, 0, opacity);
            }
            continue;
        }

        const int clampedStartArea = qBound(0, startAreaIndex, marker.wifiTrackAreaPoints.size());
        int partialTrimCount = 0;
        if (trimMode == PreviewSlideTrackTrimMode::AreaImmediate
            && clampedStartArea >= 0
            && clampedStartArea < marker.wifiTrackAreaPoints.size()) {
            const QVector<double>& areaCheckpoints = marker.wifiTrackAreaCheckpoints.value(clampedStartArea);
            if (!areaCheckpoints.isEmpty()) {
                int passed = 0;
                for (double checkpoint : areaCheckpoints) {
                    if (startProportion >= checkpoint) {
                        ++passed;
                    } else {
                        break;
                    }
                }
                partialTrimCount = qBound(
                    0,
                    qFloor(
                        static_cast<qreal>(passed) * marker.wifiTrackAreaPoints[clampedStartArea].size()
                        / areaCheckpoints.size()
                    ),
                    marker.wifiTrackAreaPoints[clampedStartArea].size()
                );
            }
        }

        if (trimMode == PreviewSlideTrackTrimMode::AreaImmediate) {
            for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= clampedStartArea; --areaIndex) {
                const int localCut = areaIndex == clampedStartArea ? partialTrimCount : 0;
                appendWifiArea(marker, areaIndex, localCut, opacity);
            }
        } else {
            int trimArea = marker.wifiTrackAreaPoints.size();
            int trimLocalCut = 0;
            int remainingToRemove = removedArrowCount;
            for (int areaIndex = 0; areaIndex < marker.wifiTrackAreaPoints.size(); ++areaIndex) {
                const int areaArrowCount = marker.wifiTrackAreaPoints[areaIndex].size();
                if (remainingToRemove < areaArrowCount) {
                    trimArea = areaIndex;
                    trimLocalCut = qBound(0, remainingToRemove, areaArrowCount);
                    remainingToRemove = 0;
                    break;
                }
                remainingToRemove -= areaArrowCount;
            }
            for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= trimArea; --areaIndex) {
                const int localCut = areaIndex == trimArea ? trimLocalCut : 0;
                appendWifiArea(marker, areaIndex, localCut, opacity);
            }
        }
    }

    return layerState;
}

}  // namespace miacode::preview::scene
