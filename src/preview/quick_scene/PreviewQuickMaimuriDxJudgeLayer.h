#pragma once

#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewJudgeOverlayShared.h"

class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;
namespace miacode::preview::scene {
class PreviewPreparedSceneCache;
struct PreviewLayerWindowCursor;
}

class PreviewQuickMaimuriDxJudgeLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::preview::scene::PreviewFrameState& state,
        const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
        const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
        const QSize& renderSize,
        QQuickWindow* window,
        PreviewTextureRepository* textures,
        miacode::preview::scene::SlideJudgeRenderGroup group =
            miacode::preview::scene::SlideJudgeRenderGroup::All
    ) const;
};
