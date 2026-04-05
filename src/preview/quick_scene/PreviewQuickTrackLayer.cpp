#include "preview/quick_scene/PreviewQuickTrackLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/scene/PreviewTrackLayerState.h"

QSGNode* PreviewQuickTrackLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    const auto layerState = miacode::preview::scene::buildPreviewTrackLayerState(
        state,
        miacode::preview::scene::playfieldRectForStage(
            miacode::preview::scene::stageRectForSize(renderSize),
            state.render.layoutSquareScale
        )
    );
    return buildPreviewSpriteNodeTree(oldNode, layerState.sprites, window, textures, state.playheadSeconds, "track");
}
