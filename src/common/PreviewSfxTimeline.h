#pragma once

#include <QVector>
#include <QHash>
#include <QString>
#include <QtMath>

#include <algorithm>

#include "PreviewGameplayConfig.h"
#include "PreviewSfxSemantics.h"
#include "PreviewSfxTiming.h"
#include "timeline/TimelineData.h"

namespace miacode::preview_sfx_timeline {

constexpr double kTimelineEpsilonSeconds = 1e-6;

struct Event {
    double second = 0.0;
    int priority = 0;
    QString kind;
    int spanIndex = -1;
    double gain = 1.0;
    double originClampAllowanceSeconds = 0.0;
    bool breakSlideTailBreak = false;
};

struct TouchholdSpan {
    double startSecond = 0.0;
    double endSecond = 0.0;
};

struct AggregatedPlayback {
    QString kind;
    int count = 0;
    double maxGain = 0.0;
    double maxOriginClampAllowanceSeconds = 0.0;
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
    double originClampAllowanceSeconds = 0.0;
};

inline double adjustedAnswerSecond(
    double second,
    double playbackRate,
    const PreviewTimingSettings& settings)
{
    return qMax(0.0, miacode::preview_sfx_timing::answerTriggerSecond(second, settings, playbackRate));
}

inline double adjustedAnswerSecond(double second)
{
    return adjustedAnswerSecond(second, 1.0, PreviewTimingSettings());
}

inline void accumulateAggregatedPlayback(
    QVector<AggregatedPlayback>* playbacks,
    const QString& kind,
    double gain,
    double originClampAllowanceSeconds)
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
        playback.maxOriginClampAllowanceSeconds = qMax(
            playback.maxOriginClampAllowanceSeconds,
            qMax(0.0, originClampAllowanceSeconds));
        return;
    }

    AggregatedPlayback playback;
    playback.kind = kind;
    playback.count = 1;
    playback.maxGain = qMax(0.0, gain);
    playback.maxOriginClampAllowanceSeconds = qMax(0.0, originClampAllowanceSeconds);
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
    int groupEnd,
    bool muteBreakSlideTailBreak = false
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
        if (muteBreakSlideTailBreak && event.breakSlideTailBreak) {
            continue;
        }
        const QString aggregateKind = event.breakSlideTailBreak
            ? QStringLiteral("break_slide_tail_break")
            : event.kind;
        if (previewSfxShouldAggregateKind(aggregateKind)) {
            accumulateAggregatedPlayback(
                &group.aggregatedPlaybacks,
                aggregateKind,
                event.gain,
                event.originClampAllowanceSeconds);
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

inline QVector<ScheduledPlayback> buildScheduledPlaybacks(
    const QVector<Event>& events,
    bool muteBreakSlideTailBreak = false)
{
    QVector<ScheduledPlayback> playbacks;
    int index = 0;
    while (index < events.size()) {
        const int groupEnd = eventGroupEndIndex(events, index);
        const CollapsedEventGroup group = collapseEventGroup(events, index, groupEnd, muteBreakSlideTailBreak);

        for (const Event& event : group.orderedEvents) {
            if (event.kind == QLatin1String("touchhold_start")
                || event.kind == QLatin1String("touchhold_stop")) {
                continue;
            }
            ScheduledPlayback playback;
            playback.second = event.second;
            playback.kind = event.kind;
            playback.gain = event.gain;
            playback.originClampAllowanceSeconds = event.originClampAllowanceSeconds;
            playbacks.append(playback);
        }
        for (const AggregatedPlayback& playback : group.aggregatedPlaybacks) {
            ScheduledPlayback scheduled;
            scheduled.second = group.second;
            scheduled.kind = playback.kind;
            scheduled.gain = aggregatedPlaybackGain(playback);
            scheduled.originClampAllowanceSeconds = playback.maxOriginClampAllowanceSeconds;
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
    return playback.second + qMax(0.0, playback.originClampAllowanceSeconds) + kTimelineEpsilonSeconds
        >= timelineOriginSecond;
}

inline double scheduledPlaybackMixSecond(const ScheduledPlayback& playback, double timelineOriginSecond)
{
    return qMax(0.0, playback.second - timelineOriginSecond);
}

// Owner of the single shared touch-hold voice at `second`, using latest-wins
// semantics: among all spans whose [startSecond, endSecond) window contains
// `second`, the one that began most recently (greatest startSecond) wins.
// Returns -1 when no span is active. This is what lets a newer touch-hold take
// over the voice from an older one (seamless join, overlap, or nesting) instead
// of the older note clobbering the newer one — see runtime reconcile callers in
// BassPreviewAudioBackend / MiniaudioPreviewAudioBackend.
inline int touchholdOwnerSpanIndexAt(const QVector<TouchholdSpan>& spans, double second)
{
    int owner = -1;
    for (int i = 0; i < spans.size(); ++i) {
        const TouchholdSpan& span = spans[i];
        if (second + kTimelineEpsilonSeconds < span.startSecond) {
            continue;  // not started yet
        }
        if (second + kTimelineEpsilonSeconds >= span.endSecond) {
            continue;  // already finished (also excludes the prior span at a seamless join)
        }
        if (owner < 0 || span.startSecond >= spans[owner].startSecond) {
            owner = i;
        }
    }
    return owner;
}

// One stretch of time during which a single span owns the shared touch-hold
// voice. `sourceOffsetSecond` is where the voice sits inside the riser sample at
// `startSecond`: 0 when the owning span begins here (it restarts the sample),
// and > 0 when ownership falls back to an older span that has been running
// underneath a newer one — exactly the offset reconcileTouchholdVoice() seeks to.
struct TouchholdOwnershipSegment {
    double startSecond = 0.0;
    double endSecond = 0.0;
    int spanIndex = -1;
    double sourceOffsetSecond = 0.0;
};

// Flattens latest-wins ownership into the piecewise playback the preview voice
// actually produces. Ownership can only change at a span boundary, so evaluating
// touchholdOwnerSpanIndexAt() once per boundary interval reproduces the runtime
// exactly. Preview reconciles a live voice against that same helper event by
// event; export has no voice, so it mixes one clip per segment from here
// (buildTouchholdSpanPlaybacks in VideoExportAudioRenderPlan.cpp). Both sides
// must keep resolving ownership through touchholdOwnerSpanIndexAt() so a
// seamless join, overlap or nesting sounds the same in the preview and the file.
inline QVector<TouchholdOwnershipSegment> buildTouchholdOwnershipSegments(
    const QVector<TouchholdSpan>& spans)
{
    QVector<double> boundaries;
    boundaries.reserve(spans.size() * 2);
    for (const TouchholdSpan& span : spans) {
        if (span.endSecond <= span.startSecond + kTimelineEpsilonSeconds) {
            continue;
        }
        boundaries.append(span.startSecond);
        boundaries.append(span.endSecond);
    }
    std::sort(boundaries.begin(), boundaries.end());

    QVector<TouchholdOwnershipSegment> segments;
    segments.reserve(boundaries.size());
    for (int i = 0; i + 1 < boundaries.size(); ++i) {
        const double startSecond = boundaries[i];
        const double endSecond = boundaries[i + 1];
        if (endSecond <= startSecond + kTimelineEpsilonSeconds) {
            continue;  // duplicate boundary, e.g. both sides of a seamless join
        }
        const int owner = touchholdOwnerSpanIndexAt(spans, startSecond);
        if (owner < 0) {
            continue;  // gap between spans — the voice is stopped here
        }
        if (!segments.isEmpty()
            && segments.last().spanIndex == owner
            && segments.last().endSecond + kTimelineEpsilonSeconds >= startSecond) {
            // A boundary the owner took no part in (some other span started or
            // ended underneath it) — keep it as one uninterrupted playback.
            segments.last().endSecond = endSecond;
            continue;
        }
        TouchholdOwnershipSegment segment;
        segment.startSecond = startSecond;
        segment.endSecond = endSecond;
        segment.spanIndex = owner;
        segment.sourceOffsetSecond = qMax(0.0, startSecond - spans[owner].startSecond);
        segments.append(segment);
    }
    return segments;
}

inline void buildTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings,
    QVector<Event>* events,
    QVector<TouchholdSpan>* touchholdSpans,
    bool mineSfxEnabled = true
)
{
    if (events == nullptr || touchholdSpans == nullptr) {
        return;
    }

    events->clear();
    touchholdSpans->clear();
    touchholdSpans->reserve(noteMarkers.size());
    events->reserve(noteMarkers.size() * 5);

    PreviewTimingSettings normalizedTimingSettings = timingSettings;
    normalizedTimingSettings.normalize();

    const auto addEvent = [events](
                              double second,
                              const QString& kind,
                              int priority = 1,
                              int spanIndex = -1,
                              double gain = 1.0,
                              double originClampAllowanceSeconds = 0.0,
                              bool breakSlideTailBreak = false) {
        if (second < 0.0 || kind.isEmpty()) {
            return;
        }
        Event event;
        event.second = second;
        event.priority = priority;
        event.kind = kind;
        event.spanIndex = spanIndex;
        event.gain = qMax(0.0, gain);
        event.originClampAllowanceSeconds = qMax(0.0, originClampAllowanceSeconds);
        event.breakSlideTailBreak = breakSlideTailBreak;
        events->append(event);
    };

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!mineSfxEnabled && (marker.isMine || marker.trackMine)) {
            continue;
        }
        const QString type = marker.type.toLower();
        // Enabled mines keep their avoid-note semantics while emitting the
        // same type-based SFX as ordinary notes.
        const double answerCompensationSeconds =
            miacode::preview_sfx_timing::answerPreTriggerChartSeconds(playbackRate);
        if (type == QLatin1String("tap")) {
            addEvent(
                adjustedAnswerSecond(marker.second, playbackRate, normalizedTimingSettings),
                QStringLiteral("answer"),
                1,
                -1,
                1.0,
                answerCompensationSeconds);
            const double judgeSecond =
                qMax(0.0, miacode::preview_sfx_timing::judgeTriggerSecond(marker.second, normalizedTimingSettings, playbackRate));
            addEvent(judgeSecond, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
            if (marker.isBreak) {
                addEvent(judgeSecond, QStringLiteral("break"));
            }
            if (marker.isEx) {
                addEvent(judgeSecond, QStringLiteral("ex"));
            }
            continue;
        }
        if (type == QLatin1String("hold")) {
            addEvent(
                adjustedAnswerSecond(marker.second, playbackRate, normalizedTimingSettings),
                QStringLiteral("answer"),
                1,
                -1,
                1.0,
                answerCompensationSeconds);
            const double judgeSecond =
                qMax(0.0, miacode::preview_sfx_timing::judgeTriggerSecond(marker.second, normalizedTimingSettings, playbackRate));
            if (marker.isBreak) {
                addEvent(judgeSecond, QStringLiteral("judge_break"));
                addEvent(judgeSecond, QStringLiteral("break"));
            }
            if (marker.isEx) {
                addEvent(judgeSecond, QStringLiteral("ex"));
            }
            if (!marker.isBreak && !marker.isEx) {
                addEvent(judgeSecond, QStringLiteral("judge"));
            }
            if (marker.endSecond > marker.second) {
                addEvent(
                    adjustedAnswerSecond(marker.endSecond, playbackRate, normalizedTimingSettings),
                    QStringLiteral("answer"),
                    1,
                    -1,
                    1.0,
                    answerCompensationSeconds);
                if (!marker.isBreak && !marker.isEx) {
                    const double tailJudgeSecond =
                        qMax(0.0, miacode::preview_sfx_timing::judgeTriggerSecond(marker.endSecond, normalizedTimingSettings, playbackRate));
                    addEvent(tailJudgeSecond, QStringLiteral("judge"));
                }
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            const double judgeSecond =
                qMax(0.0, miacode::preview_sfx_timing::judgeTriggerSecond(marker.second, normalizedTimingSettings, playbackRate));
            addEvent(
                adjustedAnswerSecond(marker.second, playbackRate, normalizedTimingSettings),
                QStringLiteral("answer"),
                1,
                -1,
                1.0,
                answerCompensationSeconds);
            addEvent(judgeSecond, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework) {
                addEvent(
                    qMax(0.0, miacode::preview_sfx_timing::slideTriggerSecond(marker.second, normalizedTimingSettings, playbackRate)),
                    QStringLiteral("firework"));
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            const double judgeSecond =
                qMax(0.0, miacode::preview_sfx_timing::judgeTriggerSecond(marker.second, normalizedTimingSettings, playbackRate));
            addEvent(
                adjustedAnswerSecond(marker.second, playbackRate, normalizedTimingSettings),
                QStringLiteral("answer"),
                1,
                -1,
                1.0,
                answerCompensationSeconds);
            addEvent(judgeSecond, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework && marker.endSecond >= 0.0) {
                addEvent(
                    qMax(0.0, miacode::preview_sfx_timing::slideTriggerSecond(marker.endSecond, normalizedTimingSettings, playbackRate)),
                    QStringLiteral("firework"));
            }
            if (marker.endSecond > marker.second) {
                addEvent(
                    adjustedAnswerSecond(marker.endSecond, playbackRate, normalizedTimingSettings),
                    QStringLiteral("answer"),
                    1,
                    -1,
                    1.0,
                    answerCompensationSeconds);
            }
            if (marker.endSecond > marker.second) {
                TouchholdSpan span;
                span.startSecond = qMax(
                    0.0,
                    miacode::preview_sfx_timing::slideTriggerSecond(marker.second, normalizedTimingSettings, playbackRate));
                span.endSecond = qMax(
                    0.0,
                    miacode::preview_sfx_timing::slideTriggerSecond(marker.endSecond, normalizedTimingSettings, playbackRate));
                if (span.endSecond <= span.startSecond) {
                    continue;
                }
                const int spanIndex = touchholdSpans->size();
                touchholdSpans->append(span);
                addEvent(span.startSecond, QStringLiteral("touchhold_start"), 0, spanIndex);
                addEvent(span.endSecond, QStringLiteral("touchhold_stop"), 2, spanIndex);
            }
            continue;
        }
        if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
            if (marker.hasHeadStar) {
                const double judgeSecond =
                    qMax(0.0, miacode::preview_sfx_timing::judgeTriggerSecond(marker.second, normalizedTimingSettings, playbackRate));
                addEvent(
                    adjustedAnswerSecond(marker.second, playbackRate, normalizedTimingSettings),
                    QStringLiteral("answer"),
                    1,
                    -1,
                    1.0,
                    answerCompensationSeconds);
                addEvent(judgeSecond, marker.headBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
                if (marker.headBreak) {
                    addEvent(judgeSecond, QStringLiteral("break"));
                }
                if (marker.headEx) {
                    addEvent(judgeSecond, QStringLiteral("ex"));
                }
            }
            const double traceSecond = marker.slideTraceSecond >= 0.0 ? marker.slideTraceSecond : marker.second;
            const double trackSecond =
                qMax(0.0, miacode::preview_sfx_timing::slideTriggerSecond(traceSecond, normalizedTimingSettings, playbackRate));
            addEvent(trackSecond, marker.trackBreak ? QStringLiteral("break_slide_start") : QStringLiteral("slide"));
            if (marker.trackBreak && marker.endSecond > traceSecond) {
                const double tailSecond =
                    qMax(0.0, miacode::preview_sfx_timing::slideTriggerSecond(marker.endSecond, normalizedTimingSettings, playbackRate));
                addEvent(tailSecond, QStringLiteral("break"), 1, -1, 1.0, 0.0, true);
                addEvent(tailSecond, QStringLiteral("judge_break_slide"));
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
