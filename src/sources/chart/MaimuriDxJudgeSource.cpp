#include "sources/chart/MaimuriDxJudgeSource.h"

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "render/snapshot_builder.h"

#include <QVector>

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

MaimuriDxJudgeSource::MaimuriDxJudgeSource(
    const scene::PreviewPreparedSceneCache* cache,
    const scene::PreviewLayerWindowCursor* cursor)
    : cache_(cache), cursor_(cursor)
{}

bool MaimuriDxJudgeSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::MaimuriDxJudgeLayer);
}

void MaimuriDxJudgeSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (cache_ == nullptr || cursor_ == nullptr) return;
    QVector<MuriJudgeSpriteEvent> activeEvents;
    QVector<int> activeMarkerIndices;
    cache_->collectMaimuriDxJudgeData(
        cursor_->activePreparedIndices,
        &activeEvents, &activeMarkerIndices);
    scene::PreviewActiveMarkerView allMarkers(ctx.frameState.noteMarkers);
    sb::pushSpriteBatch(
        snapshot,
        scene::buildPreviewMaimuriDxJudgeLayerSprites(
            ctx.frameState, allMarkers, activeEvents, ctx.playfieldRect));
}

}  // namespace miacode::sources::chart
