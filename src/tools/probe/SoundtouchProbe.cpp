#include "PreviewAudioSettings.h"
#include "QtPreviewSfxRuntime.h"
#include "common/DebugLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include <chrono>
#include <thread>

namespace {
QString probeLogPath()
{
    return miacode::debug_log::audioLogPath();
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QStringList args = app.arguments();
    if (args.size() <= 1) {
        out << "usage: soundtouch_probe <path-to-maidata.txt> [rate] [runMs]\n";
        return 2;
    }
    const QString chartPath = QDir::cleanPath(args.at(1));
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
    settings.trackVolume = 1.0;
    settings.answerVolume = 0.0;
    settings.slideVolume = 0.0;
    settings.breakVolume = 0.0;
    settings.exVolume = 0.0;
    settings.touchVolume = 0.0;
    settings.touchVolume = 0.0;
    settings.normalize();

    QtPreviewSfxRuntime runtime;
    runtime.setChartPath(chartPath);
    const QtPreviewSfxRuntime::AssetSubmission reload = runtime.reloadAssets(settings);
    runtime.setBackgroundTrackOffsetSeconds(0.0);
    runtime.setBackgroundTrackPlaybackRate(rate);
    const miacode::preview_audio::WorkerPostResult start = runtime.startBackgroundTrack(0.0);
    if (!reload.post.accepted || !start.accepted) {
        out << "initial preview-audio command rejected\n";
        return 1;
    }

    struct WaitResult {
        miacode::preview_audio::NonGuiBarrierWaitStatus ready;
        miacode::preview_audio::NonGuiBarrierWaitStatus reload;
        miacode::preview_audio::NonGuiBarrierWaitStatus start;
    } waitResult{
        miacode::preview_audio::NonGuiBarrierWaitStatus::Timeout,
        miacode::preview_audio::NonGuiBarrierWaitStatus::Timeout,
        miacode::preview_audio::NonGuiBarrierWaitStatus::Timeout,
    };
    std::thread waiter([&] {
        using miacode::preview_audio::NonGuiBarrierWaitStatus;
        waitResult.ready = runtime.waitForReadyForNonGui(std::chrono::seconds(5));
        if (waitResult.ready != NonGuiBarrierWaitStatus::Ready) {
            return;
        }
        waitResult.reload = runtime.waitForCompletionForNonGui(reload.identity.sequence, std::chrono::seconds(10));
        if (waitResult.reload != NonGuiBarrierWaitStatus::Completed) {
            return;
        }
        waitResult.start = runtime.waitForCompletionForNonGui(start.sequence, std::chrono::seconds(10));
    });
    waiter.join();
    if (waitResult.ready != miacode::preview_audio::NonGuiBarrierWaitStatus::Ready
        || waitResult.reload != miacode::preview_audio::NonGuiBarrierWaitStatus::Completed
        || waitResult.start != miacode::preview_audio::NonGuiBarrierWaitStatus::Completed) {
        out << "preview-audio worker barrier failed"
            << " ready=" << static_cast<int>(waitResult.ready)
            << " reload=" << static_cast<int>(waitResult.reload)
            << " start=" << static_cast<int>(waitResult.start) << "\n";
        return 1;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < runMs) {
        const double second = static_cast<double>(timer.elapsed()) / 1000.0;
        runtime.syncBackgroundTrack(second);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    out << "hasBackgroundTrack=" << (runtime.hasBackgroundTrack() ? "true" : "false") << "\n";
    out << "isBackgroundTrackRunning=" << (runtime.isBackgroundTrackRunning() ? "true" : "false") << "\n";
    out << "backgroundPlaybackSecond=" << runtime.backgroundPlaybackSecond() << "\n";
    out << "done\n";
    out.flush();
    return 0;
}

