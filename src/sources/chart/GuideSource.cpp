#include "sources/chart/GuideSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewGuideLayerState.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

GuideSource::GuideSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool GuideSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::GuideLayer);
}

void GuideSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    const scene::PreviewActiveMarkerView view(
        ctx.frameState.noteMarkers, cache_->guideLayer(), *cursor_);
    sb::pushSpriteBatch(
        snapshot,
        scene::buildPreviewGuideLayerSprites(ctx.frameState, view, ctx.playfieldRect));
}

}  // namespace miacode::sources::chart
