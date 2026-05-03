#include "sources/timeline/TimelineGridLinesSource.h"

#include "render/snapshot_builder.h"

namespace miacode::sources::timeline {

namespace sb = miacode::render::snapshot_builder;

bool TimelineGridLinesSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineGridLinesSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;
    // Bar lines (theme.gridMajor / now timelineLabel) and per-comma note
    // ticks (theme.gridMinor) come pre-baked in state.gridLines from
    // TimelineSceneStateBuilder::addGridLine. Pushing them at this
    // source's z=2 places them directly above the waveform (z=1) while
    // staying below the notes (z=3). No coordinate translation here —
    // gridLines x coords are content-X (absolute world coords); the
    // compositor's Phase-8 scroll translate handles the viewport mapping.
    sb::pushTimelineLineBatch(snapshot, state->gridLines);
}

}  // namespace miacode::sources::timeline
