#include <QCoreApplication>
#include <QTextStream>

#include "common/PreviewSfxTimeline.h"

namespace {

using miacode::preview_sfx_timeline::Event;
using miacode::preview_sfx_timeline::ScheduledPlayback;
using miacode::preview_sfx_timeline::TouchholdSpan;
using miacode::preview_sfx_timeline::adjustedAnswerSecond;

double realDeltaMs(double chartDeltaSeconds, double playbackRate)
{
    if (!qIsFinite(playbackRate) || qAbs(playbackRate) <= 1e-9) {
        return 0.0;
    }
    return (chartDeltaSeconds / playbackRate) * 1000.0;
}

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyHoldTailJudgeAndTouchHoldTailAnswer(QTextStream& err)
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
    miacode::preview_sfx_timeline::buildTimeline(markers, 1.0, PreviewTimingSettings(), &events, &spans);

    int holdStartAnswerCount = 0;
    int holdEndAnswerCount = 0;
    int holdEndJudgeCount = 0;
    int touchHoldStartAnswerCount = 0;
    int touchHoldEndAnswerCount = 0;
    int touchHoldEndJudgeCount = 0;
    int touchHoldStartSpanCount = 0;
    int touchHoldStopSpanCount = 0;
    const double holdTailJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(2.0, PreviewTimingSettings(), 1.0);
    const double touchHoldTailJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(4.5, PreviewTimingSettings(), 1.0);

