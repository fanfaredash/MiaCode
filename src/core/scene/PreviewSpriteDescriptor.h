#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

#include "core/scene/PreviewAnimatedSpriteHelpers.h"

class QImage;

namespace miacode::preview::scene {

struct PreviewSpriteDescriptor {
    const QImage* image = nullptr;
    QPointF center;
    qreal width = 0.0;
    qreal height = 0.0;
    qreal rotationDegrees = 0.0;
    qreal opacity = 1.0;
    QRectF sourceRect;
    PreviewAnimatedSpriteEffect effect = PreviewAnimatedSpriteEffect::None;
    bool cacheable = true;
};

using PreviewSpriteDescriptors = QVector<PreviewSpriteDescriptor>;

}  // namespace miacode::preview::scene
