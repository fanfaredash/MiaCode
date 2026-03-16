#pragma once

#include <functional>

#include <QString>

namespace miacode::chart_transform {

QString toggleBreakForSelection(const QString& input, int* changedCount = nullptr);
QString toggleExForSelection(const QString& input, int* changedCount = nullptr);
QString toggleFireworkForSelection(const QString& input, int* changedCount = nullptr);
QString randomRotateForSelection(const QString& input, int* changedCount = nullptr);
QString randomRotateForSelection(
    const QString& input,
    const std::function<int()>& nextStep,
    int* changedCount = nullptr);

}  // namespace miacode::chart_transform
