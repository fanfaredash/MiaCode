#pragma once

#include <QString>
#include <QVector>

namespace miacode::simai {

// A simai `||` comment runs from the marker to the end of ITS LINE. The chart
// and timeline parsers express that by `break`ing out of their per-line scan
// (SimaiNativeParser.Driver.cpp / TimelineQuickModelParser.cpp); code that
// walks raw chart text as one flat string has no per-line loop to break out of,
// so it uses these helpers instead of re-deriving the rule.

// Index of the `||` that comments out `position`, or -1 when `position` holds
// chart content. A position ON the marker counts as content.
int commentStartForPosition(const QString& text, int position);

// Nearest `,` strictly before / at-or-after `position` that is a real beat
// separator. Commas inside a comment are prose. Returns -1 when there is none.
int previousChartComma(const QString& text, int position);
int nextChartComma(const QString& text, int position);

struct ChartContentSpan {
    int start = 0;
    int end = 0;  // exclusive; not whitespace-trimmed
};

// `[start, end)` minus every comment it covers. A token may span several lines
// and therefore several comments, so this can return more than one span. The
// newline that terminates a comment stays at the head of the following span so
// callers can still see the line break.
QVector<ChartContentSpan> chartContentSpans(const QString& text, int start, int end);

}  // namespace miacode::simai
