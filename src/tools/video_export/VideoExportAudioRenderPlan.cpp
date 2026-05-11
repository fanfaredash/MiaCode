#include "VideoExportAudioRenderPlan.h"

#include "VideoExportController.h"

#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxSemantics.h"
#include "common/PreviewSfxTimeline.h"
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

void appendClockCountPlaybacks(
    const VideoExportTask& task,
    const PreviewAudioSettings& audioSettings,
    miacode::video_export::VideoExportAudioRenderPlan* plan
)
{
    if (plan == nullptr
        || !task.fullRangeExport
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
    built.leadInSeconds = task.fullRangeExport
        ? miacode::video_export::kLeadInSeconds
        : miacode::video_export::kPartialRangePreloadSeconds;
    built.timelineOriginSecond = built.segmentStartSecond - built.leadInSeconds;
    built.totalSeconds = built.leadInSeconds + segmentDurationSeconds;
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
        if (built.timelineOriginSecond > kTimelineEpsilonSeconds) {
            backgroundPlan.sourceStartSecond = built.timelineOriginSecond;
            backgroundPlan.mixStartSecond = 0.0;
            backgroundPlan.durationSeconds = built.alignedTotalSeconds;
        } else if (built.timelineOriginSecond < -kTimelineEpsilonSeconds) {
            backgroundPlan.sourceStartSecond = 0.0;
            backgroundPlan.mixStartSecond = -built.timelineOriginSecond;
            backgroundPlan.durationSeconds = qMax(0.0, built.alignedTotalSeconds + built.timelineOriginSecond);
        } else {
            backgroundPlan.sourceStartSecond = 0.0;
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
            scheduled.maxDurationSeconds = qMax(0.0, playback.nextSameKindSecond - playback.second);
        }
        built.scheduledSfxPlaybacks.append(scheduled);
    }
    appendClockCountPlaybacks(task, normalizedAudioSettings, &built);

    built.mergedTouchholdSpans = buildMergedTouchholdSpans(
        touchholdSpans,
        built.timelineOriginSecond,
        built.alignedTotalSeconds,
        qMax(0.0, previewSfxVolumeForKind(normalizedAudioSettings, QStringLiteral("touchhold"))));

    *plan = built;
    return true;
}

}  // namespace miacode::video_export
