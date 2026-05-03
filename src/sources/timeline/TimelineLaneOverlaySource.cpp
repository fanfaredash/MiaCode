#include "sources/timeline/TimelineLaneOverlaySource.h"

#include "render/snapshot_builder.h"

namespace miacode::sources::timeline {

namespace sb = miacode::render::snapshot_builder;

bool TimelineLaneOverlaySource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineLaneOverlaySource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;

    // Phase 9a-fix1 (preserved from TimelineNotesSource where this code
    // used to live) — laneOverlayRects are emitted at viewport-relative
    // X coordinates and in the QSG path live OUTSIDE the gridTransformRoot,
    // so they never translate with scroll. The DComp TimelineRects batch
    // applies a unified Phase-8 -scroll translate to all rects, which
    // would push these off-screen as the user scrolls. Bake +scroll
    // into their X coords here so the pipeline's -scroll cancels out
    // and they stay pinned to the viewport.
    const qreal scrollOffsetX = static_cast<qreal>(state->horizontalScrollValue);
    if (scrollOffsetX != 0.0 && !state->laneOverlayRects.isEmpty()) {
        QVector<miacode::timeline::TimelineSceneRect> shifted;
        shifted.reserve(state->laneOverlayRects.size());
        for (const auto& r : state->laneOverlayRects) {
            miacode::timeline::TimelineSceneRect copy = r;
            copy.rect.translate(scrollOffsetX, 0.0);
            shifted.append(copy);
        }
        sb::pushTimelineRectBatch(snapshot, shifted);
    } else {
        sb::pushTimelineRectBatch(snapshot, state->laneOverlayRects);
    }
}

}  // namespace miacode::sources::timeline
