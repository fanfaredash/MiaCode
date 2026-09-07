#include "tools/media/PvCompressionPolicy.h"
#include "app/v2/UiRequestService.h"
#include "app/v2/JobProgressService.h"
#include "runtime/media/MediaJobsHost.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "UiText.h"
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
#include <QTemporaryDir>
#include <QUrl>
#include <QtCore>
#include <QtGui>

#include "common/DebugLog.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <RestartManager.h>
#pragma comment(lib, "Rstrtmgr.lib")
#endif

using namespace miacode::runtime::shared;

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
        *error = UiText::text(QStringLiteral("media_tools.failed_to_write_file_1"))
            .arg(destinationPath);
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
            *error = UiText::text(QStringLiteral("media_tools.failed_to_stage_original_file"))
                .arg(destinationPath)
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

// Runs ffmpeg on the shell's shared progress surface. When
// `totalDurationSeconds` is positive the bar tracks real percent-done by
// reading ffmpeg's machine-readable `-progress pipe:1` stream: each block
// carries an `out_time_us=` line (the output timestamp reached so far) which,
// divided by the expected total duration, gives the percentage. When the
// duration is unknown (<= 0) it falls back to an indeterminate busy bar.
bool runFfmpegBlocking(
    const QString& ffmpegPath,
    const QStringList& args,
    miacode::v2::JobProgressService* jobProgress,
    const QString& title,
    const QString& label,
    double totalDurationSeconds,
    QString* error,
    bool* cancelled = nullptr)
{
    if (jobProgress == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("progress surface unavailable");
        }
        return false;
    }
    if (cancelled != nullptr) {
        *cancelled = false;
    }
    const bool determinate = totalDurationSeconds > 0.0;
    const quint64 jobToken = jobProgress->begin(title, label, /*cancellable=*/true);
    if (!determinate) {
        jobProgress->reportIndeterminate(label);
    }
    const auto endJob = [jobProgress, jobToken]() {
        if (jobProgress->token() == jobToken) {
            jobProgress->end();
        }
    };
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
        endJob();
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
                        jobProgress->report(
                            qBound(0, qRound(seconds / totalDurationSeconds * 100.0), 99), label);
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
        // AllEvents (not ExcludeUserInputEvents) so the overlay's Cancel button
        // receives clicks; the overlay swallows input to everything beneath it.
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (jobProgress->cancelRequested()) {
            process.kill();
            process.waitForFinished(2000);
            endJob();
            if (cancelled != nullptr) {
                *cancelled = true;
            }
            if (error != nullptr) {
                *error = UiText::text(QStringLiteral("media_tools.canceled"));
            }
            return false;
        }
    }
    process.waitForFinished(200);
    pump();  // drain anything emitted between the last read and exit
    if (determinate) {
        jobProgress->report(100, label);
    }
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
    miacode::v2::JobProgressService* jobProgress,
    QString* error,
    bool* cancelled = nullptr,
    bool* preservedCompressed = nullptr)
{
    if (preservedCompressed != nullptr) {
        *preservedCompressed = false;
    }
    const QFileInfo videoInfo(videoPath);
    const qint64 originalBytes = videoInfo.size();
    if (originalBytes > 0 && originalBytes < miacode::media::kPvCompressionHardLimitBytes) {
        if (error != nullptr) {
            *error = UiText::text(QStringLiteral("media_tools.the_current_video_is_already"));
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

    QTemporaryDir passLogDirectory;
    if (!passLogDirectory.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not create the two-pass log directory.");
        }
        return false;
    }

    miacode::media::PvCompressionPlan plan =
        miacode::media::makePvCompressionPlan(durationSeconds);
    qint64 compressedBytes = 0;
    bool encoded = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        QFile::remove(tempPath);
        const QString passLogPath = QDir(passLogDirectory.path()).filePath(
            QStringLiteral("x264-attempt-%1").arg(attempt));
        const QStringList firstPass = miacode::media::makePvCompressionPassArguments(
            backupPath, tempPath, passLogPath, plan, 1);
        const QStringList secondPass = miacode::media::makePvCompressionPassArguments(
            backupPath, tempPath, passLogPath, plan, 2);
        const bool firstPassOk = runFfmpegBlocking(
            ffmpegPath,
            firstPass,
            jobProgress,
            UiText::text(QStringLiteral("media_tools.compressing_video")),
            UiText::text(QStringLiteral("media_tools.compressing_video")),
            durationSeconds,
            error,
            cancelled);
        const bool secondPassOk = firstPassOk && runFfmpegBlocking(
            ffmpegPath,
            secondPass,
            jobProgress,
            UiText::text(QStringLiteral("media_tools.compressing_video")),
            UiText::text(QStringLiteral("media_tools.compressing_video")),
            durationSeconds,
            error,
            cancelled);
        if (!firstPassOk || !secondPassOk) {
            QFile::remove(tempPath);
            return false;
        }

        compressedBytes = QFileInfo(tempPath).size();
        if (miacode::media::isAcceptablePvCompressionOutput(originalBytes, compressedBytes)) {
            encoded = true;
            break;
        }
        if (attempt == 0 && compressedBytes >= miacode::media::kPvCompressionHardLimitBytes) {
            plan = miacode::media::adjustedPvCompressionPlan(plan, compressedBytes);
        } else {
            break;
        }
    }

    if (!encoded) {
        QFile::remove(tempPath);
        if (error != nullptr) {
            *error = compressedBytes >= miacode::media::kPvCompressionHardLimitBytes
                ? QStringLiteral("Compressed video is still larger than 20 MB.")
                : QStringLiteral("Compressed video was not smaller than the original file.");
        }
        return false;
    }
    if (replaceFileWithTemp(tempPath, videoPath, error)) {
        return true;
    }
    // Compression itself SUCCEEDED — only the in-place replace could not complete
    // (the original is still held open). Don't discard the finished clip: move it
    // beside the original under a clear, user-visible name so the work is kept and
    // the user can swap it in by hand after freeing the file. The original is left
    // untouched, and its bytes are also in the <base>_bak backup made up-front, so
    // nothing is lost. `error` currently holds replaceFileWithTemp's lock
    // diagnostic; prepend the "saved as …" note so both are shown.
    const QString preservedPath = videoInfo.dir().filePath(
        QStringLiteral("%1_compressed.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix()));
    QFile::remove(preservedPath);
    if (QFile::rename(tempPath, preservedPath)) {
        if (preservedCompressed != nullptr) {
            *preservedCompressed = true;
        }
        if (error != nullptr) {
            const QString staged = *error;
            *error = UiText::text(QStringLiteral("media_tools.compressed_but_replace_failed"))
                         .arg(QDir::toNativeSeparators(preservedPath));
            if (!staged.isEmpty()) {
                *error += QStringLiteral("\n\n") + staged;
            }
        }
    }
    // If even the rename-out failed, leave the temp in place (still recoverable)
    // and keep replaceFileWithTemp's error as-is.
    return false;
}

bool convertTrackTo44100Hz(
    const QString& ffmpegPath,
    const QString& trackPath,
    miacode::v2::JobProgressService* jobProgress,
    QString* error,
    bool* cancelled = nullptr)
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
            jobProgress,
            UiText::text(QStringLiteral("media_tools.processing_audio")),
            UiText::text(QStringLiteral("media_tools.processing_audio")),
            trackDurationSeconds,
            error,
            cancelled)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependTrackSilence(
    const QString& ffmpegPath,
    const QString& trackPath,
    double silenceSeconds,
    miacode::v2::JobProgressService* jobProgress,
    QString* error,
    bool* cancelled = nullptr)
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
            jobProgress,
            UiText::text(QStringLiteral("media_tools.processing_track_mp3")),
            UiText::text(QStringLiteral("media_tools.processing_track_mp3")),
            totalDurationSeconds,
            error,
            cancelled)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependPvBlack(
    const QString& ffmpegPath,
    const QString& pvPath,
    double silenceSeconds,
    miacode::v2::JobProgressService* jobProgress,
    QString* error,
    bool* cancelled = nullptr)
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
            jobProgress,
            UiText::text(QStringLiteral("media_tools.processing_pv_mp4")),
            UiText::text(QStringLiteral("media_tools.processing_pv_mp4")),
            totalDurationSeconds,
            error,
            cancelled)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, pvPath, error);
}

} // namespace

