#include "sources/timeline/TimelineOverlaySource.h"

#include "render/snapshot_builder.h"

#include <QVector>

namespace miacode::sources::timeline {

namespace sb = miacode::render::snapshot_builder;

bool TimelineOverlaySource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineOverlaySource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;
    // Ordering mirrors TimelineQuickOverlayLayer's child sequence:
    //   1. selection frame rects (per-marker selection highlight)
    //   2. selection frame lines (border strokes around selected items)
    //   3. cursor line (vertical under mouse, when present)
    //   4. playhead line (vertical at the audio playhead)
    //   5. drag-center line (horizontal crosshair during drag-select)
    //   6. entry marker triangle (indicates "chart starts here")
    //   7. cursor marker triangle (indicates editor cursor position)
    //
    // The single-element optionals (cursor, playhead, drag-center,
    // entry marker) only push a batch when the corresponding `has*`
    // flag is true, mirroring the QSG path's gated-add pattern.

    // Phase 9a-fix3 — frameRects + frameLines moved to
    // TimelineGridSource so they render BEFORE laneLabels (which are
    // emitted by TimelineHeaderSource at z=3). In the QSG path these
    // share `staticRoot` with laneLabels, with the labels added LATER
    // and therefore drawn on top of the opaque sidebar frameRect.
    // Keeping them in Overlay (z=4) made them paint over the labels.

    if (state->hasCursorLine) {
        QVector<miacode::timeline::TimelineSceneLine> tmp;
        tmp.append(state->cursorLine);
        sb::pushTimelineLineBatch(snapshot, tmp);
    }
    if (state->hasPlayheadLine) {
        QVector<miacode::timeline::TimelineSceneLine> tmp;
        tmp.append(state->playheadLine);
        sb::pushTimelineLineBatch(snapshot, tmp);
    }
    if (state->hasDragCenterLine) {
        QVector<miacode::timeline::TimelineSceneLine> tmp;
        tmp.append(state->dragCenterLine);
        sb::pushTimelineLineBatch(snapshot, tmp);
    }
    if (state->hasEntryMarker) {
        QVector<miacode::timeline::TimelineSceneTriangle> tmp;
        tmp.append(state->entryMarker);
        sb::pushTimelineTriangleBatch(snapshot, tmp);
    }
    if (state->hasCursorMarker) {
        QVector<miacode::timeline::TimelineSceneTriangle> tmp;
        tmp.append(state->cursorMarker);
        sb::pushTimelineTriangleBatch(snapshot, tmp);
    }
}

}  // namespace miacode::sources::timeline
