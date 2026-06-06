#include "preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "core/scene/PreviewPreparedSceneCache.h"
#include "core/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "core/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickMaimuriDxJudgeLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const miacode::preview::scene::PreviewPreparedSceneCache* preparedCache,
    const miacode::preview::scene::PreviewLayerWindowCursor* cursor,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures,
    miacode::preview::scene::SlideJudgeRenderGroup group
) const
{
    QVector<MuriJudgeSpriteEvent> activeEvents = state.muriAnalysisReport.judgeSpriteEvents;
    if (preparedCache != nullptr && cursor != nullptr) {
        QVector<int> markerIndices;
        preparedCache->collectMaimuriDxJudgeData(cursor->activePreparedIndices, &activeEvents, &markerIndices);
    }
    const miacode::preview::scene::PreviewActiveMarkerView activeMarkers(state.noteMarkers);
    return buildPreviewSpriteNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewMaimuriDxJudgeLayerSprites(
            state,
            activeMarkers,
            activeEvents,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            ),
            group
        ),
        window,
        textures,
        state.playheadSeconds,
        "maimuri_dx_judge"
    );
}
