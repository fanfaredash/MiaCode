#pragma once

#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewTouchJudgeLayerState {
    PreviewSpriteDescriptors sprites;
};

PreviewTouchJudgeLayerState buildPreviewTouchJudgeLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