void miacode::runtime::MediaJobsHost::onCompressBackgroundVideo()
{
    MC_OP("miacode::runtime::MediaJobsHost::onCompressBackgroundVideo");
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    const QString title = UiText::text(QStringLiteral("media_tools.compress_video"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning, title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }

    const QString videoPath = miacode::chart_assets::resolveChartVideoPath(
        session_.currentFilePath_, session_.applicationServices_.workspace().document().videoPath);
    if (!QFileInfo::exists(videoPath)) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning, title,
            UiText::text(QStringLiteral("media_tools.no_background_mp4_video_was")));
        return;
    }

    const QFileInfo videoInfo(videoPath);
    // Size gate up-front: if the video is already under 20 MB there is nothing
    // to compress, so say so immediately instead of making the user confirm
    // first and only then discovering there's no work to do.
    const qint64 videoSizeBytes = videoInfo.size();
    if (videoSizeBytes > 0
        && videoSizeBytes < miacode::media::kPvCompressionHardLimitBytes) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Information, title,
            UiText::text(QStringLiteral("media_tools.the_current_video_is_already_2"))
                .arg(QLocale().formattedDataSize(videoSizeBytes)));
        return;
    }
    const QString backupName =
        QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix());
    requests->requestConfirmation(
        title,
        UiText::text(QStringLiteral("media_tools.compress_1_under_20_mib"))
            .arg(videoInfo.fileName(), backupName),
        UiText::text(QStringLiteral("media_tools.compress_video")),
        [this, title, videoPath, backupName](bool accepted) {
            if (accepted) {
                runCompressBackgroundVideo(title, videoPath, backupName);
            }
        });
}

