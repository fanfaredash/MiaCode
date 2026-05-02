#pragma once

#include "render/source.h"

namespace miacode::sources::chart {

// z=3: muri action overlay (solid-colour ellipses).
class MuriActionSource final : public render::IPreviewSource
{
public:
    MuriActionSource() = default;

    int zOrder() const override { return 3; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::chart
