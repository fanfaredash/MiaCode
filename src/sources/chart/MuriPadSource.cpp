#include "sources/chart/MuriPadSource.h"

#include "core/scene/PreviewMuriPadLayerState.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

bool MuriPadSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::MuriPadStateLayer);
}

void MuriPadSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    sb::pushCircleBatch(
        snapshot,
        scene::buildPreviewMuriPadLayerState(ctx.frameState, ctx.playfieldRect).circles);
}

}  // namespace miacode::sources::chart
