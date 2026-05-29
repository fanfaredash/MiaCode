#include "tools/muri/MuriAnalyzerGeometry.h"

#include <cmath>
#include <limits>

#include <QtGlobal>

#include "common/MuriConfig.h"

namespace miacode::muri::detail {

namespace {
constexpr double kPadTimeEpsilon = 1e-6;
}  // namespace

QVector<QPointF> centeredPathPoints(const QVector<QPointF>& points)
{
    QVector<QPointF> result;
    result.reserve(points.size());
    for (const QPointF& point : points) {
        result.append(QPointF(
            miacode::muri::kLogicalCanvasCenter + point.x(),
            miacode::muri::kLogicalCanvasCenter + point.y()
        ));
    }
    return result;
}

double pointDistance(const QPointF& a, const QPointF& b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return std::sqrt(dx * dx + dy * dy);
}

QPointF normalizedDirection(const QPointF& vector)
{
    const double length = std::sqrt(vector.x() * vector.x() + vector.y() * vector.y());
    if (length <= kPadTimeEpsilon) {
        return QPointF();
    }
    return QPointF(vector.x() / length, vector.y() / length);
}

double tangentDelta(const QPointF& a, const QPointF& b)
{
    return pointDistance(a, b);
}

CoveringCircle coveringCircleFromTwoPoints(const QPointF& a, const QPointF& b)
{
    CoveringCircle circle;
    circle.center = QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
    circle.radius = pointDistance(a, b) * 0.5;
    circle.valid = true;
    return circle;
}

CoveringCircle coveringCircleFromThreePoints(const QPointF& a, const QPointF& b, const QPointF& c)
{
    const double ax = a.x();
    const double ay = a.y();
    const double bx = b.x();
    const double by = b.y();
    const double cx = c.x();
    const double cy = c.y();
    const double denominator =
        2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (qAbs(denominator) <= kPadTimeEpsilon) {
        return CoveringCircle{};
    }

    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;

    CoveringCircle circle;
    circle.center = QPointF(
        (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / denominator,
        (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / denominator);
    circle.radius = pointDistance(circle.center, a);
    circle.valid = true;
    return circle;
}

bool coveringCircleContainsAll(const CoveringCircle& circle, const QVector<QPointF>& points)
{
    if (!circle.valid) {
        return false;
    }
    for (const QPointF& point : points) {
        if (pointDistance(circle.center, point) > circle.radius + 1e-3) {
            return false;
        }
    }
    return true;
}

CoveringCircle smallestCoveringCircle(const QVector<QPointF>& points)
{
    if (points.isEmpty()) {
        return CoveringCircle{};
    }

    CoveringCircle best;
    best.center = points.constFirst();
    best.radius = 0.0;
    best.valid = true;
    if (coveringCircleContainsAll(best, points)) {
        return best;
    }

    best.radius = std::numeric_limits<double>::infinity();
    for (int i = 0; i < points.size(); ++i) {
        CoveringCircle circle;
        circle.center = points.at(i);
        circle.radius = 0.0;
        circle.valid = true;
        if (coveringCircleContainsAll(circle, points) && circle.radius < best.radius) {
            best = circle;
        }
    }

    for (int i = 0; i < points.size(); ++i) {
        for (int j = i + 1; j < points.size(); ++j) {
            const CoveringCircle circle = coveringCircleFromTwoPoints(points.at(i), points.at(j));
            if (coveringCircleContainsAll(circle, points) && circle.radius < best.radius) {
                best = circle;
            }
        }
    }

    for (int i = 0; i < points.size(); ++i) {
        for (int j = i + 1; j < points.size(); ++j) {
            for (int k = j + 1; k < points.size(); ++k) {
                const CoveringCircle circle =
                    coveringCircleFromThreePoints(points.at(i), points.at(j), points.at(k));
                if (coveringCircleContainsAll(circle, points) && circle.radius < best.radius) {
                    best = circle;
                }
            }
        }
    }

    return best.valid ? best : CoveringCircle{};
}

}  // namespace miacode::muri::detail
