#pragma once

#include "render/source.h"

namespace miacode::preview::scene {
class PreviewPreparedSceneCache;
struct PreviewLayerWindowCursor;
}

namespace miacode::sources::chart {

// z=14: maimuri DX judge. Uses the SIMPLE full-marker view because
// the maimuriDxJudgeLayer cursor windows the *event* list, not
// markers. The cursor still feeds collectMaimuriDxJudgeData for the
// events themselves.
class MaimuriDxJudgeSource final : public render::IPreviewSource
{
public:
    MaimuriDxJudgeSource(
        const miacode::preview::scene::PreviewPreparedSceneCache* cache,
        const miacode::preview::scene::PreviewLayerWindowCursor* cursor);

    int zOrder() const override { return 14; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    const miacode::preview::scene::PreviewPreparedSceneCache* cache_;
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor_;
};

}  // namespace miacode::sources::chart
