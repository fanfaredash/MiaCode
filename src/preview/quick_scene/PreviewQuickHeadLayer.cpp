#include "preview/quick_scene/PreviewQuickHeadLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewHeadLayerState.h"
#include "preview/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickHeadLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    const miacode::preview::scene::PreviewHeadLayerState layerState =
        miacode::preview::scene::buildPreviewHeadLayerState(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        );
    return buildPreviewSpriteNodeTree(oldNode, layerState.sprites, window, textures);
}
