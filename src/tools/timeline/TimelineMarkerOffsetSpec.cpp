#include <QString>
#include <QTextStream>
#include <QVector>
#include <QtNumeric>

#include "timeline/TimelineMarkerOffset.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool nearlyEqual(double a, double b, double epsilon = 1e-9)
{
    return qAbs(a - b) <= epsilon;
}

}  // namespace

int main()
{
    using miacode::timeline::offset::NonFiniteHandling;
    using miacode::timeline::offset::parsedFirstSeconds;
    using miacode::timeline::offset::shiftedBeatMarkers;
    using miacode::timeline::offset::shiftedNoteMarkers;
    using miacode::timeline::offset::shiftedTimelineSecond;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    // ---- parsedFirstSeconds ----

    // A blank field is a valid "no offset", not a parse failure: charts routinely omit
    // `&first`, and reporting ok == false there would flag every one of them as broken.
    {
        bool parsedOk = false;
        ok &= require(nearlyEqual(parsedFirstSeconds(QString(), &parsedOk), 0.0) && parsedOk,
                      QStringLiteral("an empty string is 0.0 and parses cleanly"), err);
    }
    {
        bool parsedOk = false;
        ok &= require(nearlyEqual(parsedFirstSeconds(QStringLiteral("   \t "), &parsedOk), 0.0) && parsedOk,
                      QStringLiteral("an all-whitespace string is 0.0 and parses cleanly"), err);
    }
    {
        bool parsedOk = false;
        ok &= require(nearlyEqual(parsedFirstSeconds(QStringLiteral("1.25"), &parsedOk), 1.25) && parsedOk,
                      QStringLiteral("a plain number parses to its value"), err);
    }
    // A negative `&first` is legal — it pulls the chart earlier against the audio.
    {
        bool parsedOk = false;
        ok &= require(nearlyEqual(parsedFirstSeconds(QStringLiteral("-2.5"), &parsedOk), -2.5) && parsedOk,
                      QStringLiteral("a negative number keeps its sign"), err);
    }
    {
        bool parsedOk = false;
        ok &= require(nearlyEqual(parsedFirstSeconds(QStringLiteral("  3.5\n"), &parsedOk), 3.5) && parsedOk,
                      QStringLiteral("surrounding whitespace is trimmed before parsing"), err);
    }
    // Unparseable text falls back to 0.0 rather than leaving the offset undefined, but
    // reports ok == false so callers can surface the bad field.
    {
        bool parsedOk = true;
        ok &= require(nearlyEqual(parsedFirstSeconds(QStringLiteral("abc"), &parsedOk), 0.0) && !parsedOk,
                      QStringLiteral("unparseable text is 0.0 with ok == false"), err);
    }
    // The `ok` out-param is optional; VideoExportSnapshot's call site passes nothing.
    ok &= require(nearlyEqual(parsedFirstSeconds(QStringLiteral("4.0")), 4.0),
                  QStringLiteral("the ok out-param is optional"), err);
    ok &= require(nearlyEqual(parsedFirstSeconds(QStringLiteral("abc")), 0.0),
                  QStringLiteral("unparseable text is 0.0 without an ok out-param"), err);

    // This is the load-bearing case behind NonFiniteHandling: QString::toDouble ACCEPTS
    // "inf" and "nan" and reports success, so a non-finite offset reaches the shift
    // helpers with ok == true and no upstream rejection.
    {
        bool parsedOk = false;
        const double value = parsedFirstSeconds(QStringLiteral("inf"), &parsedOk);
        ok &= require(parsedOk && qIsInf(value),
                      QStringLiteral("\"inf\" parses successfully into a non-finite offset"), err);
    }
    {
        bool parsedOk = false;
        const double value = parsedFirstSeconds(QStringLiteral("nan"), &parsedOk);
        ok &= require(parsedOk && qIsNaN(value),
                      QStringLiteral("\"nan\" parses successfully into a non-finite offset"), err);
    }

    // ---- shiftedTimelineSecond: the two inherited modes ----

    ok &= require(nearlyEqual(shiftedTimelineSecond(2.0, 0.5, NonFiniteHandling::PassThrough), 2.5),
                  QStringLiteral("PassThrough adds finite operands normally"), err);
    ok &= require(nearlyEqual(shiftedTimelineSecond(2.0, 0.5, NonFiniteHandling::Propagate), 2.5),
                  QStringLiteral("Propagate adds finite operands normally"), err);

    // preview / timeline / muri: a non-finite offset leaves the second untouched.
    ok &= require(nearlyEqual(shiftedTimelineSecond(2.0, qQNaN(), NonFiniteHandling::PassThrough), 2.0),
                  QStringLiteral("PassThrough returns the second unchanged for a NaN offset"), err);
    ok &= require(nearlyEqual(shiftedTimelineSecond(2.0, qInf(), NonFiniteHandling::PassThrough), 2.0),
                  QStringLiteral("PassThrough returns the second unchanged for an infinite offset"), err);
    ok &= require(qIsNaN(shiftedTimelineSecond(qQNaN(), 0.5, NonFiniteHandling::PassThrough)),
                  QStringLiteral("PassThrough returns a NaN second unchanged"), err);

    // both export paths: the same inputs poison the result instead.
    ok &= require(qIsNaN(shiftedTimelineSecond(2.0, qQNaN(), NonFiniteHandling::Propagate)),
                  QStringLiteral("Propagate turns a NaN offset into a NaN second"), err);
    ok &= require(qIsInf(shiftedTimelineSecond(2.0, qInf(), NonFiniteHandling::Propagate)),
                  QStringLiteral("Propagate turns an infinite offset into an infinite second"), err);
    ok &= require(qIsNaN(shiftedTimelineSecond(qQNaN(), 0.5, NonFiniteHandling::Propagate)),
                  QStringLiteral("Propagate keeps a NaN second NaN"), err);

    // Guard against the parameter quietly becoming a no-op: on the one input that the six
    // original copies disagreed about, the two modes must still disagree.
    ok &= require(
        qIsNaN(shiftedTimelineSecond(2.0, qQNaN(), NonFiniteHandling::Propagate))
            && !qIsNaN(shiftedTimelineSecond(2.0, qQNaN(), NonFiniteHandling::PassThrough)),
        QStringLiteral("the two modes disagree on a non-finite offset"), err);

    // ---- shiftedNoteMarkers: all five shifted fields ----

    {
        TimelineNoteMarker marker;
        marker.second = 1.0;
        marker.endSecond = 2.0;
        marker.slideTraceSecond = 3.0;
        marker.availableSecond = 4.0;
        marker.slideSegmentShootSeconds = {5.0, 6.5};

        const QVector<TimelineNoteMarker> shifted =
            shiftedNoteMarkers({marker}, 0.5, NonFiniteHandling::PassThrough);

        ok &= require(shifted.size() == 1, QStringLiteral("shiftedNoteMarkers preserves size"), err);
        if (shifted.size() == 1) {
            const TimelineNoteMarker& result = shifted.at(0);
            ok &= require(nearlyEqual(result.second, 1.5), QStringLiteral("second shifts"), err);
            ok &= require(nearlyEqual(result.endSecond, 2.5), QStringLiteral("endSecond shifts"), err);
            ok &= require(nearlyEqual(result.slideTraceSecond, 3.5),
                          QStringLiteral("slideTraceSecond shifts"), err);
            ok &= require(nearlyEqual(result.availableSecond, 4.5),
                          QStringLiteral("availableSecond shifts"), err);
            ok &= require(result.slideSegmentShootSeconds.size() == 2
                              && nearlyEqual(result.slideSegmentShootSeconds.at(0), 5.5)
                              && nearlyEqual(result.slideSegmentShootSeconds.at(1), 7.0),
                          QStringLiteral("every slideSegmentShootSeconds element shifts"), err);
        }
    }

    // -1.0 is the "this note has no end / no trace / not yet available" sentinel. Shifting
    // it would turn it into an ordinary-looking timestamp, so the `>= 0.0` guards must skip
    // it — while `second` (which has no sentinel) still shifts.
    {
        TimelineNoteMarker marker;
        marker.second = 1.0;
        marker.endSecond = -1.0;
        marker.slideTraceSecond = -1.0;
        marker.availableSecond = -1.0;

        const QVector<TimelineNoteMarker> shifted =
            shiftedNoteMarkers({marker}, 0.5, NonFiniteHandling::PassThrough);

        if (require(shifted.size() == 1, QStringLiteral("sentinel case preserves size"), err)) {
            const TimelineNoteMarker& result = shifted.at(0);
            ok &= require(nearlyEqual(result.second, 1.5),
                          QStringLiteral("second still shifts alongside sentinels"), err);
            ok &= require(nearlyEqual(result.endSecond, -1.0),
                          QStringLiteral("the -1.0 endSecond sentinel is left untouched"), err);
            ok &= require(nearlyEqual(result.slideTraceSecond, -1.0),
                          QStringLiteral("the -1.0 slideTraceSecond sentinel is left untouched"), err);
            ok &= require(nearlyEqual(result.availableSecond, -1.0),
                          QStringLiteral("the -1.0 availableSecond sentinel is left untouched"), err);
        } else {
            ok = false;
        }
    }

    // 0.0 is a real timestamp, not the sentinel — the guard is `>= 0.0`, so it shifts.
    {
        TimelineNoteMarker marker;
        marker.second = 0.0;
        marker.endSecond = 0.0;

        const QVector<TimelineNoteMarker> shifted =
            shiftedNoteMarkers({marker}, 0.5, NonFiniteHandling::PassThrough);
        if (shifted.size() == 1) {
            ok &= require(nearlyEqual(shifted.at(0).endSecond, 0.5),
                          QStringLiteral("a 0.0 endSecond is a real timestamp and shifts"), err);
        } else {
            ok = false;
        }
    }

    // The handling mode reaches every field, not just `second`.
    {
        TimelineNoteMarker marker;
        marker.second = 1.0;
        marker.endSecond = 2.0;
        marker.slideSegmentShootSeconds = {3.0};

        const QVector<TimelineNoteMarker> passThrough =
            shiftedNoteMarkers({marker}, qQNaN(), NonFiniteHandling::PassThrough);
        if (passThrough.size() == 1) {
            const TimelineNoteMarker& result = passThrough.at(0);
            ok &= require(nearlyEqual(result.second, 1.0)
                              && nearlyEqual(result.endSecond, 2.0)
                              && result.slideSegmentShootSeconds.size() == 1
                              && nearlyEqual(result.slideSegmentShootSeconds.at(0), 3.0),
                          QStringLiteral("PassThrough leaves every field alone for a NaN offset"), err);
        } else {
            ok = false;
        }

        const QVector<TimelineNoteMarker> propagate =
            shiftedNoteMarkers({marker}, qQNaN(), NonFiniteHandling::Propagate);
        if (propagate.size() == 1) {
            const TimelineNoteMarker& result = propagate.at(0);
            ok &= require(qIsNaN(result.second)
                              && qIsNaN(result.endSecond)
                              && result.slideSegmentShootSeconds.size() == 1
                              && qIsNaN(result.slideSegmentShootSeconds.at(0)),
                          QStringLiteral("Propagate NaNs every field for a NaN offset"), err);
        } else {
            ok = false;
        }
    }

    // ---- shiftedBeatMarkers ----

    {
        TimelineBeatMarker major;
        major.second = 1.0;
        major.major = true;
        TimelineBeatMarker minor;
        minor.second = 2.0;
        minor.major = false;

        const QVector<TimelineBeatMarker> shifted =
            shiftedBeatMarkers({major, minor}, 0.25, NonFiniteHandling::PassThrough);
        ok &= require(shifted.size() == 2
                          && nearlyEqual(shifted.at(0).second, 1.25)
                          && nearlyEqual(shifted.at(1).second, 2.25),
                      QStringLiteral("every beat marker shifts"), err);
        ok &= require(shifted.size() == 2 && shifted.at(0).major && !shifted.at(1).major,
                      QStringLiteral("beat markers keep their non-time fields"), err);
    }

    ok &= require(shiftedBeatMarkers({}, 1.0, NonFiniteHandling::PassThrough).isEmpty(),
                  QStringLiteral("an empty beat vector stays empty"), err);
    ok &= require(shiftedNoteMarkers({}, 1.0, NonFiniteHandling::Propagate).isEmpty(),
                  QStringLiteral("an empty note vector stays empty"), err);

    if (!ok) {
        return 1;
    }
    out << "timeline_marker_offset_spec: OK" << Qt::endl;
    return 0;
}
