#pragma once

#include <QJsonObject>
#include <QString>

#include "simai/document/SimaiTimingMetadata.h"

namespace miacode::chart_transform {

struct ChartNormalizationOptions {
    bool startAtNewMeasure = true;
    bool reduceTo384Grid = true;
};

struct ChartNormalizationResult {
    bool ok = false;
    QString text;
    QString errorMessage;
    int changedCount = 0;
    int measureLineCount = 0;
};

inline constexpr auto kChartNormalizeStartAtNewMeasurePreferenceKey =
    "chart_normalize_start_at_new_measure";
inline constexpr auto kChartNormalizeReduceTo384GridPreferenceKey =
    "chart_normalize_reduce_to_384_grid";

ChartNormalizationOptions chartNormalizationOptionsFromPreferences(
    const QJsonObject& preview,
    const ChartNormalizationOptions& defaults = ChartNormalizationOptions());

void saveChartNormalizationOptionsToPreferences(
    QJsonObject* preview,
    const ChartNormalizationOptions& options);

ChartNormalizationResult normalizeChartText(
    const QString& input,
    const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata(),
    const ChartNormalizationOptions& options = ChartNormalizationOptions());

ChartNormalizationResult normalizeChartSelectionText(
    const QString& fullText,
    int selectionStart,
    int selectionEnd,
    const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata(),
    const ChartNormalizationOptions& options = ChartNormalizationOptions());

}  // namespace miacode::chart_transform
