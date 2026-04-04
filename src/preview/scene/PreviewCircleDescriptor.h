#pragma once

#include <QColor>
#include <QPointF>
#include <QVector>

namespace miacode::preview::scene {

struct PreviewCircleDescriptor {
    QPointF center;
    qreal radiusX = 0.0;
    qreal radiusY = 0.0;
    QColor fillColor;
    QColor strokeColor;
    qreal strokeWidth = 0.0;
};

using PreviewCircleDescriptors = QVector<PreviewCircleDescriptor>;

}  // namespace miacode::preview::scene
