#include "PreviewAudioSettings.h"
#include "QtPreviewSfxRuntime.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

namespace {
QString probeLogPath()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) {
        return QDir::temp().filePath("miacode_audio_debug.log");
    }
    return QDir(baseDir).filePath("miacode_audio_debug.log");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QStringList args = app.arguments();
    const QString chartPath = args.size() > 1 ? QDir::cleanPath(args.at(1)) : QStringLiteral("D:/Files/snowloop/maidata.txt");
    const double rate = args.size() > 2 ? args.at(2).toDouble() : 0.5;
    const int runMs = args.size() > 3 ? args.at(3).toInt() : 8000;

    out << "soundtouch_probe started at " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "chartPath=" << chartPath << "\n";
    out << "rate=" << rate << " runMs=" << runMs << "\n";
    out << "logPath=" << probeLogPath() << "\n";
    out.flush();

    if (!QFileInfo::exists(chartPath)) {
        out << "chart file not found\n";
        return 2;
    }

    PreviewAudioSettings settings;
    settings.bgmVolume = 1.0;
    settings.answerVolume = 0.0;
    settings.slideVolume = 0.0;
    settings.breakVolume = 0.0;
    settings.exVolume = 0.0;
    settings.touchVolume = 0.0;
    settings.touchholdVolume = 0.0;
    settings.normalize();

    QtPreviewSfxRuntime runtime;
    runtime.reloadAssets(settings);
    runtime.setChartPath(chartPath);
    runtime.setBackgroundTrackOffsetSeconds(0.0);
    runtime.setBackgroundTrackPlaybackRate(rate);
    runtime.startBackgroundTrack(0.0);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < runMs) {
        const double second = static_cast<double>(timer.elapsed()) / 1000.0;
        runtime.syncBackgroundTrack(second);
        QThread::msleep(50);
    }

    out << "hasBackgroundTrack=" << (runtime.hasBackgroundTrack() ? "true" : "false") << "\n";
    out << "isBackgroundTrackRunning=" << (runtime.isBackgroundTrackRunning() ? "true" : "false") << "\n";
    out << "backgroundPlaybackSecond=" << runtime.backgroundPlaybackSecond() << "\n";
    out << "done\n";
    out.flush();
    return 0;
}