    for (const Event& event : events) {
        if (event.kind != QLatin1String("answer")
            && event.kind != QLatin1String("judge")
            && event.kind != QLatin1String("touchhold_start")
            && event.kind != QLatin1String("touchhold_stop")) {
            continue;
        }

        if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(1.0)) <= 1e-6) {
            ++holdStartAnswerCount;
        } else if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(2.0)) <= 1e-6) {
            ++holdEndAnswerCount;
        } else if (event.kind == QLatin1String("judge") && qAbs(event.second - holdTailJudgeSecond) <= 1e-6) {
            ++holdEndJudgeCount;
        } else if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(3.0)) <= 1e-6) {
            ++touchHoldStartAnswerCount;
        } else if (event.kind == QLatin1String("answer") && qAbs(event.second - adjustedAnswerSecond(4.5)) <= 1e-6) {
            ++touchHoldEndAnswerCount;
        } else if (event.kind == QLatin1String("judge") && qAbs(event.second - touchHoldTailJudgeSecond) <= 1e-6) {
            ++touchHoldEndJudgeCount;
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
    if (!require(holdEndJudgeCount == 1, QStringLiteral("hold should emit one tail judge"), err)) {
        return false;
    }
    if (!require(touchHoldStartAnswerCount == 1, QStringLiteral("touch_hold should emit one start answer"), err)) {
        return false;
    }
    if (!require(touchHoldEndAnswerCount == 1, QStringLiteral("touch_hold should emit one tail answer"), err)) {
        return false;
    }
    if (!require(touchHoldEndJudgeCount == 0, QStringLiteral("touch_hold tail should not emit judge"), err)) {
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

bool verifyMajdataHoldVariantSfx(QTextStream& err)
{
    QVector<TimelineNoteMarker> markers;

    TimelineNoteMarker exHold;
    exHold.type = QStringLiteral("hold");
    exHold.second = 1.0;
    exHold.endSecond = 2.0;
    exHold.isEx = true;
    markers.append(exHold);

    TimelineNoteMarker breakHold;
    breakHold.type = QStringLiteral("hold");
    breakHold.second = 3.0;
    breakHold.endSecond = 4.0;
    breakHold.isBreak = true;
    markers.append(breakHold);

    TimelineNoteMarker breakExHold;
    breakExHold.type = QStringLiteral("hold");
    breakExHold.second = 5.0;
    breakExHold.endSecond = 6.0;
    breakExHold.isBreak = true;
    breakExHold.isEx = true;
    markers.append(breakExHold);

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline(markers, 1.0, PreviewTimingSettings(), &events, &spans);

    const auto countKindAt = [&events](const QString& kind, double second) {
        int count = 0;
        for (const Event& event : events) {
            if (event.kind == kind && qAbs(event.second - second) <= 1e-6) {
                ++count;
            }
        }
        return count;
    };

    const double exHeadJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(1.0, PreviewTimingSettings(), 1.0);
    const double exTailJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(2.0, PreviewTimingSettings(), 1.0);
    const double breakHeadJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(3.0, PreviewTimingSettings(), 1.0);
    const double breakTailJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(4.0, PreviewTimingSettings(), 1.0);
    const double breakExHeadJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(5.0, PreviewTimingSettings(), 1.0);
    const double breakExTailJudgeSecond =
        miacode::preview_sfx_timing::judgeTriggerSecond(6.0, PreviewTimingSettings(), 1.0);

    if (!require(countKindAt(QStringLiteral("ex"), exHeadJudgeSecond) == 1, QStringLiteral("ex hold head should emit ex judge"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("judge"), exHeadJudgeSecond) == 0, QStringLiteral("ex hold head should not emit normal judge"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("judge"), exTailJudgeSecond) == 0, QStringLiteral("ex hold tail should not emit normal judge"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("ex"), exTailJudgeSecond) == 0, QStringLiteral("ex hold tail should not emit ex judge"), err)) {
        return false;
    }

    if (!require(countKindAt(QStringLiteral("judge_break"), breakHeadJudgeSecond) == 1, QStringLiteral("break hold head should emit break judge"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("break"), breakHeadJudgeSecond) == 1, QStringLiteral("break hold head should emit break cheer"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("judge_break"), breakTailJudgeSecond) == 0, QStringLiteral("break hold tail should not emit break judge"), err)) {
        return false;
    }

    if (!require(countKindAt(QStringLiteral("judge_break"), breakExHeadJudgeSecond) == 1, QStringLiteral("break ex hold head should emit break judge"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("break"), breakExHeadJudgeSecond) == 1, QStringLiteral("break ex hold head should emit break cheer"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("ex"), breakExHeadJudgeSecond) == 1, QStringLiteral("break ex hold head should emit ex judge"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("judge_break"), breakExTailJudgeSecond) == 0, QStringLiteral("break ex hold tail should not emit break judge"), err)) {
        return false;
    }
    if (!require(countKindAt(QStringLiteral("ex"), breakExTailJudgeSecond) == 0, QStringLiteral("break ex hold tail should not emit ex judge"), err)) {
        return false;
    }
    if (!require(spans.isEmpty(), QStringLiteral("hold variants should not create touchhold spans"), err)) {
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
    miacode::preview_sfx_timeline::buildTimeline(markers, 1.0, PreviewTimingSettings(), &events, &spans);

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
    miacode::preview_sfx_timeline::buildTimeline(markers, 1.0, PreviewTimingSettings(), &events, &spans);

    int breakSlideStartCount = 0;
    int breakSlideBreakTailCount = 0;
    int plainBreakTailCount = 0;
    int flaggedBreakTailCount = 0;
    int judgeBreakSlideTailCount = 0;
    int legacyBreakSlideFinishCount = 0;

    for (const Event& event : events) {
        if (event.kind == QLatin1String("break_slide_start") && qAbs(event.second - 1.5) <= 1e-6) {
            ++breakSlideStartCount;
        } else if (event.kind == QLatin1String("break_slide_break") && qAbs(event.second - 3.0) <= 1e-6) {
            ++breakSlideBreakTailCount;
        } else if (event.kind == QLatin1String("break") && qAbs(event.second - 3.0) <= 1e-6) {
            ++plainBreakTailCount;
            if (event.breakSlideTailBreak) {
                ++flaggedBreakTailCount;
            }
        } else if (event.kind == QLatin1String("judge_break_slide") && qAbs(event.second - 3.0) <= 1e-6) {
            ++judgeBreakSlideTailCount;
        } else if (event.kind == QLatin1String("break_slide_finish") && qAbs(event.second - 3.0) <= 1e-6) {
            ++legacyBreakSlideFinishCount;
        }
    }

    if (!require(breakSlideStartCount == 1, QStringLiteral("break slide should emit one start SFX"), err)) {
        return false;
    }
    if (!require(plainBreakTailCount == 1, QStringLiteral("break slide tail should emit one ordinary break bucket event"), err)) {
        return false;
    }
    if (!require(flaggedBreakTailCount == 1, QStringLiteral("break slide tail break should be flagged for optional filtering"), err)) {
        return false;
    }
    if (!require(breakSlideBreakTailCount == 0, QStringLiteral("break slide tail should not emit a separate break_slide_break event"), err)) {
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

    const QVector<ScheduledPlayback> scheduledPlaybacks =
        miacode::preview_sfx_timeline::buildScheduledPlaybacks(events);
    int scheduledTailBreakCount = 0;
    for (const ScheduledPlayback& playback : scheduledPlaybacks) {
        if (playback.kind == QLatin1String("break_slide_tail_break") && qAbs(playback.second - 3.0) <= 1e-6) {
            ++scheduledTailBreakCount;
        }
    }
    if (!require(
            scheduledTailBreakCount == 1,
            QStringLiteral("break slide tail break should route to the Break Slide volume bucket after scheduling"),
            err)) {
        return false;
    }

    const QVector<ScheduledPlayback> mutedTailPlaybacks =
        miacode::preview_sfx_timeline::buildScheduledPlaybacks(events, true);
    for (const ScheduledPlayback& playback : mutedTailPlaybacks) {
        if (playback.kind == QLatin1String("break_slide_tail_break")) {
            return require(false, QStringLiteral("muting break-slide tail break should remove only the tail break playback"), err);
        }
    }

    return true;
}

bool verifySharedPlaybackScheduling(QTextStream& err)
{
    QVector<Event> events;

    Event answerA;
    answerA.second = 1.0;
    answerA.kind = QStringLiteral("answer");
    answerA.gain = 0.25;
    events.append(answerA);

    Event answerB = answerA;
    answerB.gain = 0.75;
    events.append(answerB);

    Event judge;
    judge.second = 1.0;
    judge.kind = QStringLiteral("judge");
    judge.gain = 0.50;
    events.append(judge);

    Event laterAnswer = answerA;
    laterAnswer.second = 1.5;
    laterAnswer.gain = 1.0;
    events.append(laterAnswer);

    const QVector<ScheduledPlayback> playbacks = miacode::preview_sfx_timeline::buildScheduledPlaybacks(events);
    if (!require(playbacks.size() == 3, QStringLiteral("scheduled playback should collapse same-second same-kind hits"), err)) {
        return false;
    }
    if (!require(
            playbacks.at(0).kind == QLatin1String("answer")
                && qAbs(playbacks.at(0).gain - 0.75) <= 1e-6
                && qAbs(playbacks.at(0).nextSameKindSecond - 1.5) <= 1e-6,
            QStringLiteral("scheduled playback should keep strongest same-second answer and annotate latest-wins"),
            err)) {
        return false;
    }
    if (!require(
            playbacks.at(1).kind == QLatin1String("judge")
                && qAbs(playbacks.at(1).gain - 0.50) <= 1e-6
                && playbacks.at(1).nextSameKindSecond < 0.0,
            QStringLiteral("scheduled playback should preserve non-conflicting kinds"),
            err)) {
        return false;
    }
    if (!require(
            playbacks.at(2).kind == QLatin1String("answer")
                && qAbs(playbacks.at(2).gain - 1.0) <= 1e-6
                && playbacks.at(2).nextSameKindSecond < 0.0,
            QStringLiteral("latest scheduled answer should not point to a later same-kind hit"),
            err)) {
        return false;
    }

    return true;
}

bool verifyPartialExportAnswerClamp(QTextStream& err)
{
    ScheduledPlayback answer;
    answer.second = adjustedAnswerSecond(1.0, 1.0, PreviewTimingSettings());
    answer.kind = QStringLiteral("answer");
    answer.originClampAllowanceSeconds =
        miacode::preview_sfx_timing::answerPreTriggerChartSeconds(1.0);

    ScheduledPlayback judge;
    judge.second = adjustedAnswerSecond(1.0, 1.0, PreviewTimingSettings());
    judge.kind = QStringLiteral("judge");

    if (!require(
            miacode::preview_sfx_timeline::scheduledPlaybackSurvivesTimelineOriginClamp(answer, 1.0),
            QStringLiteral("compensated answer should survive exact partial-export origin"),
            err)) {
        return false;
    }
    if (!require(
            !miacode::preview_sfx_timeline::scheduledPlaybackSurvivesTimelineOriginClamp(judge, 1.0),
            QStringLiteral("non-answer playback should not survive partial-export origin clamp"),
            err)) {
        return false;
    }
    if (!require(
            qAbs(miacode::preview_sfx_timeline::scheduledPlaybackMixSecond(answer, 1.0)) <= 1e-6,
            QStringLiteral("surviving compensated answer should clamp to frame zero"),
            err)) {
        return false;
    }

    return true;
}

bool verifyRateAwareAnswerAndJudgeTiming(QTextStream& err)
{
    TimelineNoteMarker tap;
    tap.type = QStringLiteral("tap");
    tap.second = 1.0;
    QVector<TimelineNoteMarker> markers{tap};

    const struct {
        double rate;
        double expectedRealMs;
    } cases[] = {
        {0.5, 33.3333334},
        {1.0, 16.6666667},
        {2.0, 8.33333335},
    };

    for (const auto& testCase : cases) {
        QVector<Event> events;
        QVector<TouchholdSpan> spans;
        miacode::preview_sfx_timeline::buildTimeline(markers, testCase.rate, PreviewTimingSettings(), &events, &spans);

        double answerSecond = -1.0;
        double judgeSecond = -1.0;
        for (const Event& event : events) {
            if (event.kind == QLatin1String("answer")) {
                answerSecond = event.second;
            } else if (event.kind == QLatin1String("judge")) {
                judgeSecond = event.second;
            }
        }

        if (!require(answerSecond >= 0.0, QStringLiteral("answer timing should exist"), err)) {
            return false;
        }
        if (!require(judgeSecond >= 0.0, QStringLiteral("judge timing should exist"), err)) {
            return false;
        }

        const double answerEarlyMs = realDeltaMs(tap.second - answerSecond, testCase.rate);
        const double judgeEarlyMs = realDeltaMs(tap.second - judgeSecond, testCase.rate);
        if (!require(qAbs(answerEarlyMs - testCase.expectedRealMs) <= 0.01,
                     QStringLiteral("answer chart-domain pre-trigger should scale in real time with playback rate like Majdata View"),
                     err)) {
            return false;
        }
        if (!require(qAbs(judgeEarlyMs - testCase.expectedRealMs) <= 0.01,
                     QStringLiteral("judge chart-domain pre-trigger should scale in real time with playback rate like Majdata View"),
                     err)) {
            return false;
        }
    }

    return true;
}

bool verifySlideTrackTimingAvoidsFixedJudgeCompensation(QTextStream& err)
{
    TimelineNoteMarker slide;
    slide.type = QStringLiteral("slide");
    slide.second = 1.0;
    slide.slideTraceSecond = 1.5;
    slide.endSecond = 2.5;

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline({slide}, 0.5, PreviewTimingSettings(), &events, &spans);

    int exactTraceCount = 0;
    for (const Event& event : events) {
        if (event.kind == QLatin1String("slide") && qAbs(event.second - 1.5) <= 1e-6) {
            ++exactTraceCount;
        }
    }

    return require(
        exactTraceCount == 1,
        QStringLiteral("slide track timing should stay on trace second instead of taking fixed judge compensation"),
        err);
}

bool verifyOffsetLayerSemantics(QTextStream& err)
{
    TimelineNoteMarker tap;
    tap.type = QStringLiteral("tap");
    tap.second = 1.0;
    QVector<TimelineNoteMarker> markers{tap};

    PreviewTimingSettings settings;
    settings.audioOffsetSeconds = 0.1;
    settings.displayOffsetSeconds = 0.1;
    settings.judgeOffsetSeconds = 0.1;
    settings.answerOffsetSeconds = 0.1;

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline(markers, 0.5, settings, &events, &spans);

    double answerSecond = -1.0;
    double judgeSecond = -1.0;
    for (const Event& event : events) {
        if (event.kind == QLatin1String("answer")) {
            answerSecond = event.second;
        } else if (event.kind == QLatin1String("judge")) {
            judgeSecond = event.second;
        }
    }

    if (!require(answerSecond >= 0.0 && judgeSecond >= 0.0,
                 QStringLiteral("offset-layer timing should emit both answer and judge events"),
                 err)) {
        return false;
    }

    const double answerRealOffsetMs = realDeltaMs(answerSecond - tap.second, 0.5);
    const double judgeRealOffsetMs = realDeltaMs(judgeSecond - tap.second, 0.5);
    if (!require(qAbs(answerRealOffsetMs - 166.6666667) <= 0.02,
                 QStringLiteral("answer should apply audio and answer offsets while positive display offset advances the answer family"),
                 err)) {
        return false;
    }
    if (!require(qAbs(judgeRealOffsetMs - 166.6666667) <= 0.02,
                 QStringLiteral("judge should apply audio and judge offsets while positive display offset advances the judge family"),
                 err)) {
        return false;
    }

    return true;
}

bool verifyDisplayOffsetDoesNotRetargetSlideFamily(QTextStream& err)
{
    TimelineNoteMarker slide;
    slide.type = QStringLiteral("slide");
    slide.second = 1.0;
    slide.slideTraceSecond = 1.5;
    slide.endSecond = 2.0;

    PreviewTimingSettings settings;
    settings.audioOffsetSeconds = 0.1;
    settings.displayOffsetSeconds = 0.1;

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline({slide}, 1.0, settings, &events, &spans);

    int shiftedTrackCount = 0;
    int displayShiftedTrackCount = 0;
    for (const Event& event : events) {
        if (event.kind == QLatin1String("slide") && qAbs(event.second - 1.6) <= 1e-6) {
            ++shiftedTrackCount;
        }
        if (event.kind == QLatin1String("slide") && qAbs(event.second - 1.5) <= 1e-6) {
            ++displayShiftedTrackCount;
        }
    }

    if (!require(
            shiftedTrackCount == 1,
            QStringLiteral("slide-family timing should keep only the global chart-domain shift and ignore display offset"),
            err)) {
        return false;
    }
    if (!require(
            displayShiftedTrackCount == 0,
            QStringLiteral("slide-family timing should not inherit Majdata View display/judge layering"),
            err)) {
        return false;
    }

    return true;
}

bool verifyTouchholdVoiceLatestWinsOwnership(QTextStream& err)
{
    using miacode::preview_sfx_timeline::touchholdOwnerSpanIndexAt;

    const auto span = [](double start, double end) {
        TouchholdSpan s;
        s.startSecond = start;
        s.endSecond = end;
        return s;
    };

    // Seamless join: span 0 = [1,3], span 1 = [3,5]. At the boundary the later
    // span must own the voice so the second touch-hold sounds instead of the
    // first's stop clobbering it.
    {
        QVector<TouchholdSpan> spans{span(1.0, 3.0), span(3.0, 5.0)};
        if (!require(touchholdOwnerSpanIndexAt(spans, 2.0) == 0, QStringLiteral("seamless: first span owns before the join"), err)) {
            return false;
        }
        if (!require(touchholdOwnerSpanIndexAt(spans, 3.0) == 1, QStringLiteral("seamless: second span owns at the join instant"), err)) {
            return false;
        }
        if (!require(touchholdOwnerSpanIndexAt(spans, 4.0) == 1, QStringLiteral("seamless: second span owns after the join"), err)) {
            return false;
        }
        if (!require(touchholdOwnerSpanIndexAt(spans, 5.0) == -1, QStringLiteral("seamless: no owner once the second span ends"), err)) {
            return false;
        }
    }

    // True overlap: span 0 = [1,4], span 1 = [2,5]. While both are active the
    // most-recently-started span (1) wins — the newer note takes over, the
    // older note's stop must not kill it.
    {
        QVector<TouchholdSpan> spans{span(1.0, 4.0), span(2.0, 5.0)};
        if (!require(touchholdOwnerSpanIndexAt(spans, 1.5) == 0, QStringLiteral("overlap: only the first span is active early"), err)) {
            return false;
        }
        if (!require(touchholdOwnerSpanIndexAt(spans, 3.0) == 1, QStringLiteral("overlap: later span takes over while both active"), err)) {
            return false;
        }
        if (!require(touchholdOwnerSpanIndexAt(spans, 4.5) == 1, QStringLiteral("overlap: first span's end does not steal ownership from the survivor"), err)) {
            return false;
        }
    }

    // Nesting: span 0 = [1,10] fully contains span 1 = [3,5]. The inner span
    // owns while active; ownership returns to the outer span once the inner ends.
    {
        QVector<TouchholdSpan> spans{span(1.0, 10.0), span(3.0, 5.0)};
        if (!require(touchholdOwnerSpanIndexAt(spans, 4.0) == 1, QStringLiteral("nesting: inner span owns while active"), err)) {
            return false;
        }
        if (!require(touchholdOwnerSpanIndexAt(spans, 6.0) == 0, QStringLiteral("nesting: outer span resumes ownership after the inner ends"), err)) {
            return false;
        }
    }

    return true;
}

bool verifyMineNotesEmitTypeSfx(QTextStream& err)
{
    // Mine judgement remains an autoplay dodge, but its chart timing stays
    // audible through the same type-based SFX used by ordinary notes.
    QVector<TimelineNoteMarker> markers;

    TimelineNoteMarker mineTap;
    mineTap.type = QStringLiteral("tap");
    mineTap.second = 1.0;
    mineTap.isMine = true;
    mineTap.isBreak = true;
    markers.append(mineTap);

    TimelineNoteMarker mineTouchHold;
    mineTouchHold.type = QStringLiteral("touch_hold");
    mineTouchHold.second = 2.0;
    mineTouchHold.endSecond = 3.0;
    mineTouchHold.isMine = true;
    markers.append(mineTouchHold);

    TimelineNoteMarker mineSlide;
    mineSlide.type = QStringLiteral("slide");
    mineSlide.second = 4.0;
    mineSlide.endSecond = 5.0;
    mineSlide.trackMine = true;
    markers.append(mineSlide);

    TimelineNoteMarker normalTap;
    normalTap.type = QStringLiteral("tap");
    normalTap.second = 6.0;
    markers.append(normalTap);

    QVector<Event> events;
    QVector<TouchholdSpan> spans;
    miacode::preview_sfx_timeline::buildTimeline(markers, 1.0, PreviewTimingSettings(), &events, &spans);

    if (!require(spans.size() == 1, QStringLiteral("[mine] touch-hold mine emits its sustain span"), err)) {
        return false;
    }
    int answerCount = 0;
    int breakCount = 0;
    int touchCount = 0;
    int slideCount = 0;
    int touchholdStartCount = 0;
    int touchholdStopCount = 0;
    for (const Event& event : events) {
        if (event.kind == QLatin1String("answer")) {
            ++answerCount;
        } else if (event.kind == QLatin1String("break")) {
            ++breakCount;
        } else if (event.kind == QLatin1String("touch")) {
            ++touchCount;
        } else if (event.kind == QLatin1String("slide")) {
            ++slideCount;
        } else if (event.kind == QLatin1String("touchhold_start")) {
            ++touchholdStartCount;
        } else if (event.kind == QLatin1String("touchhold_stop")) {
            ++touchholdStopCount;
        }
    }
    if (!require(answerCount == 5, QStringLiteral("[mine] all mine heads/tails emit answer timing"), err)
        && require(breakCount == 1, QStringLiteral("[mine] break mine emits break SFX"), err)
        && require(touchCount == 1, QStringLiteral("[mine] touch-hold mine emits touch SFX"), err)
        && require(slideCount == 1, QStringLiteral("[mine] slide mine emits slide SFX"), err)
        && require(touchholdStartCount == 1 && touchholdStopCount == 1,
                   QStringLiteral("[mine] touch-hold mine starts and stops sustain SFX"), err)) {
        return false;
    }

    miacode::preview_sfx_timeline::buildTimeline(
        markers, 1.0, PreviewTimingSettings(), &events, &spans, false);
    if (!require(spans.isEmpty(), QStringLiteral("[mine switch] disabled mine touch-hold has no sustain span"), err)) {
        return false;
    }
    int normalAnswerCount = 0;
    int normalJudgeCount = 0;
    for (const Event& event : events) {
        normalAnswerCount += event.kind == QLatin1String("answer") ? 1 : 0;
        normalJudgeCount += event.kind == QLatin1String("judge") ? 1 : 0;
    }
    return require(events.size() == 2, QStringLiteral("[mine switch] only the normal tap remains audible"), err)
        && require(normalAnswerCount == 1 && normalJudgeCount == 1,
                   QStringLiteral("[mine switch] ordinary note SFX remain unchanged"), err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyHoldTailJudgeAndTouchHoldTailAnswer(err)) {
        return 1;
    }
    if (!verifyMajdataHoldVariantSfx(err)) {
        return 1;
    }
    if (!verifyAnswerTimingCompensation(err)) {
        return 1;
    }
    if (!verifyBreakSlideTailSfx(err)) {
        return 1;
    }
    if (!verifySharedPlaybackScheduling(err)) {
        return 1;
    }
    if (!verifyPartialExportAnswerClamp(err)) {
        return 1;
    }
    if (!verifyRateAwareAnswerAndJudgeTiming(err)) {
        return 1;
    }
    if (!verifySlideTrackTimingAvoidsFixedJudgeCompensation(err)) {
        return 1;
    }
    if (!verifyOffsetLayerSemantics(err)) {
        return 1;
    }
    if (!verifyDisplayOffsetDoesNotRetargetSlideFamily(err)) {
        return 1;
    }
    if (!verifyTouchholdVoiceLatestWinsOwnership(err)) {
        return 1;
    }
    if (!verifyMineNotesEmitTypeSfx(err)) {
        return 1;
    }

    out << "preview_sfx_timeline_spec ok" << Qt::endl;
    return 0;
}
