#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

class TimelineLabelCache;

// z=3: timeline ruler header — second/minute labels, downbeat markers,
// lane labels along the left margin. Mirrors TimelineQuickHeaderLayer.
// Sits above notes so the ruler is always legible.
//
// Phase 3d-2: text labels (laneLabels + headerLabels) are CPU-rasterised
// to QImages via a TimelineLabelCache and emitted as chart-style
// sprites — no font atlas or glyph SDF needed yet. Triangles still
// emit as TimelineTriangles (Phase 3d-1 GPU pipeline).
class TimelineHeaderSource final : public render::IPreviewSource
{
public:
    explicit TimelineHeaderSource(TimelineLabelCache* labelCache);

    int zOrder() const override { return 3; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    TimelineLabelCache* labelCache_;
};

}  // namespace miacode::sources::timeline
