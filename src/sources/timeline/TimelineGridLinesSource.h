#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

// Beta21-fix7 — dedicated source for vertical bar lines + per-comma
// note ticks. Promoted out of TimelineGridSource (which keeps emitting
// the lane backgrounds and timeline frame at z=0) so they can sit
// ABOVE the waveform (z=1) but still BELOW the notes / header / overlay.
//
// The original z=0 placement put grid lines underneath every other
// element including the waveform — visually rendering measure markers
// "below" the waveform pixels at the same X column. That was the root
// cause of the long-standing "lines too faint / behind waveform" report.
//
// New z-stack:
//   z=0: TimelineGridSource         (baseBackgrounds + frameRects + frameLines)
//   z=1: TimelineWaveformSource     (waveform amplitude bars)
//   z=2: TimelineGridLinesSource    ← THIS SOURCE (bar + note grid lines)
//   z=3: TimelineNotesSource        (notes / hold / slide sprites)
//   z=4: TimelineHeaderSource       (lane overlays + measure-marker triangles)
//   z=5: TimelineOverlaySource      (playhead, cursor, drag center, sidebar mask)
class TimelineGridLinesSource final : public render::IPreviewSource
{
public:
    TimelineGridLinesSource() = default;

    int zOrder() const override { return 3; }  // Beta21-fix12: bumped 2→3 to make room for TimelineLaneOverlaySource at z=2
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::timeline
