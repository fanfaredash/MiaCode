#include "VideoExportSettings.h"
#include "VideoExportSnapshot.h"

#include <QJsonObject>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool nearlyEqual(double lhs, double rhs)
{
    return qAbs(lhs - rhs) <= 1e-9;
}

bool verifyPreferencesAndDifficultyReseed(QTextStream& err)
{
    VideoExportTask edited;
    edited.introSoundFileName = QStringLiteral("custom-start.flac");
    edited.introSoundVolume = 1.75;

    QJsonObject preferences;
    miacode::video_export::appendVideoExportPreferences(&preferences, edited);
    bool ok = require(
        nearlyEqual(preferences.value(QStringLiteral("intro_sound_volume")).toDouble(), 1.75),
        QStringLiteral("shared export preferences serialize the independent intro volume"),
        err);

    VideoExportTask restored;
    miacode::video_export::applyVideoExportPreferences(preferences, &restored);
    ok &= require(
        nearlyEqual(restored.introSoundVolume, 1.75),
        QStringLiteral("shared export preferences restore the independent intro volume"),
        err);

    VideoExportTask reseeded;
    reseeded.outputPath = QStringLiteral("new-difficulty.mp4");
    reseeded.exportStartSeconds = 3.0;
    miacode::video_export::copyVideoExportUserSettings(edited, &reseeded);
    ok &= require(
        reseeded.introSoundFileName == QStringLiteral("custom-start.flac")
            && nearlyEqual(reseeded.introSoundVolume, 1.75),
        QStringLiteral("difficulty reseeding preserves the selected intro sound and volume"),
        err);
    ok &= require(
        reseeded.outputPath == QStringLiteral("new-difficulty.mp4")
            && nearlyEqual(reseeded.exportStartSeconds, 3.0),
        QStringLiteral("difficulty reseeding keeps chart-owned output and range fields"),
        err);
    return ok;
}

bool verifySnapshotAndWorkerRoundTrip(QTextStream& err)
{
    VideoExportSnapshot source;
    source.chartTextUtf8 = QStringLiteral(
        "&title=Intro Sound Spec\n"
        "&artist=MiaCode\n"
        "&first=0\n"
        "&lv_5=12\n"
        "&inote_5=(120){4}1,\n");
    source.difficultyId = 5;
    source.originalChartPath = QStringLiteral("C:/charts/intro-sound/maidata.txt");
    source.outputPath = QStringLiteral("C:/charts/intro-sound/out.mp4");
    source.contentDurationSeconds = 1.0;
    source.intro.enabled = true;
    source.introSoundFileName = QStringLiteral("../custom-start.flac");
    source.introSoundVolume = 1.75;

    const QJsonObject json = source.toJson();
    const QJsonObject intro = json.value(QStringLiteral("intro")).toObject();
    bool ok = require(
        intro.value(QStringLiteral("sound_file")).toString() == QStringLiteral("custom-start.flac")
            && nearlyEqual(intro.value(QStringLiteral("sound_volume")).toDouble(), 1.75),
        QStringLiteral("snapshot JSON stores a basename and the 0..2 intro multiplier"),
        err);

    VideoExportSnapshot restored;
    QString error;
    const bool restoredOk = VideoExportSnapshot::fromJson(json, &restored, &error);
    ok &= require(
        restoredOk,
        QStringLiteral("snapshot JSON restores successfully: %1").arg(error),
        err);
    ok &= require(
        restored.introSoundFileName == QStringLiteral("custom-start.flac")
            && nearlyEqual(restored.introSoundVolume, 1.75),
        QStringLiteral("snapshot parsing restores intro sound settings"),
        err);

    VideoExportTask workerTask;
    error.clear();
    const bool workerTaskOk = buildVideoExportTaskFromSnapshot(restored, &workerTask, &error);
    ok &= require(
        workerTaskOk,
        QStringLiteral("worker task rebuild succeeds: %1").arg(error),
        err);
    ok &= require(
        workerTask.introSoundFileName == QStringLiteral("custom-start.flac")
            && nearlyEqual(workerTask.introSoundVolume, 1.75),
        QStringLiteral("worker task receives the selected intro sound and volume"),
        err);

    QJsonObject clampedJson = json;
    QJsonObject clampedIntro = clampedJson.value(QStringLiteral("intro")).toObject();
    clampedIntro.insert(QStringLiteral("sound_file"), QStringLiteral("../../unsafe.ogg"));
    clampedIntro.insert(QStringLiteral("sound_volume"), 9.0);
    clampedJson.insert(QStringLiteral("intro"), clampedIntro);
    VideoExportSnapshot clamped;
    error.clear();
    ok &= require(
        VideoExportSnapshot::fromJson(clampedJson, &clamped, &error)
            && clamped.introSoundFileName == QStringLiteral("unsafe.ogg")
            && nearlyEqual(clamped.introSoundVolume, 2.0),
        QStringLiteral("snapshot parsing strips directories and clamps oversized intro volume"),
        err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    const bool ok = verifyPreferencesAndDifficultyReseed(err)
        && verifySnapshotAndWorkerRoundTrip(err);
    if (ok) {
        QTextStream out(stdout);
        out << "video_export_intro_sound_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
