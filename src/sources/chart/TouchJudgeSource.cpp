#include "sources/chart/TouchJudgeSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "core/scene/PreviewTouchJudgeLayerState.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

TouchJudgeSource::TouchJudgeSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool TouchJudgeSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::JudgeTouchLayer);
}

void TouchJudgeSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    const scene::PreviewActiveMarkerView view(
        ctx.frameState.noteMarkers, cache_->touchJudgeLayer(), *cursor_);
    sb::pushSpriteBatch(
        snapshot,
        scene::buildPreviewTouchJudgeLayerState(ctx.frameState, view, ctx.playfieldRect).sprites);
}

}  // namespace miacode::sources::chart
