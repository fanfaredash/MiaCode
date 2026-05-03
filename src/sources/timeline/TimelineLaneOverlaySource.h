#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

// Beta21-fix12 — dedicated source for the alternating-row lane overlay
// rectangles. Promoted out of TimelineNotesSource so it can sit AFTER
// the waveform (which it's designed to mute via translucent fill) but
// BEFORE the bar/note grid lines (which should NOT be muted).
//
// Old layout had laneOverlayRects pushed inside TimelineNotesSource at
// z=3, which placed them on top of TimelineGridLinesSource at z=2 —
// the lane overlays' alpha 190 (light) / 210 (dark) was then blending
// over the bar lines, knocking their visible color down to ~25%/~18%
// of the source RGB. Splitting solves it cleanly: lane overlays at
// z=2, bar/note lines at z=3.
//
// Final z-stack (post fix):
//   z=0: TimelineGridSource          (base backgrounds + frame lines)
//   z=1: TimelineWaveformSource      (waveform amplitude bars)
//   z=2: TimelineLaneOverlaySource   ← THIS SOURCE (translucent row fills)
//   z=3: TimelineGridLinesSource     (bar lines + per-comma note ticks)
//   z=4: TimelineNotesSource         (note/hold/slide sprites — no lane overlay)
//   z=5: TimelineHeaderSource        (sidebar mask + header markers)
//   z=6: TimelineOverlaySource       (playhead / cursor / drag center)
class TimelineLaneOverlaySource final : public render::IPreviewSource
{
public:
    TimelineLaneOverlaySource() = default;

    int zOrder() const override { return 2; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::timeline
