#include "preview/scene/PreviewSceneMath.h"

#include "preview/scene/PreviewSceneConstants.h"

#include <QtMath>

namespace miacode::preview::scene {

QPointF laneUnitVector(int lane)
{
    if (lane < 1 || lane > 8) {
        return QPointF(0.0, 0.0);
    }
    const qreal angleDeg = kLaneUnitVectorBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
    const qreal angleRad = qDegreesToRadians(angleDeg);
    return QPointF(qCos(angleRad), qSin(angleRad));
}

QPointF mapLogicalPointToRect(const QPointF& logicalPoint, const QRectF& targetRect)
{
    const qreal scale = targetRect.width() / kLogicalCanvasSize;
    return QPointF(
        targetRect.left() + logicalPoint.x() * scale,
        targetRect.top() + logicalPoint.y() * scale
    );
}

qreal mapLogicalLengthToRect(qreal logicalLength, const QRectF& targetRect)
{
    return logicalLength * (targetRect.width() / kLogicalCanvasSize);
}

int wrappedLane(int lane)
{
    const int normalized = (lane - 1) % 8;
    return (normalized < 0 ? normalized + 8 : normalized) + 1;
}

QString padTokenForRing(QChar ring, int lane)
{
    return QStringLiteral("%1%2").arg(ring).arg(wrappedLane(lane));
}

qreal laneRotationDegrees(int lane)
{
    if (lane < 1 || lane > 8) {
        return 0.0;
    }
    return kLaneRotationBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
}

qreal laneRotationDegreesForIndex(qreal laneIndex)
{
    return (kLaneRotationBaseDegrees - kLaneAngleStepDegrees) + laneIndex * kLaneAngleStepDegrees;
}

quint64 touchPointKey(const QPointF& point)
{
    const qint32 x = qRound(point.x() * kPreviewPointHashScale);
    const qint32 y = qRound(point.y() * kPreviewPointHashScale);
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32)
        | static_cast<quint64>(static_cast<quint32>(y));
}

quint64 touchRegionKey(const TimelineNoteMarker& marker)
{
    if (!marker.touchPad.isEmpty()) {
        return kTouchPadRegionHashFlag | static_cast<quint64>(qHash(marker.touchPad.toUpper()));
    }
    return touchPointKey(marker.touchPoint);
}

}  // namespace miacode::preview::scene
