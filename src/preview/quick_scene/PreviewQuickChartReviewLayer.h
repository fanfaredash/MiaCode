#pragma once

#include "preview/scene/PreviewChartReviewLayerState.h"

class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;
namespace miacode::preview::scene {
class PreviewPreparedSceneCache;
struct PreviewLayerWindowCursor;
}

class PreviewQuickChartReviewLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::preview::scene::PreviewFrameState& state,
        const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
        const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
        const QSize& renderSize,
        QQuickWindow* window,
        PreviewTextureRepository* textures
    ) const;
};
