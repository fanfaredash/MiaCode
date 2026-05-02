#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

// z=0 within the timeline render order: lane background fills + grid
// lines. Mirrors TimelineQuickGridLayer's updateNode body — the legacy
// QSG version walks state.baseBackgroundRects and emits one QSG rect
// node per entry (gated on gridRevision/appearanceRevision change).
//
// Phase 2d emits the descriptors as TimelineRects + TimelineLines
// batches. The render thread's batch switch currently skips both
// types; Phase 3 RenderView migration adds GPU pipelines that consume
// them. Until then, registering this source with a compositor and
// producing batches is harmless: the chart preview path doesn't hand
// timeline state into PreviewBuildContext, so the source short-circuits
// on the null check.
class TimelineGridSource final : public render::IPreviewSource
{
public:
    TimelineGridSource() = default;

    int zOrder() const override { return 0; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::timeline
