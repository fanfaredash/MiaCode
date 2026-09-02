#pragma once

#include "LayoutRingConfig.h"

#include <QRectF>

#include <algorithm>
#include <cmath>

namespace miacode::preview_video {

inline constexpr double kBackgroundBrightnessDefault = 0.50;
inline constexpr double kBackgroundBrightnessInnerDefault = 0.20;
inline constexpr double kLayoutSquareScaleDefault = 0.95;
inline constexpr double kLayoutSquareScaleMin = 0.5;
inline constexpr double kLayoutSquareScaleMax = 1.0;
inline constexpr double kLayoutSquareScaleStep = 0.05;
inline constexpr bool kSmoothBrightnessDefault = false;
inline constexpr double kSmoothBrightnessBlendMidpoint = 0.5;
inline constexpr double kGeometryEpsilon = 1e-6;

inline double clampLayoutSquareScale(double value)
{
    return std::clamp(value, kLayoutSquareScaleMin, kLayoutSquareScaleMax);
}

inline double normalizedLayoutSquareScale(double value)
{
    const double clamped = clampLayoutSquareScale(value);
    const double steps = std::round((clamped - kLayoutSquareScaleMin) / kLayoutSquareScaleStep);
    return clampLayoutSquareScale(kLayoutSquareScaleMin + steps * kLayoutSquareScaleStep);
}

inline double layoutSquareSideForStage(const QRectF& stageRect, double layoutSquareScale)
{
    const double shortSide = std::max(
        1.0,
        std::min(std::max(1.0, stageRect.width()), std::max(1.0, stageRect.height()))
    );
    return std::max(1.0, shortSide * normalizedLayoutSquareScale(layoutSquareScale));
}

inline QRectF centeredLayoutRectForStage(const QRectF& stageRect, double layoutSquareScale)
{
    const double side = layoutSquareSideForStage(stageRect, layoutSquareScale);
    const QPointF center = stageRect.center();
    return QRectF(
        center.x() - side * 0.5,
        center.y() - side * 0.5,
        side,
        side
    );
}

inline double effectiveLayoutRingDiameterRatio(double layoutRingDiameterRatio)
{
    return std::clamp(
        layoutRingDiameterRatio,
        miacode::layout_ring::kPlayfieldRatioMin,
        miacode::layout_ring::kRenderRatioMax
    );
}

inline double innerBrightnessOuterRadius(double layoutSquareSide)
{
    return std::max(1.0, layoutSquareSide * 0.5);
}

inline double layoutRingRadius(double layoutSquareSide, double layoutRingDiameterRatio)
{
    return innerBrightnessOuterRadius(layoutSquareSide) * effectiveLayoutRingDiameterRatio(layoutRingDiameterRatio);
}

inline double smoothBrightnessBlendStartRadius(double layoutSquareSide, double layoutRingDiameterRatio)
{
    const double outerRadius = innerBrightnessOuterRadius(layoutSquareSide);
    const double ringRadius = layoutRingRadius(layoutSquareSide, layoutRingDiameterRatio);
    return (outerRadius + ringRadius) * kSmoothBrightnessBlendMidpoint;
}

inline double smoothStep01(double t)
{
    const double x = std::clamp(t, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

inline double dimAlphaForRadius(
    double radius,
    double outerAlpha,
    double innerAlpha,
    double layoutSquareSide,
    double layoutRingDiameterRatio,
    bool smoothBrightness
)
{
    const double outerRadius = innerBrightnessOuterRadius(layoutSquareSide);
    if (radius >= outerRadius) {
        return outerAlpha;
    }
    if (!smoothBrightness) {
        return innerAlpha;
    }

    const double blendStart = smoothBrightnessBlendStartRadius(layoutSquareSide, layoutRingDiameterRatio);
    if (radius <= blendStart) {
        return innerAlpha;
    }

    const double blendSpan = outerRadius - blendStart;
    if (blendSpan <= kGeometryEpsilon) {
        return innerAlpha;
    }
    const double t = smoothStep01((radius - blendStart) / blendSpan);
    return innerAlpha + (outerAlpha - innerAlpha) * t;
}

}  // namespace miacode::preview_video
