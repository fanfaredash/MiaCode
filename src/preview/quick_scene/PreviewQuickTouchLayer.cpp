#include "preview/quick_scene/PreviewQuickTouchLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/scene/PreviewTouchLayerState.h"

QSGNode* PreviewQuickTouchLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    return buildPreviewSpriteNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewTouchLayerSprites(
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
