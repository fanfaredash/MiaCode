#pragma once

// Internal shared helpers for the TimelineQuickModel translation units. These
// were originally file-local (anonymous-namespace) helpers in
// TimelineQuickModel.cpp; they are promoted here in a named namespace so the
// split TUs (parser/snapshot/indexing/core) can share the ones used by more
// than one TU without internal-linkage link errors. Helpers used by exactly one
// TU stay in that TU's own anonymous namespace.

#include <algorithm>

#include <QString>

namespace miacode {
namespace timeline {
namespace tqm_detail {

inline constexpr double kDefaultBpm = 120.0;
inline constexpr int kDefaultBeats = 4;
inline constexpr double kTimelineEpsilon = 1e-6;

inline double noteStepSeconds(double bpm, int beats)
{
    const double clampedBpm = bpm > 0.0 ? bpm : kDefaultBpm;
    const int clampedBeats = std::max(1, beats);
    return 240.0 / (clampedBpm * static_cast<double>(clampedBeats));
}

inline double measureDurationSeconds(double bpm, int meterNumerator, int meterDenominator)
{
    const int clampedNumerator = std::max(1, meterNumerator);
    return noteStepSeconds(bpm, meterDenominator) * static_cast<double>(clampedNumerator);
}

inline bool tryConsumeLeadingFollowControlToken(const QString& text, int limit, int* cursor)
{
    if (cursor == nullptr || *cursor < 0 || *cursor >= limit || limit > text.size()) {
        return false;
    }

    const int start = *cursor;
    if (text.at(start) == QLatin1Char('(')) {
        const int close = text.indexOf(QLatin1Char(')'), start + 1);
        if (close > start && close < limit) {
            bool bpmOk = false;
            const double bpm = text.mid(start + 1, close - start - 1).trimmed().toDouble(&bpmOk);
            if (bpmOk && bpm > 0.0) {
                *cursor = close + 1;
                return true;
            }
        }
    }

    if (text.at(start) == QLatin1Char('{')) {
        const int close = text.indexOf(QLatin1Char('}'), start + 1);
        if (close > start && close < limit) {
            bool beatsOk = false;
            const int beats = text.mid(start + 1, close - start - 1).trimmed().toInt(&beatsOk);
            if (beatsOk && beats > 0) {
                *cursor = close + 1;
                return true;
            }
        }
    }

    if (text.mid(start, 4) == QStringLiteral("<HS*")) {
        const int close = text.indexOf(QLatin1Char('>'), start + 4);
        if (close >= start + 4 && close < limit) {
            *cursor = close + 1;
            return true;
        }
    }

    return false;
}

inline bool tryConsumeTrailingFollowControlToken(const QString& text, int start, int* endExclusive)
{
    if (endExclusive == nullptr || start < 0 || *endExclusive <= start || *endExclusive > text.size()) {
        return false;
    }

    const int end = *endExclusive;
    if (text.at(end - 1) == QLatin1Char(')')) {
        const int open = text.lastIndexOf(QLatin1Char('('), end - 1);
        if (open >= start) {
            bool bpmOk = false;
            const double bpm = text.mid(open + 1, end - open - 2).trimmed().toDouble(&bpmOk);
            if (bpmOk && bpm > 0.0) {
                *endExclusive = open;
                return true;
            }
        }
    }

    if (text.at(end - 1) == QLatin1Char('}')) {
        const int open = text.lastIndexOf(QLatin1Char('{'), end - 1);
        if (open >= start) {
            bool beatsOk = false;
            const int beats = text.mid(open + 1, end - open - 2).trimmed().toInt(&beatsOk);
            if (beatsOk && beats > 0) {
                *endExclusive = open;
                return true;
            }
        }
    }

    if (text.at(end - 1) == QLatin1Char('>')) {
        const int open = text.lastIndexOf(QStringLiteral("<HS*"), end - 1);
        if (open >= start && open + 4 < end) {
            *endExclusive = open;
            return true;
        }
    }

    return false;
}

// Returns the position of the first '||' on the last line of the [start, endExclusive)
// region, or -1 if none. In simai '||' starts a comment that runs to end of line, so this
// covers every comment kind (plain notes, inline time-signature hints, section labels, …).
// Used only at rebuild time to keep comments out of the preview-follow selection; the
// per-tick path is a pure lookup, so this adds no playback cost.
inline int findTrailingCommentStart(const QString& text, int start, int endExclusive)
{
    if (start < 0 || endExclusive > text.size() || start >= endExclusive) {
        return -1;
    }

    const int lineStart = qMax(start, text.lastIndexOf(QLatin1Char('\n'), endExclusive - 1) + 1);
    for (int index = lineStart; index + 1 < endExclusive; ++index) {
        if (text.at(index) == QLatin1Char('|') && text.at(index + 1) == QLatin1Char('|')) {
            return index;
        }
    }

    return -1;
}

inline void trimFollowSelectionSegment(const QString& text, int anchorCol, int* startCol, int* endCol)
{
    const int lineLength = text.size();
    const int normalizedAnchorCol = qBound(1, anchorCol, lineLength + 1);
    int segmentStart = qBound(0, normalizedAnchorCol - 1, lineLength);
    int segmentEnd = text.indexOf(QLatin1Char(','), segmentStart);
    if (segmentEnd < 0) {
        segmentEnd = lineLength;
    }

    bool trimmedLeading = true;
    while (trimmedLeading) {
        trimmedLeading = false;
        while (segmentStart < segmentEnd && text.at(segmentStart).isSpace()) {
            ++segmentStart;
            trimmedLeading = true;
        }
        if (tryConsumeLeadingFollowControlToken(text, segmentEnd, &segmentStart)) {
            trimmedLeading = true;
        }
    }

    bool trimmedTrailing = true;
    while (trimmedTrailing) {
        trimmedTrailing = false;
        while (segmentEnd > segmentStart && text.at(segmentEnd - 1).isSpace()) {
            --segmentEnd;
            trimmedTrailing = true;
        }

        const int commentStart = findTrailingCommentStart(text, segmentStart, segmentEnd);
        if (commentStart >= 0) {
            segmentEnd = commentStart;
            trimmedTrailing = true;
            continue;
        }

        if (tryConsumeTrailingFollowControlToken(text, segmentStart, &segmentEnd)) {
            trimmedTrailing = true;
        }
    }

    while (segmentStart < segmentEnd && text.at(segmentStart).isSpace()) {
        ++segmentStart;
    }
    while (segmentEnd > segmentStart && text.at(segmentEnd - 1).isSpace()) {
        --segmentEnd;
    }

    if (segmentEnd > segmentStart) {
        if (startCol != nullptr) {
            *startCol = segmentStart + 1;
        }
        if (endCol != nullptr) {
            *endCol = segmentEnd;
        }
        return;
    }

    if (startCol != nullptr) {
        *startCol = normalizedAnchorCol;
    }
    if (endCol != nullptr) {
        *endCol = normalizedAnchorCol;
    }
}

}  // namespace tqm_detail
}  // namespace timeline
}  // namespace miacode
