#pragma once

#include <QVector>
#include <QString>
#include <QtMath>

#include <algorithm>

#include "PreviewGameplayConfig.h"
#include "TimelineView.h"

namespace miacode::preview_sfx_timeline {

constexpr double kTimelineEpsilonSeconds = 1e-6;
constexpr double kFireworkTouchTriggerDelaySeconds =
    miacode::preview_gameplay::kJudgeEffectFireworkTouchTriggerDelaySeconds;

struct Event {
    double second = 0.0;
    int priority = 0;
    QString kind;
    int spanIndex = -1;
    double gain = 1.0;
};

struct TouchholdSpan {
    double startSecond = 0.0;
    double endSecond = 0.0;
};

inline void buildTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    QVector<Event>* events,
    QVector<TouchholdSpan>* touchholdSpans
)
{
    if (events == nullptr || touchholdSpans == nullptr) {
        return;
    }

    events->clear();
    touchholdSpans->clear();
    touchholdSpans->reserve(noteMarkers.size());
    events->reserve(noteMarkers.size() * 5);

    const auto addEvent = [events](double second, const QString& kind, int priority = 1, int spanIndex = -1, double gain = 1.0) {
        if (second < 0.0 || kind.isEmpty()) {
            return;
        }
        Event event;
        event.second = second;
        event.priority = priority;
        event.kind = kind;
        event.spanIndex = spanIndex;
        event.gain = qMax(0.0, gain);
        events->append(event);
    };

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString type = marker.type.toLower();
        if (type == QLatin1String("tap")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
            if (marker.isBreak) {
                addEvent(marker.second, QStringLiteral("break"));
            }
            if (marker.isEx) {
                addEvent(marker.second, QStringLiteral("ex"));
            }
            continue;
        }
        if (type == QLatin1String("hold")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
            if (marker.isBreak) {
                addEvent(marker.second, QStringLiteral("break"));
            }
            if (marker.endSecond > marker.second) {
                addEvent(marker.endSecond, QStringLiteral("answer"));
            }
            if (marker.isEx) {
                addEvent(marker.second, QStringLiteral("ex"));
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework) {
                addEvent(marker.second + kFireworkTouchTriggerDelaySeconds, QStringLiteral("firework"));
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework && marker.endSecond >= 0.0) {
                addEvent(marker.endSecond, QStringLiteral("firework"));
            }
            if (marker.endSecond > marker.second) {
                TouchholdSpan span;
                span.startSecond = marker.second;
                span.endSecond = marker.endSecond;
                const int spanIndex = touchholdSpans->size();
                touchholdSpans->append(span);
                addEvent(span.startSecond, QStringLiteral("touchhold_start"), 0, spanIndex);
                addEvent(span.endSecond, QStringLiteral("touchhold_stop"), 2, spanIndex);
            }
            continue;
        }
        if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
            if (marker.hasHeadStar) {
                addEvent(marker.second, QStringLiteral("answer"));
                addEvent(marker.second, marker.headBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
                if (marker.headBreak && !marker.trackBreak) {
                    addEvent(marker.second, QStringLiteral("break"));
                }
                if (marker.headEx) {
                    addEvent(marker.second, QStringLiteral("ex"));
                }
            }
            const double traceSecond = marker.slideTraceSecond >= 0.0 ? marker.slideTraceSecond : marker.second;
            addEvent(traceSecond, marker.trackBreak ? QStringLiteral("break_slide_start") : QStringLiteral("slide"));
            if (marker.trackBreak && marker.endSecond > traceSecond) {
                addEvent(marker.endSecond, QStringLiteral("break_slide_finish"), 1, -1, 0.5);
                addEvent(marker.endSecond, QStringLiteral("judge_break_slide"), 1, -1, 0.5);
            }
        }
    }

    std::sort(events->begin(), events->end(), [](const Event& a, const Event& b) {
        if (qAbs(a.second - b.second) > kTimelineEpsilonSeconds) {
            return a.second < b.second;
        }
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.kind < b.kind;
    });
}

}  // namespace miacode::preview_sfx_timeline
