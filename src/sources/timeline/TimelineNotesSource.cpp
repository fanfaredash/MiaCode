#include "sources/timeline/TimelineNotesSource.h"

#include "render/snapshot_builder.h"

namespace miacode::sources::timeline {

namespace sb = miacode::render::snapshot_builder;

bool TimelineNotesSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineNotesSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;
    // Push order matches TimelineQuickNotesLayer's child sequence:
    //   1. lane overlay rects (per-lane highlight bands behind notes)
    //   2. firework bands (rect overlays for firework events)
    //   3. track lines (slide/hold trail rendering)
    //   4. touch hold lines (touch-hold trail rendering)
    //   5. hold spans (the wide rect under each hold note)
    //   6. note sprites (head sprites for taps/slide-stars/touch-holds)
    //   7. track sprites (head/tail glyphs along tracks)
    //   8. muri dots (small glyph circles flagging unforgiving sections)
    //
    // Phase 3 GPU pipelines render each batch type with the right
    // shader (filled rect, stroked line, textured sprite, glyph SDF).
    sb::pushTimelineRectBatch(snapshot, state->laneOverlayRects);
    sb::pushTimelineRectBatch(snapshot, state->fireworkBands);
    sb::pushTimelineLineBatch(snapshot, state->trackLines);
    sb::pushTimelineLineBatch(snapshot, state->touchHoldLines);
    sb::pushTimelineHoldSpanBatch(snapshot, state->holdSpans);
    sb::pushTimelineSpriteBatch(snapshot, state->noteSprites);
    sb::pushTimelineSpriteBatch(snapshot, state->trackSprites);
    sb::pushTimelineGlyphBatch(snapshot, state->muriDots);
}

}  // namespace miacode::sources::timeline
