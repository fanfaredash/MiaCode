#pragma once

#include "core/scene/PreviewCircleDescriptor.h"
#include "core/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

struct PreviewMuriActionLayerState {
    PreviewCircleDescriptors circles;
};

PreviewMuriActionLayerState buildPreviewMuriActionLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
