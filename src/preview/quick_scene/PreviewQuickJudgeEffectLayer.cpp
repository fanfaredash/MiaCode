#include "preview/quick_scene/PreviewQuickJudgeEffectLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewJudgeEffectLayerState.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickJudgeEffectLayer::updateNode(
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
            preparedCache->judgeEffectLayer(),
            cursor->activePreparedIndices,
            &filteredMarkers
        );
        filteredState.noteMarkers = filteredMarkers;
    }
    const auto layerState = miacode::preview::scene::buildPreviewJudgeEffectLayerState(
        filteredState,
        miacode::preview::scene::playfieldRectForStage(
            miacode::preview::scene::stageRectForSize(renderSize),
            state.render.layoutSquareScale
        )
    );
    return buildPreviewSpriteNodeTree(oldNode, layerState.sprites, window, textures, state.playheadSeconds, "judge_effect");
}
