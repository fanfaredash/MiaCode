#pragma once

#include "render/source.h"

namespace miacode::preview::scene {
class PreviewPreparedSceneCache;
struct PreviewLayerWindowCursor;
}

namespace miacode::sources::chart {

// z=12: touch hold layer. Sprites first, then arcs — legacy QSG
// renders them as separate child nodes inside the same layer slot,
// with arcs above sprites visually. Both come from the same
// buildPreviewTouchHoldLayerState call.
class TouchHoldSource final : public render::IPreviewSource
{
public:
    TouchHoldSource(
        const miacode::preview::scene::PreviewPreparedSceneCache* cache,
        const miacode::preview::scene::PreviewLayerWindowCursor* cursor);

    int zOrder() const override { return 12; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    const miacode::preview::scene::PreviewPreparedSceneCache* cache_;
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor_;
};

}  // namespace miacode::sources::chart
