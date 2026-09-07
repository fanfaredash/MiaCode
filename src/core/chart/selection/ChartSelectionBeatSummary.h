#pragma once

#include <QString>
#include <QVector>

namespace miacode::chart_selection {

struct ChartSelectionBeatPart {
    int count = 0;
    int denominator = 4;
};

struct ChartSelectionBeatSummary {
    int totalCommaCount = 0;
    QVector<ChartSelectionBeatPart> parts;
    // False when either selection edge cuts through a chart token or directive.
    // Counts remain useful in that case, but they should not be presented as an
    // exact subdivision summary.
    bool exact = true;
};

ChartSelectionBeatSummary summarizeChartSelectionBeats(
    const QString& text, int selectionStart, int selectionEnd);

}  // namespace miacode::chart_selection
