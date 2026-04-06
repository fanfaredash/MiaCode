#include "preview/quick_scene/PreviewQuickTouchLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewSceneGeometry.h"
#include "preview/scene/PreviewTouchLayerState.h"

QSGNode* PreviewQuickTouchLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    miacode::preview::scene::PreviewFrameState filteredState = state;
    QVector<TimelineNoteMarker> filteredMarkers;
    if (preparedCache != nullptr && cursor != nullptr) {
        preparedCache->collectMarkers(
            state.noteMarkers,
            preparedCache->touchLayer(),
            cursor->activePreparedIndices,
            &filteredMarkers
        );
        filteredState.noteMarkers = filteredMarkers;
    }
    const miacode::preview::scene::PreviewTouchLayerState layerState =
        miacode::preview::scene::buildPreviewTouchLayerState(
            filteredState,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        );
    return buildPreviewSpriteNodeTree(
        oldNode,
        layerState.sprites,
        window,
        textures,
        filteredState.playheadSeconds,
        "touch"
    );
}
