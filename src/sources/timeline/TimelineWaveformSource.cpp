#include "sources/timeline/TimelineWaveformSource.h"

#include "render/snapshot_builder.h"

namespace miacode::sources::timeline {

namespace sb = miacode::render::snapshot_builder;

bool TimelineWaveformSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineWaveformSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;
    // waveformBars are pre-binned amplitude rects — TimelineSceneStateBuilder
    // already runs the audio analyzer and clamps the bar count to the
    // pixel-width of the timeline. We just forward them.
    sb::pushTimelineRectBatch(snapshot, state->waveformBars);
}

}  // namespace miacode::sources::timeline
