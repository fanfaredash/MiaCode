#include "preview/quick_scene/PreviewQuickMuriActionLayer.h"

#include "preview/quick_scene/PreviewQuickCircleNodes.h"
#include "preview/scene/PreviewMuriActionLayerState.h"
#include "preview/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickMuriActionLayer::updateNode(
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
        miacode::preview::scene::buildPreviewMuriActionLayerState(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        ).circles
    );
}
