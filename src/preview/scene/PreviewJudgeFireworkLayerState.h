#pragma once

#include <QSharedPointer>

#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSectorDescriptor.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewJudgeFireworkLayerState {
    PreviewSectorDescriptors sectors;
    PreviewSpriteDescriptors sprites;
    QVector<QSharedPointer<QImage>> ownedImages;
    QPointF clipCenter;
    qreal clipRadius = 0.0;
};

PreviewJudgeFireworkLayerState buildPreviewJudgeFireworkLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
);

}  // namespace miacode::preview::scene