void miacode::runtime::MediaJobsHost::runCompressBackgroundVideo(
    const QString& title, const QString& videoPath, const QString& backupName)
{
    MC_OP("miacode::runtime::MediaJobsHost::runCompressBackgroundVideo");
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Error, title,
            UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place")));
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    bool cancelled = false;
    bool preservedCompressed = false;
    if (!compressVideoUnder20Mb(
            ffmpegPath, videoPath, session_.jobProgressService(), &error, &cancelled,
            &preservedCompressed)) {
        // preservedCompressed means the encode succeeded and only the
        // auto-replace was blocked by a file lock — a heads-up, not a failure.
        requests->postNotice(
            (cancelled || preservedCompressed) ? miacode::v2::NoticeSeverity::Information
                                               : miacode::v2::NoticeSeverity::Error,
            title,
            cancelled ? UiText::text(QStringLiteral("media_tools.video_compression_canceled"))
                      : error);
        reloadPreviewMediaAfterFileOperation(false);
        return;
    }
    reloadPreviewMediaAfterFileOperation(false);
    showMediaOperationCompleteDialog(
        title,
        UiText::text(QStringLiteral("media_tools.compressed_1_under_20_mib_2"))
            .arg(QFileInfo(videoPath).fileName(), backupName),
        videoPath);
}

void miacode::runtime::MediaJobsHost::onConvertTrackTo44100Hz()
{
    MC_OP("miacode::runtime::MediaJobsHost::onConvertTrackTo44100Hz");
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    const QString title = UiText::text(QStringLiteral("media_tools.sample_rate"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning, title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }

    const QString trackPath = QDir(chartDirPath).filePath(QStringLiteral("track.mp3"));
    if (!QFileInfo::exists(trackPath)) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning, title,
            UiText::text(QStringLiteral("media_tools.track_mp3_was_not_found")));
        return;
    }

    requests->requestConfirmation(
        title,
        UiText::text(QStringLiteral("media_tools.convert_track_mp3_to_44100")),
        UiText::text(QStringLiteral("media_tools.sample_rate")),
        [this, title, trackPath](bool accepted) {
            if (accepted) {
                runConvertTrackTo44100Hz(title, trackPath);
            }
        });
}

