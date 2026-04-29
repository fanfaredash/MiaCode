#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

// z=2: note markers (taps, slides, holds, touch holds, hold spans,
// muri dots, lane overlay highlights). Mirrors
// TimelineQuickNotesLayer — the largest of the timeline layers by
// primitive count.
class TimelineNotesSource final : public render::IPreviewSource
{
public:
    TimelineNotesSource() = default;

    int zOrder() const override { return 2; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::timeline
