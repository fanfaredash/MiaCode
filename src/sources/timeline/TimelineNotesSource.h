#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

class TimelineSpriteAssetCache;

// z=2: note markers (taps, slides, holds, touch holds, hold spans,
// muri dots, lane overlay highlights). Mirrors
// TimelineQuickNotesLayer — the largest of the timeline layers by
// primitive count.
//
// Phase 3d-3: takes a TimelineSpriteAssetCache for converting
// `spriteType` strings to QImages via TimelineNoteAssets. Cache is
// owned by the TimelineRenderView so all timeline sources share one
// asset registry; this source holds a non-owning pointer.
class TimelineNotesSource final : public render::IPreviewSource
{
public:
    explicit TimelineNotesSource(TimelineSpriteAssetCache* assetCache);

    int zOrder() const override { return 2; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    TimelineSpriteAssetCache* assetCache_;
};

}  // namespace miacode::sources::timeline
