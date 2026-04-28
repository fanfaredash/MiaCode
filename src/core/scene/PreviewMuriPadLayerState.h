#pragma once

#include "core/scene/PreviewCircleDescriptor.h"
#include "core/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

struct PreviewMuriPadLayerState {
    PreviewCircleDescriptors circles;
};

PreviewMuriPadLayerState buildPreviewMuriPadLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
