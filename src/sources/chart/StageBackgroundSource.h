#pragma once

#include "render/source.h"

namespace miacode::sources::chart {

// z=0: stage background image (resolved media frame, fallback frame,
// or image media). Mirrors the inline block in
// PreviewDCompSurface::buildAndPublishSnapshot keyed on
// scene::StageBackgroundLayer.
//
// Reads ctx.frameState.media + ctx.frameState.render. No additional
// dependencies — the surface owns no per-source state for this layer.
class StageBackgroundSource final : public render::IPreviewSource
{
public:
    StageBackgroundSource() = default;

    int zOrder() const override { return 0; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::chart
