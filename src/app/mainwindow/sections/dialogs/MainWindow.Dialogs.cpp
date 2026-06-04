#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/Id3TagReader.h"
#include "common/OperationLog.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewGameplayConfig.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/latency/LatencyAnalysis.h"

#include <QDesktopServices>
#include <QUrl>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <algorithm>
#include <cmath>

#include "common/DebugLog.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <RestartManager.h>
#pragma comment(lib, "Rstrtmgr.lib")
#endif

using namespace miacode::mainwindow::shared;

namespace {

// --- "pv占用" diagnostics -------------------------------------------------
// Name the process(es) currently holding `path` open, via the Windows Restart
// Manager. Tells us whether the lock is MiaCode itself (internal handle leak)
// or an external program. See project_pv_file_lock_release.
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
    LPCWSTR resources[1] = { native.c_str() };
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
                if (name.isEmpty()) {
                    name = QStringLiteral("?");
                }
                holders << QStringLiteral("%1(PID=%2%3)")
                               .arg(name)
                               .arg(pid)
                               .arg(pid == self ? QStringLiteral(",本进程") : QString());
            }
            if (needed > shown) {
                holders << QStringLiteral("…(+%1)").arg(needed - shown);
            }
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
// Try to open with NO sharing. Succeeds only if nothing holds the file; on
// failure the Win32 error distinguishes a sharing lock (32) from access-denied
// (5), not-found (2), etc. — so we can tell "locked" from "fails for another
// reason" even when the Restart Manager reports no holder.
QString probeWin32Access(const QString& path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    HANDLE h = CreateFileW(native.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
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
    const QString envPath = qEnvironmentVariable("MIACODE_FFMPEG_PATH", qEnvironmentVariable("MIACODE_FFMPEG"));
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
    if (mediaToolFileIsExecutable(fromPath)) {
        return QDir::cleanPath(QFileInfo(fromPath).absoluteFilePath());
    }
    return QString();
}

// Windows can briefly refuse a rename/remove while the file is still held by a
// just-released decoder handle, an antivirus on-write scan, or Explorer's
// preview/thumbnail handler. Retry with a short backoff (~2s max) so these
// transient locks don't fail the whole operation. processEvents keeps the modal
// progress UI responsive between attempts. See project_pv_file_lock_release.
constexpr int kFileLockRetryAttempts = 40;
constexpr int kFileLockRetryDelayMs = 50;

void pumpFileLockRetryDelay()
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QThread::msleep(kFileLockRetryDelayMs);
}

bool removeWithRetry(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        return true;
    }
    for (int attempt = 0; attempt < kFileLockRetryAttempts; ++attempt) {
        if (QFile::remove(path)) {
            return true;
        }
        pumpFileLockRetryDelay();
    }
    return false;
}

bool renameWithRetry(const QString& from, const QString& to)
{
    for (int attempt = 0; attempt < kFileLockRetryAttempts; ++attempt) {
        if (QFile::rename(from, to)) {
            return true;
        }
        pumpFileLockRetryDelay();
    }
    return false;
}

bool copyFileReplacing(const QString& sourcePath, const QString& destinationPath, QString* error)
{
    // Diagnostic bracket #1: who holds the source right after the preview
    // release (and before any ffmpeg work). For pv ops sourcePath is pv.mp4.
    logFileLockDiag(QStringLiteral("copy_entry_src"), sourcePath);
    removeWithRetry(destinationPath);
    for (int attempt = 0; attempt < kFileLockRetryAttempts; ++attempt) {
        if (QFile::copy(sourcePath, destinationPath)) {
            return true;
        }
        // A failed copy can leave a partial/zero-byte destination; clear it
        // before the next attempt so QFile::copy doesn't bail on "already exists".
        QFile::remove(destinationPath);
        pumpFileLockRetryDelay();
    }
    if (error != nullptr) {
        *error = UiText::isChineseUi()
            ? QStringLiteral("无法写入文件：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。").arg(destinationPath)
            : QStringLiteral("Failed to write file: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program.").arg(destinationPath);
    }
    return false;
}

bool restoreFileFromBackup(const QString& backupPath, const QString& destinationPath, QString* error)
{
    if (!QFileInfo::exists(backupPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("Backup file was not found: %1").arg(backupPath);
        }
        return false;
    }
    if (!copyFileReplacing(backupPath, destinationPath, error)) {
        if (error != nullptr) {
            *error = UiText::isChineseUi()
                ? QStringLiteral("还原备份失败：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。").arg(destinationPath)
                : QStringLiteral("Failed to restore backup to: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program.").arg(destinationPath);
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
        if (ok && value > 0) {
            return value;
        }
        return 0;
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
    // Diagnostic bracket #2: who holds the file right before we rename it
    // (after ffmpeg finished). Compare with bracket #1 to tell "never released"
    // from "re-acquired during the encode".
    logFileLockDiag(QStringLiteral("before_rename"), destinationPath);
    if (!renameWithRetry(destinationPath, replacingPath)) {
        const QString diag = QStringLiteral("%1 | %2")
                                 .arg(probeWin32Access(destinationPath),
                                      describeFileLockHolders(destinationPath));
        logFileLockDiag(QStringLiteral("rename_failed"), destinationPath);
        if (error != nullptr) {
            *error = (UiText::isChineseUi()
                ? QStringLiteral("无法替换原文件：%1\n\n文件可能仍被预览、播放器、资源管理器预览窗格或其他程序占用。请停止预览并关闭占用该文件的程序后重试。").arg(destinationPath)
                : QStringLiteral("Failed to stage original file for replacement: %1\n\nThe file may still be open in preview, a media player, File Explorer preview pane, or another program. Stop preview and close programs using it, then try again.").arg(destinationPath))
                + QStringLiteral("\n\n[占用诊断] %1").arg(diag);
        }
        return false;
    }
    if (!renameWithRetry(tempPath, destinationPath)) {
        renameWithRetry(replacingPath, destinationPath);
        if (error != nullptr) {
            *error = QStringLiteral("Failed to replace file: %1").arg(destinationPath);
        }
        return false;
    }
    removeWithRetry(replacingPath);
    return true;
}

// Runs ffmpeg modally with a progress dialog. When `totalDurationSeconds` is
// positive the bar tracks real percent-done — matching the video-export
// progress UX — by reading ffmpeg's machine-readable `-progress pipe:1`
// stream: each block carries an `out_time_us=` line (the output timestamp
// reached so far) which, divided by the expected total duration, gives the
// percentage. When the duration is unknown (<= 0) it falls back to an
// indeterminate busy bar.
bool runFfmpegBlocking(
    const QString& ffmpegPath,
    const QStringList& args,
    QWidget* parent,
    const QString& label,
    double totalDurationSeconds,
    QString* error)
{
    const bool determinate = totalDurationSeconds > 0.0;
    QProgressDialog progress(label, QString(), 0, determinate ? 100 : 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    if (determinate) {
        progress.setValue(0);
    }
    progress.show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QStringList progressArgs;
    progressArgs << QStringLiteral("-progress") << QStringLiteral("pipe:1")
                 << QStringLiteral("-nostats")
                 << args;

    QProcess process;
    // Keep the channels separate: `-progress` writes to stdout while ffmpeg's
    // human-readable diagnostics (which we surface on failure) go to stderr.
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpegPath, progressArgs, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) {
            *error = process.errorString();
        }
        return false;
    }

    QString progressBuffer;
    QString stderrTail;
    const auto pump = [&]() {
        if (determinate) {
            const QByteArray out = process.readAllStandardOutput();
            if (!out.isEmpty()) {
                progressBuffer += QString::fromLatin1(out);
                const int lastNewline = progressBuffer.lastIndexOf(QLatin1Char('\n'));
                if (lastNewline >= 0) {
                    const QString complete = progressBuffer.left(lastNewline);
                    progressBuffer = progressBuffer.mid(lastNewline + 1);
                    static const QRegularExpression outTimePattern(QStringLiteral(R"(out_time_us=(\d+))"));
                    qint64 lastMicros = -1;
                    QRegularExpressionMatchIterator it = outTimePattern.globalMatch(complete);
                    while (it.hasNext()) {
                        lastMicros = it.next().captured(1).toLongLong();
                    }
                    if (lastMicros >= 0) {
                        const double seconds = static_cast<double>(lastMicros) / 1000000.0;
                        // Cap at 99% until the process actually exits so the
                        // bar doesn't read "done" while ffmpeg is still muxing.
                        progress.setValue(qBound(0, qRound(seconds / totalDurationSeconds * 100.0), 99));
                    }
                }
            }
        }
        const QByteArray err = process.readAllStandardError();
        if (!err.isEmpty()) {
            stderrTail += QString::fromLocal8Bit(err);
            if (stderrTail.size() > 8000) {
                stderrTail = stderrTail.right(8000);
            }
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        pump();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    process.waitForFinished(200);
    pump();  // drain anything emitted between the last read and exit
    if (determinate) {
        progress.setValue(100);
    }
    progress.close();

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

bool probeMediaDurationSeconds(const QString& ffmpegPath, const QString& mediaPath, double* durationSeconds, QString* error)
{
    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-i") << mediaPath
         << QStringLiteral("-f") << QStringLiteral("null")
         << QStringLiteral("-");

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(ffmpegPath, args, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) {
            *error = process.errorString();
        }
        return false;
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(3000);
        if (error != nullptr) {
            *error = QStringLiteral("Timed out while probing media duration.");
        }
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAll());
    static const QRegularExpression durationPattern(
        QStringLiteral(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))")
    );
    const QRegularExpressionMatch match = durationPattern.match(output);
    if (!match.hasMatch()) {
        if (error != nullptr) {
            *error = QStringLiteral("Failed to read media duration.");
        }
        return false;
    }

    const double hours = match.captured(1).toDouble();
    const double minutes = match.captured(2).toDouble();
    const double seconds = match.captured(3).toDouble();
    const double totalSeconds = hours * 3600.0 + minutes * 60.0 + seconds;
    if (!(totalSeconds > 0.0)) {
        if (error != nullptr) {
            *error = QStringLiteral("Invalid media duration.");
        }
        return false;
    }
    if (durationSeconds != nullptr) {
        *durationSeconds = totalSeconds;
    }
    return true;
}

bool compressVideoUnder20Mb(
    const QString& ffmpegPath,
    const QString& videoPath,
    QWidget* parent,
    QString* error)
{
    constexpr qint64 kTargetBytes = 20LL * 1024LL * 1024LL;
    constexpr int kAudioBitrateKbps = 96;
    constexpr int kMinVideoBitrateKbps = 120;
    const QFileInfo videoInfo(videoPath);
    const qint64 originalBytes = videoInfo.size();
    if (originalBytes > 0 && originalBytes <= kTargetBytes) {
        if (error != nullptr) {
            *error = UiText::isChineseUi()
                ? QStringLiteral("当前视频已经小于 20 MiB，无需压缩。")
                : QStringLiteral("The current video is already under 20 MiB; compression is not needed.");
        }
        return false;
    }
    const QString backupPath = videoInfo.dir().filePath(
        QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix())
    );
    const QString tempPath = videoInfo.dir().filePath(QStringLiteral(".miacode_video_compress_tmp.mp4"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(videoPath, backupPath, error)) {
        return false;
    }

    double durationSeconds = 0.0;
    if (!probeMediaDurationSeconds(ffmpegPath, backupPath, &durationSeconds, error)) {
        return false;
    }

    const qint64 outputTargetBytes = originalBytes > 0
        ? std::min(kTargetBytes, static_cast<qint64>(std::floor(static_cast<double>(originalBytes) * 0.86)))
        : kTargetBytes;
    const double targetBits = static_cast<double>(outputTargetBytes) * 8.0 * 0.965;
    int totalBitrateKbps = static_cast<int>(std::floor(targetBits / durationSeconds / 1000.0));
    int videoBitrateKbps = std::max(kMinVideoBitrateKbps, totalBitrateKbps - kAudioBitrateKbps);

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-map") << QStringLiteral("0:v:0")
         << QStringLiteral("-map") << QStringLiteral("0:a?")
         << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-preset") << QStringLiteral("slow")
         << QStringLiteral("-b:v") << QStringLiteral("%1k").arg(videoBitrateKbps)
         << QStringLiteral("-maxrate") << QStringLiteral("%1k").arg(videoBitrateKbps)
         << QStringLiteral("-bufsize") << QStringLiteral("%1k").arg(videoBitrateKbps * 2)
         << QStringLiteral("-vf") << QStringLiteral("scale='min(1280,iw)':-2")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-c:a") << QStringLiteral("aac")
         << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(kAudioBitrateKbps)
         << QStringLiteral("-movflags") << QStringLiteral("+faststart")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在压缩视频...") : QStringLiteral("Compressing video..."),
            durationSeconds,
            error)) {
        QFile::remove(tempPath);
        return false;
    }

    const qint64 compressedBytes = QFileInfo(tempPath).size();
    if (compressedBytes <= 0 || compressedBytes > kTargetBytes || (originalBytes > 0 && compressedBytes >= originalBytes)) {
        QFile::remove(tempPath);
        if (error != nullptr) {
            *error = compressedBytes > kTargetBytes
                ? QStringLiteral("Compressed video is still larger than 20 MiB.")
                : QStringLiteral("Compressed video was not smaller than the original file.");
        }
        return false;
    }
    return replaceFileWithTemp(tempPath, videoPath, error);
}

