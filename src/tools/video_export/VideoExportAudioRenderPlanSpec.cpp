#include <QTextStream>
#include <QTemporaryDir>
#include <QFile>

#include "VideoExportAudioRenderPlan.h"
#include "VideoExportController.h"
#include "common/ChartClockCount.h"

namespace {

using miacode::video_export::VideoExportAudioRenderPlan;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

TimelineNoteMarker makeTap(double second)
{
    TimelineNoteMarker marker;
    marker.type = QStringLiteral("tap");
    marker.second = second;
    return marker;
}

TimelineNoteMarker makeTouchHold(double startSecond, double endSecond)
{
    TimelineNoteMarker marker;
    marker.type = QStringLiteral("touch_hold");
    marker.second = startSecond;
    marker.endSecond = endSecond;
    return marker;
}

bool verifyFullRangePlan(QTextStream& err)
{
    QTemporaryDir tempDir;
    QFile trackFile(tempDir.filePath(QStringLiteral("track.mp3")));
    if (!trackFile.open(QIODevice::WriteOnly)) {
        err << "failed to create temp track" << Qt::endl;
        return false;
    }
    trackFile.close();

    VideoExportTask task;
    task.trackPath = trackFile.fileName();
    task.noteMarkers = {makeTap(1.0), makeTap(2.0)};
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 4.0;
    task.fullRangeExport = true;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    if (!require(qAbs(plan.leadInSeconds - 2.0) <= 1e-6, QStringLiteral("full-range export should keep 2s lead-in"), err)) {
        return false;
    }
    if (!require(qAbs(plan.timelineOriginSecond + 2.0) <= 1e-6, QStringLiteral("full-range export should start at -2s timeline origin"), err)) {
        return false;
    }
    if (!require(plan.backgroundTrack.enabled, QStringLiteral("existing track path should enable BGM render plan"), err)) {
        return false;
    }
    if (!require(qAbs(plan.backgroundTrack.mixStartSecond - 2.0) <= 1e-6, QStringLiteral("negative origin should delay BGM into the mix"), err)) {
        return false;
    }
    if (!require(qAbs(plan.backgroundTrack.sourceStartSecond) <= 1e-6, QStringLiteral("negative origin should not seek BGM"), err)) {
        return false;
    }
    return true;
}

bool verifyPartialExportMutesPreRangeSfx(QTextStream& err)
{
    // A tap whose chart time would clamp into the partial pre-range
    // used to schedule an "answer" SFX at mixSecond=0. The pre-range
    // is now music-only, so that SFX must be dropped entirely.
    VideoExportTask task;
    task.noteMarkers = {makeTap(0.0)};
    task.exportStartSeconds = 1.0;
    task.contentDurationSeconds = 2.0;
    task.fullRangeExport = false;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    if (!require(qAbs(plan.leadInSeconds - 1.5) <= 1e-6, QStringLiteral("partial export should use 1.5s preload"), err)) {
        return false;
    }
    const auto it = std::find_if(
        plan.scheduledSfxPlaybacks.begin(),
        plan.scheduledSfxPlaybacks.end(),
        [](const auto& playback) { return playback.kind == QLatin1String("answer"); });
    if (!require(
            it == plan.scheduledSfxPlaybacks.end(),
            QStringLiteral("partial export pre-range should mute all SFX, including the compensated answer"),
            err)) {
        return false;
    }
    for (const auto& playback : plan.scheduledSfxPlaybacks) {
        if (!require(
                playback.mixSecond + 1e-6 >= plan.leadInSeconds,
                QStringLiteral("partial export should not schedule any SFX before the pre-range ends"),
                err)) {
            return false;
        }
    }
    return true;
}

bool verifyPartialExportClampsTouchholdSpanIntoSegment(QTextStream& err)
{
    // A touchhold whose chart-time start sits inside the partial
    // pre-range should be trimmed so its audio begins at the segment
    // boundary, not earlier. Tail-end portion past the cutoff stays.
    VideoExportTask task;
    task.noteMarkers = {makeTouchHold(0.5, 2.5)};
    task.exportStartSeconds = 1.0;
    task.contentDurationSeconds = 2.0;
    task.fullRangeExport = false;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    if (!require(plan.mergedTouchholdSpans.size() == 1, QStringLiteral("partial export should keep the spanning touchhold"), err)) {
        return false;
    }
    const auto& span = plan.mergedTouchholdSpans.first();
    if (!require(
            qAbs(span.mixSecond - plan.leadInSeconds) <= 1e-6,
            QStringLiteral("touchhold span that begins inside pre-range should clamp to segment start"),
            err)) {
        return false;
    }
    // exportStart=1.0 ⇒ timelineOrigin = 0.0 ⇒ span mix range
    // [0.5, 2.5] pre-clamp. After clamping start to leadIn=1.0 the
    // surviving portion is mix [1.0, 2.5] ⇒ duration 1.5 s.
    if (!require(
            qAbs(span.durationSeconds - 1.5) <= 1e-6,
            QStringLiteral("clamped touchhold span should keep the portion past the cutoff"),
            err)) {
        return false;
    }
    return true;
}

bool verifyPartialExportDropsTouchholdSpanEntirelyInPreRange(QTextStream& err)
{
    // A touchhold whose entire chart-time range sits in the partial
    // pre-range should be dropped completely (it never overlaps the
    // playable segment).
    VideoExportTask task;
    task.noteMarkers = {makeTouchHold(0.1, 0.4)};
    task.exportStartSeconds = 1.0;
    task.contentDurationSeconds = 2.0;
    task.fullRangeExport = false;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    return require(
        plan.mergedTouchholdSpans.isEmpty(),
        QStringLiteral("touchhold span fully inside the partial pre-range should be dropped"),
        err);
}

bool verifyPartialExportKeepsBackgroundTrackInPreRange(QTextStream& err)
{
    // Per beta51+ partial-range semantics, the pre-roll window is a
    // frozen state — chart, HUD, AND BGM all hold on the segment-start
    // moment while the pause glyph overlay plays. BGM is still scheduled
    // (the plan needs an entry so the ffmpeg mix knows what to do), but
    // its mixStartSecond is shifted to the end of the preload, and the
    // source position starts from segmentStart so the music's natural
    // beat lands the instant the playfield unfreezes.
    QTemporaryDir tempDir;
    QFile trackFile(tempDir.filePath(QStringLiteral("track.mp3")));
    if (!trackFile.open(QIODevice::WriteOnly)) {
        err << "failed to create temp track" << Qt::endl;
        return false;
    }
    trackFile.close();

    VideoExportTask task;
    task.trackPath = trackFile.fileName();
    task.noteMarkers = {makeTap(0.0)};
    task.exportStartSeconds = 1.0;
    task.contentDurationSeconds = 2.0;
    task.fullRangeExport = false;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    if (!require(plan.backgroundTrack.enabled, QStringLiteral("partial export should still schedule the background track"), err)) {
        return false;
    }
    if (!require(
            qAbs(plan.backgroundTrack.mixStartSecond - plan.leadInSeconds) <= 1e-6,
            QStringLiteral("partial export should delay BGM mix start by the preload (silent pre-roll)"),
            err)) {
        return false;
    }
    if (!require(
            qAbs(plan.backgroundTrack.sourceStartSecond - task.exportStartSeconds) <= 1e-6,
            QStringLiteral("partial export should source BGM from segmentStart so beat 0 lands when freeze ends"),
            err)) {
        return false;
    }
    return true;
}

bool verifyLatestWinsScheduling(QTextStream& err)
{
    VideoExportTask task;
    task.noteMarkers = {makeTap(1.0), makeTap(1.5)};
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 3.0;
    task.fullRangeExport = false;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    if (!require(plan.scheduledSfxPlaybacks.size() >= 2, QStringLiteral("plan should expose multiple SFX playbacks"), err)) {
        return false;
    }
    bool foundTruncatedAnswer = false;
    for (const auto& playback : plan.scheduledSfxPlaybacks) {
        if (playback.kind == QLatin1String("answer") && playback.maxDurationSeconds > 0.0) {
            foundTruncatedAnswer = qAbs(playback.maxDurationSeconds - 0.5) <= 1e-6;
            break;
        }
    }
    if (!require(foundTruncatedAnswer, QStringLiteral("latest-wins answer playback should carry next-hit truncation"), err)) {
        return false;
    }
    return true;
}

bool verifyTouchholdSpanMerge(QTextStream& err)
{
    VideoExportTask task;
    task.noteMarkers = {
        makeTouchHold(1.0, 2.0),
        makeTouchHold(1.5, 3.0),
        makeTouchHold(4.0, 4.5),
    };
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 5.0;
    task.fullRangeExport = false;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    if (!require(plan.mergedTouchholdSpans.size() == 2, QStringLiteral("touchhold spans should merge overlapping sustains"), err)) {
        return false;
    }
    // touchhold chart-time [1.0, 2.0] ⊕ [1.5, 3.0] merges into [1.0, 3.0] chart.
    // mixSecond = chart - timelineOrigin, timelineOrigin = -leadIn = -1.5
    // ⇒ mix [2.5, 4.5] ⇒ first span mixSecond=2.5, duration=2.0.
    if (!require(
            qAbs(plan.mergedTouchholdSpans.at(0).mixSecond - (1.0 + plan.leadInSeconds)) <= 1e-6,
            QStringLiteral("merged touchhold span should preserve delayed preload start"),
            err)) {
        return false;
    }
    if (!require(qAbs(plan.mergedTouchholdSpans.at(0).durationSeconds - 2.0) <= 1e-6, QStringLiteral("merged touchhold span should cover full overlapped duration"), err)) {
        return false;
    }
    return true;
}

bool verifyPositiveOriginTrackSeek(QTextStream& err)
{
    // Even when the timeline origin (segmentStart - leadIn) is positive,
    // partial-range exports keep the freeze-then-go contract: BGM is
    // silent during the preload window and starts from segmentStart at
    // mix-second = leadIn. The pre-G2 "preserve continuity by sourcing
    // from timeline origin" branch is gone — see VideoExportAudioRenderPlan.cpp.
    QTemporaryDir tempDir;
    QFile trackFile(tempDir.filePath(QStringLiteral("track.mp3")));
    if (!trackFile.open(QIODevice::WriteOnly)) {
        err << "failed to create temp track" << Qt::endl;
        return false;
    }
    trackFile.close();

    VideoExportTask task;
    task.trackPath = trackFile.fileName();
    task.noteMarkers = {makeTap(4.0)};
    task.exportStartSeconds = 4.0;
    task.contentDurationSeconds = 2.0;
    task.fullRangeExport = false;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    if (!require(plan.backgroundTrack.enabled, QStringLiteral("positive-origin partial export should still schedule BGM"), err)) {
        return false;
    }
    if (!require(
            qAbs(plan.backgroundTrack.mixStartSecond - plan.leadInSeconds) <= 1e-6,
            QStringLiteral("positive-origin partial export should delay BGM mix start by the preload"),
            err)) {
        return false;
    }
    if (!require(
            qAbs(plan.backgroundTrack.sourceStartSecond - task.exportStartSeconds) <= 1e-6,
            QStringLiteral("positive-origin partial export should source BGM from segmentStart, not from timeline origin"),
            err)) {
        return false;
    }
    return true;
}

bool verifyClockCountScheduling(QTextStream& err)
{
    VideoExportTask task;
    task.noteMarkers = {makeTap(1.0)};
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 3.0;
    task.fullRangeExport = true;
    task.fps = 60;
    task.clockCount = 3;
    task.clockBpm = 120.0;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    QVector<double> clockSeconds;
    for (const auto& playback : plan.scheduledSfxPlaybacks) {
        if (playback.kind == QLatin1String("clock")) {
            clockSeconds.append(playback.mixSecond);
        }
    }
    if (!require(clockSeconds.size() == 3, QStringLiteral("full export should schedule requested clock count"), err)) {
        return false;
    }
    if (!require(qAbs(clockSeconds.at(0) - 2.0) <= 1e-6, QStringLiteral("clock should start at chart time zero after lead-in"), err)) {
        return false;
    }
    if (!require(qAbs(clockSeconds.at(1) - 2.5) <= 1e-6, QStringLiteral("clock should repeat at quarter-note interval"), err)) {
        return false;
    }
    if (!require(qAbs(clockSeconds.at(2) - 3.0) <= 1e-6, QStringLiteral("clock should repeat at quarter-note interval"), err)) {
        return false;
    }
    return true;
}

bool verifyPartialExportSkipsClockCount(QTextStream& err)
{
    VideoExportTask task;
    task.noteMarkers = {makeTap(1.0)};
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 3.0;
    task.fullRangeExport = false;
    task.fps = 60;
    task.clockCount = 3;
    task.clockBpm = 120.0;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    const auto it = std::find_if(
        plan.scheduledSfxPlaybacks.begin(),
        plan.scheduledSfxPlaybacks.end(),
        [](const auto& playback) { return playback.kind == QLatin1String("clock"); });
    return require(it == plan.scheduledSfxPlaybacks.end(), QStringLiteral("partial export should not schedule clocks"), err);
}

bool verifyClockCountMetadataParsing(QTextStream& err)
{
    SimaiDocument document;
    document.extraFields = {
        {QStringLiteral("clock_count"), QStringLiteral("4")},
        {QStringLiteral("wholebpm"), QStringLiteral("150")},
    };
    if (!require(miacode::chart_clock::clockCountFromDocument(document) == 4, QStringLiteral("clock_count should parse as non-negative integer"), err)) {
        return false;
    }
    if (!require(qAbs(miacode::chart_clock::clockBpmForChart(document, QStringLiteral("(180)")) - 150.0) <= 1e-6, QStringLiteral("wholebpm should take priority over inline bpm"), err)) {
        return false;
    }

    SimaiDocument inlineBpmDocument;
    inlineBpmDocument.extraFields = {
        {QStringLiteral("clock_count"), QStringLiteral("2")},
    };
    if (!require(qAbs(miacode::chart_clock::clockBpmForChart(inlineBpmDocument, QStringLiteral("{4},,,(180),,")) - 180.0) <= 1e-6, QStringLiteral("first inline bpm should be used when wholebpm is absent"), err)) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    QTextStream err(stderr);
    if (!verifyFullRangePlan(err)) {
        return 1;
    }
    if (!verifyPartialExportMutesPreRangeSfx(err)) {
        return 1;
    }
    if (!verifyPartialExportClampsTouchholdSpanIntoSegment(err)) {
        return 1;
    }
    if (!verifyPartialExportDropsTouchholdSpanEntirelyInPreRange(err)) {
        return 1;
    }
    if (!verifyPartialExportKeepsBackgroundTrackInPreRange(err)) {
        return 1;
    }
    if (!verifyLatestWinsScheduling(err)) {
        return 1;
    }
    if (!verifyTouchholdSpanMerge(err)) {
        return 1;
    }
    if (!verifyPositiveOriginTrackSeek(err)) {
        return 1;
    }
    if (!verifyClockCountScheduling(err)) {
        return 1;
    }
    if (!verifyPartialExportSkipsClockCount(err)) {
        return 1;
    }
    if (!verifyClockCountMetadataParsing(err)) {
        return 1;
    }

    QTextStream out(stdout);
    out << "video_export_audio_render_plan_spec ok" << Qt::endl;
    return 0;
}
