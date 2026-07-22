#include "VideoExportAudioRenderPlan.h"

#include "VideoExportController.h"

#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxSemantics.h"
#include "common/PreviewSfxTimeline.h"
#include "common/IntroConfig.h"
#include "common/VideoExportConfig.h"
#include "audio/PreviewAudioSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QtMath>

#include <algorithm>

namespace {

constexpr double kTimelineEpsilonSeconds = miacode::preview_sfx_timeline::kTimelineEpsilonSeconds;

QString normalizePath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

bool noteMarkerTimestampInRange(const TimelineNoteMarker& marker, double startSecond, double endSecond)
{
    if (endSecond <= startSecond) {
        return true;
    }
    return marker.second + kTimelineEpsilonSeconds >= startSecond
        && marker.second <= endSecond + kTimelineEpsilonSeconds;
}

QVector<TimelineNoteMarker> filteredMarkersForRange(
    const QVector<TimelineNoteMarker>& markers,
    double startSecond,
    double endSecond
)
{
    QVector<TimelineNoteMarker> filtered;
    filtered.reserve(markers.size());
    for (const TimelineNoteMarker& marker : markers) {
        if (noteMarkerTimestampInRange(marker, startSecond, endSecond)) {
            filtered.append(marker);
        }
    }
    return filtered;
}

QString resolveSfxDirectory()
{
    return normalizePath(miacode::preview_sfx::resolveSfxDirectory());
}

QVector<miacode::video_export::TouchholdSpanRenderPlan> buildMergedTouchholdSpans(
    const QVector<miacode::preview_sfx_timeline::TouchholdSpan>& spans,
    double timelineOriginSecond,
    double totalSeconds,
    double gain
)
{
    if (gain <= 0.0) {
        return {};
    }

    struct ActiveSpan {
        double startSecond = 0.0;
        double endSecond = 0.0;
    };

    QVector<ActiveSpan> activeSpans;
    activeSpans.reserve(spans.size());
    for (const auto& span : spans) {
        if (span.endSecond <= span.startSecond) {
            continue;
        }
        if (span.startSecond + kTimelineEpsilonSeconds < timelineOriginSecond) {
            continue;
        }
        const double mixStartSecond = span.startSecond - timelineOriginSecond;
        const double mixEndSecond = qMin(span.endSecond - timelineOriginSecond, totalSeconds);
        if (mixStartSecond < 0.0 || mixEndSecond <= mixStartSecond + kTimelineEpsilonSeconds) {
            continue;
        }

        ActiveSpan active;
        active.startSecond = mixStartSecond;
        active.endSecond = mixEndSecond;
        activeSpans.append(active);
    }

    if (activeSpans.isEmpty()) {
        return {};
    }

    std::sort(activeSpans.begin(), activeSpans.end(), [](const ActiveSpan& left, const ActiveSpan& right) {
        if (!qFuzzyCompare(left.startSecond + 1.0, right.startSecond + 1.0)) {
            return left.startSecond < right.startSecond;
        }
        return left.endSecond < right.endSecond;
    });

    QVector<miacode::video_export::TouchholdSpanRenderPlan> merged;
    double mergedStart = activeSpans.first().startSecond;
    double mergedEnd = activeSpans.first().endSecond;
    for (int index = 1; index < activeSpans.size(); ++index) {
        const ActiveSpan& span = activeSpans.at(index);
        if (span.startSecond <= mergedEnd + kTimelineEpsilonSeconds) {
            mergedEnd = qMax(mergedEnd, span.endSecond);
            continue;
        }

        miacode::video_export::TouchholdSpanRenderPlan plan;
        plan.kind = QStringLiteral("touchhold");
        plan.assetKind = QStringLiteral("touchhold");
        plan.mixSecond = mergedStart;
        plan.durationSeconds = mergedEnd - mergedStart;
        plan.gain = gain;
        merged.append(plan);

        mergedStart = span.startSecond;
        mergedEnd = span.endSecond;
    }

    miacode::video_export::TouchholdSpanRenderPlan plan;
    plan.kind = QStringLiteral("touchhold");
    plan.assetKind = QStringLiteral("touchhold");
    plan.mixSecond = mergedStart;
    plan.durationSeconds = mergedEnd - mergedStart;
    plan.gain = gain;
    merged.append(plan);
    return merged;
}

void suppressSfxBeforePreRangeEnd(
    double preRangeEndSecond,
    miacode::video_export::VideoExportAudioRenderPlan* plan
)
{
    // Partial-range exports always emit a fixed pre-range before the
    // chart segment proper begins (currently kPartialRangePreloadSeconds
    // = 1.5 s — see VideoExportConfig.h). The pre-range is a frozen
    // state: chart playfield, HUD timestamp, AND BGM are all held on
    // the segment-start moment while a translucent pause glyph sits
    // on top (drawLeadInPauseOverlay in VideoExportController.cpp).
    // SFX in the pre-range would sound like artificial early hits, so
    // they're stripped from the audio plan here regardless of where
    // BGM mix start lands.
    //
    // Point-in-time SFX (tap, judge, answer, break, clock, …) are
    // dropped entirely when their mix second falls before the cutoff.
    // Sustained touchhold spans get their start clamped to the cutoff
    // so a touchhold that begins inside the pre-range still produces
    // audio for the part that overlaps the playable segment, but is
    // dropped if it ends before the segment starts.
    if (plan == nullptr || preRangeEndSecond <= kTimelineEpsilonSeconds) {
        return;
    }

    auto& scheduled = plan->scheduledSfxPlaybacks;
    scheduled.erase(
        std::remove_if(
            scheduled.begin(),
            scheduled.end(),
            [preRangeEndSecond](const miacode::video_export::ScheduledSfxPlaybackRenderPlan& playback) {
                return playback.mixSecond + kTimelineEpsilonSeconds < preRangeEndSecond;
            }
        ),
        scheduled.end()
    );

    auto& spans = plan->mergedTouchholdSpans;
    spans.erase(
        std::remove_if(
            spans.begin(),
            spans.end(),
            [preRangeEndSecond](const miacode::video_export::TouchholdSpanRenderPlan& span) {
                return span.mixSecond + span.durationSeconds + kTimelineEpsilonSeconds <= preRangeEndSecond;
            }
        ),
        spans.end()
    );
    for (miacode::video_export::TouchholdSpanRenderPlan& span : spans) {
        if (span.mixSecond + kTimelineEpsilonSeconds < preRangeEndSecond) {
            const double endSecond = span.mixSecond + span.durationSeconds;
            span.mixSecond = preRangeEndSecond;
            span.durationSeconds = qMax(0.0, endSecond - preRangeEndSecond);
        }
    }
}

void appendClockCountPlaybacks(
    const VideoExportTask& task,
    const PreviewAudioSettings& audioSettings,
    miacode::video_export::VideoExportAudioRenderPlan* plan
)
{
    if (plan == nullptr
        || !task.fullRangeExport
        || !task.clockCountEnabled
        || task.clockCount <= 0
        || !qIsFinite(task.clockBpm)
        || task.clockBpm <= 0.0) {
        return;
    }

    const double chartZeroMixSecond = -plan->timelineOriginSecond;
    const double quarterNoteSeconds = 60.0 / task.clockBpm;
    if (!qIsFinite(chartZeroMixSecond) || !qIsFinite(quarterNoteSeconds) || quarterNoteSeconds <= 0.0) {
        return;
    }

    const double gain = qMax(0.0, previewSfxVolumeForKind(audioSettings, QStringLiteral("clock")));
    if (gain <= 0.0) {
        return;
    }

    plan->scheduledSfxPlaybacks.reserve(plan->scheduledSfxPlaybacks.size() + task.clockCount);
    for (int index = 0; index < task.clockCount; ++index) {
        const double mixSecond = chartZeroMixSecond + quarterNoteSeconds * index;
        if (mixSecond + kTimelineEpsilonSeconds < 0.0
            || mixSecond > plan->alignedTotalSeconds + kTimelineEpsilonSeconds) {
            continue;
        }

        miacode::video_export::ScheduledSfxPlaybackRenderPlan scheduled;
        scheduled.kind = QStringLiteral("clock");
        scheduled.assetKind = QStringLiteral("clock");
        scheduled.mixSecond = mixSecond;
        scheduled.gain = gain;
        plan->scheduledSfxPlaybacks.append(scheduled);
    }
}

}  // namespace

