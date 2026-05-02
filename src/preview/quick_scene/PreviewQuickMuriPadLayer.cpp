#include "preview/quick_scene/PreviewQuickMuriPadLayer.h"

#include "preview/quick_scene/PreviewQuickCircleNodes.h"
#include "core/scene/PreviewMuriPadLayerState.h"
#include "core/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickMuriPadLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    Q_UNUSED(window);
    Q_UNUSED(textures);
    return buildPreviewCircleNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewMuriPadLayerState(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        ).circles
    );
}
