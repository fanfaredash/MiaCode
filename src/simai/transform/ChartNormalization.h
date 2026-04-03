#pragma once

#include <QString>

#include "simai/document/SimaiTimingMetadata.h"

namespace miacode::chart_transform {

struct ChartNormalizationResult {
    bool ok = false;
    QString text;
    QString errorMessage;
    int changedCount = 0;
    int measureLineCount = 0;
};

ChartNormalizationResult normalizeChartText(
    const QString& input,
    const miacode::simai::SimaiTimingMetadata& timingMetadata = miacode::simai::SimaiTimingMetadata());

}  // namespace miacode::chart_transform
