#pragma once

#include <functional>

#include <QString>

namespace miacode::chart_transform {

enum class ChartTransformOp {
    MirrorLeftRight,
    MirrorUpDown,
    Rotate180,
    Rotate45CounterClockwise,
    Rotate45Clockwise,
};

QString transformChartText(const QString& input, ChartTransformOp op, int* changedCount = nullptr);
QString transformChartSelectionText(const QString& input, ChartTransformOp op, int* changedCount = nullptr);
QString toggleBreakForSelection(const QString& input, int* changedCount = nullptr);
QString toggleExForSelection(const QString& input, int* changedCount = nullptr);
QString toggleFireworkForSelection(const QString& input, int* changedCount = nullptr);
QString randomRotateForSelection(const QString& input, int* changedCount = nullptr);
QString randomRotateForSelection(
    const QString& input,
    const std::function<int()>& nextStep,
    int* changedCount = nullptr);

}  // namespace miacode::chart_transform
