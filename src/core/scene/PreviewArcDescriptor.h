#pragma once

#include <QPointF>
#include <QVector>

class QImage;

namespace miacode::preview::scene {

struct PreviewArcDescriptor {
    const QImage* image = nullptr;
    QPointF center;
    qreal width = 0.0;
    qreal height = 0.0;
    qreal startDegrees = 90.0;
    qreal sweepDegrees = 0.0;
    qreal opacity = 1.0;
    bool cacheable = true;
};

using PreviewArcDescriptors = QVector<PreviewArcDescriptor>;

}  // namespace miacode::preview::scene
