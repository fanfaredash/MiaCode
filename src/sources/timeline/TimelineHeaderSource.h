#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

// z=3: timeline ruler header — second/minute labels, downbeat markers,
// lane labels along the left margin. Mirrors TimelineQuickHeaderLayer.
// Sits above notes so the ruler is always legible.
class TimelineHeaderSource final : public render::IPreviewSource
{
public:
    TimelineHeaderSource() = default;

    int zOrder() const override { return 3; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::timeline
