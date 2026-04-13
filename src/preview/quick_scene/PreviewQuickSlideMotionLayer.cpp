#include "preview/quick_scene/PreviewQuickSlideMotionLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/scene/PreviewSlideMotionLayerState.h"

QSGNode* PreviewQuickSlideMotionLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    const miacode::preview::scene::PreviewActiveMarkerView activeMarkers =
        preparedCache != nullptr && cursor != nullptr
        ? miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers, preparedCache->slideLikeLayer(), *cursor)
        : miacode::preview::scene::PreviewActiveMarkerView(state.noteMarkers);
    const auto layerState = miacode::preview::scene::buildPreviewSlideMotionLayerState(
        state,
        activeMarkers,
        miacode::preview::scene::playfieldRectForStage(
            miacode::preview::scene::stageRectForSize(renderSize),
            state.render.layoutSquareScale
        )
    );
    return buildPreviewSpriteNodeTree(oldNode, layerState.sprites, window, textures, state.playheadSeconds, "slide_motion");
}
