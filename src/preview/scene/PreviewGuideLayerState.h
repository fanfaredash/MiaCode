#pragma once

#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

PreviewSpriteDescriptors buildPreviewGuideLayerSprites(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
