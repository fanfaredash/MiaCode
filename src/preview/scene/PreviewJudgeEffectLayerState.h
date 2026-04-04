#pragma once

#include <QSharedPointer>
#include <QVector>

#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewJudgeEffectLayerState {
    PreviewSpriteDescriptors sprites;
    QVector<QSharedPointer<QImage>> ownedImages;
};

PreviewJudgeEffectLayerState buildPreviewJudgeEffectLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
