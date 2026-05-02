#pragma once

#include <QPointF>
#include <QRectF>

#include "timeline/TimelineData.h"

namespace miacode::preview::scene {

QPointF laneUnitVector(int lane);
QPointF mapLogicalPointToRect(const QPointF& logicalPoint, const QRectF& targetRect);
qreal mapLogicalLengthToRect(qreal logicalLength, const QRectF& targetRect);
int wrappedLane(int lane);
QString padTokenForRing(QChar ring, int lane);
qreal laneRotationDegrees(int lane);
qreal laneRotationDegreesForIndex(qreal laneIndex);
quint64 touchPointKey(const QPointF& point);
quint64 touchRegionKey(const TimelineNoteMarker& marker);

}  // namespace miacode::preview::scene
