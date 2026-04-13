#pragma once

#include "preview/scene/PreviewActiveMarkerView.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewTouchJudgeLayerState {
    PreviewSpriteDescriptors sprites;
};

PreviewTouchJudgeLayerState buildPreviewTouchJudgeLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
