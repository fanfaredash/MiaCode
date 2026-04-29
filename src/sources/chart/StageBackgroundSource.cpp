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
    // Skipped when separate-surface external media is active (legacy
    // QSG also short-circuits in that case — the media is rendered
    // by an external HWND/QQuickWindow). Skipped when no media is
    // available so the dark canvasBg from embeddedPreviewFrame
    // shows through DComp's transparent clear.
    const auto& state = ctx.frameState;
    const auto& media = state.media;
    const bool usesExternalMedia =
        media.presentationMode
            == scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem;

    QImage bgImage;
    bool bgCacheable = true;
    if (!usesExternalMedia) {
        if (!media.resolvedStageImage.isNull()) {
            bgImage = media.resolvedStageImage;
            bgCacheable = media.resolvedStageImageCacheable;
        } else if (!media.mediaFrame.isNull()) {
            bgImage = media.mediaFrame;
            bgCacheable = true;
        } else if (!media.retainedVideoFallbackFrame.isNull()) {
            bgImage = media.retainedVideoFallbackFrame;
            bgCacheable = false;  // last-known fallback, treat as transient
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
