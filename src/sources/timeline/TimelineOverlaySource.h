#pragma once

#include "render/source.h"

namespace miacode::sources::timeline {

// z=4 (top): user-interaction overlays — selection frames, selection
// drag handles, the playhead vertical line, the cursor line under the
// mouse, the entry-marker triangle, the drag-center crosshair.
// Mirrors TimelineQuickOverlayLayer. Always last so its primitives
// paint over notes/header without being obscured.
//
// Driven by overlayDynamicRevision rather than overlayRevision because
// the sub-rect that the user is hover/dragging changes every mouse
// move; the legacy QSG path uses a similar split (full rebuild on
// overlayRevision, dynamic-only update on overlayDynamicRevision).
class TimelineOverlaySource final : public render::IPreviewSource
{
public:
    TimelineOverlaySource() = default;

    int zOrder() const override { return 4; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;
};

}  // namespace miacode::sources::timeline
