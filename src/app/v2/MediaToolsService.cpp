#include "MediaToolsService.h"

#include "ChartWorkspace.h"
#include "JobProgressService.h"
#include "PreviewSurface.h"
#include "UiRequestService.h"

#include "app/ui/UiText.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "tools/latency/LatencyAnalysis.h"
#include "tools/media/PvBatchCompressionScanner.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QtMath>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <RestartManager.h>
#include <windows.h>
#pragma comment(lib, "Rstrtmgr.lib")
#endif

namespace miacode::v2 {
namespace {

#ifdef Q_OS_WIN
QString describeFileLockHolders(const QString& path)
{
    DWORD session = 0;
    WCHAR sessionKey[CCH_RM_SESSION_KEY + 1] = {0};
    if (RmStartSession(&session, 0, sessionKey) != ERROR_SUCCESS) {
        return QStringLiteral("(RmStartSession failed)");
    }
    QString result;
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    LPCWSTR resources[1] = {native.c_str()};
    if (RmRegisterResources(session, 1, resources, 0, nullptr, 0, nullptr) == ERROR_SUCCESS) {
        UINT needed = 0;
        UINT count = 16;
        RM_PROCESS_INFO infos[16];
        DWORD reasons = 0;
        const DWORD rc = RmGetList(session, &needed, &count, infos, &reasons);
        if (rc == ERROR_SUCCESS || rc == ERROR_MORE_DATA) {
            const DWORD self = GetCurrentProcessId();
            const UINT shown = count < 16 ? count : 16;
            QStringList holders;
            for (UINT i = 0; i < shown; ++i) {
                const DWORD pid = infos[i].Process.dwProcessId;
                QString name = QString::fromWCharArray(infos[i].strAppName);
                if (name.isEmpty()) name = QStringLiteral("?");
                holders << QStringLiteral("%1(PID=%2%3)")
                               .arg(name)
                               .arg(pid)
                               .arg(pid == self ? QStringLiteral(",本进程") : QString());
            }
            if (needed > shown) holders << QStringLiteral("…(+%1)").arg(needed - shown);
            result = holders.isEmpty() ? QStringLiteral("(无持有进程)") : holders.join(QStringLiteral("; "));
        } else {
            result = QStringLiteral("(RmGetList rc=%1)").arg(static_cast<uint>(rc));
        }
    } else {
        result = QStringLiteral("(RmRegisterResources failed)");
    }
    RmEndSession(session);
    return result;
}

QString probeWin32Access(const QString& path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    HANDLE handle = CreateFileW(native.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return QStringLiteral("excl_open=ok");
    }
    return QStringLiteral("excl_open=err%1").arg(static_cast<uint>(GetLastError()));
}
#else
QString describeFileLockHolders(const QString&) { return QString(); }
QString probeWin32Access(const QString&) { return QString(); }
#endif

void logFileLockDiag(const QString& where, const QString& path)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Fatal,
        QStringLiteral("diag/file_lock"),
        QStringLiteral("where=%1 path=%2 %3 holders=%4")
            .arg(where, path, probeWin32Access(path), describeFileLockHolders(path)));
}

bool mediaToolFileIsExecutable(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
}

