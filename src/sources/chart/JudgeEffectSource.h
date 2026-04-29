#pragma once

#include "render/source.h"

namespace miacode::preview::scene {
class PreviewPreparedSceneCache;
struct PreviewLayerWindowCursor;
}

namespace miacode::sources::chart {

// z=8: judge effect (per-marker hit/miss visual).
class JudgeEffectSource final : public render::IPreviewSource
{
public:
    JudgeEffectSource(
        const miacode::preview::scene::PreviewPreparedSceneCache* cache,
        const miacode::preview::scene::PreviewLayerWindowCursor* cursor);

    int zOrder() const override { return 8; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    const miacode::preview::scene::PreviewPreparedSceneCache* cache_;
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor_;
};

}  // namespace miacode::sources::chart
