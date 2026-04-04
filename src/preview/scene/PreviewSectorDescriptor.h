#pragma once

#include <QColor>
#include <QPointF>
#include <QVector>

namespace miacode::preview::scene {

struct PreviewSectorDescriptor {
    QPointF center;
    qreal innerRadius = 0.0;
    qreal outerRadius = 0.0;
    qreal startDegrees = 0.0;
    qreal sweepDegrees = 0.0;
    QColor color;
};

using PreviewSectorDescriptors = QVector<PreviewSectorDescriptor>;

}  // namespace miacode::preview::scene
