#include "sources/chart/JudgeEffectSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewJudgeEffectLayerState.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "render/snapshot_builder.h"

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

JudgeEffectSource::JudgeEffectSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool JudgeEffectSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::JudgeLayer);
}

void JudgeEffectSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    const scene::PreviewActiveMarkerView view(
        ctx.frameState.noteMarkers, cache_->judgeEffectLayer(), *cursor_);
    auto layerState = scene::buildPreviewJudgeEffectLayerState(
        ctx.frameState, view, ctx.playfieldRect);
    sb::pushSpriteBatch(snapshot, layerState.sprites);
    sb::appendOwnedImages(snapshot, layerState.ownedImages);
}

}  // namespace miacode::sources::chart
