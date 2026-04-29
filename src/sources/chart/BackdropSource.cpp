#include "sources/chart/BackdropSource.h"

#include "render/snapshot_builder.h"

#include <QImage>
#include <QSharedPointer>

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

bool BackdropSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::BackdropLayer);
}

void BackdropSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    // Snapshot owns its own QImage copy so the render thread never
    // reads runtime-mutated QImage state.
    const auto& state = ctx.frameState;
    if (state.assets.outlineImage.isNull()) {
        return;
    }
    auto backdropImage = QSharedPointer<QImage>::create(state.assets.outlineImage);
    snapshot.retainedImages.append(backdropImage);
    scene::PreviewSpriteDescriptor backdrop;
    backdrop.image = backdropImage.data();
    backdrop.center = ctx.playfieldRect.center();
    backdrop.width = ctx.playfieldRect.width();
    backdrop.height = ctx.playfieldRect.height();
    backdrop.rotationDegrees = 0.0;
    backdrop.opacity = 1.0;
    backdrop.effect = scene::PreviewAnimatedSpriteEffect::None;
    backdrop.cacheable = true;
    scene::PreviewSpriteDescriptors batch;
    batch.append(backdrop);
    sb::pushSpriteBatch(snapshot, batch);
}

}  // namespace miacode::sources::chart
