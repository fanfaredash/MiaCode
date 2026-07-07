#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
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
#include "tools/video_export/HudFontSettings.h"

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
    QString* error,
    bool* cancelled = nullptr)
{
    if (cancelled != nullptr) {
        *cancelled = false;
    }
    const bool determinate = totalDurationSeconds > 0.0;
    QProgressDialog progress(label, QString(), 0, determinate ? 100 : 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    // Real Cancel button (mirrors the export flow). Clicking it asks for
    // confirmation before actually aborting the ffmpeg process (see below).
    progress.setCancelButtonText(UiText::text(QStringLiteral("action.cancel")));
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

    // Cancel = abort immediately (no confirmation prompt). Clicking the
    // dialog's Cancel button just flags the loop below, which kills the ffmpeg
    // process; the caller then surfaces a "canceled" popup.
    bool cancelConfirmed = false;
    QObject::connect(&progress, &QProgressDialog::canceled, &progress, [&]() {
        cancelConfirmed = true;
    });

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
        // AllEvents (not ExcludeUserInputEvents) so the Cancel button receives
        // clicks; the dialog is application-modal, so the main window stays inert.
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (cancelConfirmed) {
            process.kill();
            process.waitForFinished(2000);
            progress.close();
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
    QString* error,
    bool* cancelled = nullptr)
{
    constexpr qint64 kTargetBytes = 20LL * 1024LL * 1024LL;
    constexpr int kAudioBitrateKbps = 96;
    constexpr int kMinVideoBitrateKbps = 120;
    const QFileInfo videoInfo(videoPath);
    const qint64 originalBytes = videoInfo.size();
    if (originalBytes > 0 && originalBytes <= kTargetBytes) {
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
            UiText::text(QStringLiteral("media_tools.compressing_video")),
            durationSeconds,
            error,
            cancelled)) {
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
            parent,
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
    QWidget* parent,
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
            parent,
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
    QWidget* parent,
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
            parent,
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
    const QString title = UiText::text(QStringLiteral("media_tools.compress_video"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart"))
        );
        return;
    }

    const QString videoPath = miacode::chart_assets::resolveChartVideoPath(owner_.currentFilePath_, owner_.document_.videoPath);
    if (!QFileInfo::exists(videoPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.no_background_mp4_video_was"))
        );
        return;
    }

    const QFileInfo videoInfo(videoPath);
    // Size gate up-front: if the video is already under 20 MiB there is nothing
    // to compress, so say so immediately instead of making the user confirm
    // first and only then discovering there's no work to do.
    constexpr qint64 kCompressTargetBytes = 20LL * 1024LL * 1024LL;
    const qint64 videoSizeBytes = videoInfo.size();
    if (videoSizeBytes > 0 && videoSizeBytes <= kCompressTargetBytes) {
        QMessageBox::information(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.the_current_video_is_already_2")).arg(QLocale().formattedDataSize(videoSizeBytes))
        );
        return;
    }
    const QString backupName = QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix());
    if (QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.compress_1_under_20_mib")).arg(videoInfo.fileName(), backupName)
        ) != QMessageBox::Yes) {
        return;
    }

    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place"))
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    bool cancelled = false;
    if (!compressVideoUnder20Mb(ffmpegPath, videoPath, UiDialogs::effectiveParentWidget(&owner_), &error, &cancelled)) {
        if (cancelled) {
            QMessageBox::information(
                UiDialogs::effectiveParentWidget(&owner_), title,
                UiText::text(QStringLiteral("media_tools.video_compression_canceled")));
        } else {
            QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
        }
        reloadPreviewMediaAfterFileOperation(false);
        return;
    }
    reloadPreviewMediaAfterFileOperation(false);
    owner_.statusBar()->showMessage(
        UiText::text(QStringLiteral("media_tools.compressed_1_under_20_mib")).arg(videoInfo.fileName()),
        6000
    );
    showMediaOperationCompleteDialog(
        title,
        UiText::text(QStringLiteral("media_tools.compressed_1_under_20_mib_2")).arg(videoInfo.fileName(), backupName),
        videoPath
    );
}

void MainWindow::DialogsSection::onConvertTrackTo44100Hz()
{
    MC_OP("MainWindow::DialogsSection::onConvertTrackTo44100Hz");
    const QString title = UiText::text(QStringLiteral("media_tools.sample_rate"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart"))
        );
        return;
    }

    const QString trackPath = QDir(chartDirPath).filePath(QStringLiteral("track.mp3"));
    if (!QFileInfo::exists(trackPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.track_mp3_was_not_found"))
        );
        return;
    }

    if (QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.convert_track_mp3_to_44100"))
        ) != QMessageBox::Yes) {
        return;
    }

    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place"))
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    bool cancelled = false;
    if (!convertTrackTo44100Hz(ffmpegPath, trackPath, UiDialogs::effectiveParentWidget(&owner_), &error, &cancelled)) {
        if (cancelled) {
            QMessageBox::information(
                UiDialogs::effectiveParentWidget(&owner_), title,
                UiText::text(QStringLiteral("media_tools.sample_rate_conversion_canceled")));
        } else {
            QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
        }
        reloadPreviewMediaAfterFileOperation(true);
        return;
    }
    reloadPreviewMediaAfterFileOperation(true);
    owner_.statusBar()->showMessage(
        UiText::text(QStringLiteral("media_tools.converted_track_mp3_to_44100")),
        6000
    );
    showMediaOperationCompleteDialog(
        title,
        UiText::text(QStringLiteral("media_tools.converted_track_mp3_to_44100_2")),
        trackPath
    );
}

