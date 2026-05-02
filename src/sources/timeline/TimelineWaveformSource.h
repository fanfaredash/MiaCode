#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

// z=1: audio waveform bars. Mirrors TimelineQuickWaveformLayer — one
// rect per amplitude bar, gated on waveformRevision. Sits above the
// grid background so the bars are visible on the dark lane fill, but
// below the notes/header so notes draw on top.
class TimelineWaveformSource final : public render::IPreviewSource
{
public:
    TimelineWaveformSource() = default;

    int zOrder() const override { return 1; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::timeline
