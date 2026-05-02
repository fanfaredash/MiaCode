#include "sources/chart/MuriActionSource.h"

#include "core/scene/PreviewMuriActionLayerState.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

bool MuriActionSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::MuriActionLayer);
}

void MuriActionSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    sb::pushCircleBatch(
        snapshot,
        scene::buildPreviewMuriActionLayerState(ctx.frameState, ctx.playfieldRect).circles);
}

}  // namespace miacode::sources::chart
