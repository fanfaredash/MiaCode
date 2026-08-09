#pragma once

#include <QString>
#include <QVector>
#include <QtNumeric>

#include "timeline/TimelineData.h"

// The `&first` offset pipeline: parse the raw field, then slide timeline markers by it.
//
// Every consumer does the same two steps back to back — read `&first` into a double, then
// shift a freshly parsed marker vector by that double — so both halves live here even
// though the parse itself has no marker dependency. Splitting them would make all four
// call sites include two headers to perform one operation, and would put the parse in
// `src/core/chart/` where nothing else in `src/core` would use it. It sits under
// `src/timeline/` (not `src/core/`) because the marker structs do: `src/core/*` must not
// depend on `src/timeline`, while `src/app`, `src/timeline` and `src/tools` all may.
//
// ---- On NonFiniteHandling: this split is inherited, not designed ----
//
// Six hand-copied versions of `shiftedTimelineSecond` had drifted apart before this header
// existed, and the parameter preserves that drift verbatim rather than picking a winner.
// It matters because `QString::toDouble` accepts "inf" and "nan" and reports ok == true, so
// a chart whose `&first` is literally `inf` yields a non-finite offset that reaches these
// functions with no upstream rejection:
//
//   PassThrough (guard present) — preview/timeline/muri: markers come back untouched.
//     TimelineSlowRefresh.cpp, MainWindow.PreviewTimelineFlow.cpp, MuriDump.cpp, MuriSpec.cpp
//   Propagate (no guard) — both export paths: every shifted field becomes NaN.
//     VideoExportSnapshot.cpp, MainWindow.ExportSnapshot.cpp
//
// Nobody chose to have the exporter NaN out a chart the editor renders fine; the two export
// copies were simply written without the guard. Unifying them is a real behavior change and
// deliberately out of scope here — this header only makes the divergence visible at each
// call site so that decision can be taken with the evidence in hand. There is intentionally
// no default argument: every caller must state which group it belongs to.
namespace miacode::timeline::offset {

enum class NonFiniteHandling {
    PassThrough,  // non-finite input or offset returns `second` unchanged
    Propagate,    // always `second + offsetSeconds`, NaN included
};

inline double shiftedTimelineSecond(double second, double offsetSeconds, NonFiniteHandling handling)
{
    if (handling == NonFiniteHandling::PassThrough && (!qIsFinite(second) || !qIsFinite(offsetSeconds))) {
        return second;
    }
    return second + offsetSeconds;
}

inline QVector<TimelineBeatMarker> shiftedBeatMarkers(
    const QVector<TimelineBeatMarker>& beatMarkers,
    double offsetSeconds,
    NonFiniteHandling handling)
{
    QVector<TimelineBeatMarker> shifted = beatMarkers;
    for (TimelineBeatMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds, handling);
    }
    return shifted;
}

// `endSecond`, `slideTraceSecond` and `availableSecond` default to -1.0 as "absent"; the
// `>= 0.0` tests keep that sentinel out of the arithmetic so it stays recognisable.
inline QVector<TimelineNoteMarker> shiftedNoteMarkers(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double offsetSeconds,
    NonFiniteHandling handling)
{
    QVector<TimelineNoteMarker> shifted = noteMarkers;
    for (TimelineNoteMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds, handling);
        if (marker.endSecond >= 0.0) {
            marker.endSecond = shiftedTimelineSecond(marker.endSecond, offsetSeconds, handling);
        }
        if (marker.slideTraceSecond >= 0.0) {
            marker.slideTraceSecond = shiftedTimelineSecond(marker.slideTraceSecond, offsetSeconds, handling);
        }
        if (marker.availableSecond >= 0.0) {
            marker.availableSecond = shiftedTimelineSecond(marker.availableSecond, offsetSeconds, handling);
        }
        for (double& shootSecond : marker.slideSegmentShootSeconds) {
            shootSecond = shiftedTimelineSecond(shootSecond, offsetSeconds, handling);
        }
    }
    return shifted;
}

// Raw `&first` text to seconds. Empty (or all-whitespace) is a valid "no offset" and reports
// ok == true, so callers can tell "the field is blank" from "the field is broken" — an
// unparseable value yields 0.0 with ok == false. Callers that read the value from a live
// widget rather than the document resolve that string themselves and pass it in here.
inline double parsedFirstSeconds(const QString& rawValue, bool* ok = nullptr)
{
    const QString trimmed = rawValue.trimmed();
    bool localOk = false;
    const double value = trimmed.isEmpty() ? 0.0 : trimmed.toDouble(&localOk);
    if (ok != nullptr) {
        *ok = trimmed.isEmpty() ? true : localOk;
    }
    return (trimmed.isEmpty() || localOk) ? value : 0.0;
}

}  // namespace miacode::timeline::offset
