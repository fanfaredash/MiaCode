#pragma once

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewSpriteDescriptor.h"

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
