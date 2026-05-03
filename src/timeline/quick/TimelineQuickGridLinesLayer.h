#pragma once

#include "timeline/TimelineSceneState.h"

class QQuickWindow;
class QSGNode;

// Beta21-fix7 — dedicated slot for grid lines (bar lines + per-comma
// note ticks). Previously these were rendered inside TimelineQuickHeaderLayer
// alongside lane overlays and header markers, which let Qt's QSG batched
// renderer reorder draws within the same slot — at alpha 255 a bar line
// would land in the opaque batch and could be drawn BEFORE the waveform's
// opaque batch despite a higher scene-graph z position. Promoting grid
// lines to their own slot, placed AFTER both waveformLayer and headerLayer,
// guarantees they cannot be reordered against the waveform regardless of
// material/blend flags.
//
// Slot ordering in TimelineQuickItem (post-fix):
//   0: gridLayer       (basebackgroundRects)
//   1: waveformLayer
//   2: headerLayer     (lane overlays + header markers — NO grid lines)
//   3: gridLinesLayer  ← THIS LAYER (grid lines, with clip + scroll transform)
//   4: notesLayer
//   5: overlayLayer
class TimelineQuickGridLinesLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::timeline::TimelineSceneState& state,
        QQuickWindow* window) const;
};
