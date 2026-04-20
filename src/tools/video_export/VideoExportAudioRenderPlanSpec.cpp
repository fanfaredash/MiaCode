#include <QTextStream>
#include <QTemporaryDir>
#include <QFile>

#include "VideoExportAudioRenderPlan.h"
#include "VideoExportController.h"

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

    if (!require(qAbs(plan.leadInSeconds - 3.0) <= 1e-6, QStringLiteral("full-range export should keep 3s lead-in"), err)) {
        return false;
    }
    if (!require(qAbs(plan.timelineOriginSecond + 3.0) <= 1e-6, QStringLiteral("full-range export should start at -3s timeline origin"), err)) {
        return false;
    }
    if (!require(plan.backgroundTrack.enabled, QStringLiteral("existing track path should enable BGM render plan"), err)) {
        return false;
    }
    if (!require(qAbs(plan.backgroundTrack.mixStartSecond - 3.0) <= 1e-6, QStringLiteral("negative origin should delay BGM into the mix"), err)) {
        return false;
    }
    if (!require(qAbs(plan.backgroundTrack.sourceStartSecond) <= 1e-6, QStringLiteral("negative origin should not seek BGM"), err)) {
        return false;
    }
    return true;
}

bool verifyPartialExportAnswerClamp(QTextStream& err)
{
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

    if (!require(qAbs(plan.leadInSeconds - 1.0) <= 1e-6, QStringLiteral("partial export should use 1s preload"), err)) {
        return false;
    }
    if (!require(!plan.scheduledSfxPlaybacks.isEmpty(), QStringLiteral("partial export should keep compensated answer playback"), err)) {
        return false;
    }
    auto it = std::find_if(
        plan.scheduledSfxPlaybacks.begin(),
        plan.scheduledSfxPlaybacks.end(),
        [](const auto& playback) { return playback.kind == QLatin1String("answer"); });
    if (!require(it != plan.scheduledSfxPlaybacks.end(), QStringLiteral("partial export should include answer playback"), err)) {
        return false;
    }
    if (!require(qAbs(it->mixSecond) <= 1e-6, QStringLiteral("partial export answer should clamp to frame zero"), err)) {
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
    if (!require(qAbs(plan.mergedTouchholdSpans.at(0).mixSecond - 2.0) <= 1e-6, QStringLiteral("merged touchhold span should preserve delayed preload start"), err)) {
        return false;
    }
    if (!require(qAbs(plan.mergedTouchholdSpans.at(0).durationSeconds - 2.0) <= 1e-6, QStringLiteral("merged touchhold span should cover full overlapped duration"), err)) {
        return false;
    }
    return true;
}

bool verifyPositiveOriginTrackSeek(QTextStream& err)
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

    if (!require(plan.backgroundTrack.enabled, QStringLiteral("positive-origin export should still schedule BGM"), err)) {
        return false;
    }
    if (!require(qAbs(plan.backgroundTrack.mixStartSecond) <= 1e-6, QStringLiteral("positive-origin export should mix BGM from frame zero"), err)) {
        return false;
    }
    if (!require(qAbs(plan.backgroundTrack.sourceStartSecond - 3.0) <= 1e-6, QStringLiteral("positive-origin export should seek BGM by timeline origin"), err)) {
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
    if (!verifyPartialExportAnswerClamp(err)) {
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

    QTextStream out(stdout);
    out << "video_export_audio_render_plan_spec ok" << Qt::endl;
    return 0;
}