bool convertTrackTo44100Hz(
    const QString& ffmpegPath,
    const QString& trackPath,
    QWidget* parent,
    QString* error)
{
    const QFileInfo trackInfo(trackPath);
    const QString backupPath = trackInfo.dir().filePath(QStringLiteral("track_bak.mp3"));
    const QString tempPath = trackInfo.dir().filePath(QStringLiteral(".miacode_track_44100_tmp.mp3"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(trackPath, backupPath, error)) {
        return false;
    }

    // Best-effort duration probe so the progress bar can read real percent;
    // a failed probe just degrades to the indeterminate busy bar.
    double trackDurationSeconds = 0.0;
    probeMediaDurationSeconds(ffmpegPath, backupPath, &trackDurationSeconds, nullptr);

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-vn")
         << QStringLiteral("-ar") << QStringLiteral("44100")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-q:a") << QStringLiteral("2")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在处理音频...") : QStringLiteral("Processing audio..."),
            trackDurationSeconds,
            error)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependTrackSilence(
    const QString& ffmpegPath,
    const QString& trackPath,
    double silenceSeconds,
    QWidget* parent,
    QString* error)
{
    const QFileInfo trackInfo(trackPath);
    const QString backupPath = trackInfo.dir().filePath(QStringLiteral("track_bak.mp3"));
    const QString tempPath = trackInfo.dir().filePath(QStringLiteral(".miacode_track_prepend_tmp.mp3"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(trackPath, backupPath, error)) {
        return false;
    }

    // Output runs for the original track plus the prepended silence; probe
    // the source so the progress bar can track real percent (best-effort).
    double inputDurationSeconds = 0.0;
    probeMediaDurationSeconds(ffmpegPath, backupPath, &inputDurationSeconds, nullptr);
    const double totalDurationSeconds =
        inputDurationSeconds > 0.0 ? inputDurationSeconds + silenceSeconds : 0.0;

    const QString silenceDuration = QString::number(silenceSeconds, 'f', 6);
    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-f") << QStringLiteral("lavfi")
         << QStringLiteral("-i") << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=44100:d=%1").arg(silenceDuration)
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-filter_complex")
         << QStringLiteral("[0:a]atrim=duration=%1,asetpts=PTS-STARTPTS[s];[1:a]aresample=44100,aformat=channel_layouts=stereo,asetpts=PTS-STARTPTS[a];[s][a]concat=n=2:v=0:a=1[out]").arg(silenceDuration)
         << QStringLiteral("-map") << QStringLiteral("[out]")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-q:a") << QStringLiteral("2")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在处理 track.mp3...") : QStringLiteral("Processing track.mp3..."),
            totalDurationSeconds,
            error)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependPvBlack(
    const QString& ffmpegPath,
    const QString& pvPath,
    double silenceSeconds,
    QWidget* parent,
    QString* error)
{
    const QFileInfo pvInfo(pvPath);
    const QString backupPath = pvInfo.dir().filePath(
        QStringLiteral("%1_bak.%2").arg(pvInfo.completeBaseName(), pvInfo.suffix())
    );
    const QString tempPath = pvInfo.dir().filePath(QStringLiteral(".miacode_pv_prepend_tmp.mp4"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(pvPath, backupPath, error)) {
        return false;
    }

    // Output runs for the original video plus the prepended black screen;
    // probe the source so the progress bar can track real percent (best-effort).
    double inputDurationSeconds = 0.0;
    probeMediaDurationSeconds(ffmpegPath, backupPath, &inputDurationSeconds, nullptr);
    const double totalDurationSeconds =
        inputDurationSeconds > 0.0 ? inputDurationSeconds + silenceSeconds : 0.0;

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-f") << QStringLiteral("lavfi")
         << QStringLiteral("-t") << QString::number(silenceSeconds, 'f', 6)
         << QStringLiteral("-i") << QStringLiteral("color=c=black:s=1920x1080:r=30")
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-filter_complex")
         << QStringLiteral("[0:v]format=yuv420p[v0];[1:v]scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p[v1];[v0][v1]concat=n=2:v=1:a=0[v]")
         << QStringLiteral("-map") << QStringLiteral("[v]")
         << QStringLiteral("-an")
         << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-preset") << QStringLiteral("veryfast")
         << QStringLiteral("-crf") << QStringLiteral("18")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-movflags") << QStringLiteral("+faststart")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在处理 pv.mp4...") : QStringLiteral("Processing pv.mp4..."),
            totalDurationSeconds,
            error)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, pvPath, error);
}

} // namespace

MainWindow::DialogsSection::DialogsSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

QString MainWindow::DialogsSection::resolveLatencyDetectorTrackPath() const
{
    if (owner_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return miacode::chart_assets::resolveTrackPath(owner_.currentFilePath_);
}

QString MainWindow::DialogsSection::resolveCurrentChartDirectory() const
{
    if (owner_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return QFileInfo(owner_.currentFilePath_).absoluteDir().absolutePath();
}

void MainWindow::DialogsSection::releasePreviewMediaForFileOperation()
{
    owner_.onStopPreview();
    // clearPreviewStageMediaRoute() -> setChartPath("") -> clearMedia(), which
    // now pushes an empty frame to the QML sink so the retained QVideoFrame ->
    // QAVStream -> QAVFormatContext reference is dropped and the pv.mp4 avio
    // handle is actually closed (the real "pv占用" fix lives in clearMedia()).
    // We deliberately do NOT destroy the host here: deleting it detaches the
    // QML VideoOutput's sink and it does not reliably re-attach to the
    // re-created host, which left the post-op preview blank ("压缩后视频不加载").
    // Keeping the host alive means the post-op reload re-decodes onto the same
    // still-attached sink. See project_pv_file_lock_release.
    owner_.clearPreviewStageMediaRoute();
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
}

void MainWindow::DialogsSection::reloadPreviewMediaAfterFileOperation(bool reloadTrack)
{
    if (owner_.currentFilePath_.isEmpty()) {
        return;
    }
    if (reloadTrack) {
        owner_.lastTrackPath_ = miacode::chart_assets::resolveTrackPath(owner_.currentFilePath_);
        if (state_.waveformCacheService_ != nullptr) {
            state_.waveformCacheService_->clear();
        }
        if (owner_.previewSfxRuntime_ != nullptr) {
            owner_.previewSfxRuntime_->setChartPath(QString());
            owner_.previewSfxRuntime_->setChartPath(owner_.currentFilePath_);
            owner_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(owner_.previewPlaybackRate_);
            owner_.previewSfxRuntime_->resetRetainedPreviewPlaybackTransaction(qMax(0.0, owner_.qtPreviewPauseSecond_));
        }
        owner_.refreshWaveformCache();
        owner_.updatePreviewSliderRange();
        owner_.updatePreviewSliderPosition(qMax(0.0, owner_.qtPreviewPauseSecond_));
    }
    owner_.syncPreviewStageMediaRouteChartPath(
        owner_.currentFilePath_,
        owner_.lastTrackPath_,
        qMax(0.0, owner_.qtPreviewPauseSecond_),
        owner_.document_.videoPath
    );
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

void MainWindow::DialogsSection::onPreviewAudioSettings()
{
    MC_OP("MainWindow::DialogsSection::onPreviewAudioSettings");
    openPreviewSettingsDialog(
        true,
        false,
        uiText("dialog.audio_settings.title", "Audio Settings")
    );
}

void MainWindow::DialogsSection::onPreviewVideoSettings()
{
    MC_OP("MainWindow::DialogsSection::onPreviewVideoSettings");
    openPreviewSettingsDialog(
        false,
        true,
        uiText("dialog.video_settings.title", "Preview Settings")
    );
}

void MainWindow::DialogsSection::onAbout()
{
    MC_OP("MainWindow::DialogsSection::onAbout");
    QString buildType = "Release";
#ifndef NDEBUG
    buildType = "Debug";
#endif
    const QString platform = QString("%1 / %2 / %3")
        .arg(QSysInfo::productType())
        .arg(QSysInfo::currentCpuArchitecture())
        .arg(QSysInfo::buildAbi());

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(uiText("action.about", "About"));
    dialog.setModal(true);
    dialog.setMinimumWidth(500);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(10);

    auto* card = new QFrame(&dialog);
    card->setObjectName("AboutCard");
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName("AboutIcon");
    iconLabel->setFixedSize(64, 64);
    QPixmap appIcon = QIcon(":/icons/app.png").pixmap(48, 48);
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon);
        iconLabel->setAlignment(Qt::AlignCenter);
    }
    owner_.aboutIconLabel_ = iconLabel;
    iconLabel->installEventFilter(&owner_);
    titleRow->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto* titleTextCol = new QVBoxLayout();
    titleTextCol->setSpacing(4);
    auto* titleLabel = new QLabel("MiaCode", card);
    titleLabel->setObjectName("AboutTitle");
    QString displayVersion = QString::fromLatin1(MIACODE_DISPLAY_VERSION_STRING).trimmed();
    if (displayVersion.isEmpty()) {
        displayVersion = QCoreApplication::applicationVersion().trimmed();
    }
    if (displayVersion.isEmpty()) {
        displayVersion = QStringLiteral("0.0.0");
    }
    auto* versionLabel = new QLabel(QStringLiteral("v%1").arg(displayVersion), card);
    versionLabel->setObjectName("AboutVersion");
    titleTextCol->addWidget(titleLabel, 0, Qt::AlignLeft);
    titleTextCol->addWidget(versionLabel, 0, Qt::AlignLeft);
    titleRow->addLayout(titleTextCol, 0);
    titleRow->addStretch(1);
    cardLayout->addLayout(titleRow);

    auto* infoGrid = new QGridLayout();
    infoGrid->setHorizontalSpacing(12);
    infoGrid->setVerticalSpacing(6);
    auto addRow = [card, infoGrid](int row, const QString& key, const QString& value) {
        auto* k = new QLabel(key, card);
        k->setObjectName("AboutKey");
        auto* v = new QLabel(value, card);
        v->setObjectName("AboutValue");
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoGrid->addWidget(k, row, 0);
        infoGrid->addWidget(v, row, 1);
    };
    addRow(0, uiText("about.platform", "Release Platform"), platform);
    addRow(1, uiText("about.build_type", "Build Type"), buildType);
    cardLayout->addLayout(infoGrid);
    rootLayout->addWidget(card);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);
    dialog.exec();
    if (owner_.aboutIconLabel_ != nullptr) {
        owner_.aboutIconLabel_->removeEventFilter(&owner_);
    }
    owner_.aboutIconLabel_.clear();
    owner_.invalidStarPreviewAboutClickCount_ = 0;
    owner_.invalidStarPreviewAboutClickElapsed_.invalidate();
}

void MainWindow::DialogsSection::onPrependTrackSilence()
{
    onPrependMediaBlank(MediaBlankTarget::Track);
}

void MainWindow::DialogsSection::onPrependPvBlack()
{
    onPrependMediaBlank(MediaBlankTarget::Pv);
}

void MainWindow::DialogsSection::onCompressBackgroundVideo()
{
    MC_OP("MainWindow::DialogsSection::onCompressBackgroundVideo");
    const QString title = UiText::isChineseUi() ? QStringLiteral("视频压缩") : QStringLiteral("Compress Video");
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }

    const QString videoPath = miacode::chart_assets::resolveChartVideoPath(owner_.currentFilePath_, owner_.document_.videoPath);
    if (!QFileInfo::exists(videoPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少背景视频 .mp4。")
                : QStringLiteral("No background .mp4 video was found next to the current chart.")
        );
        return;
    }

    const QFileInfo videoInfo(videoPath);
    const QString backupName = QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix());
    if (QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("将压缩 %1 到 20M 内，并生成/覆盖备份 %2。是否继续？").arg(videoInfo.fileName(), backupName)
                : QStringLiteral("Compress %1 under 20 MiB and create/replace backup %2?").arg(videoInfo.fileName(), backupName)
        ) != QMessageBox::Yes) {
        return;
    }

    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。")
                : QStringLiteral("ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH.")
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    if (!compressVideoUnder20Mb(ffmpegPath, videoPath, UiDialogs::effectiveParentWidget(&owner_), &error)) {
        QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
        reloadPreviewMediaAfterFileOperation(false);
        return;
    }
    reloadPreviewMediaAfterFileOperation(false);
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已压缩 %1 到 20M 内。").arg(videoInfo.fileName())
            : QStringLiteral("Compressed %1 under 20 MiB.").arg(videoInfo.fileName()),
        6000
    );
    showMediaOperationCompleteDialog(
        title,
        UiText::isChineseUi()
            ? QStringLiteral("已压缩 %1 到 20M 内（原文件已备份为 %2）。").arg(videoInfo.fileName(), backupName)
            : QStringLiteral("Compressed %1 under 20 MiB (original backed up as %2).").arg(videoInfo.fileName(), backupName),
        videoPath
    );
}