namespace miacode::video_export {

bool buildVideoExportAudioRenderPlan(
    const VideoExportTask& task,
    VideoExportAudioRenderPlan* plan,
    QString* errorMessage
)
{
    if (plan == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("audio render plan output is null");
        }
        return false;
    }

    PreviewAudioSettings normalizedAudioSettings = task.audioSettings;
    normalizedAudioSettings.normalize();
    PreviewTimingSettings normalizedTimingSettings = task.timingSettings;
    normalizedTimingSettings.normalize();

    VideoExportAudioRenderPlan built;
    built.segmentStartSecond = qMax(0.0, task.exportStartSeconds);
    const double segmentDurationSeconds = qMax(0.0, task.contentDurationSeconds);
    built.segmentEndSecond = built.segmentStartSecond + segmentDurationSeconds;
    // Full-range exports front-pad a 2 s count-down lead-in before chart 0 (the
    // playfield previews chart-time -leadIn → 0 with the HUD ramping -2s → 0s). The
    // maimai intro is an ADDITIONAL front-pad ON TOP of that lead-in: the wipe
    // retract hands off to the live lead-in preview, which then runs out to chart 0
    // (music + clock_count). So the intro does NOT replace the lead-in.
    built.leadInSeconds = task.fullRangeExport
        ? miacode::video_export::kLeadInSeconds
        : miacode::video_export::kPartialRangePreloadSeconds;
    // Pre-roll maimai track-start intro: extra silent padding in FRONT of the
    // lead-in (full-range exports only). The chart timeline origin, total
    // duration and frame count all absorb it, so the existing linear
    // frame->chart-second mapping still reaches -leadIn at frame introFrameCount
    // and chart 0 at the same wall-time as the rendered intro hand-off. The
    // window is pure silence today (the opening SFX is added later).
    built.introLeadSeconds = (task.fullRangeExport && task.intro.enabled)
        ? miacode::intro::kDurationSeconds
        : 0.0;
    built.introFrameCount = built.introLeadSeconds > 0.0
        ? qMax(0, qRound(built.introLeadSeconds * qMax(1, task.fps)))
        : 0;
    built.timelineOriginSecond =
        built.segmentStartSecond - built.leadInSeconds - built.introLeadSeconds;
    built.totalSeconds = built.introLeadSeconds + built.leadInSeconds + segmentDurationSeconds;
    built.frameCount = qMax(1, qRound(built.totalSeconds * qMax(1, task.fps)));
    built.alignedTotalSeconds = static_cast<double>(built.frameCount) / qMax(1, task.fps);
    built.sfxDirectory = resolveSfxDirectory();
    built.exportMarkers = filteredMarkersForRange(
        task.noteMarkers,
        built.timelineOriginSecond,
        built.segmentEndSecond);

