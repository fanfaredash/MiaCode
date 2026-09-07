#include <QTextStream>
#include <QTemporaryDir>
#include <QFile>

#include "VideoExportAudioRenderPlan.h"
#include "VideoExportController.h"
#include "common/ChartClockCount.h"
#include "common/PreviewAudioMixConfig.h"

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

    if (!require(plan.touchholdSpanPlaybacks.size() == 1, QStringLiteral("partial export should keep the spanning touchhold"), err)) {
        return false;
    }
    const auto& span = plan.touchholdSpanPlaybacks.first();
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
    // Preview resumes a touch-hold it seeked into mid-sample (restoreTouchholdVoices
    // reconciles to second - span.startSecond), so the clamped export playback has to
    // enter the riser at the same offset instead of restarting it at the cutoff.
    if (!require(
            qAbs(span.sourceStartSecond - 0.5) <= 1e-6,
            QStringLiteral("clamped touchhold span should enter the riser mid-sample"),
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
        plan.touchholdSpanPlaybacks.isEmpty(),
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

bool verifySubSampleSupersedeDropsMaskedAnswer(QTextStream& err)
{
    // Two taps less than one mixer sample apart (e.g. a hold tail whose absolute
    // duration lands microseconds before a coincident tap). The first "answer" is
    // superseded within a sub-sample window; realtime preview's monophonic voice
    // cuts it off inaudibly. Export cannot express a sub-sample truncation length
    // (BASS rounds it to zero and treats zero as "no limit"), so the masked hit
    // must be dropped outright rather than left un-truncated and doubled.
    const double kSubSampleGap = 1.0 / (2.0 * miacode::preview_audio::kMixSampleRate);
    VideoExportTask task;
    task.noteMarkers = {makeTap(1.0), makeTap(1.0 + kSubSampleGap)};
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

    int answerCount = 0;
    double survivingAnswerMaxDuration = -1.0;
    for (const auto& playback : plan.scheduledSfxPlaybacks) {
        if (playback.kind == QLatin1String("answer")) {
            ++answerCount;
            survivingAnswerMaxDuration = playback.maxDurationSeconds;
        }
    }
    if (!require(
            answerCount == 1,
            QStringLiteral("sub-sample supersede should drop the masked answer, leaving exactly one"),
            err)) {
        return false;
    }
    if (!require(
            survivingAnswerMaxDuration <= 0.0,
            QStringLiteral("the surviving answer should play un-truncated"),
            err)) {
        return false;
    }
    return true;
}

bool verifyTouchholdSpanLatestWinsPlaybacks(QTextStream& err)
{
    // Export mirrors the preview voice: the riser is owned latest-wins, so a
    // seamless join / overlap / nesting each hand the sound to the newer
    // touch-hold, which restarts the sample from its own start. Spans must NOT
    // be merged into one long playback (that made the second touch-hold sound
    // like a continuation of the first).
    VideoExportTask task;
    task.noteMarkers = {
        makeTouchHold(1.0, 2.0),   // seamless join with the next one
        makeTouchHold(2.0, 3.0),
        makeTouchHold(4.0, 6.0),   // outer span of a nesting pair
        makeTouchHold(5.0, 5.5),   // nested inside [4, 6]
        makeTouchHold(7.0, 9.0),   // overlapped by the next one
        makeTouchHold(8.0, 10.0),
    };
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 11.0;
    task.fullRangeExport = true;
    task.fps = 60;

    VideoExportAudioRenderPlan plan;
    QString errorMessage;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &plan, &errorMessage)) {
        err << errorMessage << Qt::endl;
        return false;
    }

    struct ExpectedPlayback {
        double chartStartSecond;
        double durationSeconds;
        double sourceStartSecond;
    };
    // chart second -> mix second is a pure shift by the timeline origin.
    const ExpectedPlayback expected[] = {
        {1.0, 1.0, 0.0},   // first of the seamless pair
        {2.0, 1.0, 0.0},   // second one restarts the riser instead of continuing
        {4.0, 1.0, 0.0},   // outer span up to the nested one
        {5.0, 0.5, 0.0},   // nested span takes over from its own start
        {5.5, 0.5, 1.5},   // outer span resumes mid-sample, as the voice would
        {7.0, 1.0, 0.0},   // overlapped span up to the takeover
        {8.0, 2.0, 0.0},   // newer span takes over and runs to its own end
    };
    const int expectedCount = static_cast<int>(sizeof(expected) / sizeof(expected[0]));

    if (!require(
            plan.touchholdSpanPlaybacks.size() == expectedCount,
            QStringLiteral("touchhold ownership should emit one playback per owned stretch (got %1, want %2)")
                .arg(plan.touchholdSpanPlaybacks.size())
                .arg(expectedCount),
            err)) {
        return false;
    }

    for (int index = 0; index < expectedCount; ++index) {
        const auto& playback = plan.touchholdSpanPlaybacks.at(index);
        const double wantMixSecond = expected[index].chartStartSecond - plan.timelineOriginSecond;
        if (!require(
                qAbs(playback.mixSecond - wantMixSecond) <= 1e-6,
                QStringLiteral("touchhold playback %1 should start at chart %2")
                    .arg(index)
                    .arg(expected[index].chartStartSecond),
                err)) {
            return false;
        }
        if (!require(
                qAbs(playback.durationSeconds - expected[index].durationSeconds) <= 1e-6,
                QStringLiteral("touchhold playback %1 should last until ownership changes")
                    .arg(index),
                err)) {
            return false;
        }
        if (!require(
                qAbs(playback.sourceStartSecond - expected[index].sourceStartSecond) <= 1e-6,
                QStringLiteral("touchhold playback %1 should enter the riser at %2 s")
                    .arg(index)
                    .arg(expected[index].sourceStartSecond),
                err)) {
            return false;
        }
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
    if (!verifySubSampleSupersedeDropsMaskedAnswer(err)) {
        return 1;
    }
    if (!verifyTouchholdSpanLatestWinsPlaybacks(err)) {
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