void MainWindow::DialogsSection::onConvertTrackTo44100Hz()
{
    MC_OP("MainWindow::DialogsSection::onConvertTrackTo44100Hz");
    const QString title = UiText::isChineseUi() ? QStringLiteral("采样率转换") : QStringLiteral("Sample Rate");
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }

    const QString trackPath = QDir(chartDirPath).filePath(QStringLiteral("track.mp3"));
    if (!QFileInfo::exists(trackPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 track.mp3。")
                : QStringLiteral("track.mp3 was not found next to the current chart.")
        );
        return;
    }

    if (QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("将 track.mp3 处理为 44100Hz，并生成/覆盖备份 track_bak.mp3。是否继续？")
                : QStringLiteral("Convert track.mp3 to 44100 Hz and create/replace backup track_bak.mp3?")
        ) != QMessageBox::Yes) {
        return;
    }

    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。")
                : QStringLiteral("ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH.")
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    if (!convertTrackTo44100Hz(ffmpegPath, trackPath, UiDialogs::effectiveParentWidget(&owner_), &error)) {
        QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
        reloadPreviewMediaAfterFileOperation(true);
        return;
    }
    reloadPreviewMediaAfterFileOperation(true);
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已将 track.mp3 处理为 44100Hz。")
            : QStringLiteral("Converted track.mp3 to 44100 Hz."),
        6000
    );
    showMediaOperationCompleteDialog(
        title,
        UiText::isChineseUi()
            ? QStringLiteral("已将 track.mp3 处理为 44100Hz（原文件已备份为 track_bak.mp3）。")
            : QStringLiteral("Converted track.mp3 to 44100 Hz (original backed up as track_bak.mp3)."),
        trackPath
    );
}

void MainWindow::DialogsSection::onMediaProcessingTools()
{
    MC_OP("MainWindow::DialogsSection::onMediaProcessingTools");
    const bool zh = UiText::isChineseUi();

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(zh ? QStringLiteral("音频/视频处理") : QStringLiteral("Audio/Video Processing"));
    dialog.setModal(true);
    dialog.setMinimumWidth(480);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(10);

    struct MediaToolEntry {
        QString label;
        QString description;
        void (MainWindow::DialogsSection::*handler)();
    };
    const QVector<MediaToolEntry> entries = {
        { zh ? QStringLiteral("采样率转换") : QStringLiteral("Sample Rate"),
          zh ? QStringLiteral("将 track.mp3 转换为 44100Hz，并自动备份原文件。")
             : QStringLiteral("Convert track.mp3 to 44100 Hz (the original is backed up)."),
          &MainWindow::DialogsSection::onConvertTrackTo44100Hz },
        { zh ? QStringLiteral("视频压缩") : QStringLiteral("Compress Video"),
          zh ? QStringLiteral("将背景视频压缩到 20M 以内，并自动备份原文件。")
             : QStringLiteral("Compress the background video under 20 MiB (the original is backed up)."),
          &MainWindow::DialogsSection::onCompressBackgroundVideo },
        { zh ? QStringLiteral("音频开头静音处理") : QStringLiteral("Prepend Track Silence"),
          zh ? QStringLiteral("在 track.mp3 开头插入一段静音，并自动备份原文件。")
             : QStringLiteral("Insert silence at the start of track.mp3 (the original is backed up)."),
          &MainWindow::DialogsSection::onPrependTrackSilence },
        { zh ? QStringLiteral("视频开头黑幕处理") : QStringLiteral("Prepend PV Black Screen"),
          zh ? QStringLiteral("在背景视频开头插入一段黑幕，并自动备份原文件。")
             : QStringLiteral("Insert a black screen at the start of the background video (the original is backed up)."),
          &MainWindow::DialogsSection::onPrependPvBlack },
    };

    QVector<QPushButton*> toolButtons;
    QVector<QLabel*> descLabels;
    toolButtons.reserve(entries.size());
    descLabels.reserve(entries.size());
    for (const MediaToolEntry& entry : entries) {
        auto* card = new QFrame(&dialog);
        card->setObjectName("AboutCard");
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);
        cardLayout->setSpacing(12);

        auto* button = new QPushButton(entry.label, card);
        button->setCursor(Qt::PointingHandCursor);
        toolButtons.append(button);
        // Each button runs its existing handler, which shows its own
        // confirm prompt and a determinate progress bar. The dialog stays
        // open (modal) so several tools can be run in one sitting.
        connect(button, &QPushButton::clicked, &dialog, [this, handler = entry.handler]() {
            (this->*handler)();
        });
        cardLayout->addWidget(button, 0, Qt::AlignTop);

        auto* desc = new QLabel(entry.description, card);
        desc->setObjectName("AboutValue");
        desc->setWordWrap(true);
        descLabels.append(desc);
        cardLayout->addWidget(desc, 1);

        rootLayout->addWidget(card);
    }

    // Give every button the same width, wide enough for the longest label so
    // the multi-character Chinese labels (e.g. "音频开头静音处理") don't clip.
    int uniformButtonWidth = 0;
    for (QPushButton* button : toolButtons) {
        const int advance = button->fontMetrics().horizontalAdvance(button->text());
        uniformButtonWidth = qMax(uniformButtonWidth, qMax(advance, button->sizeHint().width()));
    }
    uniformButtonWidth += 40;  // padding beyond the raw text advance
    for (QPushButton* button : toolButtons) {
        button->setFixedWidth(uniformButtonWidth);
    }

    // Widen the dialog so the longest description fits on a single line next to
    // its button (button + spacing + full description text + all the margins).
    int widestDescription = 0;
    for (QLabel* desc : descLabels) {
        widestDescription = qMax(widestDescription, desc->fontMetrics().horizontalAdvance(desc->text()));
    }
    const int requiredWidth = 14 + 14            // rootLayout left/right margins
        + 16 + 16                                // card left/right margins
        + uniformButtonWidth + 12                // button + card spacing
        + widestDescription + 16;                // description + slack
    dialog.setMinimumWidth(qMax(480, requiredWidth));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);

    dialog.exec();
}

void MainWindow::DialogsSection::showMediaOperationCompleteDialog(
    const QString& title,
    const QString& summary,
    const QString& producedFilePath)
{
    const QFileInfo info(producedFilePath);
    const QString nativePath = QDir::toNativeSeparators(producedFilePath);
    QMessageBox dialog(
        QMessageBox::Information,
        title,
        QStringLiteral("%1\n\n%2").arg(summary, nativePath),
        QMessageBox::NoButton,
        UiDialogs::effectiveParentWidget(&owner_)
    );
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QPushButton* openButton = dialog.addButton(
        UiText::isChineseUi() ? QStringLiteral("打开文件夹") : QStringLiteral("Open Folder"),
        QMessageBox::AcceptRole
    );
    dialog.addButton(uiText("action.close", "Close"), QMessageBox::RejectRole);
    dialog.setDefaultButton(openButton);
    dialog.exec();
    if (dialog.clickedButton() == openButton) {
        const QString dir = info.absoluteDir().absolutePath();
        if (!dir.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        }
    }
}

namespace {

// Shared helper for the title/artist buttons. Resolves track.mp3, reads
// its ID3v2 tag, and returns the requested field's value. Reports any
// failure through QMessageBox at `parent`; on success returns the
// trimmed field value, or an empty string if the tag exists but the
// requested field is blank (the caller decides whether to warn).
enum class TrackTagField {
    Title,
    Artist,
};

QString readTrackTagField(
    const QString& trackPath,
    TrackTagField field,
    QWidget* parent,
    const QString& dialogTitle)
{
    if (trackPath.isEmpty()) {
        QMessageBox::warning(
            parent,
            dialogTitle,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 track.mp3。")
                : QStringLiteral("track.mp3 was not found next to the current chart.")
        );
        return QString();
    }
    const miacode::id3::Tag tag = miacode::id3::readTagFromFile(trackPath);
    if (!tag.valid) {
        QMessageBox::information(
            parent,
            dialogTitle,
            UiText::isChineseUi()
                ? QStringLiteral("没能在 track.mp3 中读取到 ID3v2 标签。")
                : QStringLiteral("No ID3v2 tag was found in track.mp3.")
        );
        return QString();
    }
    const QString value = (field == TrackTagField::Title ? tag.title : tag.artist).trimmed();
    if (value.isEmpty()) {
        const QString fieldLabelZh = (field == TrackTagField::Title)
            ? QStringLiteral("标题") : QStringLiteral("曲师");
        const QString fieldLabelEn = (field == TrackTagField::Title)
            ? QStringLiteral("title") : QStringLiteral("artist");
        QMessageBox::information(
            parent,
            dialogTitle,
            UiText::isChineseUi()
                ? QStringLiteral("track.mp3 的 ID3 标签里没有%1信息。").arg(fieldLabelZh)
                : QStringLiteral("track.mp3's ID3 tag carries no %1.").arg(fieldLabelEn)
        );
        return QString();
    }
    return value;
}

}  // namespace

void MainWindow::DialogsSection::onReadTitleFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onReadTitleFromTrack");
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("从 MP3 读取标题")
        : QStringLiteral("Read Title from MP3");
    const QString trackPath = resolveLatencyDetectorTrackPath();
    const QString value = readTrackTagField(
        trackPath, TrackTagField::Title, UiDialogs::effectiveParentWidget(&owner_), title);
    if (value.isEmpty() || ui_.titleEdit_ == nullptr) {
        return;
    }
    // setText() fires QLineEdit::textChanged, which the FrameBootstrap
    // wiring already routes through markCurrentFieldDirty() — no extra
    // dirty-tracking call needed here.
    ui_.titleEdit_->setText(value);
    _mc_op_.note(QStringLiteral("track=%1 title=%2").arg(trackPath, value));
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已从 track.mp3 读取标题。")
            : QStringLiteral("Loaded title from track.mp3."),
        6000
    );
}

void MainWindow::DialogsSection::onReadArtistFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onReadArtistFromTrack");
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("从 MP3 读取曲师")
        : QStringLiteral("Read Artist from MP3");
    const QString trackPath = resolveLatencyDetectorTrackPath();
    const QString value = readTrackTagField(
        trackPath, TrackTagField::Artist, UiDialogs::effectiveParentWidget(&owner_), title);
    if (value.isEmpty() || ui_.artistEdit_ == nullptr) {
        return;
    }
    ui_.artistEdit_->setText(value);
    _mc_op_.note(QStringLiteral("track=%1 artist=%2").arg(trackPath, value));
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已从 track.mp3 读取曲师。")
            : QStringLiteral("Loaded artist from track.mp3."),
        6000
    );
}

void MainWindow::DialogsSection::onExtractBackgroundFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onExtractBackgroundFromTrack");
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("提取封面为 bg.jpg")
        : QStringLiteral("Extract Cover to bg.jpg");
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }
    const QString trackPath = resolveLatencyDetectorTrackPath();
    if (trackPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 track.mp3。")
                : QStringLiteral("track.mp3 was not found next to the current chart.")
        );
        return;
    }

    const miacode::id3::Tag tag = miacode::id3::readTagFromFile(trackPath);
    if (!tag.valid || tag.pictureBytes.isEmpty()) {
        QMessageBox::information(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("track.mp3 中没有内嵌的封面图。")
                : QStringLiteral("track.mp3 has no embedded cover artwork.")
        );
        return;
    }

    // Decode the APIC payload via QImage so we can re-encode to JPEG
    // regardless of the embedded format (PNG, WebP, etc.). Writing the
    // raw bytes verbatim would be slightly higher quality but would
    // require renaming bg.jpg when the source isn't JPEG, which doesn't
    // match the "always bg.jpg" naming the user asked for.
    QImage cover;
    if (!cover.loadFromData(tag.pictureBytes)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("内嵌封面解码失败（MIME=%1）。").arg(tag.pictureMimeType)
                : QStringLiteral("Failed to decode embedded cover (MIME=%1).").arg(tag.pictureMimeType)
        );
        return;
    }

    const QString bgPath = QDir(chartDirPath).filePath(QStringLiteral("bg.jpg"));
    const bool existed = QFileInfo::exists(bgPath);
    if (existed) {
        const auto answer = QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("bg.jpg 已经存在，是否覆盖？")
                : QStringLiteral("bg.jpg already exists. Overwrite?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    // Drop the preview's grip on the bg file before overwriting it so
    // Windows doesn't reject the write with a sharing-violation error,
    // mirroring the pattern used by the track/video file operations.
    releasePreviewMediaForFileOperation();

    if (!cover.save(bgPath, "JPG", 92)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("写入 bg.jpg 失败。")
                : QStringLiteral("Failed to write bg.jpg.")
        );
        // Still try to reload — the previous file (if any) is back.
        reloadPreviewMediaAfterFileOperation(false);
        return;
    }

    // reloadPreviewMediaAfterFileOperation re-runs the chart-path
    // resolution pipeline; PreviewStageMediaHost::setChartPath was
    // cleared by releasePreviewMediaForFileOperation above, so the
    // re-resolution lands on the brand-new bg.jpg even if the chart
    // path itself didn't change.
    reloadPreviewMediaAfterFileOperation(false);
    _mc_op_.note(QStringLiteral("bg=%1 replaced=%2 source_mime=%3 source_bytes=%4")
                     .arg(bgPath)
                     .arg(existed ? 1 : 0)
                     .arg(tag.pictureMimeType)
                     .arg(tag.pictureBytes.size()));
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? (existed
                   ? QStringLiteral("已覆盖 bg.jpg（来源：track.mp3 内嵌封面）。")
                   : QStringLiteral("已生成 bg.jpg（来源：track.mp3 内嵌封面）。"))
            : (existed
                   ? QStringLiteral("Overwrote bg.jpg with embedded cover from track.mp3.")
                   : QStringLiteral("Wrote bg.jpg from track.mp3's embedded cover.")),
        6000
    );
}

