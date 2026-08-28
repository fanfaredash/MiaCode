#pragma once

#include <QJsonObject>
#include <QString>

#include "core/chart/document/SimaiTimingMetadata.h"

namespace miacode::chart_transform {

enum class ChartNormalizationSyntax {
    Fpd,
    Hinata,
};

struct ChartNormalizationOptions {
    bool startAtNewMeasure = true;
    bool reduceTo384Grid = true;
    bool splitEveryFourMeasures = true;
    ChartNormalizationSyntax syntax = ChartNormalizationSyntax::Fpd;
    int sectionMeasureCount = 4;
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
inline constexpr auto kChartNormalizeSplitEveryFourMeasuresPreferenceKey =
    "chart_normalize_split_every_four_measures";
inline constexpr auto kChartNormalizeSyntaxPreferenceKey =
    "chart_normalize_syntax";
inline constexpr auto kChartNormalizeSectionMeasureCountPreferenceKey =
    "chart_normalize_section_measure_count";

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

// Normalized output is always whole measure lines. When the replaced selection
// did not begin at a line start, or did not end at a line boundary, splice in
// the separators that keep the surrounding text intact.
QString composeNormalizedSelectionReplacement(
    const QString& original,
    int selectionStart,
    int selectionEnd,
    const QString& normalizedText);

}  // namespace miacode::chart_transform
