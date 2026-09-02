#pragma once

#include "timeline/TimelineSceneState.h"

class QQuickWindow;
class QSGNode;

class TimelineQuickHeaderLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::timeline::TimelineSceneState& state,
        QQuickWindow* window) const;
};
