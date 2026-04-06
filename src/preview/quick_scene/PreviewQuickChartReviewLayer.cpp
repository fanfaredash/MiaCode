#include "preview/quick_scene/PreviewQuickChartReviewLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewChartReviewLayerState.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickChartReviewLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    miacode::preview::scene::PreviewChartReviewPreparedEvents activeEvents;
    if (preparedCache != nullptr && cursor != nullptr) {
        preparedCache->collectChartReviewEvents(cursor->activePreparedIndices, &activeEvents);
    }

    return buildPreviewSpriteNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewChartReviewLayerSprites(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            ),
            &activeEvents
        ),
        window,
        textures,
        state.playheadSeconds,
        "chart_review"
    );
}
