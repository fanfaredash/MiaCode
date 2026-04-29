#pragma once

#include "render/source.h"

namespace miacode::preview::scene {
class PreviewPreparedSceneCache;
struct PreviewLayerWindowCursor;
}

namespace miacode::sources::chart {

// z=6: track layer (slide track lines).
class TrackSource final : public render::IPreviewSource
{
public:
    TrackSource(
        const miacode::preview::scene::PreviewPreparedSceneCache* cache,
        const miacode::preview::scene::PreviewLayerWindowCursor* cursor);

    int zOrder() const override { return 6; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    const miacode::preview::scene::PreviewPreparedSceneCache* cache_;
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor_;
};

}  // namespace miacode::sources::chart