QString resolveMediaToolFfmpegExecutable()
{
#ifdef Q_OS_WIN
    const QString ffmpegName = QStringLiteral("ffmpeg.exe");
#else
    const QString ffmpegName = QStringLiteral("ffmpeg");
#endif
    const QString envPath = qEnvironmentVariable(
        "MIACODE_FFMPEG_PATH", qEnvironmentVariable("MIACODE_FFMPEG"));
    if (mediaToolFileIsExecutable(envPath)) {
        return QDir::cleanPath(QFileInfo(envPath).absoluteFilePath());
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates{
        appDir.filePath(ffmpegName),
        appDir.filePath(QStringLiteral("ffmpeg/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../Resources/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/windows/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/macos/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/linux/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/windows/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/macos/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/linux/%1").arg(ffmpegName)),
    };
    for (const QString& candidate : candidates) {
        if (mediaToolFileIsExecutable(candidate)) {
            return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(ffmpegName);
    return mediaToolFileIsExecutable(fromPath)
        ? QDir::cleanPath(QFileInfo(fromPath).absoluteFilePath()) : QString();
}

constexpr int kFileLockRetryAttempts = 40;
constexpr int kFileLockRetryDelayMs = 50;
constexpr double kMaxMediaBlankSeconds = 24.0 * 60.0 * 60.0;

void pumpFileLockRetryDelay()
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QThread::msleep(kFileLockRetryDelayMs);
}

bool removeWithRetry(const QString& path)
{
    if (!QFileInfo::exists(path)) return true;
    for (int attempt = 0; attempt < kFileLockRetryAttempts; ++attempt) {
        if (QFile::remove(path)) return true;
        pumpFileLockRetryDelay();
    }
    return false;
}

bool renameWithRetry(const QString& from, const QString& to)
{
    for (int attempt = 0; attempt < kFileLockRetryAttempts; ++attempt) {
        if (QFile::rename(from, to)) return true;
        pumpFileLockRetryDelay();
    }
    return false;
}

bool copyFileReplacing(const QString& sourcePath, const QString& destinationPath, QString* error)
{
    logFileLockDiag(QStringLiteral("copy_entry_src"), sourcePath);
    removeWithRetry(destinationPath);
    for (int attempt = 0; attempt < kFileLockRetryAttempts; ++attempt) {
        if (QFile::copy(sourcePath, destinationPath)) return true;
        QFile::remove(destinationPath);
        pumpFileLockRetryDelay();
    }
    if (error != nullptr) {
        *error = UiText::text(QStringLiteral("media_tools.failed_to_write_file_1"))
            .arg(destinationPath);
    }
    return false;
}

bool restoreFileFromBackup(const QString& backupPath, const QString& destinationPath, QString* error)
{
    if (!QFileInfo::exists(backupPath)) {
        if (error != nullptr) *error = QStringLiteral("Backup file was not found: %1").arg(backupPath);
        return false;
    }
    if (!copyFileReplacing(backupPath, destinationPath, error)) {
        if (error != nullptr) {
            *error = UiText::text(QStringLiteral("media_tools.failed_to_restore_backup_to"))
                .arg(destinationPath);
        }
        return false;
    }
    return true;
}

int mediaBlankClockCountFromFields(const QVector<SimaiRawField>& fields)
{
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) != 0
            && field.key.compare(QStringLiteral("clockcount"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool ok = false;
        const int value = field.value.trimmed().toInt(&ok);
        return ok && value > 0 ? value : 0;
    }
    return 0;
}

int mediaBlankBeatsFromMeterId(const QString& meterId)
{
    bool ok = false;
    const int slash = meterId.indexOf(QChar('/'));
    const int numerator = slash > 0 ? meterId.left(slash).toInt(&ok) : 0;
    return ok && numerator > 0 ? numerator : 4;
}

bool replaceFileWithTemp(const QString& tempPath, const QString& destinationPath, QString* error)
{
    const QString replacingPath = destinationPath + QStringLiteral(".replacing");
    removeWithRetry(replacingPath);
    logFileLockDiag(QStringLiteral("before_rename"), destinationPath);
    if (!renameWithRetry(destinationPath, replacingPath)) {
        const QString diag = QStringLiteral("%1 | %2")
                                 .arg(probeWin32Access(destinationPath),
                                      describeFileLockHolders(destinationPath));
        logFileLockDiag(QStringLiteral("rename_failed"), destinationPath);
        if (error != nullptr) {
            *error = UiText::text(QStringLiteral("media_tools.failed_to_stage_original_file"))
                .arg(destinationPath) + QStringLiteral("\n\n[占用诊断] %1").arg(diag);
        }
        return false;
    }
    if (!renameWithRetry(tempPath, destinationPath)) {
        renameWithRetry(replacingPath, destinationPath);
        if (error != nullptr) *error = QStringLiteral("Failed to replace file: %1").arg(destinationPath);
        return false;
    }
    removeWithRetry(replacingPath);
    return true;
}

bool runFfmpegBlocking(
    const QString& ffmpegPath,
    const QStringList& args,
    JobProgressService* jobProgress,
    const QString& title,
    const QString& label,
    double totalDurationSeconds,
    QString* error,
    bool* cancelled = nullptr)
{
    if (jobProgress == nullptr) {
        if (error != nullptr) *error = QStringLiteral("progress surface unavailable");
        return false;
    }
    if (cancelled != nullptr) *cancelled = false;
    const bool determinate = totalDurationSeconds > 0.0;
    const quint64 jobToken = jobProgress->begin(title, label, true);
    if (!determinate) jobProgress->reportIndeterminate(label);
    const auto endJob = [jobProgress, jobToken]() {
        if (jobProgress->token() == jobToken) jobProgress->end();
    };
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QStringList progressArgs;
    progressArgs << QStringLiteral("-progress") << QStringLiteral("pipe:1")
                 << QStringLiteral("-nostats") << args;
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpegPath, progressArgs, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        endJob();
        if (error != nullptr) *error = process.errorString();
        return false;
    }

    QString progressBuffer;
    QString stderrTail;
    const auto pump = [&]() {
        // -progress pipe:1 is always enabled. Drain stdout even when the
        // duration probe failed, otherwise ffmpeg can block on a full pipe.
        const QByteArray out = process.readAllStandardOutput();
        if (determinate && !out.isEmpty()) {
            progressBuffer += QString::fromLatin1(out);
            const int lastNewline = progressBuffer.lastIndexOf(QLatin1Char('\n'));
            if (lastNewline >= 0) {
                const QString complete = progressBuffer.left(lastNewline);
                progressBuffer = progressBuffer.mid(lastNewline + 1);
                static const QRegularExpression pattern(QStringLiteral(R"(out_time_us=(\d+))"));
                qint64 lastMicros = -1;
                QRegularExpressionMatchIterator it = pattern.globalMatch(complete);
                while (it.hasNext()) lastMicros = it.next().captured(1).toLongLong();
                if (lastMicros >= 0) {
                    const double seconds = static_cast<double>(lastMicros) / 1000000.0;
                    jobProgress->report(
                        qBound(0, qRound(seconds / totalDurationSeconds * 100.0), 99), label);
                }
            }
        }
        const QByteArray err = process.readAllStandardError();
        if (!err.isEmpty()) {
            stderrTail += QString::fromLocal8Bit(err);
            if (stderrTail.size() > 8000) stderrTail = stderrTail.right(8000);
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        pump();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (jobProgress->cancelRequested()) {
            process.kill();
            process.waitForFinished(2000);
            endJob();
            if (cancelled != nullptr) *cancelled = true;
            if (error != nullptr) *error = UiText::text(QStringLiteral("media_tools.canceled"));
            return false;
        }
    }
    process.waitForFinished(200);
    pump();
    if (determinate) jobProgress->report(100, label);
    endJob();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            const QString trimmed = stderrTail.trimmed();
            *error = trimmed.isEmpty()
                ? QStringLiteral("ffmpeg exited with code %1.").arg(process.exitCode())
                : trimmed.right(2000);
        }
        return false;
    }
    return true;
}

