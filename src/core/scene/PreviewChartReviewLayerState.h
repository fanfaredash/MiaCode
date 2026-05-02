#pragma once

#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewJudgeOverlayShared.h"
#include "core/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

enum class PreviewChartReviewPreparedKind {
    SimpleNormal,
    SimpleBreak,
    StraightLeft,
    StraightRight,
    CircleLeft,
    CircleRight,
    WifiUp,
    WifiDown,
};

struct PreviewChartReviewPreparedEvent {
    PreviewChartReviewPreparedKind kind = PreviewChartReviewPreparedKind::SimpleNormal;
    qreal second = -1.0;
    PreviewJudgeOverlayPlacement placement;
};

using PreviewChartReviewPreparedEvents = QVector<PreviewChartReviewPreparedEvent>;

PreviewChartReviewPreparedEvents buildPreviewChartReviewPreparedEvents(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool showSlideJudgeOverlay,
    bool showSimpleJudgeOverlay
);

PreviewSpriteDescriptors buildPreviewChartReviewLayerSprites(
    const PreviewFrameState& state,
    const QRectF& playfieldRect,
    const PreviewChartReviewPreparedEvents* preparedEvents = nullptr
);

}  // namespace miacode::preview::scene
