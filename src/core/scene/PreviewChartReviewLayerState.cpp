#include "core/scene/PreviewChartReviewLayerState.h"

#include "core/scene/PreviewOpacityCurves.h"

#include <QSet>

namespace {

bool hasAnyChartReviewJudgeAssets(const miacode::preview::scene::PreviewJudgeOverlayAssets& assets)
{
    return !assets.simpleText.normal.image.isNull()
        || !assets.simpleText.breakText.image.isNull()
        || !assets.neutral.straightLeftImage.isNull()
        || !assets.neutral.straightRightImage.isNull()
        || !assets.neutral.circleLeftImage.isNull()
        || !assets.neutral.circleRightImage.isNull()
        || !assets.neutral.wifiUpImage.isNull()
        || !assets.neutral.wifiDownImage.isNull();
}

}  // namespace

namespace miacode::preview::scene {

PreviewChartReviewPreparedEvents buildPreviewChartReviewPreparedEvents(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool showSlideJudgeOverlay,
    bool showTapJudgeOverlay,
    bool showBreakJudgeOverlay,
    bool showTouchJudgeOverlay,
    PreviewTapJudgeTextDistance tapJudgeTextDistance
)
{
    PreviewChartReviewPreparedEvents events;
    if ((!showSlideJudgeOverlay && !showTapJudgeOverlay && !showBreakJudgeOverlay && !showTouchJudgeOverlay)
        || noteMarkers.isEmpty()) {
        return events;
    }

    events.reserve(noteMarkers.size() * 2);

    QSet<QString> emittedHeadEvents;
    emittedHeadEvents.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (marker.type == QLatin1String("tap")) {
            if ((marker.isBreak && !showBreakJudgeOverlay)
                || (!marker.isBreak && !showTapJudgeOverlay)) {
                continue;
            }
            PreviewChartReviewPreparedEvent event;
            event.kind = marker.isBreak
                ? PreviewChartReviewPreparedKind::SimpleBreak
                : PreviewChartReviewPreparedKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.second);
            if (buildJudgeOverlaySimplePlacement(lanePadToken(marker.lane), tapJudgeTextDistance, &event.placement)) {
                events.append(event);
            }
            continue;
        }
        if (marker.type == QLatin1String("hold")) {
            if (marker.endSecond < 0.0
                || (marker.isBreak && !showBreakJudgeOverlay)
                || (!marker.isBreak && !showTapJudgeOverlay)) {
                continue;
            }
            PreviewChartReviewPreparedEvent event;
            event.kind = marker.isBreak
                ? PreviewChartReviewPreparedKind::SimpleBreak
                : PreviewChartReviewPreparedKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.endSecond);
            if (buildJudgeOverlaySimplePlacement(
                    lanePadToken((marker.endLane >= 1 && marker.endLane <= 8) ? marker.endLane : marker.lane),
                    tapJudgeTextDistance,
                    &event.placement)) {
                events.append(event);
            }
            continue;
        }
        if (marker.type == QLatin1String("touch")) {
            if (marker.touchPad.isEmpty()
                || (marker.isBreak && !showBreakJudgeOverlay)
                || (!marker.isBreak && !showTouchJudgeOverlay)) {
                continue;
            }
            PreviewChartReviewPreparedEvent event;
            event.kind = marker.isBreak
                ? PreviewChartReviewPreparedKind::SimpleBreak
                : PreviewChartReviewPreparedKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.second);
            if (buildJudgeOverlaySimplePlacement(marker.touchPad, tapJudgeTextDistance, &event.placement)) {
                events.append(event);
            }
            continue;
        }
        if (marker.type == QLatin1String("touch_hold")) {
            if (marker.endSecond < 0.0
                || marker.touchPad.isEmpty()
                || (marker.isBreak && !showBreakJudgeOverlay)
                || (!marker.isBreak && !showTouchJudgeOverlay)) {
                continue;
            }
            PreviewChartReviewPreparedEvent event;
            event.kind = marker.isBreak
                ? PreviewChartReviewPreparedKind::SimpleBreak
                : PreviewChartReviewPreparedKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.endSecond);
            if (buildJudgeOverlaySimplePlacement(marker.touchPad, tapJudgeTextDistance, &event.placement)) {
                events.append(event);
            }
            continue;
        }
        if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
            continue;
        }

        if (marker.hasHeadStar) {
            const bool showHeadJudgeOverlay = marker.headBreak ? showBreakJudgeOverlay : showTapJudgeOverlay;
            const QString helperKey = slideHeadEventKey(marker);
            if (showHeadJudgeOverlay && !emittedHeadEvents.contains(helperKey)) {
                emittedHeadEvents.insert(helperKey);
                PreviewChartReviewPreparedEvent headEvent;
                headEvent.kind = marker.headBreak
                    ? PreviewChartReviewPreparedKind::SimpleBreak
                    : PreviewChartReviewPreparedKind::SimpleNormal;
                headEvent.second = static_cast<qreal>(marker.second);
                if (buildJudgeOverlaySimplePlacement(lanePadToken(marker.lane), tapJudgeTextDistance, &headEvent.placement)) {
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
            appendEvent = buildJudgeOverlayStraightPlacement(marker, &event.placement, &useRightImage);
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
    const PreviewChartReviewPreparedEvents* preparedEvents,
    SlideJudgeRenderGroup group
)
{
    PreviewSpriteDescriptors sprites;
    const bool showSlideJudgeOverlay = state.muriRenderOptions.showChartReviewSlideJudgeOverlay;
    const bool showTapJudgeOverlay = state.muriRenderOptions.showChartReviewTapJudgeOverlay;
    const bool showBreakJudgeOverlay = state.muriRenderOptions.showChartReviewBreakJudgeOverlay;
    const bool showTouchJudgeOverlay = state.muriRenderOptions.showChartReviewTouchJudgeOverlay;
    if (state.muriRenderOptions.renderMode == RenderMode::MaimuriDxStyle
        || (!showSlideJudgeOverlay && !showTapJudgeOverlay && !showBreakJudgeOverlay && !showTouchJudgeOverlay)
        || !hasAnyChartReviewJudgeAssets(state.judgeOverlay)) {
        return sprites;
    }

    const PreviewChartReviewPreparedEvents ownedPreparedEvents =
        preparedEvents != nullptr
        ? PreviewChartReviewPreparedEvents()
        : buildPreviewChartReviewPreparedEvents(
              state.noteMarkers,
              showSlideJudgeOverlay,
              showTapJudgeOverlay,
              showBreakJudgeOverlay,
              showTouchJudgeOverlay,
              state.render.tapJudgeTextDistance);
    const PreviewChartReviewPreparedEvents& events =
        preparedEvents != nullptr ? *preparedEvents : ownedPreparedEvents;

    sprites.reserve(events.size());
    for (const PreviewChartReviewPreparedEvent& event : events) {
        const bool isJudgeText =
            event.kind == PreviewChartReviewPreparedKind::SimpleNormal
            || event.kind == PreviewChartReviewPreparedKind::SimpleBreak;
        if ((group == SlideJudgeRenderGroup::JudgeTextOnly && !isJudgeText)
            || (group == SlideJudgeRenderGroup::SlideShapeOnly && isJudgeText)) {
            continue;
        }

        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - event.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kMaimuriDxJudgeMaxLifetimeSeconds) {
            continue;
        }

        const QImage* image = nullptr;
        QRectF sourceRect;
        qreal alpha = 0.0;
        qreal simpleScale = 1.0;

        switch (event.kind) {
        case PreviewChartReviewPreparedKind::SimpleNormal:
        case PreviewChartReviewPreparedKind::SimpleBreak: {
            const bool isBreak = event.kind == PreviewChartReviewPreparedKind::SimpleBreak;
            image = isBreak
                ? &state.judgeOverlay.simpleText.breakText.image
                : &state.judgeOverlay.simpleText.normal.image;
            sourceRect = isBreak
                ? state.judgeOverlay.simpleText.breakText.sourceRect
                : state.judgeOverlay.simpleText.normal.sourceRect;
            if (image->isNull()) {
                image = isBreak
                    ? &state.judgeOverlay.simpleText.normal.image
                    : &state.judgeOverlay.simpleText.breakText.image;
                sourceRect = isBreak
                    ? state.judgeOverlay.simpleText.normal.sourceRect
                    : state.judgeOverlay.simpleText.breakText.sourceRect;
            }
            alpha = maimuriDxSimpleJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxSimpleJudgeFadeInSeconds,
                isBreak ? kMaimuriDxBreakJudgeFadeOutStartSeconds : kMaimuriDxJudgeFadeOutStartSeconds,
                kMaimuriDxSimpleJudgeFadeOutEndSeconds
            );
            simpleScale = maimuriDxSimpleJudgeScale(
                elapsedSeconds,
                kMaimuriDxSimpleJudgeScalePopStartScale,
                kMaimuriDxSimpleJudgeScalePopEndScale,
                kMaimuriDxSimpleJudgeScalePopEndSeconds
            );
            if (isBreak) {
                alpha *= maimuriDxSimpleJudgeBreakFlash(
                    elapsedSeconds,
                    kMaimuriDxSimpleJudgeBreakFlashPeriodSeconds,
                    kMaimuriDxSimpleJudgeBreakFlashEndSeconds,
                    kMaimuriDxSimpleJudgeBreakFlashFloor
                );
            }
            break;
        }
        case PreviewChartReviewPreparedKind::StraightLeft:
            image = &state.judgeOverlay.neutral.straightLeftImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::StraightRight:
            image = &state.judgeOverlay.neutral.straightRightImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::CircleLeft:
            image = &state.judgeOverlay.neutral.circleLeftImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::CircleRight:
            image = &state.judgeOverlay.neutral.circleRightImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::WifiUp:
            image = &state.judgeOverlay.neutral.wifiUpImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case PreviewChartReviewPreparedKind::WifiDown:
            image = &state.judgeOverlay.neutral.wifiDownImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        }

        const PreviewSpriteDescriptor sprite =
            buildJudgeOverlaySpriteDescriptor(image, sourceRect, event.placement, alpha, playfieldRect, simpleScale);
        if (sprite.image != nullptr) {
            sprites.append(sprite);
        }
    }

    return sprites;
}

}  // namespace miacode::preview::scene
