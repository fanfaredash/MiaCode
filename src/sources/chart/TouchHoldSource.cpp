#include "sources/chart/TouchHoldSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "core/scene/PreviewTouchHoldLayerState.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

TouchHoldSource::TouchHoldSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool TouchHoldSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::TouchHoldLayer);
}

void TouchHoldSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    const scene::PreviewActiveMarkerView view(
        ctx.frameState.noteMarkers, cache_->touchHoldLayer(), *cursor_);
    auto layerState = scene::buildPreviewTouchHoldLayerState(
        ctx.frameState, view, ctx.playfieldRect);
    sb::pushSpriteBatch(snapshot, layerState.sprites);
    sb::pushArcBatch(snapshot, layerState.arcs);
    sb::appendOwnedImages(snapshot, layerState.ownedImages);
}

}  // namespace miacode::sources::chart
