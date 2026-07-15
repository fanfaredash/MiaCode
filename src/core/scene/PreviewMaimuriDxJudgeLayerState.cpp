#include "core/scene/PreviewMaimuriDxJudgeLayerState.h"

#include "core/scene/PreviewJudgeOverlayShared.h"
#include "core/scene/PreviewOpacityCurves.h"

#include <QHash>

namespace {

bool hasAnyMaimuriDxJudgeAssets(const miacode::preview::scene::PreviewJudgeOverlayAssets& assets)
{
    return !assets.simpleText.good.image.isNull()
        || !assets.simpleText.normal.image.isNull()
        || !assets.simpleText.breakText.image.isNull()
        || !assets.fastGood.straightLeftImage.isNull()
        || !assets.fastGood.straightRightImage.isNull()
        || !assets.fastGood.circleLeftImage.isNull()
        || !assets.fastGood.circleRightImage.isNull()
        || !assets.fastGood.wifiUpImage.isNull()
        || !assets.fastGood.wifiDownImage.isNull();
}

}  // namespace

namespace miacode::preview::scene {

PreviewSpriteDescriptors buildPreviewMaimuriDxJudgeLayerSprites(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QVector<MuriJudgeSpriteEvent>& activeEvents,
    const QRectF& playfieldRect,
    SlideJudgeRenderGroup group
)
{
    PreviewSpriteDescriptors sprites;
    if (state.muriRenderOptions.renderMode != RenderMode::MaimuriDxStyle
        || activeEvents.isEmpty()
        || !hasAnyMaimuriDxJudgeAssets(state.judgeOverlay)) {
        return sprites;
    }

    QHash<QString, const TimelineNoteMarker*> markerByKey;
    markerByKey.reserve(markers.size());

    QHash<QString, bool> simpleBreakByKey;
    simpleBreakByKey.reserve(markers.size() * 2);

    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        const QString markerKey = makeMarkerAnalysisKey(marker);
        markerByKey.insert(markerKey, &marker);
        simpleBreakByKey.insert(markerKey, marker.isBreak);
        if ((marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) && marker.hasHeadStar) {
            simpleBreakByKey.insert(slideHeadEventKey(marker), marker.headBreak);
        }
    }

    sprites.reserve(activeEvents.size());
    for (const MuriJudgeSpriteEvent& event : activeEvents) {
        const bool isJudgeText = event.kind == MuriJudgeSpriteKind::Simple;
        if ((group == SlideJudgeRenderGroup::JudgeTextOnly && !isJudgeText)
            || (group == SlideJudgeRenderGroup::SlideShapeOnly && isJudgeText)) {
            continue;
        }

        if (state.playheadSeconds + 1e-6 < event.spawnSecond) {
            continue;
        }

        const qreal elapsedSeconds = static_cast<qreal>(state.playheadSeconds - event.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kMaimuriDxJudgeMaxLifetimeSeconds) {
            continue;
        }

        PreviewJudgeOverlayPlacement placement;
        const QImage* image = nullptr;
        qreal alpha = 0.0;
        qreal simpleScale = 1.0;

        switch (event.kind) {
        case MuriJudgeSpriteKind::Simple: {
            if (!buildJudgeOverlaySimplePlacement(event.pad, PreviewTapJudgeTextDistance::Outer, &placement)) {
                continue;
            }
            if (event.simpleEffect == MuriSimpleJudgeEffect::Perfect) {
                const bool isBreak = simpleBreakByKey.value(event.markerKey, false);
                image = (isBreak && !state.judgeOverlay.simpleText.breakText.image.isNull())
                    ? &state.judgeOverlay.simpleText.breakText.image
                    : &state.judgeOverlay.simpleText.normal.image;
                if ((image == nullptr || image->isNull()) && !state.judgeOverlay.simpleText.breakText.image.isNull()) {
                    image = &state.judgeOverlay.simpleText.breakText.image;
                }
                if (image == nullptr || image->isNull()) {
                    image = &state.judgeOverlay.simpleText.good.image;
                }
            } else {
                image = !state.judgeOverlay.simpleText.good.image.isNull()
                    ? &state.judgeOverlay.simpleText.good.image
                    : &state.judgeOverlay.simpleText.normal.image;
            }
            const bool usesBreakText =
                event.simpleEffect == MuriSimpleJudgeEffect::Perfect
                && simpleBreakByKey.value(event.markerKey, false);
            alpha = maimuriDxSimpleJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxJudgeLifetimeSeconds,
                kMaimuriDxSimpleJudgeFadeInSeconds,
                usesBreakText ? kMaimuriDxBreakJudgeFadeOutStartSeconds : kMaimuriDxJudgeFadeOutStartSeconds,
                kMaimuriDxSimpleJudgeFadeOutEndSeconds
            );
            simpleScale = maimuriDxSimpleJudgeScale(
                elapsedSeconds,
                kMaimuriDxSimpleJudgeScalePopStartScale,
                kMaimuriDxSimpleJudgeScalePopEndScale,
                kMaimuriDxSimpleJudgeScalePopEndSeconds
            );
            if (usesBreakText) {
                alpha *= maimuriDxSimpleJudgeBreakFlash(
                    elapsedSeconds,
                    kMaimuriDxSimpleJudgeBreakFlashPeriodSeconds,
                    kMaimuriDxSimpleJudgeBreakFlashEndSeconds,
                    kMaimuriDxSimpleJudgeBreakFlashFloor
                );
            }
            break;
        }
        case MuriJudgeSpriteKind::SlideStraight: {
            const TimelineNoteMarker* marker = markerByKey.value(event.markerKey, nullptr);
            bool useRightImage = false;
            if (marker == nullptr || !buildJudgeOverlayStraightPlacement(*marker, true, &placement, &useRightImage)) {
                continue;
            }
            image = useRightImage
                ? &state.judgeOverlay.fastGood.straightRightImage
                : &state.judgeOverlay.fastGood.straightLeftImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        }
        case MuriJudgeSpriteKind::SlideCircleCcw:
            placement = buildJudgeOverlayCircleCcwPlacement(event.lane);
            image = &state.judgeOverlay.fastGood.circleLeftImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case MuriJudgeSpriteKind::SlideCircleCw:
            placement = buildJudgeOverlayCircleCwPlacement(event.lane);
            image = &state.judgeOverlay.fastGood.circleRightImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case MuriJudgeSpriteKind::WifiUp:
            placement = buildJudgeOverlayWifiPlacement(event.lane, true);
            image = &state.judgeOverlay.fastGood.wifiUpImage;
            alpha = maimuriDxSlideJudgeAlpha(
                elapsedSeconds,
                kMaimuriDxSlideJudgeLifetimeSeconds,
                kMaimuriDxSlideJudgeFadeInEndSeconds,
                kMaimuriDxSlideJudgeFadeOutStartSeconds,
                kMaimuriDxSlideJudgeFadeOutEndSeconds
            );
            break;
        case MuriJudgeSpriteKind::WifiDown:
            placement = buildJudgeOverlayWifiPlacement(event.lane, false);
            image = &state.judgeOverlay.fastGood.wifiDownImage;
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
            buildJudgeOverlaySpriteDescriptor(image, QRectF(), placement, alpha, playfieldRect, simpleScale);
        if (sprite.image != nullptr) {
            sprites.append(sprite);
        }
    }

    return sprites;
}

}  // namespace miacode::preview::scene
