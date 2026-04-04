#include "preview/scene/PreviewMaimuriDxJudgeLayerState.h"

#include "preview/scene/PreviewJudgeOverlayShared.h"
#include "preview/scene/PreviewOpacityCurves.h"

#include <QHash>

namespace {

bool hasAnyMaimuriDxJudgeAssets(const miacode::preview::scene::PreviewJudgeOverlayAssets& assets)
{
    return !assets.muriJudgeSimpleImage.isNull()
        || !assets.reviewJudgeSimpleNormalImage.isNull()
        || !assets.reviewJudgeSimpleBreakImage.isNull()
        || !assets.muriJudgeStraightLeftImage.isNull()
        || !assets.muriJudgeStraightRightImage.isNull()
        || !assets.muriJudgeCircleLeftImage.isNull()
        || !assets.muriJudgeCircleRightImage.isNull()
        || !assets.muriJudgeWifiUpImage.isNull()
        || !assets.muriJudgeWifiDownImage.isNull();
}

}  // namespace

namespace miacode::preview::scene {

PreviewSpriteDescriptors buildPreviewMaimuriDxJudgeLayerSprites(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
)
{
    PreviewSpriteDescriptors sprites;
    if (state.muriRenderOptions.renderMode != RenderMode::MaimuriDxStyle
        || state.muriAnalysisReport.judgeSpriteEvents.isEmpty()
        || !hasAnyMaimuriDxJudgeAssets(state.judgeOverlay)) {
        return sprites;
    }
    if (!state.muriAnalysisReport.sourceSignature.isEmpty()
        && state.muriAnalysisReport.sourceSignature != buildJudgeOverlayMarkerSourceSignature(state.noteMarkers)) {
        return sprites;
    }

    QHash<QString, const TimelineNoteMarker*> markerByKey;
    markerByKey.reserve(state.noteMarkers.size());

    QHash<QString, bool> simpleBreakByKey;
    simpleBreakByKey.reserve(state.noteMarkers.size() * 2);

    for (const TimelineNoteMarker& marker : state.noteMarkers) {
        const QString markerKey = makeMarkerAnalysisKey(marker);
        markerByKey.insert(markerKey, &marker);
        simpleBreakByKey.insert(markerKey, marker.isBreak);
        if ((marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) && marker.hasHeadStar) {
            simpleBreakByKey.insert(slideHeadEventKey(marker), marker.headBreak);
        }
    }

    sprites.reserve(state.muriAnalysisReport.judgeSpriteEvents.size());
    for (const MuriJudgeSpriteEvent& event : state.muriAnalysisReport.judgeSpriteEvents) {
        if (state.playheadSeconds + 1e-6 < event.spawnSecond) {
            continue;
        }

        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - event.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kMaimuriDxJudgeLifetimeSeconds) {
            continue;
        }

        PreviewJudgeOverlayPlacement placement;
        const QImage* image = nullptr;
        qreal alpha = 0.0;

        switch (event.kind) {
        case MuriJudgeSpriteKind::Simple: {
            if (!buildJudgeOverlaySimplePlacement(event.pad, &placement)) {
                continue;
            }
            if (event.simpleEffect == MuriSimpleJudgeEffect::Perfect) {
                const bool isBreak = simpleBreakByKey.value(event.markerKey, false);
                image = (isBreak && !state.judgeOverlay.reviewJudgeSimpleBreakImage.isNull())
                    ? &state.judgeOverlay.reviewJudgeSimpleBreakImage
                    : &state.judgeOverlay.reviewJudgeSimpleNormalImage;
                if ((image == nullptr || image->isNull()) && !state.judgeOverlay.reviewJudgeSimpleBreakImage.isNull()) {
                    image = &state.judgeOverlay.reviewJudgeSimpleBreakImage;
                }
                if (image == nullptr || image->isNull()) {
                    image = &state.judgeOverlay.muriJudgeSimpleImage;
                }
            } else {
                image = !state.judgeOverlay.muriJudgeSimpleImage.isNull()
                    ? &state.judgeOverlay.muriJudgeSimpleImage
                    : &state.judgeOverlay.reviewJudgeSimpleNormalImage;
            }
            alpha = maimuriDxSimpleJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds,
                kMaimuriDxSimpleJudgeFadeInSeconds
            );
            break;
        }
        case MuriJudgeSpriteKind::SlideStraight: {
            const TimelineNoteMarker* marker = markerByKey.value(event.markerKey, nullptr);
            bool useRightImage = false;
            if (marker == nullptr || !buildJudgeOverlayStraightPlacement(*marker, true, &placement, &useRightImage)) {
                continue;
            }
            image = useRightImage
                ? &state.judgeOverlay.muriJudgeStraightRightImage
                : &state.judgeOverlay.muriJudgeStraightLeftImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        }
        case MuriJudgeSpriteKind::SlideCircleCcw:
            placement = buildJudgeOverlayCircleCcwPlacement(event.lane);
            image = &state.judgeOverlay.muriJudgeCircleLeftImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case MuriJudgeSpriteKind::SlideCircleCw:
            placement = buildJudgeOverlayCircleCwPlacement(event.lane);
            image = &state.judgeOverlay.muriJudgeCircleRightImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case MuriJudgeSpriteKind::WifiUp:
            placement = buildJudgeOverlayWifiPlacement(event.lane, true);
            image = &state.judgeOverlay.muriJudgeWifiUpImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        case MuriJudgeSpriteKind::WifiDown:
            placement = buildJudgeOverlayWifiPlacement(event.lane, false);
            image = &state.judgeOverlay.muriJudgeWifiDownImage;
            alpha = maimuriDxJudgeFadeOutAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxJudgeFadeOutStartSeconds
            );
            break;
        }

        const PreviewSpriteDescriptor sprite =
            buildJudgeOverlaySpriteDescriptor(image, QRectF(), placement, alpha, playfieldRect);
        if (sprite.image != nullptr) {
            sprites.append(sprite);
        }
    }

    return sprites;
}

}  // namespace miacode::preview::scene
