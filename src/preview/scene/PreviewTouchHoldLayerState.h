#pragma once

#include "preview/scene/PreviewArcDescriptor.h"
#include "preview/scene/PreviewActiveMarkerView.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

#include <QSharedPointer>

namespace miacode::preview::scene {

struct PreviewTouchHoldLayerState {
    PreviewSpriteDescriptors sprites;
    PreviewArcDescriptors arcs;
    QVector<QSharedPointer<QImage>> ownedImages;
};

PreviewTouchHoldLayerState buildPreviewTouchHoldLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