void MainWindow::DialogsSection::onMediaProcessingTools()
{
    MC_OP("MainWindow::DialogsSection::onMediaProcessingTools");
    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(UiText::text(QStringLiteral("media_tools.audio_video_processing")));
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
        { UiText::text(QStringLiteral("media_tools.sample_rate")),
          UiText::text(QStringLiteral("media_tools.convert_track_mp3_to_44100_2")),
          &MainWindow::DialogsSection::onConvertTrackTo44100Hz },
        { UiText::text(QStringLiteral("media_tools.compress_video")),
          UiText::text(QStringLiteral("media_tools.compress_the_background_video_under")),
          &MainWindow::DialogsSection::onCompressBackgroundVideo },
        { UiText::text(QStringLiteral("media_tools.prepend_track_silence")),
          UiText::text(QStringLiteral("media_tools.insert_silence_at_the_start")),
          &MainWindow::DialogsSection::onPrependTrackSilence },
        { UiText::text(QStringLiteral("media_tools.prepend_pv_black_screen")),
          UiText::text(QStringLiteral("media_tools.insert_a_black_screen_at")),
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

void MainWindow::DialogsSection::onPrependMediaBlank(MediaBlankTarget target)
{
    MC_OP("MainWindow::DialogsSection::onPrependMediaBlank");
    const bool isTrack = target == MediaBlankTarget::Track;
    const QString title = isTrack
        ? (UiText::text(QStringLiteral("media_tools.prepend_track_silence")))
        : (UiText::text(QStringLiteral("media_tools.prepend_pv_black_screen")));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart"))
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
            UiText::text(QStringLiteral("media_tools.1_was_not_found_next"))
                .arg(isTrack
                    ? inputName
                    : UiText::text(QStringLiteral("media_tools.background_mp4_video")))
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
    auto* detectBeatsButton = new QPushButton(UiText::text(QStringLiteral("media_tools.detect")), beatsRow);
    beatsRowLayout->addWidget(detectBeatsButton, 0);
    auto* bpmRow = new QWidget(&dialog);
    auto* bpmRowLayout = new QHBoxLayout(bpmRow);
    bpmRowLayout->setContentsMargins(0, 0, 0, 0);
    bpmRowLayout->setSpacing(6);
    bpmRowLayout->addWidget(bpmSpin, 1);
    auto* detectBpmButton = new QPushButton(UiText::text(QStringLiteral("media_tools.detect")), bpmRow);
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
    form->addRow(UiText::text(QStringLiteral("media_tools.beats")), beatsRow);
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
        const QString mediaKind = isTrack
            ? UiText::text(QStringLiteral("media_tools.silence"))
            : UiText::text(QStringLiteral("media_tools.a_black_screen"));
        const QString target = isTrack
            ? QStringLiteral("track.mp3")
            : UiText::text(QStringLiteral("media_tools.the_background_video"));
        summaryLabel->setText(UiText::text(QStringLiteral("media_tools.prepends_1_to_2_3"))
            .arg(mediaKind, target, formatNumber(beats), formatNumber(bpm), formatNumber(seconds)));
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
        UiText::text(QStringLiteral("media_tools.restore_backup")),
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
            UiText::text(QStringLiteral("media_tools.backup_restored"))
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
            UiText::text(QStringLiteral("media_tools.ffmpeg_was_not_found_place"))
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    bool cancelled = false;
    const QWidget* parent = UiDialogs::effectiveParentWidget(&owner_);
    if (isTrack && !prependTrackSilence(ffmpegPath, trackPath, silenceSeconds, const_cast<QWidget*>(parent), &error, &cancelled)) {
        if (cancelled) {
            QMessageBox::information(
                UiDialogs::effectiveParentWidget(&owner_), title,
                UiText::text(QStringLiteral("media_tools.track_mp3_processing_canceled")));
        } else {
            QMessageBox::critical(
                UiDialogs::effectiveParentWidget(&owner_),
                UiText::text(QStringLiteral("media_tools.track_mp3_failed")),
                error
            );
        }
        reloadPreviewMediaAfterFileOperation(isTrack);
        return;
    }
    if (!isTrack && !prependPvBlack(ffmpegPath, videoPath, silenceSeconds, const_cast<QWidget*>(parent), &error, &cancelled)) {
        if (cancelled) {
            QMessageBox::information(
                UiDialogs::effectiveParentWidget(&owner_), title,
                UiText::text(QStringLiteral("media_tools.video_processing_canceled")));
        } else {
            QMessageBox::critical(
                UiDialogs::effectiveParentWidget(&owner_),
                UiText::text(QStringLiteral("media_tools.video_failed")),
                error
            );
        }
        reloadPreviewMediaAfterFileOperation(isTrack);
        return;
    }

    reloadPreviewMediaAfterFileOperation(isTrack);
    owner_.statusBar()->showMessage(
        UiText::text(QStringLiteral("media_tools.prepended_2_seconds_of_blank"))
            .arg(inputName)
            .arg(silenceSeconds, 0, 'f', 3),
        6000
    );
    showMediaOperationCompleteDialog(
        title,
        UiText::text(QStringLiteral("media_tools.prepended_2_s_of_3"))
            .arg(inputName)
            .arg(silenceSeconds, 0, 'f', 3)
            .arg(isTrack
                ? UiText::text(QStringLiteral("media_tools.silence"))
                : UiText::text(QStringLiteral("media_tools.black_screen")))
            .arg(backupName),
        inputPath
    );
}
