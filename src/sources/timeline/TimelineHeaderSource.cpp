#include "sources/timeline/TimelineHeaderSource.h"

#include "render/snapshot_builder.h"

namespace miacode::sources::timeline {

namespace sb = miacode::render::snapshot_builder;

bool TimelineHeaderSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineHeaderSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;
    // The header layer pushes:
    //   - laneLabels   — left-margin lane name text
    //   - headerLabels — top-ruler tick labels (00:00, 00:30, …)
    //   - headerMarkers — small triangles flagging downbeats and
    //                     section boundaries
    sb::pushTimelineTextLabelBatch(snapshot, state->laneLabels);
    sb::pushTimelineTextLabelBatch(snapshot, state->headerLabels);
    sb::pushTimelineTriangleBatch(snapshot, state->headerMarkers);
}

}  // namespace miacode::sources::timeline
