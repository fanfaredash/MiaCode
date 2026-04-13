#pragma once

#include "preview/scene/PreviewActiveMarkerView.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

PreviewSpriteDescriptors buildPreviewMaimuriDxJudgeLayerSprites(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QVector<MuriJudgeSpriteEvent>& activeEvents,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
