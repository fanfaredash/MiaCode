#pragma once

#include "render/source.h"

namespace miacode::preview::scene {
class PreviewPreparedSceneCache;
struct PreviewLayerWindowCursor;
class PreviewHeadRenderAssetCache;
}

namespace miacode::sources::chart {

// z=10: head layer (per-marker base + tinted overlay composites).
// Receives a mutable PreviewHeadRenderAssetCache so cross-frame
// composite reuse continues to work. The cache is owned by the
// surface (lives on the GUI thread, never touched by the render
// thread).
class HeadSource final : public render::IPreviewSource
{
public:
    HeadSource(
        const miacode::preview::scene::PreviewPreparedSceneCache* cache,
        const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
        miacode::preview::scene::PreviewHeadRenderAssetCache* assetCache);

    int zOrder() const override { return 10; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    const miacode::preview::scene::PreviewPreparedSceneCache* cache_;
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor_;
    miacode::preview::scene::PreviewHeadRenderAssetCache* assetCache_;
};

}  // namespace miacode::sources::chart
