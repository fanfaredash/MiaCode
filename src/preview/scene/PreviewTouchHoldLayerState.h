#pragma once

#include "preview/scene/PreviewArcDescriptor.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewTouchHoldLayerState {
    PreviewSpriteDescriptors sprites;
    PreviewArcDescriptors arcs;
};

PreviewTouchHoldLayerState buildPreviewTouchHoldLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
