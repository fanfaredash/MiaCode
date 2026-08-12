#pragma once

#include "timeline/TimelineSceneState.h"

class QSGNode;

class TimelineQuickWaveformLayer
{
public:
    // devicePixelRatio: the scroll translate is snapped to the PHYSICAL pixel grid, which is
    // where rasterisation actually happens. See the note in the .cpp.
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::timeline::TimelineSceneState& state,
        qreal devicePixelRatio) const;
};
