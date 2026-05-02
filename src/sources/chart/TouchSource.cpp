#include "sources/chart/TouchSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "core/scene/PreviewTouchLayerState.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

TouchSource::TouchSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool TouchSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::TouchLayer);
}

void TouchSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    const scene::PreviewActiveMarkerView view(
        ctx.frameState.noteMarkers, cache_->touchLayer(), *cursor_);
    auto layerState = scene::buildPreviewTouchLayerState(
        ctx.frameState, view, ctx.playfieldRect);
    sb::pushSpriteBatch(snapshot, layerState.sprites);
    sb::appendOwnedImages(snapshot, layerState.ownedImages);
}

}  // namespace miacode::sources::chart
