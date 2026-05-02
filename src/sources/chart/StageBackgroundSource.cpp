#include "sources/chart/StageBackgroundSource.h"

#include "core/scene/PreviewSceneGeometry.h"
#include "render/snapshot_builder.h"

#include <QImage>
#include <QSharedPointer>

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

bool StageBackgroundSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::StageBackgroundLayer);
}

void StageBackgroundSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    // Phase 4c-8 — image-mode override path takes precedence. The
    // PreviewStageMediaHost loads the bg.{jpg,png,...} into a QImage
    // and publishes it through ctx.stageBackgroundImageOverride; the
    // DComp HWND on top of the QML window occludes QML's
    // PreviewStageMediaItem, so DComp has to paint the bg here for
    // the user to see it. When this override is set, we ignore the
    // legacy `presentationMode == ExternalQuickMediaItem` skip
    // because the external QML render is already invisible to the
    // user.
    const auto& state = ctx.frameState;
    const auto& media = state.media;

    QImage bgImage;
    bool bgCacheable = true;
    if (!ctx.stageBackgroundImageOverride.isNull()) {
        bgImage = ctx.stageBackgroundImageOverride;
        bgCacheable = true;  // image file content is stable per chart load
    } else {
        // Legacy path: the historic external-media skip + the
        // resolvedStageImage / mediaFrame / retainedVideoFallbackFrame
        // chain. Kept intact for the (currently unwired) video-frame
        // pipeline that Phase 4c-9 will populate.
        const bool usesExternalMedia =
            media.presentationMode
                == scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem;
        if (!usesExternalMedia) {
            if (!media.resolvedStageImage.isNull()) {
                bgImage = media.resolvedStageImage;
                bgCacheable = media.resolvedStageImageCacheable;
            } else if (!media.mediaFrame.isNull()) {
                bgImage = media.mediaFrame;
                bgCacheable = true;
            } else if (!media.retainedVideoFallbackFrame.isNull()) {
                bgImage = media.retainedVideoFallbackFrame;
                bgCacheable = false;
            }
        }
    }
    if (bgImage.isNull() || bgImage.width() <= 0 || bgImage.height() <= 0) {
        return;
    }

    const bool fitContain =
        state.render.backgroundScaleMode == PreviewBackgroundScaleMode::FitContain;
    const QRectF targetRect = scene::mediaTargetRect(
        bgImage.size(), ctx.stageRect, fitContain);
    if (targetRect.width() <= 0.0 || targetRect.height() <= 0.0) {
        return;
    }

    auto bgPtr = QSharedPointer<QImage>::create(bgImage);
    snapshot.retainedImages.append(bgPtr);
    scene::PreviewSpriteDescriptor bg;
    bg.image = bgPtr.data();
    bg.center = targetRect.center();
    bg.width = targetRect.width();
    bg.height = targetRect.height();
    bg.rotationDegrees = 0.0;
    bg.opacity = 1.0;
    bg.effect = scene::PreviewAnimatedSpriteEffect::None;
    bg.cacheable = bgCacheable;
    scene::PreviewSpriteDescriptors batch;
    batch.append(bg);
    sb::pushSpriteBatch(snapshot, batch);
}

}  // namespace miacode::sources::chart
