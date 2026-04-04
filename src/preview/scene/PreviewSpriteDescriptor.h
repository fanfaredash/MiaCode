#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

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
    bool cacheable = true;
};

using PreviewSpriteDescriptors = QVector<PreviewSpriteDescriptor>;

}  // namespace miacode::preview::scene