    if (built.sfxDirectory.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("preview SFX directory could not be resolved");
        }
        return false;
    }
    if (built.exportMarkers.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export marker window is empty");
        }
        return false;
    }

    const QString normalizedTrackPath = (task.trackPath.isEmpty() || !QFileInfo::exists(task.trackPath))
        ? QString()
        : normalizePath(task.trackPath);
    if (!normalizedTrackPath.isEmpty()) {
        BackgroundTrackRenderPlan backgroundPlan;
        backgroundPlan.enabled = true;
        backgroundPlan.path = normalizedTrackPath;
        backgroundPlan.gain = qMax(0.0, previewTrackVolume(normalizedAudioSettings));
        if (!task.fullRangeExport) {
            // Partial-range pre-roll is a frozen state: chart, HUD and audio
            // all sit on the segment-start moment while drawLeadInPauseOverlay
            // shows the pause glyph. BGM joins the mix once the preload window
            // expires, sourcing from segmentStart (i.e. exportStartSeconds),
            // so the viewer hears the music from the exact second the playfield
            // unfreezes. Pre-G2 (the older "keep BGM audible during pre-range")
            // behaviour was deliberately removed per user request — the
            // freeze-then-go reading is cleaner than a silent chart with
            // running music.
            backgroundPlan.sourceStartSecond = qMax(0.0, task.exportStartSeconds);
            backgroundPlan.mixStartSecond = built.leadInSeconds;
            backgroundPlan.durationSeconds = qMax(0.0, built.alignedTotalSeconds - built.leadInSeconds);
        } else if (built.timelineOriginSecond < -kTimelineEpsilonSeconds) {
            // Full-range with a negative timeline origin: chart 0 is offset
            // by the 2 s count-down lead-in. BGM still starts at chart 0
            // (source second 0) so the music's natural intro lands at the
            // right beat; we just delay the mix by leadInSeconds.
            backgroundPlan.sourceStartSecond = 0.0;
            backgroundPlan.mixStartSecond = -built.timelineOriginSecond;
            backgroundPlan.durationSeconds = qMax(0.0, built.alignedTotalSeconds + built.timelineOriginSecond);
        } else {
            // Full-range with timelineOrigin ≥ 0 (e.g. user chose to skip
            // the count-in via flags): BGM starts at frame zero, from the
            // origin's source position.
            backgroundPlan.sourceStartSecond = qMax(0.0, built.timelineOriginSecond);
            backgroundPlan.mixStartSecond = 0.0;
            backgroundPlan.durationSeconds = built.alignedTotalSeconds;
        }
        if (backgroundPlan.durationSeconds > kTimelineEpsilonSeconds && backgroundPlan.gain > 0.0) {
            built.backgroundTrack = backgroundPlan;
        }
    }

    QVector<miacode::preview_sfx_timeline::Event> events;
    QVector<miacode::preview_sfx_timeline::TouchholdSpan> touchholdSpans;
    miacode::preview_sfx_timeline::buildTimeline(
        built.exportMarkers,
        1.0,
        normalizedTimingSettings,
        &events,
        &touchholdSpans);

    const QVector<miacode::preview_sfx_timeline::ScheduledPlayback> scheduledPlaybacks =
        miacode::preview_sfx_timeline::buildScheduledPlaybacks(
            events,
            normalizedAudioSettings.breakSlideTailCheerMuted);
    built.scheduledSfxPlaybacks.reserve(scheduledPlaybacks.size());
    for (const auto& playback : scheduledPlaybacks) {
        if (!miacode::preview_sfx_timeline::scheduledPlaybackSurvivesTimelineOriginClamp(
                playback,
                built.timelineOriginSecond)) {
            continue;
        }

        const double gain =
            qMax(0.0, playback.gain) * qMax(0.0, previewSfxVolumeForKind(normalizedAudioSettings, playback.kind));
        if (gain <= 0.0) {
            continue;
        }

        ScheduledSfxPlaybackRenderPlan scheduled;
        scheduled.kind = playback.kind;
        scheduled.assetKind = previewSfxNormalizedKind(
            playback.kind == QLatin1String("break_slide_finish")
                ? QStringLiteral("break_slide")
                : playback.kind == QLatin1String("break_slide_tail_break")
                    ? QStringLiteral("break")
                    : playback.kind);
        scheduled.mixSecond =
            miacode::preview_sfx_timeline::scheduledPlaybackMixSecond(playback, built.timelineOriginSecond);
        scheduled.gain = gain;
        if (playback.nextSameKindSecond >= 0.0) {
            const double maskWindowSeconds = qMax(0.0, playback.nextSameKindSecond - playback.second);
            // A later same-kind trigger supersedes this one. Realtime preview makes
            // every note-SFX kind monophonic (one voice per kind, latest-wins), so a
            // supersede that lands within a single mixer sample cuts this first hit
            // off inaudibly. The export mixer cannot express a sub-sample truncation
            // length: BASS rounds it to zero bytes and then treats zero as "no limit,"
            // playing the whole sample and doubling the SFX (e.g. a hold tail whose end
            // lands microseconds before a coincident tap). Drop the masked hit here so
            // export matches preview instead of emitting a full, un-truncated overlap.
            if (maskWindowSeconds * miacode::preview_audio::kMixSampleRate < 1.0) {
                continue;
            }
            scheduled.maxDurationSeconds = maskWindowSeconds;
        }
        built.scheduledSfxPlaybacks.append(scheduled);
    }
    appendClockCountPlaybacks(task, normalizedAudioSettings, &built);

    built.mergedTouchholdSpans = buildMergedTouchholdSpans(
        touchholdSpans,
        built.timelineOriginSecond,
        built.alignedTotalSeconds,
        qMax(0.0, previewSfxVolumeForKind(normalizedAudioSettings, QStringLiteral("touchhold"))));

    if (!task.fullRangeExport) {
        suppressSfxBeforePreRangeEnd(built.leadInSeconds, &built);
    }

    *plan = built;
    return true;
}

}  // namespace miacode::video_export
