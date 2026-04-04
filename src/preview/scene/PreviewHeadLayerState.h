#pragma once

#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewHeadLayerState {
    PreviewSpriteDescriptors sprites;
    QVector<QImage> ownedImages;
};

PreviewHeadLayerState buildPreviewHeadLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
