#pragma once

#include "timeline/TimelineSceneState.h"

class QQuickWindow;
class QSGNode;
class TimelineQuickTextureCache;

class TimelineQuickNotesLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::timeline::TimelineSceneState& state,
        QQuickWindow* window,
        TimelineQuickTextureCache* textures) const;
};
