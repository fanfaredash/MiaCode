#pragma once

#include "render/source.h"

namespace miacode::sources::chart {

// z=2: muri pad state (solid-colour ellipses). No marker-windowed
// inputs — reads state directly.
class MuriPadSource final : public render::IPreviewSource
{
public:
    MuriPadSource() = default;

    int zOrder() const override { return 2; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::chart
