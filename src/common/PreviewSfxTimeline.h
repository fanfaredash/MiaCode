#pragma once

#include <QVector>
#include <QHash>
#include <QString>
#include <QtMath>

#include <algorithm>

#include "PreviewGameplayConfig.h"
#include "PreviewSfxSemantics.h"
#include "timeline/TimelineData.h"

namespace miacode::preview_sfx_timeline {

constexpr double kTimelineEpsilonSeconds = 1e-6;
constexpr double kAnswerTriggerCompensationSeconds = 1.0 / 60.0;
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

struct AggregatedPlayback {
    QString kind;
    int count = 0;
    double maxGain = 0.0;
};

struct CollapsedEventGroup {
    double second = 0.0;
    QVector<Event> orderedEvents;
    QVector<AggregatedPlayback> aggregatedPlaybacks;
};

struct ScheduledPlayback {
    double second = 0.0;
    QString kind;
    double gain = 1.0;
    double nextSameKindSecond = -1.0;
};

inline double adjustedAnswerSecond(double second)
{
    return qMax(0.0, second - kAnswerTriggerCompensationSeconds);
}

inline void accumulateAggregatedPlayback(QVector<AggregatedPlayback>* playbacks, const QString& kind, double gain)
{
    if (playbacks == nullptr || kind.isEmpty()) {
        return;
    }
    for (AggregatedPlayback& playback : *playbacks) {
        if (playback.kind != kind) {
            continue;
        }
        ++playback.count;
        playback.maxGain = qMax(playback.maxGain, qMax(0.0, gain));
        return;
    }

    AggregatedPlayback playback;
    playback.kind = kind;
    playback.count = 1;
    playback.maxGain = qMax(0.0, gain);
    playbacks->append(playback);
}

inline double aggregatedPlaybackGain(const AggregatedPlayback& playback)
{
    return previewSfxPlaybackGainForAggregate(playback.kind, playback.count, playback.maxGain);
}

inline int eventGroupEndIndex(const QVector<Event>& events, int groupStart)
{
    if (groupStart < 0 || groupStart >= events.size()) {
        return groupStart;
    }
    int groupEnd = groupStart + 1;
    while (groupEnd < events.size()
           && qAbs(events[groupEnd].second - events[groupStart].second) <= kTimelineEpsilonSeconds) {
        ++groupEnd;
    }
    return groupEnd;
}

inline CollapsedEventGroup collapseEventGroup(
    const QVector<Event>& events,
    int groupStart,
    int groupEnd
)
{
    CollapsedEventGroup group;
    if (groupStart < 0 || groupStart >= events.size() || groupEnd <= groupStart) {
        return group;
    }

    group.second = events[groupStart].second;
    group.orderedEvents.reserve(groupEnd - groupStart);
    for (int i = groupStart; i < groupEnd; ++i) {
        const Event& event = events[i];
        if (previewSfxShouldAggregateKind(event.kind)) {
            accumulateAggregatedPlayback(&group.aggregatedPlaybacks, event.kind, event.gain);
            continue;
        }
        group.orderedEvents.append(event);
    }
    return group;
}

inline void annotateScheduledPlaybackLatestWins(QVector<ScheduledPlayback>* playbacks)
{
    if (playbacks == nullptr) {
        return;
    }

    QHash<QString, double> nextPlaybackSecondByKind;
    for (int i = playbacks->size() - 1; i >= 0; --i) {
        ScheduledPlayback& playback = (*playbacks)[i];
        const QString normalizedKind = previewSfxNormalizedKind(playback.kind);
        if (previewSfxShouldInterruptPreviousKind(normalizedKind)
            && nextPlaybackSecondByKind.contains(normalizedKind)) {
            playback.nextSameKindSecond = nextPlaybackSecondByKind.value(normalizedKind);
        }
        nextPlaybackSecondByKind.insert(normalizedKind, playback.second);
    }
}

inline QVector<ScheduledPlayback> buildScheduledPlaybacks(const QVector<Event>& events)
{
    QVector<ScheduledPlayback> playbacks;
    int index = 0;
    while (index < events.size()) {
        const int groupEnd = eventGroupEndIndex(events, index);
        const CollapsedEventGroup group = collapseEventGroup(events, index, groupEnd);

        for (const Event& event : group.orderedEvents) {
            if (event.kind == QLatin1String("touchhold_start")
                || event.kind == QLatin1String("touchhold_stop")) {
                continue;
            }
            ScheduledPlayback playback;
            playback.second = event.second;
            playback.kind = event.kind;
            playback.gain = event.gain;
            playbacks.append(playback);
        }
        for (const AggregatedPlayback& playback : group.aggregatedPlaybacks) {
            ScheduledPlayback scheduled;
            scheduled.second = group.second;
            scheduled.kind = playback.kind;
            scheduled.gain = aggregatedPlaybackGain(playback);
            playbacks.append(scheduled);
        }
        index = groupEnd;
    }

    annotateScheduledPlaybackLatestWins(&playbacks);
    return playbacks;
}

inline bool scheduledPlaybackSurvivesTimelineOriginClamp(
    const ScheduledPlayback& playback,
    double timelineOriginSecond
)
{
    if (playback.second + kTimelineEpsilonSeconds >= timelineOriginSecond) {
        return true;
    }
    return previewSfxNormalizedKind(playback.kind) == QLatin1String("answer")
        && playback.second + kAnswerTriggerCompensationSeconds + kTimelineEpsilonSeconds >= timelineOriginSecond;
}

inline double scheduledPlaybackMixSecond(const ScheduledPlayback& playback, double timelineOriginSecond)
{
    return qMax(0.0, playback.second - timelineOriginSecond);
}

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
            addEvent(adjustedAnswerSecond(marker.second), QStringLiteral("answer"));
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
            addEvent(adjustedAnswerSecond(marker.second), QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
            if (marker.isBreak) {
                addEvent(marker.second, QStringLiteral("break"));
            }
            if (marker.endSecond > marker.second) {
                addEvent(adjustedAnswerSecond(marker.endSecond), QStringLiteral("answer"));
            }
            if (marker.isEx) {
                addEvent(marker.second, QStringLiteral("ex"));
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            addEvent(adjustedAnswerSecond(marker.second), QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework) {
                addEvent(marker.second + kFireworkTouchTriggerDelaySeconds, QStringLiteral("firework"));
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            addEvent(adjustedAnswerSecond(marker.second), QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework && marker.endSecond >= 0.0) {
                addEvent(marker.endSecond, QStringLiteral("firework"));
            }
            if (marker.endSecond > marker.second) {
                addEvent(adjustedAnswerSecond(marker.endSecond), QStringLiteral("answer"));
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
                addEvent(adjustedAnswerSecond(marker.second), QStringLiteral("answer"));
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
                addEvent(marker.endSecond, QStringLiteral("break"));
                addEvent(marker.endSecond, QStringLiteral("judge_break_slide"));
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
