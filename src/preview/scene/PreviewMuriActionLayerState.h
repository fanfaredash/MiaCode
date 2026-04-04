#pragma once

#include "preview/scene/PreviewCircleDescriptor.h"
#include "preview/scene/PreviewFrameState.h"

namespace miacode::preview::scene {

struct PreviewMuriActionLayerState {
    PreviewCircleDescriptors circles;
};

PreviewMuriActionLayerState buildPreviewMuriActionLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
