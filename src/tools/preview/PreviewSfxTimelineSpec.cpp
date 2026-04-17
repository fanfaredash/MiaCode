#include <QCoreApplication>
#include <QTextStream>

#include "common/PreviewSfxTimeline.h"

namespace {

using miacode::preview_sfx_timeline::Event;
using miacode::preview_sfx_timeline::TouchholdSpan;
using miacode::preview_sfx_timeline::adjustedAnswerSecond;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyHoldAndTouchHoldTailAnswer(QTextStream& err)
{
    QVector<TimelineNoteMarker> markers;

    TimelineNoteMarker hold;
    hold.type = QStringLiteral("hold");
    hold.second = 1.0;
    hold.endSecond = 2.0;
    markers.append(hold);

    TimelineNoteMarker touchHold;
    touchHold.type = QStringLiteral("touch_hold");
    touchHold.second = 3.0;
    touchHold.endSecond = 4.5;
    markers.append(touchHold);

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline(markers, &events, &spans);

    int holdStartAnswerCount = 0;
    int holdEndAnswerCount = 0;
    int touchHoldStartAnswerCount = 0;
    int touchHoldEndAnswerCount = 0;
    int touchHoldStartSpanCount = 0;
    int touchHoldStopSpanCount = 0;

    for (const Event& event : events) {
        if (event.kind != QLatin1String("answer")
            && event.kind != QLatin1String("touchhold_start")
            && event.kind != QLatin1String("touchhold_stop")) {
            continue;
        }

        if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(1.0)) <= 1e-6) {
            ++holdStartAnswerCount;
        } else if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(2.0)) <= 1e-6) {
            ++holdEndAnswerCount;
        } else if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(3.0)) <= 1e-6) {
            ++touchHoldStartAnswerCount;
        } else if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(4.5)) <= 1e-6) {
            ++touchHoldEndAnswerCount;
        } else if (event.kind == QLatin1String("touchhold_start") && qAbs(event.second - 3.0) <= 1e-6) {
            ++touchHoldStartSpanCount;
        } else if (event.kind == QLatin1String("touchhold_stop") && qAbs(event.second - 4.5) <= 1e-6) {
            ++touchHoldStopSpanCount;
        }
    }

    if (!require(holdStartAnswerCount == 1, QStringLiteral("hold should emit one start answer"), err)) {
        return false;
    }
    if (!require(holdEndAnswerCount == 1, QStringLiteral("hold should emit one tail answer"), err)) {
        return false;
    }
    if (!require(touchHoldStartAnswerCount == 1, QStringLiteral("touch_hold should emit one start answer"), err)) {
        return false;
    }
    if (!require(touchHoldEndAnswerCount == 1, QStringLiteral("touch_hold should emit one tail answer"), err)) {
        return false;
    }
    if (!require(touchHoldStartSpanCount == 1, QStringLiteral("touch_hold should emit one sustain start event"), err)) {
        return false;
    }
    if (!require(touchHoldStopSpanCount == 1, QStringLiteral("touch_hold should emit one sustain stop event"), err)) {
        return false;
    }
    if (!require(spans.size() == 1, QStringLiteral("touch_hold should create one sustain span"), err)) {
        return false;
    }

    return true;
}

bool verifyAnswerTimingCompensation(QTextStream& err)
{
    QVector<TimelineNoteMarker> markers;

    TimelineNoteMarker tap;
    tap.type = QStringLiteral("tap");
    tap.second = 1.0;
    markers.append(tap);

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline(markers, &events, &spans);

    int compensatedAnswerCount = 0;
    int uncompensatedAnswerCount = 0;
    for (const Event& event : events) {
        if (event.kind != QLatin1String("answer")) {
            continue;
        }
        if (qAbs(event.second - adjustedAnswerSecond(1.0)) <= 1e-6) {
            ++compensatedAnswerCount;
        }
        if (qAbs(event.second - 1.0) <= 1e-6) {
            ++uncompensatedAnswerCount;
        }
    }

    if (!require(compensatedAnswerCount == 1, QStringLiteral("answer should emit one compensated event"), err)) {
        return false;
    }
    if (!require(uncompensatedAnswerCount == 0, QStringLiteral("answer should not emit an uncompensated event"), err)) {
        return false;
    }
    if (!require(spans.isEmpty(), QStringLiteral("tap should not create touchhold spans"), err)) {
        return false;
    }

    return true;
}

bool verifyBreakSlideTailSfx(QTextStream& err)
{
    QVector<TimelineNoteMarker> markers;

    TimelineNoteMarker slide;
    slide.type = QStringLiteral("slide");
    slide.second = 1.0;
    slide.slideTraceSecond = 1.5;
    slide.endSecond = 3.0;
    slide.trackBreak = true;
    markers.append(slide);

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline(markers, &events, &spans);

    int breakSlideStartCount = 0;
    int breakTailCount = 0;
    int judgeBreakSlideTailCount = 0;
    int legacyBreakSlideFinishCount = 0;

    for (const Event& event : events) {
        if (event.kind == QLatin1String("break_slide_start") && qAbs(event.second - 1.5) <= 1e-6) {
            ++breakSlideStartCount;
        } else if (event.kind == QLatin1String("break") && qAbs(event.second - 3.0) <= 1e-6) {
            ++breakTailCount;
        } else if (event.kind == QLatin1String("judge_break_slide") && qAbs(event.second - 3.0) <= 1e-6) {
            ++judgeBreakSlideTailCount;
        } else if (event.kind == QLatin1String("break_slide_finish") && qAbs(event.second - 3.0) <= 1e-6) {
            ++legacyBreakSlideFinishCount;
        }
    }

    if (!require(breakSlideStartCount == 1, QStringLiteral("break slide should emit one start SFX"), err)) {
        return false;
    }
    if (!require(breakTailCount == 1, QStringLiteral("break slide tail should emit one break SFX"), err)) {
        return false;
    }
    if (!require(
            judgeBreakSlideTailCount == 1,
            QStringLiteral("break slide tail should emit one judge_break_slide SFX"),
            err)) {
        return false;
    }
    if (!require(
            legacyBreakSlideFinishCount == 0,
            QStringLiteral("break slide tail should not emit legacy break_slide_finish"),
            err)) {
        return false;
    }
    if (!require(spans.isEmpty(), QStringLiteral("break slide should not create touchhold spans"), err)) {
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyHoldAndTouchHoldTailAnswer(err)) {
        return 1;
    }
    if (!verifyAnswerTimingCompensation(err)) {
        return 1;
    }
    if (!verifyBreakSlideTailSfx(err)) {
        return 1;
    }

    out << "preview_sfx_timeline_spec ok" << Qt::endl;
    return 0;
}
