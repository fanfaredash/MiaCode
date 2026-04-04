#include "preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "preview/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickMaimuriDxJudgeLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    return buildPreviewSpriteNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewMaimuriDxJudgeLayerSprites(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        ),
        window,
        textures
    );
}
