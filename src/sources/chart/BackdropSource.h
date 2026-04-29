#pragma once

#include "render/source.h"

namespace miacode::sources::chart {

// z=1: outline backdrop image (the fixed playfield ring/sectors).
// Reads ctx.frameState.assets.outlineImage. No per-source state.
class BackdropSource final : public render::IPreviewSource
{
public:
    BackdropSource() = default;

    int zOrder() const override { return 1; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::chart
