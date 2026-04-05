#include "preview/scene/PreviewChartReviewLayerState.h"

#include "preview/scene/PreviewOpacityCurves.h"

#include <QSet>

namespace {

bool hasAnyChartReviewJudgeAssets(const miacode::preview::scene::PreviewJudgeOverlayAssets& assets)
{
    return !assets.reviewJudgeSimpleNormalImage.isNull()
        || !assets.reviewJudgeSimpleBreakImage.isNull()
        || !assets.reviewJudgeStraightLeftImage.isNull()
        || !assets.reviewJudgeStraightRightImage.isNull()
        || !assets.reviewJudgeCircleLeftImage.isNull()
        || !assets.reviewJudgeCircleRightImage.isNull()
        || !assets.reviewJudgeWifiUpImage.isNull()
        || !assets.reviewJudgeWifiDownImage.isNull();
}

}  // namespace

namespace miacode::preview::scene {

PreviewChartReviewPreparedEvents buildPreviewChartReviewPreparedEvents(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool showSlideJudgeOverlay,
    bool showSimpleJudgeOverlay
)
{
    PreviewChartReviewPreparedEvents events;
    if ((!showSlideJudgeOverlay && !showSimpleJudgeOverlay) || noteMarkers.isEmpty()) {
        return events;
    }

    events.reserve(noteMarkers.size() * 2);

    QSet<QString> emittedHeadEvents;
    emittedHeadEvents.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (marker.type == QLatin1String("tap")) {
            if (!showSimpleJudgeOverlay) {
                continue;
            }
            PreviewChartReviewPreparedEvent event;
            event.kind = marker.isBreak
                ? PreviewChartReviewPreparedKind::SimpleBreak
                : PreviewChartReviewPreparedKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.second);
            if (buildJudgeOverlaySimplePlacement(lanePadToken(marker.lane), &event.placement)) {
                events.append(event);
            }
            continue;
        }
        if (marker.type == QLatin1String("hold")) {
            if (!showSimpleJudgeOverlay || marker.endSecond < 0.0) {
                continue;
            }
            PreviewChartReviewPreparedEvent event;
            event.kind = marker.isBreak
                ? PreviewChartReviewPreparedKind::SimpleBreak
                : PreviewChartReviewPreparedKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.endSecond);
            if (buildJudgeOverlaySimplePlacement(
                    lanePadToken((marker.endLane >= 1 && marker.endLane <= 8) ? marker.endLane : marker.lane),
                    &event.placement)) {
                events.append(event);
            }
            continue;
        }
        if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
            continue;
        }

        if (showSimpleJudgeOverlay && marker.hasHeadStar) {
            const QString helperKey = slideHeadEventKey(marker);
            if (!emittedHeadEvents.contains(helperKey)) {
                emittedHeadEvents.insert(helperKey);
                PreviewChartReviewPreparedEvent headEvent;
                headEvent.kind = marker.headBreak
                    ? PreviewChartReviewPreparedKind::SimpleBreak
                    : PreviewChartReviewPreparedKind::SimpleNormal;
                headEvent.second = static_cast<qreal>(marker.second);
                if (buildJudgeOverlaySimplePlacement(lanePadToken(marker.lane), &headEvent.placement)) {
                    events.append(headEvent);
                }
            }
        }
        if (!showSlideJudgeOverlay) {
            continue;
        }

        PreviewChartReviewPreparedEvent event;
        event.second = chartReviewSlideJudgeSecond(marker);
        const int lane = qBound(1, marker.endLane, 8);
        if (marker.type == QLatin1String("wifi")) {
            event.kind =
                (lane == 1 || lane == 2 || lane == 7 || lane == 8)
                ? PreviewChartReviewPreparedKind::WifiUp
                : PreviewChartReviewPreparedKind::WifiDown;
            event.placement = buildJudgeOverlayWifiPlacement(lane, event.kind == PreviewChartReviewPreparedKind::WifiUp);
            events.append(event);
            continue;
        }

        const QString segmentKey = !marker.slideSegmentKeys.isEmpty()
            ? marker.slideSegmentKeys.constLast()
            : marker.slideTrackKey;
        bool appendEvent = true;
        if (slideKeyUsesCcwJudgeSprite(segmentKey)) {
            event.kind = PreviewChartReviewPreparedKind::CircleLeft;
            event.placement = buildJudgeOverlayCircleCcwPlacement(lane);
        } else if (slideKeyUsesCwJudgeSprite(segmentKey)) {
            event.kind = PreviewChartReviewPreparedKind::CircleRight;
            event.placement = buildJudgeOverlayCircleCwPlacement(lane);
        } else {
            bool useRightImage = false;
            appendEvent = buildJudgeOverlayStraightPlacement(marker, false, &event.placement, &useRightImage);
            event.kind = useRightImage
                ? PreviewChartReviewPreparedKind::StraightRight
                : PreviewChartReviewPreparedKind::StraightLeft;
        }

        if (appendEvent) {
            events.append(event);
        }
    }

    return events;
}

