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
    // and lane-edge rules.
    //
    // Phase 9a-fix1 — baseBackgroundRects are emitted at viewport-
    // relative coordinates (full timeline width / lane bands / header
    // band) and must NOT scroll with the chart. The DComp TimelineRects
    // pipeline applies a unified Phase-8 scroll translate; we bake
    // +scroll into the X coords here so it cancels out and the rects
    // stay pinned to the viewport. Mirrors the QSG split where these
    // sit outside `gridTransformRoot`.
    const qreal scrollOffsetX =
        static_cast<qreal>(state->horizontalScrollValue);
    if (scrollOffsetX != 0.0 && !state->baseBackgroundRects.isEmpty()) {
        QVector<miacode::timeline::TimelineSceneRect> shifted;
        shifted.reserve(state->baseBackgroundRects.size());
        for (const auto& r : state->baseBackgroundRects) {
            miacode::timeline::TimelineSceneRect copy = r;
            copy.rect.translate(scrollOffsetX, 0.0);
            shifted.append(copy);
        }
        sb::pushTimelineRectBatch(snapshot, shifted);
    } else {
        sb::pushTimelineRectBatch(snapshot, state->baseBackgroundRects);
    }

    // Phase 9a-fix3 — frameRects + frameLines moved here from
    // TimelineOverlaySource. In the QSG path these live in the same
    // `staticRoot` as `laneLabels`, with laneLabels added AFTER so they
    // render on top of the opaque sidebar frameRect. The DComp
    // IPreviewSource architecture had Overlay (z=4) running AFTER
    // Header (z=3, has laneLabels), inverting the draw order — the
    // sidebar frameRect was painting over the lane labels. Emitting
    // them in TimelineGridSource (z=0) restores the correct sequence:
    // sidebar fill at z=0, lane labels at z=3 on top. Overlay (z=4)
    // keeps the playhead / cursor / drag-center / entry marker which
    // SHOULD render on top of everything else.
    if (scrollOffsetX != 0.0 && !state->frameRects.isEmpty()) {
        QVector<miacode::timeline::TimelineSceneRect> shifted;
        shifted.reserve(state->frameRects.size());
        for (const auto& r : state->frameRects) {
            miacode::timeline::TimelineSceneRect copy = r;
            copy.rect.translate(scrollOffsetX, 0.0);
            shifted.append(copy);
        }
        sb::pushTimelineRectBatch(snapshot, shifted);
    } else {
        sb::pushTimelineRectBatch(snapshot, state->frameRects);
    }
    if (scrollOffsetX != 0.0 && !state->frameLines.isEmpty()) {
        QVector<miacode::timeline::TimelineSceneLine> shifted;
        shifted.reserve(state->frameLines.size());
        for (const auto& line : state->frameLines) {
            miacode::timeline::TimelineSceneLine copy = line;
            copy.start.rx() += scrollOffsetX;
            copy.end.rx() += scrollOffsetX;
            shifted.append(copy);
        }
        sb::pushTimelineLineBatch(snapshot, shifted);
    } else {
        sb::pushTimelineLineBatch(snapshot, state->frameLines);
    }

    sb::pushTimelineLineBatch(snapshot, state->gridLines);
}

}  // namespace miacode::sources::timeline