bool probeMediaDurationSeconds(
    const QString& ffmpegPath, const QString& mediaPath, double* durationSeconds, QString* error)
{
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-i"), mediaPath,
                     QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(ffmpegPath, args, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) *error = process.errorString();
        return false;
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(3000);
        if (error != nullptr) *error = QStringLiteral("Timed out while probing media duration.");
        return false;
    }
    const QString output = QString::fromLocal8Bit(process.readAll());
    static const QRegularExpression durationPattern(
        QStringLiteral(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))"));
    const QRegularExpressionMatch match = durationPattern.match(output);
    if (!match.hasMatch()) {
        if (error != nullptr) *error = QStringLiteral("Failed to read media duration.");
        return false;
    }
    const double totalSeconds = match.captured(1).toDouble() * 3600.0
        + match.captured(2).toDouble() * 60.0 + match.captured(3).toDouble();
    if (!(totalSeconds > 0.0)) {
        if (error != nullptr) *error = QStringLiteral("Invalid media duration.");
        return false;
    }
    if (durationSeconds != nullptr) *durationSeconds = totalSeconds;
    return true;
}

bool compressVideoUnder20Mb(
    const QString& ffmpegPath,
    const QString& videoPath,
    JobProgressService* jobProgress,
    QString* error,
    bool* cancelled = nullptr,
    bool* preservedCompressed = nullptr)
{
    if (preservedCompressed != nullptr) *preservedCompressed = false;
    constexpr qint64 kTargetBytes = miacode::media::kPvCompressionTargetBytes;
    constexpr int kAudioBitrateKbps = miacode::media::kPvCompressionAudioBitrateKbps;
    constexpr int kMinVideoBitrateKbps = miacode::media::kPvCompressionMinVideoBitrateKbps;
    const QFileInfo videoInfo(videoPath);
    const qint64 originalBytes = videoInfo.size();
    if (originalBytes > 0 && originalBytes <= kTargetBytes) {
        if (error != nullptr) *error = UiText::text(QStringLiteral("media_tools.the_current_video_is_already"));
        return false;
    }
    const QString backupPath = videoInfo.dir().filePath(
        QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix()));
    const QString tempPath = videoInfo.dir().filePath(QStringLiteral(".miacode_video_compress_tmp.mp4"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(videoPath, backupPath, error)) return false;

    double durationSeconds = 0.0;
    if (!probeMediaDurationSeconds(ffmpegPath, backupPath, &durationSeconds, error)) return false;
    const qint64 outputTargetBytes = originalBytes > 0
        ? std::min(kTargetBytes, static_cast<qint64>(std::floor(
              static_cast<double>(originalBytes) * miacode::media::kPvCompressionShrinkRatio)))
        : kTargetBytes;
    const double targetBits = static_cast<double>(outputTargetBytes) * 8.0
        * miacode::media::kPvCompressionMuxSafetyRatio;
    const int totalBitrateKbps = static_cast<int>(std::floor(targetBits / durationSeconds / 1000.0));
    const int videoBitrateKbps = std::max(kMinVideoBitrateKbps, totalBitrateKbps - kAudioBitrateKbps);

    QStringList args;
    args << QStringLiteral("-hide_banner") << QStringLiteral("-y") << QStringLiteral("-i") << backupPath
         << QStringLiteral("-map") << QStringLiteral("0:v:0") << QStringLiteral("-map") << QStringLiteral("0:a?")
         << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-preset") << QStringLiteral("slow")
         << QStringLiteral("-b:v") << QStringLiteral("%1k").arg(videoBitrateKbps)
         << QStringLiteral("-maxrate") << QStringLiteral("%1k").arg(videoBitrateKbps)
         << QStringLiteral("-bufsize") << QStringLiteral("%1k").arg(videoBitrateKbps * 2)
         << QStringLiteral("-vf") << QStringLiteral("scale='min(1280,iw)':-2")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p") << QStringLiteral("-c:a") << QStringLiteral("aac")
         << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(kAudioBitrateKbps)
         << QStringLiteral("-movflags") << QStringLiteral("+faststart") << tempPath;
    if (!runFfmpegBlocking(ffmpegPath, args, jobProgress,
                           UiText::text(QStringLiteral("media_tools.compressing_video")),
                           UiText::text(QStringLiteral("media_tools.compressing_video")),
                           durationSeconds, error, cancelled)) {
        QFile::remove(tempPath);
        return false;
    }
    const qint64 compressedBytes = QFileInfo(tempPath).size();
    if (compressedBytes <= 0 || compressedBytes > kTargetBytes
        || (originalBytes > 0 && compressedBytes >= originalBytes)) {
        QFile::remove(tempPath);
        if (error != nullptr) {
            *error = compressedBytes > kTargetBytes
                ? QStringLiteral("Compressed video is still larger than 20 MiB.")
                : QStringLiteral("Compressed video was not smaller than the original file.");
        }
        return false;
    }
    if (replaceFileWithTemp(tempPath, videoPath, error)) return true;

    const QString preservedPath = videoInfo.dir().filePath(
        QStringLiteral("%1_compressed.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix()));
    QFile::remove(preservedPath);
    if (QFile::rename(tempPath, preservedPath)) {
        if (preservedCompressed != nullptr) *preservedCompressed = true;
        if (error != nullptr) {
            const QString staged = *error;
            *error = UiText::text(QStringLiteral("media_tools.compressed_but_replace_failed"))
                         .arg(QDir::toNativeSeparators(preservedPath));
            if (!staged.isEmpty()) *error += QStringLiteral("\n\n") + staged;
        }
    }
    return false;
}

bool convertTrackFileTo44100Hz(
    const QString& ffmpegPath, const QString& trackPath, JobProgressService* jobProgress,
    QString* error, bool* cancelled = nullptr)
{
    const QFileInfo trackInfo(trackPath);
    const QString backupPath = trackInfo.dir().filePath(QStringLiteral("track_bak.mp3"));
    const QString tempPath = trackInfo.dir().filePath(QStringLiteral(".miacode_track_44100_tmp.mp3"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(trackPath, backupPath, error)) return false;
    double trackDurationSeconds = 0.0;
    probeMediaDurationSeconds(ffmpegPath, backupPath, &trackDurationSeconds, nullptr);
    const QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-y"), QStringLiteral("-i"), backupPath,
        QStringLiteral("-vn"), QStringLiteral("-ar"), QStringLiteral("44100"),
        QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), QStringLiteral("-q:a"), QStringLiteral("2"), tempPath};
    if (!runFfmpegBlocking(ffmpegPath, args, jobProgress,
                           UiText::text(QStringLiteral("media_tools.processing_audio")),
                           UiText::text(QStringLiteral("media_tools.processing_audio")),
                           trackDurationSeconds, error, cancelled)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependTrackSilence(
    const QString& ffmpegPath, const QString& trackPath, double silenceSeconds,
    JobProgressService* jobProgress, QString* error, bool* cancelled = nullptr)
{
    const QFileInfo trackInfo(trackPath);
    const QString backupPath = trackInfo.dir().filePath(QStringLiteral("track_bak.mp3"));
    const QString tempPath = trackInfo.dir().filePath(QStringLiteral(".miacode_track_prepend_tmp.mp3"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(trackPath, backupPath, error)) return false;
    double inputDurationSeconds = 0.0;
    probeMediaDurationSeconds(ffmpegPath, backupPath, &inputDurationSeconds, nullptr);
    const double totalDurationSeconds = inputDurationSeconds > 0.0
        ? inputDurationSeconds + silenceSeconds : 0.0;
    const QString silenceDuration = QString::number(silenceSeconds, 'f', 6);
    const QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=44100:d=%1").arg(silenceDuration),
        QStringLiteral("-i"), backupPath, QStringLiteral("-filter_complex"),
        QStringLiteral("[0:a]atrim=duration=%1,asetpts=PTS-STARTPTS[s];[1:a]aresample=44100,aformat=channel_layouts=stereo,asetpts=PTS-STARTPTS[a];[s][a]concat=n=2:v=0:a=1[out]").arg(silenceDuration),
        QStringLiteral("-map"), QStringLiteral("[out]"), QStringLiteral("-c:a"), QStringLiteral("libmp3lame"),
        QStringLiteral("-q:a"), QStringLiteral("2"), tempPath};
    if (!runFfmpegBlocking(ffmpegPath, args, jobProgress,
                           UiText::text(QStringLiteral("media_tools.processing_track_mp3")),
                           UiText::text(QStringLiteral("media_tools.processing_track_mp3")),
                           totalDurationSeconds, error, cancelled)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependPvBlack(
    const QString& ffmpegPath, const QString& pvPath, double silenceSeconds,
    JobProgressService* jobProgress, QString* error, bool* cancelled = nullptr)
{
    const QFileInfo pvInfo(pvPath);
    const QString backupPath = pvInfo.dir().filePath(
        QStringLiteral("%1_bak.%2").arg(pvInfo.completeBaseName(), pvInfo.suffix()));
    const QString tempPath = pvInfo.dir().filePath(QStringLiteral(".miacode_pv_prepend_tmp.mp4"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(pvPath, backupPath, error)) return false;
    double inputDurationSeconds = 0.0;
    probeMediaDurationSeconds(ffmpegPath, backupPath, &inputDurationSeconds, nullptr);
    const double totalDurationSeconds = inputDurationSeconds > 0.0
        ? inputDurationSeconds + silenceSeconds : 0.0;
    const QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-t"), QString::number(silenceSeconds, 'f', 6), QStringLiteral("-i"),
        QStringLiteral("color=c=black:s=1920x1080:r=30"), QStringLiteral("-i"), backupPath,
        QStringLiteral("-filter_complex"),
        QStringLiteral("[0:v]format=yuv420p[v0];[1:v]scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p[v1];[v0][v1]concat=n=2:v=1:a=0[v]"),
        QStringLiteral("-map"), QStringLiteral("[v]"), QStringLiteral("-an"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-crf"), QStringLiteral("18"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-movflags"), QStringLiteral("+faststart"), tempPath};
    if (!runFfmpegBlocking(ffmpegPath, args, jobProgress,
                           UiText::text(QStringLiteral("media_tools.processing_pv_mp4")),
                           UiText::text(QStringLiteral("media_tools.processing_pv_mp4")),
                           totalDurationSeconds, error, cancelled)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, pvPath, error);
}

}  // namespace

MediaToolsService::MediaToolsService(
    ChartWorkspace& workspace,
    UiRequestService& uiRequests,
    JobProgressService& jobProgress,
    PreviewSurface*& previewSurfaceSlot,
    QObject* parent)
    : QObject(parent)
    , MediaToolsEngine()
    , workspace_(&workspace)
    , uiRequests_(&uiRequests)
    , jobProgress_(&jobProgress)
    , previewSurfaceSlot_(&previewSurfaceSlot)
{
}

MediaToolsService::~MediaToolsService()
{
    invalidateCallbacks();
}

void MediaToolsService::invalidateCallbacks()
{
    callbacksInvalidated_ = true;
}

QString MediaToolsService::resolveCurrentChartDirectory() const
{
    if (workspace_ == nullptr) return QString();
    const QString filePath = workspace_->snapshot().filePath;
    return filePath.isEmpty() ? QString() : QFileInfo(filePath).absoluteDir().absolutePath();
}

MediaToolsService::MediaBlankPaths MediaToolsService::resolveMediaBlankPaths(bool isTrack) const
{
    MediaBlankPaths paths;
    paths.isTrack = isTrack;
    paths.title = isTrack
        ? UiText::text(QStringLiteral("media_tools.prepend_track_silence"))
        : UiText::text(QStringLiteral("media_tools.prepend_pv_black_screen"));
    if (workspace_ == nullptr) return paths;
    const ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    const QString chartDirectory = resolveCurrentChartDirectory();
    if (chartDirectory.isEmpty()) return paths;
    const SimaiDocument& document = workspace_->document();
    paths.inputPath = isTrack
        ? QDir(chartDirectory).filePath(QStringLiteral("track.mp3"))
        : miacode::chart_assets::resolveChartVideoPath(snapshot.filePath, document.videoPath);
    const QFileInfo inputInfo(paths.inputPath);
    paths.inputName = isTrack ? QStringLiteral("track.mp3") : inputInfo.fileName();
    paths.backupName = isTrack
        ? QStringLiteral("track_bak.mp3")
        : QStringLiteral("%1_bak.%2").arg(inputInfo.completeBaseName(), inputInfo.suffix());
    paths.backupPath = paths.inputPath.isEmpty() ? QString() : inputInfo.dir().filePath(paths.backupName);
    return paths;
}

bool MediaToolsService::workspaceMediaOperationMatches(
    quint64 expectedRevision, const QString& expectedPath, const QString& currentPath) const
{
    if (workspace_ == nullptr) return false;
    const ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    return snapshot.hasDocument && snapshot.revision == expectedRevision
        && currentPath == expectedPath;
}

PreviewSurface* MediaToolsService::beginMediaFileOperation(const QString& title)
{
    if (activeMediaOperation_) {
        if (uiRequests_ != nullptr) {
            uiRequests_->postNotice(
                NoticeSeverity::Warning,
                title,
                QStringLiteral("Another media operation is already active; no file was changed."));
        }
        return nullptr;
    }

    // Set this before reading the live slot or entering PreviewSurface. Both
    // operations may pump events and therefore re-enter shutdown code.
    activeMediaOperation_ = true;
    PreviewSurface* const surface =
        previewSurfaceSlot_ != nullptr ? *previewSurfaceSlot_ : nullptr;
    if (surface == nullptr || !surface->beginMediaFileOperation()) {
        activeMediaOperation_ = false;
        if (uiRequests_ != nullptr) {
            uiRequests_->postNotice(
                NoticeSeverity::Error, title,
                QStringLiteral("Preview media could not be released; no file was changed."));
        }
        return nullptr;
    }
    return surface;
}

bool MediaToolsService::endMediaFileOperation(
    PreviewSurface* surface, bool reloadTrack, const QString& title)
{
    PreviewSurface* const liveSurface =
        previewSurfaceSlot_ != nullptr ? *previewSurfaceSlot_ : nullptr;
    if (surface == nullptr || liveSurface == nullptr || liveSurface != surface) {
        activeMediaOperation_ = false;
        if (uiRequests_ != nullptr) {
            uiRequests_->postNotice(
                NoticeSeverity::Error,
                title,
                QStringLiteral("Preview media surface changed or became unavailable; the old surface was not accessed."));
        }
        return false;
    }
    const bool accepted = liveSurface->endMediaFileOperation(reloadTrack);
    // Keep the activity guard set until PreviewSurface has returned. Its end
    // implementation may pump events and can be re-entered by shutdown.
    activeMediaOperation_ = false;
    if (!accepted && uiRequests_ != nullptr) {
        uiRequests_->postNotice(
            NoticeSeverity::Error, title, QStringLiteral("Preview media could not be restored."));
    }
    return accepted;
}

void MediaToolsService::showMediaOperationComplete(
    const QString& title, const QString& summary, const QString& producedFilePath)
{
    if (uiRequests_ == nullptr) return;
    uiRequests_->requestNoticeAction(
        NoticeSeverity::Information,
        title,
        summary,
        QDir::toNativeSeparators(producedFilePath),
        UiText::text(QStringLiteral("dialogs.open_folder")),
        [producedFilePath](bool openFolder) {
            if (!openFolder) return;
            const QString directory = QFileInfo(producedFilePath).absoluteDir().absolutePath();
            if (!directory.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
            }
        });
}

void MediaToolsService::convertTrackTo44100Hz()
{
    MC_OP("MediaToolsService::convertTrackTo44100Hz");
    if (callbacksInvalidated_ || uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString title = UiText::text(QStringLiteral("media_tools.sample_rate"));
    const ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    const QString chartDirectory = snapshot.filePath.isEmpty()
        ? QString() : QFileInfo(snapshot.filePath).absoluteDir().absolutePath();
    if (chartDirectory.isEmpty()) {
        uiRequests_->postNotice(NoticeSeverity::Warning, title,
                                UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }
    const QString trackPath = QDir(chartDirectory).filePath(QStringLiteral("track.mp3"));
    if (!QFileInfo::exists(trackPath)) {
        uiRequests_->postNotice(NoticeSeverity::Warning, title,
                                UiText::text(QStringLiteral("media_tools.track_mp3_was_not_found")));
        return;
    }
    QPointer<MediaToolsService> guard(this);
    uiRequests_->requestConfirmation(
        title,
        UiText::text(QStringLiteral("media_tools.convert_track_mp3_to_44100")),
        UiText::text(QStringLiteral("media_tools.sample_rate")),
        [guard, title, workspaceRevision = snapshot.revision, trackPath](bool accepted) {
            if (accepted && !guard.isNull() && !guard->callbacksInvalidated_) {
                guard->runConvertTrackTo44100Hz(title, workspaceRevision, trackPath);
            }
        });
}

void MediaToolsService::runConvertTrackTo44100Hz(
    const QString& title, quint64 expectedRevision, const QString& trackPath)
{
    if (callbacksInvalidated_ || uiRequests_ == nullptr) return;
    if (workspace_ == nullptr) return;
    const ChartWorkspaceSnapshot currentSnapshot = workspace_->snapshot();
    const SimaiDocument& currentDocument = workspace_->document();
    Q_UNUSED(currentDocument);
    const QString currentChartDirectory = currentSnapshot.filePath.isEmpty()
        ? QString() : QFileInfo(currentSnapshot.filePath).absoluteDir().absolutePath();
    const QString currentTrackPath = currentChartDirectory.isEmpty()
        ? QString() : QDir(currentChartDirectory).filePath(QStringLiteral("track.mp3"));
    if (!currentSnapshot.hasDocument || currentSnapshot.revision != expectedRevision
        || currentTrackPath != trackPath) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            title,
            QStringLiteral("The chart or track path changed while waiting for confirmation; no file was changed."));
        return;
    }
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        uiRequests_->postNotice(NoticeSeverity::Error, title,
                                UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place")));
        return;
    }
    PreviewSurface* const surface = beginMediaFileOperation(title);
    if (surface == nullptr) return;
    const ChartWorkspaceSnapshot postBeginSnapshot = workspace_->snapshot();
    const QString postBeginChartDirectory = postBeginSnapshot.filePath.isEmpty()
        ? QString() : QFileInfo(postBeginSnapshot.filePath).absoluteDir().absolutePath();
    const QString postBeginTrackPath = postBeginChartDirectory.isEmpty()
        ? QString() : QDir(postBeginChartDirectory).filePath(QStringLiteral("track.mp3"));
    if (!workspaceMediaOperationMatches(
            expectedRevision, trackPath, postBeginTrackPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            title,
            QStringLiteral("The chart or track path changed before writing; no file was changed."));
        endMediaFileOperation(surface, true, title);
        return;
    }
    QString error;
    bool cancelled = false;
    const bool converted = convertTrackFileTo44100Hz(
        ffmpegPath, trackPath, jobProgress_, &error, &cancelled);
    if (!converted) {
        uiRequests_->postNotice(
            cancelled ? NoticeSeverity::Information : NoticeSeverity::Error,
            title,
            cancelled ? UiText::text(QStringLiteral("media_tools.sample_rate_conversion_canceled"))
                      : error);
        endMediaFileOperation(surface, true, title);
        return;
    }
    if (!endMediaFileOperation(surface, true, title)) return;
    showMediaOperationComplete(
        title, UiText::text(QStringLiteral("media_tools.converted_track_mp3_to_44100_2")), trackPath);
}

void MediaToolsService::compressBackgroundVideo()
{
    MC_OP("MediaToolsService::compressBackgroundVideo");
    if (callbacksInvalidated_ || uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString title = UiText::text(QStringLiteral("media_tools.compress_video"));
    const ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    const QString chartDirectory = snapshot.filePath.isEmpty()
        ? QString() : QFileInfo(snapshot.filePath).absoluteDir().absolutePath();
    if (chartDirectory.isEmpty()) {
        uiRequests_->postNotice(NoticeSeverity::Warning, title,
                                UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }
    const SimaiDocument& document = workspace_->document();
    const QString videoPath = miacode::chart_assets::resolveChartVideoPath(
        snapshot.filePath, document.videoPath);
    if (!QFileInfo::exists(videoPath)) {
        uiRequests_->postNotice(NoticeSeverity::Warning, title,
                                UiText::text(QStringLiteral("media_tools.no_background_mp4_video_was")));
        return;
    }
    constexpr qint64 kTargetBytes = miacode::media::kPvCompressionTargetBytes;
    const QFileInfo videoInfo(videoPath);
    if (videoInfo.size() > 0 && videoInfo.size() <= kTargetBytes) {
        uiRequests_->postNotice(
            NoticeSeverity::Information, title,
            UiText::text(QStringLiteral("media_tools.the_current_video_is_already_2"))
                .arg(QLocale().formattedDataSize(videoInfo.size())));
        return;
    }
    const QString backupName = QStringLiteral("%1_bak.%2")
                                   .arg(videoInfo.completeBaseName(), videoInfo.suffix());
    QPointer<MediaToolsService> guard(this);
    uiRequests_->requestConfirmation(
        title,
        UiText::text(QStringLiteral("media_tools.compress_1_under_20_mib"))
            .arg(videoInfo.fileName(), backupName),
        UiText::text(QStringLiteral("media_tools.compress_video")),
        [guard, title, workspaceRevision = snapshot.revision, videoPath, backupName](bool accepted) {
            if (accepted && !guard.isNull() && !guard->callbacksInvalidated_) {
                guard->runCompressBackgroundVideo(
                    title, workspaceRevision, videoPath, backupName);
            }
        });
}

void MediaToolsService::runCompressBackgroundVideo(
    const QString& title, quint64 expectedRevision, const QString& videoPath,
    const QString& backupName)
{
    if (callbacksInvalidated_ || uiRequests_ == nullptr) return;
    if (workspace_ == nullptr) return;
    const ChartWorkspaceSnapshot currentSnapshot = workspace_->snapshot();
    const SimaiDocument& currentDocument = workspace_->document();
    const QString currentVideoPath = miacode::chart_assets::resolveChartVideoPath(
        currentSnapshot.filePath, currentDocument.videoPath);
    if (!currentSnapshot.hasDocument || currentSnapshot.revision != expectedRevision
        || currentVideoPath != videoPath) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            title,
            QStringLiteral("The chart or video path changed while waiting for confirmation; no file was changed."));
        return;
    }
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        uiRequests_->postNotice(NoticeSeverity::Error, title,
                                UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place")));
        return;
    }
    PreviewSurface* const surface = beginMediaFileOperation(title);
    if (surface == nullptr) return;
    const ChartWorkspaceSnapshot postBeginSnapshot = workspace_->snapshot();
    const SimaiDocument& postBeginDocument = workspace_->document();
    const QString postBeginVideoPath = miacode::chart_assets::resolveChartVideoPath(
        postBeginSnapshot.filePath, postBeginDocument.videoPath);
    if (!workspaceMediaOperationMatches(
            expectedRevision, videoPath, postBeginVideoPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            title,
            QStringLiteral("The chart or video path changed before writing; no file was changed."));
        endMediaFileOperation(surface, false, title);
        return;
    }
    QString error;
    bool cancelled = false;
    bool preservedCompressed = false;
    const bool compressed = compressVideoUnder20Mb(
        ffmpegPath, videoPath, jobProgress_, &error, &cancelled, &preservedCompressed);
    if (!compressed) {
        uiRequests_->postNotice(
            (cancelled || preservedCompressed) ? NoticeSeverity::Information : NoticeSeverity::Error,
            title,
            cancelled ? UiText::text(QStringLiteral("media_tools.video_compression_canceled")) : error);
        endMediaFileOperation(surface, false, title);
        return;
    }
    if (!endMediaFileOperation(surface, false, title)) return;
    showMediaOperationComplete(
        title,
        UiText::text(QStringLiteral("media_tools.compressed_1_under_20_mib_2"))
            .arg(QFileInfo(videoPath).fileName(), backupName),
        videoPath);
}

QVariantMap MediaToolsService::mediaBlankContext(bool isTrack)
{
    MC_OP("MediaToolsService::mediaBlankContext");
    QVariantMap context;
    const MediaBlankPaths paths = resolveMediaBlankPaths(isTrack);
    context.insert(QStringLiteral("available"), false);
    context.insert(QStringLiteral("title"), paths.title);
    context.insert(QStringLiteral("isTrack"), paths.isTrack);
    context.insert(QStringLiteral("inputName"), QString());
    context.insert(QStringLiteral("backupName"), QString());
    context.insert(QStringLiteral("hasBackup"), false);
    context.insert(QStringLiteral("beats"), 4);
    context.insert(QStringLiteral("bpm"), miacode::chart_clock::kFallbackClockBpm);
    if (uiRequests_ == nullptr || workspace_ == nullptr) return context;
    if (paths.inputPath.isEmpty()) {
        uiRequests_->postNotice(NoticeSeverity::Warning, paths.title,
                                UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return context;
    }
    if (!QFileInfo::exists(paths.inputPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning, paths.title,
            UiText::text(QStringLiteral("media_tools.1_was_not_found_next"))
                .arg(isTrack ? paths.inputName
                             : UiText::text(QStringLiteral("media_tools.background_mp4_video"))));
        return context;
    }

    const ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    const SimaiDocument& document = workspace_->document();
    const SimaiDifficultyData* difficulty = document.difficulty(snapshot.activeDifficultyId);
    const int clockCount = mediaBlankClockCountFromFields(document.extraFields);
    const double wholeBpm = miacode::chart_clock::wholeBpmFromFields(document.extraFields);
    const double chartBpm = difficulty == nullptr
        ? 0.0 : miacode::chart_clock::firstBpmFromChart(difficulty->chart);
    context.insert(QStringLiteral("available"), true);
    context.insert(QStringLiteral("inputName"), paths.inputName);
    context.insert(QStringLiteral("backupName"), paths.backupName);
    context.insert(QStringLiteral("hasBackup"), QFileInfo::exists(paths.backupPath));
    context.insert(QStringLiteral("beats"), clockCount > 0 ? clockCount : 4);
    context.insert(QStringLiteral("bpm"), wholeBpm > 0.0
        ? wholeBpm : (chartBpm > 0.0 ? chartBpm : miacode::chart_clock::kFallbackClockBpm));
    return context;
}

QVariantMap MediaToolsService::detectMediaBlankTiming(bool isTrack)
{
    Q_UNUSED(isTrack);
    if (workspace_ == nullptr) return {};
    const QString trackPath = miacode::chart_assets::resolveTrackPath(workspace_->snapshot().filePath);
    const auto decoded = miacode::latency_analysis::decodeMonoTrack(trackPath);
    if (decoded.samples.isEmpty()) return {};
    const auto envelope = miacode::latency_analysis::buildOnsetEnvelope(
        decoded.samples, decoded.sampleRate);
    const auto result = miacode::latency_analysis::detectBpm(envelope);
    if (!(result.bpm > 0.0)) return {};
    QVariantMap detected;
    detected.insert(QStringLiteral("bpm"), result.bpm);
    if (!result.meterId.isEmpty()) {
        detected.insert(QStringLiteral("beats"), mediaBlankBeatsFromMeterId(result.meterId));
    }
    return detected;
}

void MediaToolsService::restoreMediaBlankBackup(bool isTrack)
{
    MC_OP("MediaToolsService::restoreMediaBlankBackup");
    if (uiRequests_ == nullptr || workspace_ == nullptr) return;
    const ChartWorkspaceSnapshot capturedSnapshot = workspace_->snapshot();
    const MediaBlankPaths paths = resolveMediaBlankPaths(isTrack);
    if (paths.inputPath.isEmpty()) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }
    if (!QFileInfo::exists(paths.inputPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            UiText::text(QStringLiteral("media_tools.1_was_not_found_next"))
                .arg(paths.inputName));
        return;
    }
    if (paths.backupPath.isEmpty() || !QFileInfo::exists(paths.backupPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            UiText::text(QStringLiteral("media_tools.1_was_not_found_next"))
                .arg(paths.backupName));
        return;
    }
    PreviewSurface* const surface = beginMediaFileOperation(paths.title);
    if (surface == nullptr) return;
    const QString postBeginPath = resolveMediaBlankPaths(isTrack).inputPath;
    if (!workspaceMediaOperationMatches(
            capturedSnapshot.revision, paths.inputPath, postBeginPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            QStringLiteral("The chart or media path changed before restoring; no file was changed."));
        endMediaFileOperation(surface, paths.isTrack, paths.title);
        return;
    }
    QString error;
    const bool restored = restoreFileFromBackup(paths.backupPath, paths.inputPath, &error);
    if (!restored) {
        uiRequests_->postNotice(NoticeSeverity::Error, paths.title, error);
    } else {
        uiRequests_->postNotice(NoticeSeverity::Information, paths.title,
                                UiText::text(QStringLiteral("media_tools.backup_restored")));
    }
    endMediaFileOperation(surface, paths.isTrack, paths.title);
}

void MediaToolsService::applyMediaBlank(bool isTrack, double beats, double bpm)
{
    MC_OP("MediaToolsService::applyMediaBlank");
    if (uiRequests_ == nullptr || workspace_ == nullptr || !std::isfinite(bpm) || !(bpm > 0.0)
        || !std::isfinite(beats) || !(beats > 0.0)) {
        return;
    }
    const ChartWorkspaceSnapshot capturedSnapshot = workspace_->snapshot();
    const MediaBlankPaths paths = resolveMediaBlankPaths(isTrack);
    if (paths.inputPath.isEmpty()) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }
    if (!QFileInfo::exists(paths.inputPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            UiText::text(QStringLiteral("media_tools.1_was_not_found_next"))
                .arg(isTrack ? paths.inputName
                             : UiText::text(QStringLiteral("media_tools.background_mp4_video"))));
        return;
    }
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        uiRequests_->postNotice(NoticeSeverity::Error, paths.title,
                                UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place")));
        return;
    }
    const double silenceSeconds = beats * 60.0 / bpm;
    if (!std::isfinite(silenceSeconds) || silenceSeconds > kMaxMediaBlankSeconds) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            QStringLiteral("Blank media duration must be finite and no more than 24 hours."));
        return;
    }
    PreviewSurface* const surface = beginMediaFileOperation(paths.title);
    if (surface == nullptr) return;
    const QString postBeginPath = resolveMediaBlankPaths(isTrack).inputPath;
    if (!workspaceMediaOperationMatches(
            capturedSnapshot.revision, paths.inputPath, postBeginPath)) {
        uiRequests_->postNotice(
            NoticeSeverity::Warning,
            paths.title,
            QStringLiteral("The chart or media path changed before writing; no file was changed."));
        endMediaFileOperation(surface, paths.isTrack, paths.title);
        return;
    }
    QString error;
    bool cancelled = false;
    const bool applied = isTrack
        ? prependTrackSilence(ffmpegPath, paths.inputPath, silenceSeconds, jobProgress_, &error, &cancelled)
        : prependPvBlack(ffmpegPath, paths.inputPath, silenceSeconds, jobProgress_, &error, &cancelled);
    if (!applied) {
        uiRequests_->postNotice(
            cancelled ? NoticeSeverity::Information : NoticeSeverity::Error,
            paths.title,
            cancelled
                ? UiText::text(isTrack ? QStringLiteral("media_tools.track_mp3_processing_canceled")
                                       : QStringLiteral("media_tools.video_processing_canceled"))
                : error);
        endMediaFileOperation(surface, paths.isTrack, paths.title);
        return;
    }
    if (!endMediaFileOperation(surface, paths.isTrack, paths.title)) return;
    showMediaOperationComplete(
        paths.title,
        UiText::text(QStringLiteral("media_tools.prepended_2_s_of_3"))
            .arg(paths.inputName)
            .arg(silenceSeconds, 0, 'f', 3)
            .arg(isTrack ? UiText::text(QStringLiteral("media_tools.silence"))
                         : UiText::text(QStringLiteral("media_tools.black_screen")))
            .arg(paths.backupName),
        paths.inputPath);
}

}  // namespace miacode::v2
