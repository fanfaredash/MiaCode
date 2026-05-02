#pragma once

#include "core/scene/PreviewArcDescriptor.h"
#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewSpriteDescriptor.h"

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
