#include "preview/quick_scene/PreviewQuickTouchJudgeLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/scene/PreviewTouchJudgeLayerState.h"

QSGNode* PreviewQuickTouchJudgeLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    return buildPreviewSpriteNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewTouchJudgeLayerState(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        ).sprites,
        window,
        textures,
        state.playheadSeconds,
        "touch_judge"
    );
}
