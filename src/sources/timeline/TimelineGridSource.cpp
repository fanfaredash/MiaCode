#include "sources/timeline/TimelineGridSource.h"

#include "render/snapshot_builder.h"

namespace miacode::sources::timeline {

namespace sb = miacode::render::snapshot_builder;

bool TimelineGridSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineGridSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;
    // baseBackgroundRects covers each lane's background fill plus the
    // ruler-band fills. gridLines covers vertical bar/beat divisions
    // and lane-edge rules. The legacy QSG layer rebuilds nodes only
    // when gridRevision or appearanceRevision changes; the descriptor
    // vectors here are pre-filtered by the same revisions, so we just
    // forward whatever the scene state currently holds.
    sb::pushTimelineRectBatch(snapshot, state->baseBackgroundRects);
    sb::pushTimelineLineBatch(snapshot, state->gridLines);
}

}  // namespace miacode::sources::timeline
