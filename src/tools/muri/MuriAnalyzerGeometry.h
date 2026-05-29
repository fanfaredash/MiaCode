#pragma once

#include <QPointF>
#include <QVector>

// Pure geometry helpers extracted from the MuriAnalyzer monolith (2026-05-29
// god-file decomposition, step 1). These depend only on QPointF / QVector and
// have no coupling to the analyzer's runtime state, so they live in their own
// translation unit and can be unit-tested in isolation. Internal to the Muri
// implementation — namespace miacode::muri::detail.
namespace miacode::muri::detail {

// Smallest-enclosing-circle support for multi-touch hand-action clustering.
struct CoveringCircle {
    QPointF center;
    double radius = 0.0;
    bool valid = false;
};

// Offsets each chart-space point by the logical canvas center.
QVector<QPointF> centeredPathPoints(const QVector<QPointF>& points);

double pointDistance(const QPointF& a, const QPointF& b);
QPointF normalizedDirection(const QPointF& vector);
double tangentDelta(const QPointF& a, const QPointF& b);

CoveringCircle coveringCircleFromTwoPoints(const QPointF& a, const QPointF& b);
CoveringCircle coveringCircleFromThreePoints(const QPointF& a, const QPointF& b, const QPointF& c);
bool coveringCircleContainsAll(const CoveringCircle& circle, const QVector<QPointF>& points);
CoveringCircle smallestCoveringCircle(const QVector<QPointF>& points);

}  // namespace miacode::muri::detail