void MainWindow::DialogsSection::onPrependMediaBlank(MediaBlankTarget target)
{
    MC_OP("MainWindow::DialogsSection::onPrependMediaBlank");
    const bool isTrack = target == MediaBlankTarget::Track;
    const QString title = isTrack
        ? (UiText::isChineseUi() ? QStringLiteral("音频开头静音处理") : QStringLiteral("Prepend Track Silence"))
        : (UiText::isChineseUi() ? QStringLiteral("视频开头黑幕处理") : QStringLiteral("Prepend PV Black Screen"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }

    const QDir chartDir(chartDirPath);
    const QString trackPath = chartDir.filePath(QStringLiteral("track.mp3"));
    const QString videoPath = miacode::chart_assets::resolveChartVideoPath(owner_.currentFilePath_, owner_.document_.videoPath);
    const QString inputPath = isTrack ? trackPath : videoPath;
    const QFileInfo inputInfo(inputPath);
    const QString inputName = isTrack ? QStringLiteral("track.mp3") : inputInfo.fileName();
    const QString backupName = isTrack
        ? QStringLiteral("track_bak.mp3")
        : QStringLiteral("%1_bak.%2").arg(inputInfo.completeBaseName(), inputInfo.suffix());
    const QString backupPath = inputPath.isEmpty()
        ? QString()
        : inputInfo.dir().filePath(backupName);
    if (!QFileInfo::exists(inputPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 %1。").arg(isTrack ? inputName : QStringLiteral("背景视频 .mp4"))
                : QStringLiteral("%1 was not found next to the current chart.").arg(isTrack ? inputName : QStringLiteral("background .mp4 video"))
        );
        return;
    }

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(360);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* beatsSpin = new QDoubleSpinBox(&dialog);
    beatsSpin->setRange(0.125, 512.0);
    beatsSpin->setDecimals(3);
    beatsSpin->setSingleStep(1.0);
    auto* bpmSpin = new QDoubleSpinBox(&dialog);
    bpmSpin->setRange(1.0, 999.0);
    bpmSpin->setDecimals(3);
    bpmSpin->setSingleStep(1.0);
    const QVector<SimaiRawField> extraFields = SimaiDocument::parseRawFields(
        ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    const QString chartText = owner_.activeChartText();
    const auto detectedBeats = [extraFields]() {
        const int clockCount = mediaBlankClockCountFromFields(extraFields);
        return clockCount > 0 ? clockCount : 4;
    };
    const auto detectedBpm = [extraFields, chartText]() {
        const double wholeBpm = miacode::chart_clock::wholeBpmFromFields(extraFields);
        if (wholeBpm > 0.0) {
            return wholeBpm;
        }
        const double chartBpm = miacode::chart_clock::firstBpmFromChart(chartText);
        return chartBpm > 0.0 ? chartBpm : miacode::chart_clock::kFallbackClockBpm;
    };
    const auto analyzeTrackBpmAndMeter = [this]() -> QPair<double, QString> {
        const QString trackPath = miacode::chart_assets::resolveTrackPath(owner_.currentFilePath_);
        const auto decoded = miacode::latency_analysis::decodeMonoTrack(trackPath);
        if (decoded.samples.isEmpty()) {
            return {0.0, QString()};
        }
        const auto envelope = miacode::latency_analysis::buildOnsetEnvelope(decoded.samples, decoded.sampleRate);
        const auto result = miacode::latency_analysis::detectBpm(envelope);
        if (!(result.bpm > 0.0)) {
            return {0.0, QString()};
        }
        return {result.bpm, result.meterId};
    };
    beatsSpin->setValue(detectedBeats());
    bpmSpin->setValue(detectedBpm());
    auto* beatsRow = new QWidget(&dialog);
    auto* beatsRowLayout = new QHBoxLayout(beatsRow);
    beatsRowLayout->setContentsMargins(0, 0, 0, 0);
    beatsRowLayout->setSpacing(6);
    beatsRowLayout->addWidget(beatsSpin, 1);
    auto* detectBeatsButton = new QPushButton(UiText::isChineseUi() ? QStringLiteral("自动检测") : QStringLiteral("Detect"), beatsRow);
    beatsRowLayout->addWidget(detectBeatsButton, 0);
    auto* bpmRow = new QWidget(&dialog);
    auto* bpmRowLayout = new QHBoxLayout(bpmRow);
    bpmRowLayout->setContentsMargins(0, 0, 0, 0);
    bpmRowLayout->setSpacing(6);
    bpmRowLayout->addWidget(bpmSpin, 1);
    auto* detectBpmButton = new QPushButton(UiText::isChineseUi() ? QStringLiteral("自动检测") : QStringLiteral("Detect"), bpmRow);
    bpmRowLayout->addWidget(detectBpmButton, 0);
    const auto applyDetectedBeats = [beatsSpin, analyzeTrackBpmAndMeter, detectedBeats]() {
        const QPair<double, QString> detected = analyzeTrackBpmAndMeter();
        beatsSpin->setValue(detected.second.isEmpty() ? detectedBeats() : mediaBlankBeatsFromMeterId(detected.second));
    };
    const auto applyDetectedBpm = [bpmSpin, analyzeTrackBpmAndMeter, detectedBpm]() {
        const QPair<double, QString> detected = analyzeTrackBpmAndMeter();
        bpmSpin->setValue(detected.first > 0.0 ? detected.first : detectedBpm());
    };
    connect(detectBeatsButton, &QPushButton::clicked, &dialog, applyDetectedBeats);
    connect(detectBpmButton, &QPushButton::clicked, &dialog, applyDetectedBpm);
    form->addRow(UiText::isChineseUi() ? QStringLiteral("拍数") : QStringLiteral("Beats"), beatsRow);
    form->addRow(QStringLiteral("BPM"), bpmRow);

    // Live, plain-language description of what the entered beats/BPM produce —
    // updates as either spinner changes so the user sees the resulting blank
    // length before committing. Placed ABOVE the inputs.
    auto* summaryLabel = new QLabel(&dialog);
    summaryLabel->setWordWrap(true);
    const auto formatNumber = [](double value) {
        QString text = QString::number(value, 'f', 3);
        if (text.contains(QLatin1Char('.'))) {
            while (text.endsWith(QLatin1Char('0'))) {
                text.chop(1);
            }
            if (text.endsWith(QLatin1Char('.'))) {
                text.chop(1);
            }
        }
        return text;
    };
    const auto updateSummary = [summaryLabel, beatsSpin, bpmSpin, isTrack, inputName, formatNumber]() {
        const double bpm = bpmSpin->value();
        const double beats = beatsSpin->value();
        const double seconds = bpm > 0.0 ? beats * 60.0 / bpm : 0.0;
        summaryLabel->setText(UiText::isChineseUi()
            ? QStringLiteral("将在 %1 开头增加一段%2，时长为 BPM %3 下的 %4 个 4 分音（约 %5 秒）。")
                  .arg(isTrack ? QStringLiteral("track.mp3") : QStringLiteral("背景视频"))
                  .arg(isTrack ? QStringLiteral("空白") : QStringLiteral("黑幕"))
                  .arg(formatNumber(bpm), formatNumber(beats), formatNumber(seconds))
            : QStringLiteral("Prepends %1 to %2: %3 quarter-notes at %4 BPM (~%5 s).")
                  .arg(isTrack ? QStringLiteral("silence") : QStringLiteral("a black screen"))
                  .arg(inputName)
                  .arg(formatNumber(beats), formatNumber(bpm), formatNumber(seconds)));
    };
    connect(beatsSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, [updateSummary](double) { updateSummary(); });
    connect(bpmSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, [updateSummary](double) { updateSummary(); });
    updateSummary();
    layout->addWidget(summaryLabel);

    // Divider between the descriptive text block and the input fields. Colour
    // comes from the theme palette so it adapts to light/dark mode.
    auto* separator = new QFrame(&dialog);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setFixedHeight(1);
    separator->setStyleSheet(
        QStringLiteral("background-color: %1; border: none;")
            .arg(UiTheme::colors().borderSoft.name(QColor::HexRgb)));
    layout->addWidget(separator);

    layout->addLayout(form);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* restoreButton = buttonBox->addButton(
        UiText::isChineseUi() ? QStringLiteral("还原备份") : QStringLiteral("Restore Backup"),
        QDialogButtonBox::ActionRole
    );
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(restoreButton, &QPushButton::clicked, &dialog, [backupPath, inputPath, isTrack, title, this]() {
        releasePreviewMediaForFileOperation();
        QString error;
        if (!restoreFileFromBackup(backupPath, inputPath, &error)) {
            QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
            reloadPreviewMediaAfterFileOperation(isTrack);
            return;
        }
        QMessageBox::information(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi() ? QStringLiteral("已还原备份。") : QStringLiteral("Backup restored.")
        );
        reloadPreviewMediaAfterFileOperation(isTrack);
    });
    layout->addWidget(buttonBox, 0, Qt::AlignRight);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const double silenceSeconds = beatsSpin->value() * 60.0 / bpmSpin->value();
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。")
                : QStringLiteral("ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH.")
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    const QWidget* parent = UiDialogs::effectiveParentWidget(&owner_);
    if (isTrack && !prependTrackSilence(ffmpegPath, trackPath, silenceSeconds, const_cast<QWidget*>(parent), &error)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            UiText::isChineseUi() ? QStringLiteral("track.mp3 处理失败") : QStringLiteral("track.mp3 Failed"),
            error
        );
        reloadPreviewMediaAfterFileOperation(isTrack);
        return;
    }
    if (!isTrack && !prependPvBlack(ffmpegPath, videoPath, silenceSeconds, const_cast<QWidget*>(parent), &error)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            UiText::isChineseUi() ? QStringLiteral("视频处理失败") : QStringLiteral("Video Failed"),
            error
        );
        reloadPreviewMediaAfterFileOperation(isTrack);
        return;
    }

    reloadPreviewMediaAfterFileOperation(isTrack);
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已为 %1 开头添加 %2 秒空白。").arg(inputName).arg(silenceSeconds, 0, 'f', 3)
            : QStringLiteral("Prepended %1 seconds of blank media to %2.").arg(silenceSeconds, 0, 'f', 3).arg(inputName),
        6000
    );
    showMediaOperationCompleteDialog(
        title,
        UiText::isChineseUi()
            ? QStringLiteral("已为 %1 开头添加 %2 秒%3（原文件已备份为 %4）。")
                  .arg(inputName)
                  .arg(silenceSeconds, 0, 'f', 3)
                  .arg(isTrack ? QStringLiteral("空白") : QStringLiteral("黑幕"))
                  .arg(backupName)
            : QStringLiteral("Prepended %1 s of %2 to %3 (original backed up as %4).")
                  .arg(silenceSeconds, 0, 'f', 3)
                  .arg(isTrack ? QStringLiteral("silence") : QStringLiteral("black screen"))
                  .arg(inputName, backupName),
        inputPath
    );
}

