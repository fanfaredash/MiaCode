#include "preview/scene/PreviewChartReviewLayerState.h"

#include "preview/scene/PreviewJudgeOverlayShared.h"
#include "preview/scene/PreviewOpacityCurves.h"

#include <QHash>
#include <QSet>

namespace {

enum class ReviewJudgeKind {
    SimpleNormal,
    SimpleBreak,
    SlideStraight,
    SlideCircleCcw,
    SlideCircleCw,
    WifiUp,
    WifiDown,
};

struct ReviewJudgeEvent {
    ReviewJudgeKind kind = ReviewJudgeKind::SimpleNormal;
    qreal second = -1.0;
    QString markerKey;
    QString pad;
    int lane = 0;
};

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

PreviewSpriteDescriptors buildPreviewChartReviewLayerSprites(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
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

    QHash<QString, const TimelineNoteMarker*> markerByKey;
    markerByKey.reserve(state.noteMarkers.size());

    QVector<ReviewJudgeEvent> events;
    events.reserve(state.noteMarkers.size() * 2);

    QSet<QString> emittedHeadEvents;
    emittedHeadEvents.reserve(state.noteMarkers.size());

    for (const TimelineNoteMarker& marker : state.noteMarkers) {
        const QString markerKey = makeMarkerAnalysisKey(marker);
        markerByKey.insert(markerKey, &marker);

        if (marker.type == QLatin1String("tap")) {
            if (!showSimpleJudgeOverlay) {
                continue;
            }
            ReviewJudgeEvent event;
            event.kind = marker.isBreak ? ReviewJudgeKind::SimpleBreak : ReviewJudgeKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.second);
            event.pad = lanePadToken(marker.lane);
            events.append(event);
            continue;
        }
        if (marker.type == QLatin1String("hold")) {
            if (!showSimpleJudgeOverlay || marker.endSecond < 0.0) {
                continue;
            }
            ReviewJudgeEvent event;
            event.kind = marker.isBreak ? ReviewJudgeKind::SimpleBreak : ReviewJudgeKind::SimpleNormal;
            event.second = static_cast<qreal>(marker.endSecond);
            event.pad = lanePadToken((marker.endLane >= 1 && marker.endLane <= 8) ? marker.endLane : marker.lane);
            events.append(event);
            continue;
        }
        if (marker.type != QLatin1String("slide") && marker.type != QLatin1String("wifi")) {
            continue;
        }

        if (showSimpleJudgeOverlay && marker.hasHeadStar) {
            const QString helperKey = slideHeadEventKey(marker);
            if (!emittedHeadEvents.contains(helperKey)) {
                emittedHeadEvents.insert(helperKey);
                ReviewJudgeEvent headEvent;
                headEvent.kind = marker.headBreak ? ReviewJudgeKind::SimpleBreak : ReviewJudgeKind::SimpleNormal;
                headEvent.second = static_cast<qreal>(marker.second);
                headEvent.pad = lanePadToken(marker.lane);
                events.append(headEvent);
            }
        }
        if (!showSlideJudgeOverlay) {
            continue;
        }

        ReviewJudgeEvent event;
        event.second = chartReviewSlideJudgeSecond(marker);
        event.markerKey = markerKey;
        event.lane = qBound(1, marker.endLane, 8);
        if (marker.type == QLatin1String("wifi")) {
            event.kind =
                (event.lane == 1 || event.lane == 2 || event.lane == 7 || event.lane == 8)
                ? ReviewJudgeKind::WifiUp
                : ReviewJudgeKind::WifiDown;
            events.append(event);
            continue;
        }

        const QString segmentKey = !marker.slideSegmentKeys.isEmpty()
            ? marker.slideSegmentKeys.constLast()
            : marker.slideTrackKey;
        if (slideKeyUsesCcwJudgeSprite(segmentKey)) {
            event.kind = ReviewJudgeKind::SlideCircleCcw;
        } else if (slideKeyUsesCwJudgeSprite(segmentKey)) {
            event.kind = ReviewJudgeKind::SlideCircleCw;
        } else {
            event.kind = ReviewJudgeKind::SlideStraight;
        }
        events.append(event);
    }

    sprites.reserve(events.size());
    for (const ReviewJudgeEvent& event : events) {
        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - event.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kMaimuriDxJudgeLifetimeSeconds) {
            continue;
        }

        PreviewJudgeOverlayPlacement placement;
        const QImage* image = nullptr;
        QRectF sourceRect;
        qreal alpha = 0.0;

        switch (event.kind) {
        case ReviewJudgeKind::SimpleNormal:
        case ReviewJudgeKind::SimpleBreak: {
            const bool isBreak = event.kind == ReviewJudgeKind::SimpleBreak;
            if (!buildJudgeOverlaySimplePlacement(event.pad, &placement)) {
                continue;
            }
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
        case ReviewJudgeKind::SlideStraight: {
            const TimelineNoteMarker* marker = markerByKey.value(event.markerKey, nullptr);
            bool useRightImage = false;
            if (marker == nullptr || !buildJudgeOverlayStraightPlacement(*marker, false, &placement, &useRightImage)) {
                continue;
            }
            image = useRightImage
                ? &state.judgeOverlay.reviewJudgeStraightRightImage
                : &state.judgeOverlay.reviewJudgeStraightLeftImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        }
        case ReviewJudgeKind::SlideCircleCcw:
            placement = buildJudgeOverlayCircleCcwPlacement(event.lane);
            image = &state.judgeOverlay.reviewJudgeCircleLeftImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case ReviewJudgeKind::SlideCircleCw:
            placement = buildJudgeOverlayCircleCwPlacement(event.lane);
            image = &state.judgeOverlay.reviewJudgeCircleRightImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case ReviewJudgeKind::WifiUp:
            placement = buildJudgeOverlayWifiPlacement(event.lane, true);
            image = &state.judgeOverlay.reviewJudgeWifiUpImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case ReviewJudgeKind::WifiDown:
            placement = buildJudgeOverlayWifiPlacement(event.lane, false);
            image = &state.judgeOverlay.reviewJudgeWifiDownImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        }

        const PreviewSpriteDescriptor sprite =
            buildJudgeOverlaySpriteDescriptor(image, sourceRect, placement, alpha, playfieldRect);
        if (sprite.image != nullptr) {
            sprites.append(sprite);
        }
    }

    return sprites;
}

}  // namespace miacode::preview::scene
