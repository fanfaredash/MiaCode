#include "preview/quick_scene/PreviewQuickChartReviewLayer.h"

#include "preview/quick_scene/PreviewQuickSpriteNodes.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewChartReviewLayerState.h"
#include "preview/scene/PreviewSceneGeometry.h"

QSGNode* PreviewQuickChartReviewLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    const bool showSlideJudgeOverlay = state.muriRenderOptions.showChartReviewSlideJudgeOverlay;
    const bool showSimpleJudgeOverlay = state.muriRenderOptions.showChartReviewSimpleJudgeOverlay;
    const TimelineNoteMarker* noteMarkerData = state.noteMarkers.constData();
    const qsizetype noteMarkerCount = state.noteMarkers.size();
    if (cachedNoteMarkersData_ != noteMarkerData
        || cachedNoteMarkersSize_ != noteMarkerCount
        || cachedShowSlideJudgeOverlay_ != showSlideJudgeOverlay
        || cachedShowSimpleJudgeOverlay_ != showSimpleJudgeOverlay) {
        preparedEventsCache_ = miacode::preview::scene::buildPreviewChartReviewPreparedEvents(
            state.noteMarkers,
            showSlideJudgeOverlay,
            showSimpleJudgeOverlay
        );
        cachedNoteMarkersData_ = noteMarkerData;
        cachedNoteMarkersSize_ = noteMarkerCount;
        cachedShowSlideJudgeOverlay_ = showSlideJudgeOverlay;
        cachedShowSimpleJudgeOverlay_ = showSimpleJudgeOverlay;
    }

    return buildPreviewSpriteNodeTree(
        oldNode,
        miacode::preview::scene::buildPreviewChartReviewLayerSprites(
            state,
            miacode::preview::scene::playfieldRectForStage(
                miacode::preview::scene::stageRectForSize(renderSize),
                state.render.layoutSquareScale
            ),
            &preparedEventsCache_
        ),
        window,
        textures,
        state.playheadSeconds,
        "chart_review"
    );
}