PreviewSpriteDescriptors buildPreviewChartReviewLayerSprites(
    const PreviewFrameState& state,
    const QRectF& playfieldRect,
    const PreviewChartReviewPreparedEvents* preparedEvents
)
{
    PreviewSpriteDescriptors sprites;
    const bool showSlideJudgeOverlay = state.muriRenderOptions.showChartReviewSlideJudgeOverlay;
    const bool showSimpleJudgeOverlay = state.muriRenderOptions.showChartReviewSimpleJudgeOverlay;
    if (state.muriRenderOptions.renderMode != RenderMode::Native
        || (!showSlideJudgeOverlay && !showSimpleJudgeOverlay)
        || !hasAnyChartReviewJudgeAssets(state.judgeOverlay)) {
        return sprites;
    }

    const PreviewChartReviewPreparedEvents ownedPreparedEvents =
        preparedEvents != nullptr
        ? PreviewChartReviewPreparedEvents()
        : buildPreviewChartReviewPreparedEvents(state.noteMarkers, showSlideJudgeOverlay, showSimpleJudgeOverlay);
    const PreviewChartReviewPreparedEvents& events =
        preparedEvents != nullptr ? *preparedEvents : ownedPreparedEvents;

    sprites.reserve(events.size());
    for (const PreviewChartReviewPreparedEvent& event : events) {
        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - event.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kMaimuriDxJudgeLifetimeSeconds) {
            continue;
        }

        const QImage* image = nullptr;
        QRectF sourceRect;
        qreal alpha = 0.0;

        switch (event.kind) {
        case PreviewChartReviewPreparedKind::SimpleNormal:
        case PreviewChartReviewPreparedKind::SimpleBreak: {
            const bool isBreak = event.kind == PreviewChartReviewPreparedKind::SimpleBreak;
            image = isBreak
                ? &state.judgeOverlay.reviewJudgeSimpleBreakImage
                : &state.judgeOverlay.reviewJudgeSimpleNormalImage;
            sourceRect = isBreak
                ? state.judgeOverlay.reviewJudgeSimpleBreakSourceRect
                : state.judgeOverlay.reviewJudgeSimpleNormalSourceRect;
            if (image->isNull()) {
                image = isBreak
                    ? &state.judgeOverlay.reviewJudgeSimpleNormalImage
                    : &state.judgeOverlay.reviewJudgeSimpleBreakImage;
                sourceRect = isBreak
                    ? state.judgeOverlay.reviewJudgeSimpleNormalSourceRect
                    : state.judgeOverlay.reviewJudgeSimpleBreakSourceRect;
            }
            alpha = maimuriDxSimpleJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds,
                kMaimuriDxSimpleJudgeFadeInSeconds
            );
            break;
        }
        case PreviewChartReviewPreparedKind::StraightLeft:
            image = &state.judgeOverlay.reviewJudgeStraightLeftImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::StraightRight:
            image = &state.judgeOverlay.reviewJudgeStraightRightImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::CircleLeft:
            image = &state.judgeOverlay.reviewJudgeCircleLeftImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::CircleRight:
            image = &state.judgeOverlay.reviewJudgeCircleRightImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::WifiUp:
            image = &state.judgeOverlay.reviewJudgeWifiUpImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::WifiDown:
            image = &state.judgeOverlay.reviewJudgeWifiDownImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        }

        const PreviewSpriteDescriptor sprite =
            buildJudgeOverlaySpriteDescriptor(image, sourceRect, event.placement, alpha, playfieldRect);
        if (sprite.image != nullptr) {
            sprites.append(sprite);
        }
    }

    return sprites;
}

}  // namespace miacode::preview::scene
