#pragma once

#include <QSharedPointer>
#include <QVector>

#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewJudgeEffectLayerState {
    PreviewSpriteDescriptors sprites;
    QVector<QSharedPointer<QImage>> ownedImages;
};

PreviewJudgeEffectLayerState buildPreviewJudgeEffectLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
