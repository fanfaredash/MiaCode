#pragma once

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

PreviewSpriteDescriptors buildPreviewGuideLayerSprites(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
