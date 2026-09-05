#include "core/chart/selection/ChartSelectionBeatSummary.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

bool expect(const miacode::chart_selection::ChartSelectionBeatSummary& actual,
            int total, std::initializer_list<std::pair<int, int>> parts, bool exact,
            const char* label)
{
    bool ok = actual.totalCommaCount == total && actual.exact == exact
        && actual.parts.size() == static_cast<int>(parts.size());
    int index = 0;
    for (const auto& [count, denominator] : parts) {
        if (!ok || actual.parts.at(index).count != count
            || actual.parts.at(index).denominator != denominator) {
            ok = false;
        }
        ++index;
    }
    if (!ok) {
        qWarning().noquote() << "[FAIL]" << label;
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using miacode::chart_selection::summarizeChartSelectionBeats;
    int failures = 0;

    const QString chart = QStringLiteral("{8}1,2,3,4\r\n|| prose, 99\n{16}A1,2,");
    failures += !expect(summarizeChartSelectionBeats(chart, 0, chart.size()), 5,
                        {{3, 8}, {2, 16}}, true, "whole chart ignores comments and CRLF");

    const int secondStart = chart.indexOf(QStringLiteral("{16}"));
    failures += !expect(summarizeChartSelectionBeats(chart, 0, secondStart), 3,
                        {{3, 8}}, true, "denominator before selection");

    const int mixedEnd = chart.size();
    failures += !expect(summarizeChartSelectionBeats(chart, secondStart, mixedEnd), 2,
                        {{2, 16}}, true, "selection-local denominator");

    const int tokenStart = chart.indexOf(QStringLiteral("A1"));
    failures += !expect(summarizeChartSelectionBeats(chart, tokenStart + 1, tokenStart + 2),
                        0, {}, false, "half token is inexact");

    failures += !expect(summarizeChartSelectionBeats(chart, 0, 0), 0, {}, true,
                        "empty selection");
    if (failures != 0) {
        qWarning() << failures << "beat summary checks failed";
        return 1;
    }
    qInfo() << "Chart selection beat summary spec passed.";
    return 0;
}
