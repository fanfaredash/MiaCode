#include "core/chart/selection/ChartSelectionBeatSummary.h"

#include "core/chart/parser/SimaiCommentScan.h"

#include <QHash>

namespace miacode::chart_selection {

namespace {

struct DenominatorCount {
    int denominator = 4;
    int count = 0;
};

bool isContentCharacter(const QString& text, const QByteArray& content, int index)
{
    return index >= 0 && index < text.size() && content.at(index) != 0;
}

bool isBoundaryTokenCharacter(const QString& text, const QByteArray& content, int index)
{
    if (!isContentCharacter(text, content, index)) {
        return false;
    }
    const QChar ch = text.at(index);
    return !ch.isSpace() && ch != QLatin1Char(',') && ch != QLatin1Char('{')
        && ch != QLatin1Char('}');
}

bool edgeCutsToken(const QString& text, const QByteArray& content, int edge)
{
    return edge > 0 && edge < text.size()
        && isBoundaryTokenCharacter(text, content, edge - 1)
        && isBoundaryTokenCharacter(text, content, edge);
}

}  // namespace

ChartSelectionBeatSummary summarizeChartSelectionBeats(
    const QString& text, int selectionStart, int selectionEnd)
{
    ChartSelectionBeatSummary result;
    const int begin = qBound(0, qMin(selectionStart, selectionEnd), text.size());
    const int end = qBound(begin, qMax(selectionStart, selectionEnd), text.size());
    if (begin == end || text.isEmpty()) {
        return result;
    }

    QByteArray content(text.size(), '\0');
    for (const miacode::simai::ChartContentSpan& span :
         miacode::simai::chartContentSpans(text, 0, text.size())) {
        for (int index = span.start; index < span.end; ++index) {
            content[index] = 1;
        }
    }

    result.exact = !edgeCutsToken(text, content, begin) && !edgeCutsToken(text, content, end);

    QVector<DenominatorCount> counts;
    QHash<int, int> countIndex;
    int denominator = 4;
    for (int index = 0; index < text.size();) {
        if (!isContentCharacter(text, content, index)) {
            ++index;
            continue;
        }

        if (text.at(index) == QLatin1Char('{')) {
            int cursor = index + 1;
            int value = 0;
            bool hasDigit = false;
            while (cursor < text.size() && isContentCharacter(text, content, cursor)
                   && text.at(cursor).isDigit()) {
                hasDigit = true;
                value = value * 10 + text.at(cursor).digitValue();
                ++cursor;
            }
            if (hasDigit && cursor < text.size() && isContentCharacter(text, content, cursor)
                && text.at(cursor) == QLatin1Char('}') && value > 0) {
                denominator = value;
                index = cursor + 1;
                continue;
            }
        }

        if (text.at(index) == QLatin1Char(',') && index >= begin && index < end) {
            ++result.totalCommaCount;
            const int existing = countIndex.value(denominator, -1);
            if (existing >= 0) {
                ++counts[existing].count;
            } else {
                countIndex.insert(denominator, counts.size());
                counts.append(DenominatorCount{denominator, 1});
            }
        }
        ++index;
    }

    result.parts.reserve(counts.size());
    for (const DenominatorCount& entry : counts) {
        result.parts.append(ChartSelectionBeatPart{entry.count, entry.denominator});
    }
    return result;
}

}  // namespace miacode::chart_selection
