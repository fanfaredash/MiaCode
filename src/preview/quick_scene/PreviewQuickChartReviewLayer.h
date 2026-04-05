#pragma once

#include "preview/scene/PreviewChartReviewLayerState.h"

class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;

class PreviewQuickChartReviewLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::preview::scene::PreviewFrameState& state,
        const QSize& renderSize,
        QQuickWindow* window,
        PreviewTextureRepository* textures
    ) const;

private:
    mutable const TimelineNoteMarker* cachedNoteMarkersData_ = nullptr;
    mutable qsizetype cachedNoteMarkersSize_ = -1;
    mutable bool cachedShowSlideJudgeOverlay_ = false;
    mutable bool cachedShowSimpleJudgeOverlay_ = false;
    mutable miacode::preview::scene::PreviewChartReviewPreparedEvents preparedEventsCache_;
};