void miacode::runtime::MediaJobsHost::runConvertTrackTo44100Hz(
    const QString& title, const QString& trackPath)
{
    MC_OP("miacode::runtime::MediaJobsHost::runConvertTrackTo44100Hz");
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Error, title,
            UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place")));
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    bool cancelled = false;
    // Qualified: Session now also has a convertTrackTo44100Hz() — the
    // MediaToolsEngine entry point — which would otherwise shadow this
    // file-local ffmpeg helper from inside the nested section class.
    if (!::convertTrackTo44100Hz(ffmpegPath, trackPath, session_.jobProgressService(), &error, &cancelled)) {
        requests->postNotice(
            cancelled ? miacode::v2::NoticeSeverity::Information
                      : miacode::v2::NoticeSeverity::Error,
            title,
            cancelled
                ? UiText::text(QStringLiteral("media_tools.sample_rate_conversion_canceled"))
                : error);
        reloadPreviewMediaAfterFileOperation(true);
        return;
    }
    reloadPreviewMediaAfterFileOperation(true);
    showMediaOperationCompleteDialog(
        title,
        UiText::text(QStringLiteral("media_tools.converted_track_mp3_to_44100_2")),
        trackPath);
}

miacode::runtime::MediaJobsHost::MediaBlankPaths miacode::runtime::MediaJobsHost::resolveMediaBlankPaths(MediaBlankTarget target) const
{
    MediaBlankPaths paths;
    paths.isTrack = target == MediaBlankTarget::Track;
    paths.title = paths.isTrack
        ? UiText::text(QStringLiteral("media_tools.prepend_track_silence"))
        : UiText::text(QStringLiteral("media_tools.prepend_pv_black_screen"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        return paths;
    }
    paths.inputPath = paths.isTrack
        ? QDir(chartDirPath).filePath(QStringLiteral("track.mp3"))
        : miacode::chart_assets::resolveChartVideoPath(
              session_.currentFilePath_, session_.applicationServices_.workspace().document().videoPath);
    const QFileInfo inputInfo(paths.inputPath);
    paths.inputName = paths.isTrack ? QStringLiteral("track.mp3") : inputInfo.fileName();
    paths.backupName = paths.isTrack
        ? QStringLiteral("track_bak.mp3")
        : QStringLiteral("%1_bak.%2").arg(inputInfo.completeBaseName(), inputInfo.suffix());
    paths.backupPath = paths.inputPath.isEmpty()
        ? QString()
        : inputInfo.dir().filePath(paths.backupName);
    return paths;
}

QVariantMap miacode::runtime::MediaJobsHost::prependMediaBlankContext(MediaBlankTarget target)
{
    MC_OP("miacode::runtime::MediaJobsHost::prependMediaBlankContext");
    QVariantMap context;
    context.insert(QStringLiteral("available"), false);
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return context;
    }
    const MediaBlankPaths paths = resolveMediaBlankPaths(target);
    context.insert(QStringLiteral("title"), paths.title);
    context.insert(QStringLiteral("isTrack"), paths.isTrack);
    if (paths.inputPath.isEmpty()) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning, paths.title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return context;
    }
    if (!QFileInfo::exists(paths.inputPath)) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning, paths.title,
            UiText::text(QStringLiteral("media_tools.1_was_not_found_next"))
                .arg(paths.isTrack
                         ? paths.inputName
                         : UiText::text(QStringLiteral("media_tools.background_mp4_video"))));
        return context;
    }

    const QVector<SimaiRawField>& extraFields = session_.applicationServices_.workspace().document().extraFields;
    const int clockCount = mediaBlankClockCountFromFields(extraFields);
    const double wholeBpm = miacode::chart_clock::wholeBpmFromFields(extraFields);
    const double chartBpm = miacode::chart_clock::firstBpmFromChart(session_.activeChartText());

    context.insert(QStringLiteral("available"), true);
    context.insert(QStringLiteral("inputName"), paths.inputName);
    context.insert(QStringLiteral("backupName"), paths.backupName);
    context.insert(QStringLiteral("hasBackup"), QFileInfo::exists(paths.backupPath));
    context.insert(QStringLiteral("beats"), clockCount > 0 ? clockCount : 4);
    context.insert(
        QStringLiteral("bpm"),
        wholeBpm > 0.0 ? wholeBpm
                       : (chartBpm > 0.0 ? chartBpm : miacode::chart_clock::kFallbackClockBpm));
    return context;
}

