#include "sources/chart/ChartReviewSource.h"

#include "core/scene/PreviewChartReviewLayerState.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

ChartReviewSource::ChartReviewSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool ChartReviewSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::ChartReviewLayer);
}

void ChartReviewSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    scene::PreviewChartReviewPreparedEvents preparedEvents;
    cache_->collectChartReviewEvents(
        cursor_->activePreparedIndices, &preparedEvents);
    sb::pushSpriteBatch(
        snapshot,
        scene::buildPreviewChartReviewLayerSprites(
            ctx.frameState, ctx.playfieldRect, &preparedEvents));
}

}  // namespace miacode::sources::chart
