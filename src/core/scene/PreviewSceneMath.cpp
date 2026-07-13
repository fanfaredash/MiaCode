#include "core/scene/PreviewSceneMath.h"

#include "core/scene/PreviewSceneConstants.h"

#include <array>
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

qreal touchPadAngleDegrees(QChar ring, int lane)
{
    if (ring == QChar('D') || ring == QChar('E')) {
        return -90.0 + (lane - 1) * kLaneAngleStepDegrees;
    }
    return kLaneUnitVectorBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
}

QPointF touchPadLogicalPoint(QChar ring, int lane)
{
    const QChar normalizedRing = ring.toUpper();
    if (normalizedRing == QChar('C')) {
        return QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter);
    }
    if (lane < 1 || lane > 8) {
        return QPointF();
    }

    qreal distance = 0.0;
    switch (normalizedRing.toLatin1()) {
    case 'A': distance = kTouchPadDistanceA; break;
    case 'B': distance = kTouchPadDistanceB; break;
    case 'D': distance = kTouchPadDistanceD; break;
    case 'E': distance = kTouchPadDistanceE; break;
    default: return QPointF();
    }

    const qreal angleRad = qDegreesToRadians(touchPadAngleDegrees(normalizedRing, lane));
    return QPointF(
        kLogicalCanvasCenter + qCos(angleRad) * distance,
        kLogicalCanvasCenter + qSin(angleRad) * distance
    );
}

QPointF touchPadLogicalPoint(const QString& padToken)
{
    const QString normalized = padToken.trimmed().toUpper();
    if (normalized == QLatin1String("C")) {
        return touchPadLogicalPoint(QChar('C'), 0);
    }
    if (normalized.size() != 2) {
        return QPointF();
    }
    const QChar ring = normalized.at(0);
    const int lane = normalized.at(1).digitValue();
    return touchPadLogicalPoint(ring, lane);
}

QString touchPadTokenAtLogicalPoint(const QPointF& logicalPoint)
{
    const auto squaredDistanceTo = [logicalPoint](const QPointF& center) {
        const QPointF delta = logicalPoint - center;
        return QPointF::dotProduct(delta, delta);
    };

    const qreal centerDistanceSquared = squaredDistanceTo(touchPadLogicalPoint(QChar('C'), 0));
    QString bestPad = centerDistanceSquared <= kTouchPadCenterHitRadiusLogical * kTouchPadCenterHitRadiusLogical
        ? QStringLiteral("C")
        : QString();
    qreal bestDistanceSquared = bestPad.isEmpty()
        ? kTouchPadHitRadiusLogical * kTouchPadHitRadiusLogical
        : centerDistanceSquared;

    static constexpr std::array<char, 4> kRings = {{'A', 'B', 'D', 'E'}};
    for (const char ringChar : kRings) {
        const QChar ring = QChar::fromLatin1(ringChar);
        for (int lane = 1; lane <= 8; ++lane) {
            const qreal distanceSquared = squaredDistanceTo(touchPadLogicalPoint(ring, lane));
            if (distanceSquared > kTouchPadHitRadiusLogical * kTouchPadHitRadiusLogical
                || distanceSquared >= bestDistanceSquared) {
                continue;
            }
            bestDistanceSquared = distanceSquared;
            bestPad = padTokenForRing(ring, lane);
        }
    }

    return bestPad;
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