void MainWindow::DialogsSection::openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title)
{
    if (!includeAudioSettings && !includeVideoSettings) {
        return;
    }
    owner_.previewAudioSettings_.normalize();

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    // Was 520 → 600 → 720 → 880. The QFormLayout's label column eats ~180px before
    // the videoCheckRow grid even sees the field, and each right-half checkbox cell
    // must fit an 8-char CJK label ("显示左下角时间戳" / "暂停时显示判定区") plus the
    // checkbox indicator + spacing. 720 still clipped the trailing glyph at some
    // fonts/DPI scales; 880 leaves comfortable margin for both checkboxes per row.
    // The video-settings variant carries the wider gameplay/video controls and
    // two CJK checkbox columns ("显示左下角时间戳" / "暂停时显示判定区") that clip
    // at narrower widths, so it gets extra width; the audio-only variant keeps
    // the 880 baseline.
    // NOTE: rootLayout uses QLayout::SetFixedSize (below), which locks the dialog
    // to its content sizeHint and IGNORES this dialog-level minimum width — that
    // is exactly why bumping this value had no visible effect. The video width is
    // now driven by the tab widget's minimumWidth instead (SetFixedSize honors a
    // *child* widget's minimum via QWidgetItem). This call stays only as a soft
    // floor for the audio-only variant.
    dialog.setMinimumWidth(880);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    const auto createDialogMenuButton = [](QWidget* parent, const QString& text) {
        auto* button = new QToolButton(parent);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        button->setText(text);
        return button;
    };
    const auto flowSpeedValueLabel = [](double flowSpeed) {
        const double snapped = qRound(flowSpeed * 4.0) / 4.0;
        const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
        const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
        return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
    };
    const auto addDialogMenuChoice = [](QMenu* menu, const QString& text, const std::function<void()>& onTriggered, bool italic = false) {
        auto* action = new QWidgetAction(menu);
        auto* button = new QToolButton(menu);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        QFont buttonFont = button->font();
        buttonFont.setItalic(italic);
        button->setFont(buttonFont);
        button->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        button->setStyleSheet(
            QStringLiteral(
                "QToolButton {"
                " color: %1;"
                " background: transparent;"
                " border: none;"
                " padding: 6px 20px 6px 12px;"
                " text-align: left;"
                "}"
                "QToolButton:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(button, &QToolButton::clicked, menu, [action, menu, onTriggered]() {
            if (onTriggered) {
                onTriggered();
            }
            action->trigger();
            menu->close();
        });
        action->setDefaultWidget(button);
        menu->addAction(action);
    };

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* audioGroup = new QGroupBox(uiText("dialog.render_settings.audio_group", "Audio"), &dialog);
    auto* audioFormLayout = new QFormLayout(audioGroup);
    audioFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    audioFormLayout->setHorizontalSpacing(10);
    audioFormLayout->setVerticalSpacing(8);

    const auto addAudioRow = [&](const QString& labelText,
                                 int valuePercent,
                                 QSlider** sliderOut,
                                 QLabel** labelOut,
                                 QToolButton** muteButtonOut = nullptr,
                                 int maximumPercent = 100) {
        auto* row = new QWidget(audioGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, maximumPercent);
        slider->setValue(valuePercent);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new QLabel(QString::number(valuePercent) + "%", row);
        label->setMinimumWidth(44);
        QToolButton* muteButton = nullptr;
        if (muteButtonOut != nullptr) {
            muteButton = new QToolButton(row);
            muteButton->setCursor(Qt::PointingHandCursor);
            muteButton->setAutoRaise(true);
            muteButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            muteButton->setFixedSize(18, 18);
            muteButton->setIconSize(QSize(14, 14));
        }
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        if (muteButton != nullptr) {
            rowLayout->addWidget(muteButton, 0);
        }
        audioFormLayout->addRow(labelText, row);
        *sliderOut = slider;
        *labelOut = label;
        if (muteButtonOut != nullptr) {
            *muteButtonOut = muteButton;
        }
    };

    const QString masterAudioLabelText = uiText("dialog.render_settings.audio.global", "Global Volume");
    QSlider* masterSlider = nullptr;
    QLabel* masterLabel = nullptr;
    QToolButton* masterMuteButton = nullptr;
    addAudioRow(
        masterAudioLabelText,
        owner_.previewAudioSettings_.globalPercent(),
        &masterSlider,
        &masterLabel,
        &masterMuteButton,
        100
    );
    const QString bgmAudioLabelText = uiText("dialog.render_settings.audio.track", "Track Volume");
    QSlider* bgmSlider = nullptr;
    QLabel* bgmLabel = nullptr;
    QToolButton* bgmMuteButton = nullptr;
    addAudioRow(bgmAudioLabelText, owner_.previewAudioSettings_.trackPercent(), &bgmSlider, &bgmLabel, &bgmMuteButton);
    const QString answerAudioLabelText = uiText("dialog.render_settings.audio.answer", "Answer Volume");
    QSlider* answerSlider = nullptr;
    QLabel* answerLabel = nullptr;
    QToolButton* answerMuteButton = nullptr;
    addAudioRow(
        answerAudioLabelText,
        owner_.previewAudioSettings_.answerPercent(),
        &answerSlider,
        &answerLabel,
        &answerMuteButton
    );
    const QString judgeAudioLabelText = uiText("dialog.render_settings.audio.tap", "Tap Volume");
    QSlider* judgeSlider = nullptr;
    QLabel* judgeLabel = nullptr;
    QToolButton* judgeMuteButton = nullptr;
    addAudioRow(judgeAudioLabelText, owner_.previewAudioSettings_.tapPercent(), &judgeSlider, &judgeLabel, &judgeMuteButton);
    const QString exAudioLabelText = uiText("dialog.render_settings.audio.ex", "EX Volume");
    QSlider* exSlider = nullptr;
    QLabel* exLabel = nullptr;
    QToolButton* exMuteButton = nullptr;
    addAudioRow(exAudioLabelText, owner_.previewAudioSettings_.exPercent(), &exSlider, &exLabel, &exMuteButton);
    const QString breakAudioLabelText = uiText("dialog.render_settings.audio.break", "Break Volume");
    QSlider* breakSlider = nullptr;
    QLabel* breakLabel = nullptr;
    QToolButton* breakMuteButton = nullptr;
    addAudioRow(breakAudioLabelText, owner_.previewAudioSettings_.breakPercent(), &breakSlider, &breakLabel, &breakMuteButton);
    const QString breakSlideAudioLabelText = uiText("dialog.render_settings.audio.break_slide", "Break Slide Volume");
    QSlider* breakSlideSlider = nullptr;
    QLabel* breakSlideLabel = nullptr;
    QToolButton* breakSlideMuteButton = nullptr;
    addAudioRow(
        breakSlideAudioLabelText,
        owner_.previewAudioSettings_.breakSlidePercent(),
        &breakSlideSlider,
        &breakSlideLabel,
        &breakSlideMuteButton
    );
    const QString slideAudioLabelText = uiText("dialog.render_settings.audio.slide", "Slide Volume");
    QSlider* slideSlider = nullptr;
    QLabel* slideLabel = nullptr;
    QToolButton* slideMuteButton = nullptr;
    addAudioRow(slideAudioLabelText, owner_.previewAudioSettings_.slidePercent(), &slideSlider, &slideLabel, &slideMuteButton);
    auto* breakSlideTailCheerCheck = new QCheckBox(
        uiText("dialog.render_settings.audio.break_slide_tail_cheer_mute", "Disable breakslide tail cheer"),
        audioGroup);
    breakSlideTailCheerCheck->setChecked(owner_.previewAudioSettings_.breakSlideTailCheerMuted);
    const QString touchAudioLabelText = uiText("dialog.render_settings.audio.touch", "Touch Volume");
    QSlider* touchSlider = nullptr;
    QLabel* touchLabel = nullptr;
    QToolButton* touchMuteButton = nullptr;
    addAudioRow(touchAudioLabelText, owner_.previewAudioSettings_.touchPercent(), &touchSlider, &touchLabel, &touchMuteButton);
    const QString fireworkAudioLabelText = uiText("dialog.render_settings.audio.firework", "Firework Volume");
    QSlider* fireworkSlider = nullptr;
    QLabel* fireworkLabel = nullptr;
    QToolButton* fireworkMuteButton = nullptr;
    addAudioRow(
        fireworkAudioLabelText,
        owner_.previewAudioSettings_.fireworkPercent(),
        &fireworkSlider,
        &fireworkLabel,
        &fireworkMuteButton
    );
    audioFormLayout->addRow(QString(), breakSlideTailCheerCheck);

    const auto addVideoSliderRow = [](
        QWidget* parent,
        int minimum,
        int maximum,
        int step,
        int value,
        const QString& suffix,
        QSlider** sliderOut,
        QLabel** labelOut
    ) {
        auto* row = new QWidget(parent);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setTickInterval(step);
        slider->setValue(value);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new QLabel(QString::number(value) + suffix, row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        *sliderOut = slider;
        *labelOut = label;
        return row;
    };

    auto* videoGroup = new QGroupBox(uiText("dialog.render_settings.video_group", "Video"), &dialog);
    auto* videoFormLayout = new QFormLayout(videoGroup);
    videoFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    videoFormLayout->setHorizontalSpacing(10);
    videoFormLayout->setVerticalSpacing(8);
    auto* gameplayGroup = new QGroupBox(uiText("dialog.render_settings.gameplay_group", "Gameplay"), &dialog);
    auto* gameplayLayout = new QGridLayout(gameplayGroup);
    gameplayLayout->setContentsMargins(10, 8, 10, 8);
    gameplayLayout->setHorizontalSpacing(10);
    gameplayLayout->setVerticalSpacing(8);
    gameplayLayout->setColumnStretch(0, 1);
    gameplayLayout->setColumnStretch(1, 1);

    QSlider* outerBrightnessSlider = nullptr;
    QLabel* outerBrightnessLabel = nullptr;
    QWidget* outerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(owner_.previewBackgroundBrightnessOuter_ * 100.0),
        QStringLiteral("%"),
        &outerBrightnessSlider,
        &outerBrightnessLabel
    );
    QSlider* innerBrightnessSlider = nullptr;
    QLabel* innerBrightnessLabel = nullptr;
    QWidget* innerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(owner_.previewBackgroundBrightnessInner_ * 100.0),
        QStringLiteral("%"),
        &innerBrightnessSlider,
        &innerBrightnessLabel
    );
    QSlider* layoutSquareScaleSlider = nullptr;
    QLabel* layoutSquareScaleLabel = nullptr;
    QWidget* layoutSquareScaleRow = addVideoSliderRow(
        videoGroup,
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(owner_.previewLayoutSquareScale_ * 100.0),
        QStringLiteral("%"),
        &layoutSquareScaleSlider,
        &layoutSquareScaleLabel
    );
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    const auto snapFlowSpeed = [flowSpeedMin, flowSpeedMax, flowSpeedStep](double flowSpeed) {
        return qBound(
            flowSpeedMin,
            flowSpeedMin + qRound((flowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
            flowSpeedMax
        );
    };
    double selectedTapFlowSpeed = snapFlowSpeed(owner_.previewTapFlowSpeed_);
    double selectedTouchFlowSpeed = snapFlowSpeed(owner_.previewTouchFlowSpeed_);
    const auto createFlowSpeedEdit = [&](double& selectedFlowSpeed, const std::function<void(double)>& applyFlowSpeed) {
        auto* flowSpeedEdit = new QLineEdit(gameplayGroup);
        flowSpeedEdit->setAlignment(Qt::AlignCenter);
        flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
        flowSpeedEdit->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
        flowSpeedEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* flowSpeedValidator = new QDoubleValidator(flowSpeedMin, flowSpeedMax, 2, flowSpeedEdit);
        flowSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
        flowSpeedEdit->setValidator(flowSpeedValidator);
        QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, &dialog, [&, flowSpeedEdit, applyFlowSpeed]() {
            bool ok = false;
            const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
            if (!ok) {
                flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
                return;
            }
            selectedFlowSpeed = snapFlowSpeed(typedSpeed);
            flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
            applyFlowSpeed(selectedFlowSpeed);
            owner_.savePortableState();
        });
        return flowSpeedEdit;
    };
    auto* tapFlowSpeedEdit = createFlowSpeedEdit(selectedTapFlowSpeed, [this](double flowSpeed) {
        owner_.previewTapFlowSpeed_ = flowSpeed;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setTapFlowSpeed(flowSpeed);
        }
    });
    auto* touchFlowSpeedEdit = createFlowSpeedEdit(selectedTouchFlowSpeed, [this](double flowSpeed) {
        owner_.previewTouchFlowSpeed_ = flowSpeed;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setTouchFlowSpeed(flowSpeed);
        }
    });

    struct CanvasFrameRateOption {
        PreviewCanvasFrameRateMode mode;
        QString label;
    };
    const double detectedRefreshRate = owner_.currentPreviewCanvasRefreshRate();
    const QString displayRefreshLabel = QStringLiteral("%1 (%2 Hz)")
        .arg(uiText(
            "dialog.render_settings.preview.canvas_frame_rate.display",
            "Display Refresh Rate"
        ))
        .arg(QString::number(detectedRefreshRate, 'f', detectedRefreshRate >= 100.0 ? 0 : 1));
    QList<CanvasFrameRateOption> canvasFrameRateOptions;
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::Fps60,
        uiText("dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"),
    });
    // Only expose the 120 FPS option on a display that can sustain it.
    // The backend clamps Fps120 to display refresh at runtime (see
    // previewCanvasTargetFrameIntervalNs), so leaving it in the menu
    // would advertise a setting that silently degrades to display
    // refresh — confusing the user. Threshold uses an epsilon (119.5)
    // so panels that report 119.88 Hz (common OEM round-down of true
    // 120 Hz) still see the option.
    if (detectedRefreshRate >= 119.5) {
        canvasFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps120,
            uiText("dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"),
        });
    }
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::DisplayRefresh,
        displayRefreshLabel,
    });

    QString selectedCanvasFrameRateLabel = canvasFrameRateOptions.front().label;
    bool foundExactSelectedFrameRate = false;
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        if (option.mode == owner_.previewCanvasFrameRateMode_) {
            selectedCanvasFrameRateLabel = option.label;
            foundExactSelectedFrameRate = true;
            break;
        }
    }
    // Saved mode is no longer offered (user previously had Fps120 on a
    // higher-refresh display, then connected to a lower one). Show the
    // DisplayRefresh label since that's what the backend actually
    // applies at runtime — the saved value itself is preserved so
    // reconnecting to a 120 Hz panel restores the original selection.
    if (!foundExactSelectedFrameRate) {
        for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
            if (option.mode == PreviewCanvasFrameRateMode::DisplayRefresh) {
                selectedCanvasFrameRateLabel = option.label;
                break;
            }
        }
    }
    auto* canvasFrameRateButton = createDialogMenuButton(videoGroup, selectedCanvasFrameRateLabel);
    canvasFrameRateButton->setFixedHeight(tapFlowSpeedEdit->sizeHint().height());
    canvasFrameRateButton->setStyleSheet(
        UiTheme::dialogMenuButtonStyleSheet()
        + QStringLiteral("QToolButton { text-align: center; padding: 2px 22px 2px 10px; }")
    );
    auto* canvasFrameRateMenu = new QMenu(canvasFrameRateButton);
    styleRoundedMenu(*canvasFrameRateMenu);
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        const PreviewCanvasFrameRateMode mode = option.mode;
        const QString label = option.label;
        addDialogMenuChoice(canvasFrameRateMenu, label, [this, canvasFrameRateButton, mode, label]() {
            canvasFrameRateButton->setText(label);
            owner_.setPreviewCanvasFrameRateMode(mode, true);
        });
    }
    canvasFrameRateButton->setMenu(canvasFrameRateMenu);

    const QString scaleFillLabel = uiText("dialog.render_settings.video.scale.fill", "Fill (crop if needed)");
    const QString scaleFitLabel = uiText("dialog.render_settings.video.scale.fit", "Fit (keep full image, may letterbox)");
    const QString scaleSquareFitLabel = uiText(
        "dialog.render_settings.video.scale.square_fit",
        "1:1 Fit (center square)");
    const QString importSkinLabel = uiText("dialog.render_settings.video.skin.import", "Import...");
    const QString slideStackOrderDxLabel = uiText(
        "dialog.render_settings.gameplay.slide_stack_order.dx_style",
        "DX Style"
    );
    const QString slideStackOrderFinaleLabel = uiText(
        "dialog.render_settings.gameplay.slide_stack_order.finale_style",
        "FiNALE Style"
    );
    const auto slideStackOrderLabelForValue = [slideStackOrderDxLabel, slideStackOrderFinaleLabel](bool earlierOnTop) {
        return earlierOnTop ? slideStackOrderDxLabel : slideStackOrderFinaleLabel;
    };
    const QString currentSkinButtonLabel = owner_.previewSkinDisplayName(owner_.previewSkinDirectoryName_);
    auto* skinButton = createDialogMenuButton(
        gameplayGroup,
        currentSkinButtonLabel
    );
    skinButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* skinMenu = new QMenu(skinButton);
    styleRoundedMenu(*skinMenu);
    for (const QString& skinDirectoryName : owner_.availablePreviewSkinDirectoryNames()) {
        const QString skinLabel = owner_.previewSkinDisplayName(skinDirectoryName);
        addDialogMenuChoice(skinMenu, skinLabel, [this, skinButton, skinDirectoryName, skinLabel]() {
            owner_.previewSkinDirectoryName_ = skinDirectoryName;
            owner_.previewSkinVariant_ =
                skinDirectoryName.compare(QStringLiteral("skinDX"), Qt::CaseInsensitive) == 0
                    ? PreviewSkinVariant::Dx
                    : PreviewSkinVariant::Standard;
            skinButton->setText(skinLabel);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setSkinDirectory(owner_.resolvePreviewSkinDir());
            }
            owner_.savePortableState();
        });
    }
    if (!skinMenu->actions().isEmpty()) {
        skinMenu->addSeparator();
    }
    addDialogMenuChoice(skinMenu, importSkinLabel, [this]() {
        const QString skinRoot = owner_.resolvePreviewSkinRootDir();
        if (!skinRoot.isEmpty()) {
            QDir().mkpath(skinRoot);
            QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
        }
    });
    skinButton->setMenu(skinMenu);
    const QString disabledLabel = uiText("dialog.render_settings.option.disabled", "Disabled");
    const QString slideJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.slide", "slide");
    const QString tapJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.tap", "tap");
    const QString touchJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.touch", "touch");
    const auto judgeEffectButtonLabel = [
        this,
        slideJudgeChoiceLabel,
        tapJudgeChoiceLabel,
        touchJudgeChoiceLabel,
        disabledLabel
    ]() {
        QStringList parts;
        if (owner_.muriRenderOptions_.showChartReviewSlideJudgeOverlay) {
            parts.append(slideJudgeChoiceLabel);
        }
        if (owner_.muriRenderOptions_.showChartReviewTapJudgeOverlay) {
            parts.append(tapJudgeChoiceLabel);
        }
        if (owner_.muriRenderOptions_.showChartReviewTouchJudgeOverlay) {
            parts.append(touchJudgeChoiceLabel);
        }
        return parts.isEmpty() ? disabledLabel : parts.join(QStringLiteral(", "));
    };
    auto* judgeEffectButton = createDialogMenuButton(gameplayGroup, judgeEffectButtonLabel());
    judgeEffectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeEffectMenu = new QMenu(judgeEffectButton);
    styleRoundedMenu(*judgeEffectMenu);
    // QCheckBox-in-QWidgetAction so the menu stays open during multi-selection (regular QActions auto-close).
    // We hide the native indicator and prefix the label with √/× — visually matches the user's request to
    // surface state via a tick / cross rather than a checkbox glyph, while preserving QCheckBox's click
    // semantics (toggled signal, keyboard focus, accessibility role).
    const auto judgeEffectChoiceText = [](const QString& label, bool enabled) {
        return QStringLiteral("%1  %2")
            .arg(enabled ? QStringLiteral("√") : QStringLiteral("×"), label);
    };
    const auto addJudgeEffectChoice = [
        this,
        &dialog,
        judgeEffectButton,
        judgeEffectButtonLabel,
        judgeEffectMenu,
        judgeEffectChoiceText
    ](const QString& label, bool MuriRenderOptions::*memberPtr) {
        auto* action = new QWidgetAction(judgeEffectMenu);
        const bool initialChecked = owner_.muriRenderOptions_.*memberPtr;
        auto* checkbox = new QCheckBox(judgeEffectChoiceText(label, initialChecked), judgeEffectMenu);
        checkbox->setChecked(initialChecked);
        checkbox->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        checkbox->setStyleSheet(
            QStringLiteral(
                "QCheckBox {"
                " color: %1;"
                " background: transparent;"
                " padding: 6px 20px 6px 12px;"
                " spacing: 0px;"
                "}"
                "QCheckBox::indicator {"
                " width: 0px;"
                " height: 0px;"
                " margin: 0px;"
                " padding: 0px;"
                "}"
                "QCheckBox:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(
            checkbox,
            &QCheckBox::toggled,
            &dialog,
            [this, judgeEffectButton, judgeEffectButtonLabel, judgeEffectChoiceText,
             checkbox, label, memberPtr](bool checked) {
                if (owner_.muriRenderOptions_.*memberPtr == checked) {
                    return;
                }
                owner_.muriRenderOptions_.*memberPtr = checked;
                checkbox->setText(judgeEffectChoiceText(label, checked));
                judgeEffectButton->setText(judgeEffectButtonLabel());
                owner_.applyMuriRenderOptions();
                owner_.savePortableState();
            }
        );
        action->setDefaultWidget(checkbox);
        judgeEffectMenu->addAction(action);
    };
    addJudgeEffectChoice(slideJudgeChoiceLabel, &MuriRenderOptions::showChartReviewSlideJudgeOverlay);
    addJudgeEffectChoice(tapJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTapJudgeOverlay);
    addJudgeEffectChoice(touchJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTouchJudgeOverlay);
    judgeEffectButton->setMenu(judgeEffectMenu);
    const QString judgeLinePointLabel = uiText("dialog.render_settings.gameplay.judge_line.point", "Point");
    const QString judgeLineLineLabel = uiText("dialog.render_settings.gameplay.judge_line.line", "Line");
    const QString judgeLineAreaLabel = uiText("dialog.render_settings.gameplay.judge_line.area", "Judge Area");
    const QString judgeLineAreaLabeledLabel = uiText(
        "dialog.render_settings.gameplay.judge_line.area_labeled",
        "Judge Area (Labeled)"
    );
    const QString judgeLineImportLabel = uiText("dialog.render_settings.gameplay.judge_line.import", "Import...");
    const auto judgeLineLabelForVariant = [
        judgeLinePointLabel,
        judgeLineLineLabel,
        judgeLineAreaLabel,
        judgeLineAreaLabeledLabel
    ](PreviewOutlineVariant variant) {
        switch (variant) {
        case PreviewOutlineVariant::Point:
            return judgeLinePointLabel;
        case PreviewOutlineVariant::JudgeArea:
            return judgeLineAreaLabel;
        case PreviewOutlineVariant::JudgeAreaLabeled:
            return judgeLineAreaLabeledLabel;
        case PreviewOutlineVariant::Line:
        default:
            return judgeLineLineLabel;
        }
    };
    const auto judgeLineButtonLabel = [&]() {
        return owner_.previewCustomOutlineFileName_.isEmpty()
            ? judgeLineLabelForVariant(owner_.previewOutlineVariant_)
            : owner_.previewCustomOutlineFileName_;
    };
    auto* judgeLineButton = createDialogMenuButton(gameplayGroup, judgeLineButtonLabel());
    judgeLineButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeLineMenu = new QMenu(judgeLineButton);
    styleRoundedMenu(*judgeLineMenu);
    addDialogMenuChoice(judgeLineMenu, judgeLinePointLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::Point, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(judgeLineMenu, judgeLineLineLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::Line, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(judgeLineMenu, judgeLineAreaLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::JudgeArea, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(
        judgeLineMenu,
        judgeLineAreaLabeledLabel,
        [this, judgeLineButton, judgeLineLabelForVariant]() {
            owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::JudgeAreaLabeled, false, true);
            judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
        }
    );
    const QStringList customOutlineNames = owner_.availablePreviewCustomOutlineFileNames();
    if (!customOutlineNames.isEmpty()) {
        judgeLineMenu->addSeparator();
    }
    for (const QString& fileName : customOutlineNames) {
        addDialogMenuChoice(judgeLineMenu, fileName, [this, judgeLineButton, fileName]() {
            owner_.applyPreviewCustomOutlineFileName(fileName, true);
            judgeLineButton->setText(fileName);
        });
    }
    judgeLineMenu->addSeparator();
    addDialogMenuChoice(judgeLineMenu, judgeLineImportLabel, [this]() {
        const QString outlineDir = owner_.resolvePreviewCustomOutlineDir();
        if (!outlineDir.isEmpty()) {
            QDir().mkpath(outlineDir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
        }
    });
    judgeLineButton->setMenu(judgeLineMenu);
    auto* forceLabeledJudgeLineWhenPausedCheck = new QCheckBox(
        uiText(
            "dialog.render_settings.gameplay.force_labeled_judge_line_when_paused",
            "Show judge area while preview is paused"
        ),
        videoGroup
    );
    forceLabeledJudgeLineWhenPausedCheck->setChecked(owner_.previewForceLabeledJudgeLineWhenPaused_);
    auto* slideStackOrderButton = createDialogMenuButton(
        gameplayGroup,
        slideStackOrderLabelForValue(owner_.previewSlideEarlierSecondAndTextOnTop_)
    );
    slideStackOrderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* slideStackOrderMenu = new QMenu(slideStackOrderButton);
    styleRoundedMenu(*slideStackOrderMenu);
    const auto setSlideStackOrder = [
        this,
        slideStackOrderButton,
        slideStackOrderLabelForValue
    ](bool earlierOnTop) {
        if (owner_.previewSlideEarlierSecondAndTextOnTop_ == earlierOnTop) {
            slideStackOrderButton->setText(slideStackOrderLabelForValue(earlierOnTop));
            return;
        }
        owner_.previewSlideEarlierSecondAndTextOnTop_ = earlierOnTop;
        slideStackOrderButton->setText(slideStackOrderLabelForValue(earlierOnTop));
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(earlierOnTop);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderDxLabel, [setSlideStackOrder]() {
        setSlideStackOrder(true);
    });
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderFinaleLabel, [setSlideStackOrder]() {
        setSlideStackOrder(false);
    });
    slideStackOrderButton->setMenu(slideStackOrderMenu);
    PreviewBackgroundScaleMode selectedScaleMode = owner_.previewBackgroundScaleMode_;
    const auto scaleModeLabelFor = [&](PreviewBackgroundScaleMode mode) {
        switch (mode) {
        case PreviewBackgroundScaleMode::FitContain:
            return scaleFitLabel;
        case PreviewBackgroundScaleMode::SquareFitContain:
            return scaleSquareFitLabel;
        case PreviewBackgroundScaleMode::FillCrop:
        default:
            return scaleFillLabel;
        }
    };
    auto* scaleModeButton = createDialogMenuButton(
        videoGroup,
        scaleModeLabelFor(selectedScaleMode)
    );
    auto* scaleModeMenu = new QMenu(scaleModeButton);
    styleRoundedMenu(*scaleModeMenu);
    const auto setScaleMode = [&](PreviewBackgroundScaleMode mode) {
        selectedScaleMode = mode;
        scaleModeButton->setText(scaleModeLabelFor(selectedScaleMode));
        owner_.previewBackgroundScaleMode_ = selectedScaleMode;
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(scaleModeMenu, scaleFillLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::FillCrop);
    });
    addDialogMenuChoice(scaleModeMenu, scaleFitLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::FitContain);
    });
    addDialogMenuChoice(scaleModeMenu, scaleSquareFitLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::SquareFitContain);
    });
    scaleModeButton->setMenu(scaleModeMenu);

    auto* smoothBrightnessCheck = new QCheckBox(
        uiText("dialog.render_settings.video.smooth_brightness", "Smooth brightness"),
        videoGroup
    );
    smoothBrightnessCheck->setChecked(owner_.previewSmoothBrightness_);
    auto* timestampCheck = new QCheckBox(
        uiText("dialog.video_export.option.show_timestamp", "Show bottom-left timestamp"),
        videoGroup
    );
    timestampCheck->setChecked(owner_.previewShowTimestamp_);
    auto* debugCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.debug", "Show preview debug info"),
        videoGroup
    );
    debugCheck->setChecked(owner_.previewShowDebugInfo_);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_outer", "Outer Brightness"), outerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_inner", "Inner Brightness"), innerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.layout_square_scale", "Stage Display Scale"), layoutSquareScaleRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.scale_mode", "Background / PV Scale Mode"), scaleModeButton);
    videoFormLayout->addRow(
        uiText("dialog.render_settings.preview.canvas_frame_rate", "Preview Refresh Rate"),
        canvasFrameRateButton
    );
    auto* videoCheckRow = new QWidget(videoGroup);
    auto* videoCheckLayout = new QGridLayout(videoCheckRow);
    videoCheckLayout->setContentsMargins(0, 0, 0, 0);
    videoCheckLayout->setHorizontalSpacing(6);
    videoCheckLayout->setVerticalSpacing(6);
    videoCheckLayout->setColumnStretch(0, 1);
    videoCheckLayout->setColumnStretch(1, 1);
    videoCheckLayout->addWidget(smoothBrightnessCheck, 0, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(timestampCheck, 0, 1, Qt::AlignLeft);
    videoCheckLayout->addWidget(debugCheck, 1, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(forceLabeledJudgeLineWhenPausedCheck, 1, 1, Qt::AlignLeft);
    videoFormLayout->addRow(QString(), videoCheckRow);

    const auto addGameplayField = [gameplayGroup, gameplayLayout](int row, int column, const QString& labelText, QWidget* control) {
        auto* field = new QWidget(gameplayGroup);
        auto* fieldLayout = new QVBoxLayout(field);
        // 1px bottom margin so the rounded control underneath isn't flush
        // against the field's edge — otherwise its bottom border is clipped
        // (visible as the "missing bottom edge" on every box).
        fieldLayout->setContentsMargins(0, 0, 0, 1);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        // The QSS border-radius controls report a sizeHint a hair short of what
        // the bottom border needs; pad the height by 2px so it renders fully.
        control->setMinimumHeight(qMax(control->minimumHeight(), control->sizeHint().height() + 2));
        fieldLayout->addWidget(control, 0);
        gameplayLayout->addWidget(field, row, column);
    };
    addGameplayField(
        0,
        0,
        uiText("dialog.render_settings.video.tap_flow_speed", "Tap Flow Speed"),
        tapFlowSpeedEdit
    );
    addGameplayField(
        0,
        1,
        uiText("dialog.render_settings.video.touch_flow_speed", "Touch Flow Speed"),
        touchFlowSpeedEdit
    );
    addGameplayField(
        1,
        0,
        uiText("dialog.render_settings.video.skin", "Skin"),
        skinButton
    );
    addGameplayField(
        1,
        1,
        uiText("dialog.render_settings.gameplay.judge_line", "Judge Line"),
        judgeLineButton
    );
    addGameplayField(
        2,
        0,
        uiText("dialog.render_settings.gameplay.judge_effect", "Judge Effect Display"),
        judgeEffectButton
    );
    addGameplayField(
        2,
        1,
        uiText("dialog.render_settings.gameplay.slide_stack_order", "Slide Stack Order"),
        slideStackOrderButton
    );
    const auto centerDisplayLabelForMode = [](miacode::preview_gameplay::CenterDisplayMode mode) -> QString {
        switch (mode) {
        case miacode::preview_gameplay::CenterDisplayMode::Off:
            return uiText("dialog.render_settings.gameplay.center_display.off", "Off");
        case miacode::preview_gameplay::CenterDisplayMode::Combo:
            return uiText("dialog.render_settings.gameplay.center_display.combo", "Combo");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_dx_plus", "ACHIEVEMENT DX (+)");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_dx_minus_100", "ACHIEVEMENT DX (100-)");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_dx_minus_101", "ACHIEVEMENT DX (101-)");
        case miacode::preview_gameplay::CenterDisplayMode::DxScorePlus:
            return uiText("dialog.render_settings.gameplay.center_display.dx_score_plus", "DX SCORE (+)");
        case miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus:
            return uiText("dialog.render_settings.gameplay.center_display.dx_score_minus", "DX SCORE (-)");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_finale_plus", "ACHIEVEMENT FINALE (+)");
        }
        return uiText("dialog.render_settings.gameplay.center_display.off", "Off");
    };
    auto* centerDisplayButton = createDialogMenuButton(
        gameplayGroup,
        centerDisplayLabelForMode(owner_.previewCenterDisplayMode_)
    );
    centerDisplayButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* centerDisplayMenu = new QMenu(centerDisplayButton);
    styleRoundedMenu(*centerDisplayMenu);
    const auto setCenterDisplay = [this, centerDisplayButton, centerDisplayLabelForMode](
        miacode::preview_gameplay::CenterDisplayMode mode
    ) {
        if (owner_.previewCenterDisplayMode_ == mode) {
            centerDisplayButton->setText(centerDisplayLabelForMode(mode));
            return;
        }
        owner_.previewCenterDisplayMode_ = mode;
        centerDisplayButton->setText(centerDisplayLabelForMode(mode));
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setCenterDisplayMode(mode);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::Off), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::Off);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::Combo), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::Combo);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::DxScorePlus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::DxScorePlus);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus);
    });
    centerDisplayButton->setMenu(centerDisplayMenu);
    addGameplayField(
        3,
        0,
        uiText("dialog.render_settings.gameplay.center_display", "Center Display"),
        centerDisplayButton
    );
    audioGroup->setVisible(includeAudioSettings);
    videoGroup->setVisible(includeVideoSettings);
    gameplayGroup->setVisible(includeVideoSettings);

    if (includeAudioSettings) {
        rootLayout->addWidget(audioGroup, 0);
    }
    if (includeVideoSettings) {
        // Surface the 视频 / 游戏 split as tab pages instead of two
        // stacked QGroupBoxes — matches the Preferences dialog pattern.
        // The groupboxes keep their layouts + children intact; we only
        // strip their title bar and frame chrome so they read as plain
        // tab contents (the tab strip already shows the section name).
        const auto flattenGroup = [](QGroupBox* group) {
            if (group == nullptr) {
                return;
            }
            group->setTitle(QString());
            group->setFlat(true);
            // Belt + braces: setFlat() leaves a 1px top line on most
            // styles; clearing the border via stylesheet kills that
            // residual line so the tab pane border is the only frame
            // the user sees.
            group->setStyleSheet(QStringLiteral(
                "QGroupBox { border: none; margin-top: 0; padding-top: 0; }"
            ));
        };
        flattenGroup(videoGroup);
        flattenGroup(gameplayGroup);
        auto* videoSettingsTabs = new QTabWidget(&dialog);
        videoSettingsTabs->setObjectName(QStringLiteral("RenderSettingsTabs"));
        // Drive the dialog width from HERE, not from dialog.setMinimumWidth():
        // the rootLayout's SetFixedSize constraint sizes the dialog to its
        // content sizeHint and ignores the dialog's own minimum width, but it
        // DOES honor a child widget's minimum width (QWidgetItem expands the
        // hint up to the child minimum). Setting the tab's minimum width
        // reliably widens the dialog so the two CJK checkbox columns
        // ("显示左下角时间戳" / "暂停时显示判定区") stop clipping. The video form
        // uses ExpandingFieldsGrow, so the extra width flows into those cells.
        videoSettingsTabs->setMinimumWidth(420);
        // documentMode left at the default so the QTabWidget::pane
        // stylesheet (rounded bottom corners + no top border) actually
        // takes effect; documentMode=true would skip drawing the pane
        // frame and re-expose the global hair line above the tab strip.
        videoSettingsTabs->addTab(videoGroup,
            uiText("dialog.render_settings.video_group", "Video"));
        videoSettingsTabs->addTab(gameplayGroup,
            uiText("dialog.render_settings.gameplay_group", "Gameplay"));
        rootLayout->addWidget(videoSettingsTabs, 0);
    }
    auto* buttonBox = new QDialogButtonBox(&dialog);
    QPushButton* saveLocalAudioPresetButton = nullptr;
    QPushButton* applyLocalAudioPresetButton = nullptr;
    if (includeAudioSettings) {
        saveLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.set_software_default_audio", "Save Local Preset"),
            QDialogButtonBox::ActionRole
        );
        applyLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.restore_project_default", "Apply Local Preset"),
            QDialogButtonBox::ActionRole
        );
        if (saveLocalAudioPresetButton != nullptr) {
            saveLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
        if (applyLocalAudioPresetButton != nullptr) {
            applyLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
    }
    if (QPushButton* closeButton = buttonBox->addButton(uiText("dialog.render_settings.button.close", "Close"), QDialogButtonBox::RejectRole)) {
        closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    auto* audioApplyTimer = new QTimer(&dialog);
    audioApplyTimer->setSingleShot(true);
    audioApplyTimer->setInterval(220);
    QString pendingAudition;
    auto* dialogAuditionRuntime = new QtPreviewSfxRuntime(&dialog);
    QString dialogAuditionSfxDir;

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    const auto playDialogLocalSfxAudition = [
        this,
        dialogAuditionRuntime,
        &dialogAuditionSfxDir
    ](const QString& audition) {
        if (audition.isEmpty()) {
            return false;
        }
        // Don't audition over a running preview: the user already hears the
        // real SFX from live playback, so a second (dialog-local) runtime
        // playing the same sample just doubles/garbles it. Audition is only
        // meaningful when the preview is idle.
        if (state_.qtPreviewPlaying_) {
            return false;
        }
        QString resolvedKind = previewSfxNormalizedKind(audition);
        if (resolvedKind == QStringLiteral("break_slide")) {
            resolvedKind = QStringLiteral("break_slide_start");
        }
        const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
        if (sfxDir.isEmpty()) {
            return false;
        }
        const QString resolvedSfxDir = QDir::cleanPath(sfxDir);
        if (dialogAuditionSfxDir != resolvedSfxDir || !dialogAuditionRuntime->audioEngineInitialized()) {
            dialogAuditionRuntime->setWarmupResolvedPaths(QString(), QString(), resolvedSfxDir);
            dialogAuditionRuntime->reloadAssets(owner_.previewAudioSettings_);
            dialogAuditionSfxDir = resolvedSfxDir;
        } else {
            dialogAuditionRuntime->applyLevels(owner_.previewAudioSettings_);
        }
        if (!dialogAuditionRuntime->audioEngineInitialized()) {
            return false;
        }
        dialogAuditionRuntime->stopAll();
        return dialogAuditionRuntime->audition(resolvedKind);
    };

    const QString muteAudioButtonTooltip = uiText("dialog.render_settings.audio.button.mute", "Mute %1");
    const QString unmuteAudioButtonTooltip = uiText("dialog.render_settings.audio.button.unmute", "Unmute %1");
    const QString muteButtonStyleSheet = QStringLiteral(
        "QToolButton { border: none; background: transparent; padding: 0; margin: 0; }"
        "QToolButton:hover { border: none; background: transparent; }"
        "QToolButton:pressed { border: none; background: transparent; }"
        "QToolButton:disabled { border: none; background: transparent; }"
    );

    const auto makeAudioMuteIcon = [](bool muted) {
        constexpr int kExtent = 16;
        QPixmap pixmap(kExtent, kExtent);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        // Theme-aware foreground: iconPrimary is the brand's "appropriate
        // tint for an icon on the dialog backdrop" — dark on light themes,
        // light on dark themes — so the speaker reads correctly when the
        // user has dark mode enabled. Previously hard-coded #101010 which
        // disappeared into the dark backdrop.
        const QColor iconColor = UiTheme::colors().iconPrimary;
        painter.setBrush(iconColor);
        painter.drawPolygon(QPolygonF{
            QPointF(1.5, 5.5),
            QPointF(4.4, 5.5),
            QPointF(8.0, 2.8),
            QPointF(8.0, 13.2),
            QPointF(4.4, 10.5),
            QPointF(1.5, 10.5),
        });

        QPen pen(iconColor);
        pen.setWidthF(1.35);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        if (muted) {
            painter.drawLine(QPointF(10.1, 5.5), QPointF(13.4, 10.5));
            painter.drawLine(QPointF(13.4, 5.5), QPointF(10.1, 10.5));
        } else {
            QPainterPath waveNear;
            waveNear.moveTo(9.3, 6.2);
            waveNear.quadTo(10.8, 8.0, 9.3, 9.8);
            painter.drawPath(waveNear);

            QPainterPath waveFar;
            waveFar.moveTo(10.8, 4.5);
            waveFar.quadTo(13.9, 8.0, 10.8, 11.5);
            painter.drawPath(waveFar);
        }

        return QIcon(pixmap);
    };

    const auto syncAudioControlsFromCurrentSettings = [
        this,
        masterSlider,
        masterLabel,
        masterMuteButton,
        masterAudioLabelText,
        muteAudioButtonTooltip,
        unmuteAudioButtonTooltip,
        muteButtonStyleSheet,
        makeAudioMuteIcon,
        bgmSlider,
        bgmLabel,
        bgmMuteButton,
        answerSlider,
        answerLabel,
        answerMuteButton,
        judgeSlider,
        judgeLabel,
        judgeMuteButton,
        breakSlider,
        breakLabel,
        breakMuteButton,
        breakSlideSlider,
        breakSlideLabel,
        breakSlideMuteButton,
        slideSlider,
        slideLabel,
        slideMuteButton,
        breakSlideTailCheerCheck,
        exSlider,
        exLabel,
        exMuteButton,
        touchSlider,
        touchLabel,
        touchMuteButton,
        fireworkSlider,
        fireworkLabel,
        fireworkMuteButton,
        bgmAudioLabelText,
        answerAudioLabelText,
        judgeAudioLabelText,
        exAudioLabelText,
        breakAudioLabelText,
        breakSlideAudioLabelText,
        slideAudioLabelText,
        touchAudioLabelText,
        fireworkAudioLabelText
    ]() {
        owner_.previewAudioSettings_.normalize();
        const auto syncAudioRow = [](QSlider* slider, QLabel* label, int valuePercent) {
            if (slider == nullptr || label == nullptr) {
                return;
            }
            const QSignalBlocker blocker(slider);
            slider->setValue(valuePercent);
            label->setText(QString::number(valuePercent) + "%");
        };
        syncAudioRow(masterSlider, masterLabel, owner_.previewAudioSettings_.globalPercent());
        if (masterMuteButton != nullptr) {
            masterMuteButton->setStyleSheet(muteButtonStyleSheet);
            masterMuteButton->setIcon(makeAudioMuteIcon(owner_.previewAudioSettings_.globalMuted()));
            masterMuteButton->setToolTip(
                (owner_.previewAudioSettings_.globalMuted() ? unmuteAudioButtonTooltip : muteAudioButtonTooltip)
                    .arg(masterAudioLabelText)
            );
        }
        const auto syncPerChannelMuteButton = [
            &makeAudioMuteIcon,
            &muteButtonStyleSheet,
            &muteAudioButtonTooltip,
            &unmuteAudioButtonTooltip
        ](QToolButton* button, bool muted, const QString& labelText) {
            if (button == nullptr) {
                return;
            }
            button->setStyleSheet(muteButtonStyleSheet);
            button->setIcon(makeAudioMuteIcon(muted));
            button->setToolTip((muted ? unmuteAudioButtonTooltip : muteAudioButtonTooltip).arg(labelText));
        };
        syncAudioRow(bgmSlider, bgmLabel, owner_.previewAudioSettings_.trackPercent());
        syncAudioRow(answerSlider, answerLabel, owner_.previewAudioSettings_.answerPercent());
        syncAudioRow(judgeSlider, judgeLabel, owner_.previewAudioSettings_.tapPercent());
        syncAudioRow(exSlider, exLabel, owner_.previewAudioSettings_.exPercent());
        syncAudioRow(breakSlider, breakLabel, owner_.previewAudioSettings_.breakPercent());
        syncAudioRow(breakSlideSlider, breakSlideLabel, owner_.previewAudioSettings_.breakSlidePercent());
        syncAudioRow(slideSlider, slideLabel, owner_.previewAudioSettings_.slidePercent());
        if (breakSlideTailCheerCheck != nullptr) {
            const QSignalBlocker blocker(breakSlideTailCheerCheck);
            breakSlideTailCheerCheck->setChecked(owner_.previewAudioSettings_.breakSlideTailCheerMuted);
        }
        syncAudioRow(touchSlider, touchLabel, owner_.previewAudioSettings_.touchPercent());
        syncAudioRow(fireworkSlider, fireworkLabel, owner_.previewAudioSettings_.fireworkPercent());
        syncPerChannelMuteButton(bgmMuteButton, owner_.previewAudioSettings_.trackMuted(), bgmAudioLabelText);
        syncPerChannelMuteButton(answerMuteButton, owner_.previewAudioSettings_.answerMuted(), answerAudioLabelText);
        syncPerChannelMuteButton(judgeMuteButton, owner_.previewAudioSettings_.tapMuted(), judgeAudioLabelText);
        syncPerChannelMuteButton(exMuteButton, owner_.previewAudioSettings_.exMuted(), exAudioLabelText);
        syncPerChannelMuteButton(breakMuteButton, owner_.previewAudioSettings_.breakMuted(), breakAudioLabelText);
        syncPerChannelMuteButton(
            breakSlideMuteButton,
            owner_.previewAudioSettings_.breakSlideMuted(),
            breakSlideAudioLabelText
        );
        syncPerChannelMuteButton(slideMuteButton, owner_.previewAudioSettings_.slideMuted(), slideAudioLabelText);
        syncPerChannelMuteButton(touchMuteButton, owner_.previewAudioSettings_.touchMuted(), touchAudioLabelText);
        syncPerChannelMuteButton(fireworkMuteButton, owner_.previewAudioSettings_.fireworkMuted(), fireworkAudioLabelText);
    };

    const auto commitAudioSettingsChange = [
        this,
        syncAudioControlsFromCurrentSettings,
        queueAudioApply
    ](const QString& audition) {
        owner_.previewAudioSettings_.normalize();
        syncAudioControlsFromCurrentSettings();
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.savePortableState();
        queueAudioApply(audition);
    };

    const auto connectAudioSlider = [
        this,
        &dialog,
        commitAudioSettingsChange
    ](QSlider* slider, void (PreviewAudioSettings::*setter)(int), const QString& audition) {
        connect(slider, &QSlider::valueChanged, &dialog, [this, setter, audition, commitAudioSettingsChange](int value) {
            (owner_.previewAudioSettings_.*setter)(value);
            commitAudioSettingsChange(audition);
        });
    };

    syncAudioControlsFromCurrentSettings();

    connectAudioSlider(masterSlider, &PreviewAudioSettings::setGlobalPercent, "answer");
    connect(masterMuteButton, &QToolButton::clicked, &dialog, [this, commitAudioSettingsChange]() {
        const bool muteAll = !owner_.previewAudioSettings_.globalMuted();
        if (muteAll) {
            owner_.previewAudioSettings_.toggleGlobalMuted();
            if (!owner_.previewAudioSettings_.trackMuted()) {
                owner_.previewAudioSettings_.toggleTrackMuted();
            }
            if (!owner_.previewAudioSettings_.answerMuted()) {
                owner_.previewAudioSettings_.toggleAnswerMuted();
            }
            if (!owner_.previewAudioSettings_.tapMuted()) {
                owner_.previewAudioSettings_.toggleTapMuted();
            }
            if (!owner_.previewAudioSettings_.exMuted()) {
                owner_.previewAudioSettings_.toggleExMuted();
            }
            if (!owner_.previewAudioSettings_.breakMuted()) {
                owner_.previewAudioSettings_.toggleBreakMuted();
            }
            if (!owner_.previewAudioSettings_.breakSlideMuted()) {
                owner_.previewAudioSettings_.toggleBreakSlideMuted();
            }
            if (!owner_.previewAudioSettings_.slideMuted()) {
                owner_.previewAudioSettings_.toggleSlideMuted();
            }
            if (!owner_.previewAudioSettings_.touchMuted()) {
                owner_.previewAudioSettings_.toggleTouchMuted();
            }
            if (!owner_.previewAudioSettings_.fireworkMuted()) {
                owner_.previewAudioSettings_.toggleFireworkMuted();
            }
        } else {
            owner_.previewAudioSettings_.toggleGlobalMuted();
            if (owner_.previewAudioSettings_.trackMuted()) {
                owner_.previewAudioSettings_.toggleTrackMuted();
            }
            if (owner_.previewAudioSettings_.answerMuted()) {
                owner_.previewAudioSettings_.toggleAnswerMuted();
            }
            if (owner_.previewAudioSettings_.tapMuted()) {
                owner_.previewAudioSettings_.toggleTapMuted();
            }
            if (owner_.previewAudioSettings_.exMuted()) {
                owner_.previewAudioSettings_.toggleExMuted();
            }
            if (owner_.previewAudioSettings_.breakMuted()) {
                owner_.previewAudioSettings_.toggleBreakMuted();
            }
            if (owner_.previewAudioSettings_.breakSlideMuted()) {
                owner_.previewAudioSettings_.toggleBreakSlideMuted();
            }
            if (owner_.previewAudioSettings_.slideMuted()) {
                owner_.previewAudioSettings_.toggleSlideMuted();
            }
            if (owner_.previewAudioSettings_.touchMuted()) {
                owner_.previewAudioSettings_.toggleTouchMuted();
            }
            if (owner_.previewAudioSettings_.fireworkMuted()) {
                owner_.previewAudioSettings_.toggleFireworkMuted();
            }
        }
        commitAudioSettingsChange(owner_.previewAudioSettings_.globalMuted() ? QString() : QStringLiteral("answer"));
    });
    connectAudioSlider(bgmSlider, &PreviewAudioSettings::setTrackPercent, QString());
    connectAudioSlider(answerSlider, &PreviewAudioSettings::setAnswerPercent, "answer");
    connectAudioSlider(judgeSlider, &PreviewAudioSettings::setTapPercent, "judge");
    connectAudioSlider(exSlider, &PreviewAudioSettings::setExPercent, "ex");
    connectAudioSlider(breakSlider, &PreviewAudioSettings::setBreakPercent, "break");
    connectAudioSlider(breakSlideSlider, &PreviewAudioSettings::setBreakSlidePercent, "break_slide");
    connectAudioSlider(slideSlider, &PreviewAudioSettings::setSlidePercent, "slide");
    connectAudioSlider(touchSlider, &PreviewAudioSettings::setTouchPercent, "touch");
    connectAudioSlider(fireworkSlider, &PreviewAudioSettings::setFireworkPercent, "firework");
    connect(breakSlideTailCheerCheck, &QCheckBox::toggled, &dialog, [this, commitAudioSettingsChange](bool checked) {
        owner_.previewAudioSettings_.breakSlideTailCheerMuted = checked;
        commitAudioSettingsChange(QString());
    });
    const auto connectPerChannelMute = [
        this,
        &dialog,
        commitAudioSettingsChange
    ](QToolButton* button, void (PreviewAudioSettings::*toggleMuted)(), bool (PreviewAudioSettings::*mutedGetter)() const, const QString& audition) {
        connect(button, &QToolButton::clicked, &dialog, [this, toggleMuted, mutedGetter, audition, commitAudioSettingsChange]() {
            (owner_.previewAudioSettings_.*toggleMuted)();
            const bool mutedNow = (owner_.previewAudioSettings_.*mutedGetter)();
            commitAudioSettingsChange(mutedNow ? QString() : audition);
        });
    };
    connectPerChannelMute(bgmMuteButton, &PreviewAudioSettings::toggleTrackMuted, &PreviewAudioSettings::trackMuted, QString());
    connectPerChannelMute(answerMuteButton, &PreviewAudioSettings::toggleAnswerMuted, &PreviewAudioSettings::answerMuted, "answer");
    connectPerChannelMute(judgeMuteButton, &PreviewAudioSettings::toggleTapMuted, &PreviewAudioSettings::tapMuted, "judge");
    connectPerChannelMute(exMuteButton, &PreviewAudioSettings::toggleExMuted, &PreviewAudioSettings::exMuted, "ex");
    connectPerChannelMute(breakMuteButton, &PreviewAudioSettings::toggleBreakMuted, &PreviewAudioSettings::breakMuted, "break");
    connectPerChannelMute(
        breakSlideMuteButton,
        &PreviewAudioSettings::toggleBreakSlideMuted,
        &PreviewAudioSettings::breakSlideMuted,
        "break_slide"
    );
    connectPerChannelMute(slideMuteButton, &PreviewAudioSettings::toggleSlideMuted, &PreviewAudioSettings::slideMuted, "slide");
    connectPerChannelMute(touchMuteButton, &PreviewAudioSettings::toggleTouchMuted, &PreviewAudioSettings::touchMuted, "touch");
    connectPerChannelMute(fireworkMuteButton, &PreviewAudioSettings::toggleFireworkMuted, &PreviewAudioSettings::fireworkMuted, "firework");
    if (saveLocalAudioPresetButton != nullptr) {
        connect(saveLocalAudioPresetButton, &QPushButton::clicked, &dialog, [this]() {
            owner_.previewAudioSettings_.normalize();
            owner_.softwarePreviewAudioSettings_ = owner_.previewAudioSettings_;
            owner_.softwarePreviewAudioSettings_.normalize();
            owner_.savePortableState();
        });
    }
    if (applyLocalAudioPresetButton != nullptr) {
        connect(
            applyLocalAudioPresetButton,
            &QPushButton::clicked,
            &dialog,
            [this, audioApplyTimer, &pendingAudition, syncAudioControlsFromCurrentSettings]() {
                owner_.previewAudioSettings_ = owner_.softwarePreviewAudioSettings_;
                owner_.previewAudioSettings_.normalize();
                syncAudioControlsFromCurrentSettings();
                owner_.applyPreviewAudioSettingsToRuntime();
                owner_.savePortableState();
                if (audioApplyTimer->isActive()) {
                    audioApplyTimer->stop();
                }
                pendingAudition.clear();
            }
        );
    }

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, masterSlider, bgmSlider, answerSlider, judgeSlider, breakSlider, breakSlideSlider, slideSlider, exSlider, touchSlider, fireworkSlider, &pendingAudition, playDialogLocalSfxAudition]() {
        if (masterSlider->isSliderDown()
            || bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || judgeSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || breakSlideSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || fireworkSlider->isSliderDown()) {
            audioApplyTimer->start();
            return;
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && playDialogLocalSfxAudition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });
    connect(&dialog, &QDialog::finished, &dialog, [this, audioApplyTimer, &pendingAudition, playDialogLocalSfxAudition]() {
        if (!audioApplyTimer->isActive()) {
            return;
        }
        audioApplyTimer->stop();
        const bool handledLocally = !pendingAudition.isEmpty()
            && playDialogLocalSfxAudition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });

    connect(outerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, outerBrightnessLabel](int value) {
        owner_.previewBackgroundBrightnessOuter_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        outerBrightnessLabel->setText(QString::number(value) + "%");
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundBrightnessOuter(owner_.previewBackgroundBrightnessOuter_);
        }
        owner_.savePortableState();
    });
    connect(innerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, innerBrightnessLabel](int value) {
        owner_.previewBackgroundBrightnessInner_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        innerBrightnessLabel->setText(QString::number(value) + "%");
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundBrightnessInner(owner_.previewBackgroundBrightnessInner_);
        }
        owner_.savePortableState();
    });
    connect(layoutSquareScaleSlider, &QSlider::valueChanged, &dialog, [this, layoutSquareScaleLabel](int value) {
        owner_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        layoutSquareScaleLabel->setText(QString::number(qRound(owner_.previewLayoutSquareScale_ * 100.0)) + "%");
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
        }
        owner_.savePortableState();
    });
    connect(smoothBrightnessCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewSmoothBrightness_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
        }
        owner_.savePortableState();
    });
    connect(timestampCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowTimestamp_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowTimestamp(owner_.previewShowTimestamp_);
        }
        owner_.savePortableState();
    });
    connect(forceLabeledJudgeLineWhenPausedCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewForceLabeledJudgeLineWhenPaused_ = checked;
        owner_.applyEffectivePreviewOutlineVariantToCanvas();
        owner_.applyPreviewStageMediaRouteVisualSettings();
        owner_.savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowDebugInfo_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowDebugInfo(owner_.previewShowDebugInfo_);
        }
        owner_.savePortableState();
    });
    dialog.adjustSize();
    dialog.exec();
}
