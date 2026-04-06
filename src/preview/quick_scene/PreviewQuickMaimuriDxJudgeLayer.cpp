#include "preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewMaimuriDxJudgeLayerState.h"
#include "preview/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickMaimuriDxJudgeLayer::updateNode(
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
    QVector<MuriJudgeSpriteEvent> activeEvents;
    QVector<int> markerIndices;
    QVector<TimelineNoteMarker> filteredMarkers;
    if (preparedCache != nullptr && cursor != nullptr) {
        preparedCache->collectMaimuriDxJudgeData(cursor->activePreparedIndices, &activeEvents, &markerIndices);
        filteredMarkers.reserve(markerIndices.size());
        for (int markerIndex : markerIndices) {
            if (markerIndex >= 0 && markerIndex < state.noteMarkers.size()) {
                filteredMarkers.append(state.noteMarkers.at(markerIndex));
            }
        }
        filteredState.noteMarkers = filteredMarkers;
        filteredState.muriAnalysisReport.judgeSpriteEvents = activeEvents;
        filteredState.muriAnalysisReport.sourceSignature.clear();
    }
    return buildPreviewSpriteNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewMaimuriDxJudgeLayerSprites(
            filteredState,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            )
        ),
        window,
        textures,
        state.playheadSeconds,
        "maimuri_dx_judge"
    );
}
