#include "sources/chart/HeadSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewHeadLayerState.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

HeadSource::HeadSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor,
    scene::PreviewHeadRenderAssetCache* assetCache)
    : cache_(cache), cursor_(cursor), assetCache_(assetCache)
{}

bool HeadSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::HeadLayer);
}

void HeadSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    const scene::PreviewActiveMarkerView view(
        ctx.frameState.noteMarkers, cache_->headLayer(), *cursor_);
    auto layerState = scene::buildPreviewHeadLayerState(
        ctx.frameState, view, ctx.playfieldRect, assetCache_);
    sb::pushSpriteBatch(snapshot, layerState.sprites);
    sb::appendOwnedImages(snapshot, layerState.ownedImages);
}

}  // namespace miacode::sources::chart
