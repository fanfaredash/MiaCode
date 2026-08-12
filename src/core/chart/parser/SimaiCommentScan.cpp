#include "core/chart/parser/SimaiCommentScan.h"

namespace miacode::simai {

namespace {

bool isCommentMarkerAt(const QString& text, int index)
{
    return index >= 0
        && index + 1 < text.size()
        && text.at(index) == QLatin1Char('|')
        && text.at(index + 1) == QLatin1Char('|');
}

int lineStartForPosition(const QString& text, int position)
{
    const int previousNewline = position > 0
        ? text.lastIndexOf(QLatin1Char('\n'), position - 1)
        : -1;
    return previousNewline + 1;
}

}  // namespace

int commentStartForPosition(const QString& text, int position)
{
    const int bounded = qBound(0, position, text.size());
    for (int index = lineStartForPosition(text, bounded); index < bounded; ++index) {
        if (isCommentMarkerAt(text, index)) {
            return index;
        }
    }
    return -1;
}

int previousChartComma(const QString& text, int position)
{
    int index = qBound(0, position, text.size()) - 1;
    while (index >= 0) {
        index = text.lastIndexOf(QLatin1Char(','), index);
        if (index < 0) {
            return -1;
        }
        const int commentStart = commentStartForPosition(text, index);
        if (commentStart < 0) {
            return index;
        }
        // Everything from the marker on is prose; resume before it.
        index = commentStart - 1;
    }
    return -1;
}

int nextChartComma(const QString& text, int position)
{
    int index = qBound(0, position, text.size());
    while (index < text.size()) {
        index = text.indexOf(QLatin1Char(','), index);
        if (index < 0) {
            return -1;
        }
        if (commentStartForPosition(text, index) < 0) {
            return index;
        }
        const int newline = text.indexOf(QLatin1Char('\n'), index);
        if (newline < 0) {
            return -1;
        }
        index = newline + 1;
    }
    return -1;
}

QVector<ChartContentSpan> chartContentSpans(const QString& text, int start, int end)
{
    QVector<ChartContentSpan> spans;
    const int limit = qBound(0, end, text.size());
    int spanStart = qBound(0, start, limit);
    int index = spanStart;
    while (index < limit) {
        if (!isCommentMarkerAt(text, index)) {
            ++index;
            continue;
        }
        if (index > spanStart) {
            spans.append(ChartContentSpan{spanStart, index});
        }
        const int newline = text.indexOf(QLatin1Char('\n'), index + 2);
        index = (newline < 0 || newline >= limit) ? limit : newline;
        spanStart = index;
    }
    if (spanStart < limit) {
        spans.append(ChartContentSpan{spanStart, limit});
    }
    return spans;
}

}  // namespace miacode::simai