QVariantMap miacode::runtime::MediaJobsHost::detectMediaBlankTiming(MediaBlankTarget target)
{
    // Analysis reads the chart's own track regardless of which media the blank
    // is going in front of: the beat grid comes from the audio either way.
    Q_UNUSED(target);
    QVariantMap detected;
    const QString trackPath = miacode::chart_assets::resolveTrackPath(session_.currentFilePath_);
    const auto decoded = miacode::latency_analysis::decodeMonoTrack(trackPath);
    if (decoded.samples.isEmpty()) {
        return detected;
    }
    const auto envelope =
        miacode::latency_analysis::buildOnsetEnvelope(decoded.samples, decoded.sampleRate);
    const auto result = miacode::latency_analysis::detectBpm(envelope);
    if (!(result.bpm > 0.0)) {
        return detected;
    }
    detected.insert(QStringLiteral("bpm"), result.bpm);
    if (!result.meterId.isEmpty()) {
        detected.insert(QStringLiteral("beats"), mediaBlankBeatsFromMeterId(result.meterId));
    }
    return detected;
}

void miacode::runtime::MediaJobsHost::restoreMediaBlankBackup(MediaBlankTarget target)
{
    MC_OP("miacode::runtime::MediaJobsHost::restoreMediaBlankBackup");
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    const MediaBlankPaths paths = resolveMediaBlankPaths(target);
    releasePreviewMediaForFileOperation();
    QString error;
    if (!restoreFileFromBackup(paths.backupPath, paths.inputPath, &error)) {
        requests->postNotice(miacode::v2::NoticeSeverity::Error, paths.title, error);
    } else {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Information, paths.title,
            UiText::text(QStringLiteral("media_tools.backup_restored")));
    }
    reloadPreviewMediaAfterFileOperation(paths.isTrack);
}

void miacode::runtime::MediaJobsHost::applyMediaBlank(MediaBlankTarget target, double beats, double bpm)
{
    MC_OP("miacode::runtime::MediaJobsHost::applyMediaBlank");
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr || !(bpm > 0.0) || !(beats > 0.0)) {
        return;
    }
    const MediaBlankPaths paths = resolveMediaBlankPaths(target);
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Error, paths.title,
            UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place")));
        return;
    }

    const double silenceSeconds = beats * 60.0 / bpm;
    releasePreviewMediaForFileOperation();

    QString error;
    bool cancelled = false;
    const bool ok = paths.isTrack
        ? prependTrackSilence(
              ffmpegPath, paths.inputPath, silenceSeconds, session_.jobProgressService(), &error,
              &cancelled)
        : prependPvBlack(
              ffmpegPath, paths.inputPath, silenceSeconds, session_.jobProgressService(), &error,
              &cancelled);
    if (!ok) {
        requests->postNotice(
            cancelled ? miacode::v2::NoticeSeverity::Information
                      : miacode::v2::NoticeSeverity::Error,
            paths.title,
            cancelled
                ? UiText::text(paths.isTrack
                                   ? QStringLiteral("media_tools.track_mp3_processing_canceled")
                                   : QStringLiteral("media_tools.video_processing_canceled"))
                : error);
        reloadPreviewMediaAfterFileOperation(paths.isTrack);
        return;
    }

    reloadPreviewMediaAfterFileOperation(paths.isTrack);
    showMediaOperationCompleteDialog(
        paths.title,
        UiText::text(QStringLiteral("media_tools.prepended_2_s_of_3"))
            .arg(paths.inputName)
            .arg(silenceSeconds, 0, 'f', 3)
            .arg(paths.isTrack ? UiText::text(QStringLiteral("media_tools.silence"))
                               : UiText::text(QStringLiteral("media_tools.black_screen")))
            .arg(paths.backupName),
        paths.inputPath);
}
