#include "sources/chart/JudgeFireworkSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

JudgeFireworkSource::JudgeFireworkSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool JudgeFireworkSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::JudgeFireworkLayer);
}

void JudgeFireworkSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    const scene::PreviewActiveMarkerView view(
        ctx.frameState.noteMarkers, cache_->judgeFireworkLayer(), *cursor_);
    sb::pushFireworkBatch(
        snapshot,
        scene::buildPreviewJudgeFireworkLayerState(ctx.frameState, view, ctx.playfieldRect));
}

}  // namespace miacode::sources::chart
