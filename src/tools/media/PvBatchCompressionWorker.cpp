#include "PvBatchCompressionWorker.h"

#include "UiText.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <utility>

namespace miacode::media {
namespace {

bool isExecutableFile(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
}

bool runProcess(
    const QString& executable,
    const QStringList& arguments,
    const std::atomic_bool* cancelRequested,
    QByteArray* output,
    QString* error)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) {
            *error = process.errorString();
        }
        return false;
    }
    while (!process.waitForFinished(100)) {
        if (cancelRequested != nullptr && cancelRequested->load()) {
            process.kill();
            process.waitForFinished(3000);
            return false;
        }
    }
    if (output != nullptr) {
        *output = process.readAll();
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            const QString detail = QString::fromLocal8Bit(process.readAll()).trimmed();
            *error = detail.isEmpty() ? process.errorString() : detail.right(1200);
        }
        return false;
    }
    return true;
}

bool probeDuration(
    const QString& ffmpegPath,
    const QString& videoPath,
    const std::atomic_bool* cancelRequested,
    double* durationSeconds,
    QString* error)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
        ffmpegPath,
        {QStringLiteral("-hide_banner"), QStringLiteral("-i"), videoPath},
        QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) {
            *error = process.errorString();
        }
        return false;
    }
    while (!process.waitForFinished(100)) {
        if (cancelRequested != nullptr && cancelRequested->load()) {
            process.kill();
            process.waitForFinished(3000);
            return false;
        }
    }

    const QString output = QString::fromLocal8Bit(process.readAll());
    static const QRegularExpression pattern(
        QStringLiteral(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))"));
    const QRegularExpressionMatch match = pattern.match(output);
    if (!match.hasMatch()) {
        if (error != nullptr) {
            *error = UiText::text(QStringLiteral("media_tools.batch_pv_duration_failed"));
        }
        return false;
    }
    const double total = match.captured(1).toDouble() * 3600.0
        + match.captured(2).toDouble() * 60.0
        + match.captured(3).toDouble();
    if (!(total > 0.0)) {
        if (error != nullptr) {
            *error = UiText::text(QStringLiteral("media_tools.batch_pv_duration_failed"));
        }
        return false;
    }
    *durationSeconds = total;
    return true;
}

bool replaceWithTemp(const QString& originalPath, const QString& tempPath, QString* preservedPath)
{
    const QString replacingPath = originalPath + QStringLiteral(".replacing");
    QFile::remove(replacingPath);
    if (!QFile::rename(originalPath, replacingPath)) {
        return false;
    }
    if (!QFile::rename(tempPath, originalPath)) {
        QFile::rename(replacingPath, originalPath);
        return false;
    }
    QFile::remove(replacingPath);
    if (preservedPath != nullptr) {
        preservedPath->clear();
    }
    return true;
}

bool compressJob(
    const QString& ffmpegPath,
    const PvCompressionJob& job,
    const std::atomic_bool* cancelRequested,
    QString* resultStatus)
{
    const QFileInfo videoInfo(job.videoPath);
    const qint64 originalBytes = videoInfo.size();
    if (originalBytes <= 0) {
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_invalid_file"));
        return false;
    }
    if (originalBytes <= kPvCompressionTargetBytes) {
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_already_small"));
        return true;
    }

    const QString backupPath = videoInfo.dir().filePath(
        QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix()));
    const QString tempPath = videoInfo.dir().filePath(QStringLiteral(".miacode_video_batch_compress_tmp.mp4"));
    QFile::remove(tempPath);
    if (QFileInfo::exists(backupPath) && !QFile::remove(backupPath)) {
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_backup_failed"));
        return false;
    }
    if (!QFile::copy(job.videoPath, backupPath)) {
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_backup_failed"));
        return false;
    }

    double durationSeconds = 0.0;
    QString error;
    if (!probeDuration(ffmpegPath, backupPath, cancelRequested, &durationSeconds, &error)) {
        *resultStatus = error;
        return false;
    }

    const qint64 outputTargetBytes = std::min(
        kPvCompressionTargetBytes,
        static_cast<qint64>(std::floor(static_cast<double>(originalBytes) * kPvCompressionShrinkRatio)));
    const double targetBits = static_cast<double>(outputTargetBytes) * 8.0 * kPvCompressionMuxSafetyRatio;
    const int totalBitrateKbps = static_cast<int>(std::floor(targetBits / durationSeconds / 1000.0));
    const int videoBitrateKbps = std::max(
        kPvCompressionMinVideoBitrateKbps,
        totalBitrateKbps - kPvCompressionAudioBitrateKbps);
    const QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-nostdin"), QStringLiteral("-y"),
        QStringLiteral("-i"), backupPath,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-map"), QStringLiteral("0:a?"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("slow"),
        QStringLiteral("-b:v"), QStringLiteral("%1k").arg(videoBitrateKbps),
        QStringLiteral("-maxrate"), QStringLiteral("%1k").arg(videoBitrateKbps),
        QStringLiteral("-bufsize"), QStringLiteral("%1k").arg(videoBitrateKbps * 2),
        QStringLiteral("-vf"), QStringLiteral("scale='min(1280,iw)':-2"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"),
        QStringLiteral("-b:a"), QStringLiteral("%1k").arg(kPvCompressionAudioBitrateKbps),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        tempPath,
    };
    if (!runProcess(ffmpegPath, args, cancelRequested, nullptr, &error)) {
        QFile::remove(tempPath);
        if (cancelRequested == nullptr || !cancelRequested->load()) {
            *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_ffmpeg_failed_1")).arg(error);
        }
        return false;
    }

    const qint64 compressedBytes = QFileInfo(tempPath).size();
    if (compressedBytes <= 0
        || compressedBytes > kPvCompressionTargetBytes
        || compressedBytes >= originalBytes) {
        QFile::remove(tempPath);
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_output_invalid"));
        return false;
    }

    QString unused;
    if (replaceWithTemp(job.videoPath, tempPath, &unused)) {
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_done_1"))
            .arg(QLocale().formattedDataSize(compressedBytes));
        return true;
    }

    const QString preservedPath = videoInfo.dir().filePath(
        QStringLiteral("%1_compressed.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix()));
    QFile::remove(preservedPath);
    if (QFile::rename(tempPath, preservedPath)) {
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_replace_failed_1"))
            .arg(QDir::toNativeSeparators(preservedPath));
    } else {
        *resultStatus = UiText::text(QStringLiteral("media_tools.batch_pv_replace_failed_1"))
            .arg(QDir::toNativeSeparators(tempPath));
    }
    return false;
}

}  // namespace

QString resolvePvCompressionFfmpegExecutable()
{
#ifdef Q_OS_WIN
    const QString ffmpegName = QStringLiteral("ffmpeg.exe");
#else
    const QString ffmpegName = QStringLiteral("ffmpeg");
#endif
    const QString envPath = qEnvironmentVariable(
        "MIACODE_FFMPEG_PATH",
        qEnvironmentVariable("MIACODE_FFMPEG"));
    if (isExecutableFile(envPath)) {
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
        if (isExecutableFile(candidate)) {
            return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
        }
    }
    const QString fromPath = QStandardPaths::findExecutable(ffmpegName);
    return isExecutableFile(fromPath)
        ? QDir::cleanPath(QFileInfo(fromPath).absoluteFilePath())
        : QString();
}

PvBatchCompressionWorker::PvBatchCompressionWorker(
    QList<PvCompressionJob> jobs,
    std::atomic_bool* cancelRequested)
    : jobs_(std::move(jobs))
    , cancelRequested_(cancelRequested)
{}

bool PvBatchCompressionWorker::isCanceled() const
{
    return cancelRequested_ != nullptr && cancelRequested_->load();
}

void PvBatchCompressionWorker::run()
{
    const bool needsFfmpeg = std::any_of(jobs_.cbegin(), jobs_.cend(), [](const PvCompressionJob& job) {
        return !job.videoPath.isEmpty() && job.originalBytes > kPvCompressionTargetBytes;
    });
    const QString ffmpegPath = needsFfmpeg ? resolvePvCompressionFfmpegExecutable() : QString();
    if (needsFfmpeg && ffmpegPath.isEmpty()) {
        const QString missingMessage = UiText::text(QStringLiteral("media_tools.batch_pv_ffmpeg_missing"));
        int failed = 0;
        for (int row = 0; row < jobs_.size(); ++row) {
            const PvCompressionJob& job = jobs_.at(row);
            if (job.videoPath.isEmpty()) {
                emit rowStatus(row, UiText::text(QStringLiteral("media_tools.batch_pv_no_video")));
            } else if (job.originalBytes <= kPvCompressionTargetBytes) {
                emit rowStatus(row, UiText::text(QStringLiteral("media_tools.batch_pv_already_small")));
            } else {
                emit rowStatus(
                    row,
                    UiText::text(QStringLiteral("media_tools.batch_pv_failed_1")).arg(missingMessage));
                ++failed;
            }
            emit progress(row + 1);
        }
        emit finished(0, failed, false, missingMessage);
        return;
    }

    int succeeded = 0;
    int failed = 0;
    for (int row = 0; row < jobs_.size() && !isCanceled(); ++row) {
        const PvCompressionJob& job = jobs_.at(row);
        if (job.videoPath.isEmpty()) {
            emit rowStatus(row, UiText::text(QStringLiteral("media_tools.batch_pv_no_video")));
            emit progress(row + 1);
            continue;
        }
        if (job.originalBytes <= kPvCompressionTargetBytes) {
            emit rowStatus(row, UiText::text(QStringLiteral("media_tools.batch_pv_already_small")));
            emit progress(row + 1);
            continue;
        }
        emit rowStatus(row, UiText::text(QStringLiteral("media_tools.batch_pv_compressing")));
        emit summary(UiText::text(QStringLiteral("media_tools.batch_pv_compressing_1")).arg(job.displayName));
        QString status;
        if (compressJob(ffmpegPath, job, cancelRequested_, &status)) {
            ++succeeded;
        } else if (!isCanceled()) {
            ++failed;
            status = UiText::text(QStringLiteral("media_tools.batch_pv_failed_1")).arg(status);
        }
        if (!status.isEmpty()) {
            emit rowStatus(row, status);
        }
        emit progress(row + 1);
    }
    emit finished(succeeded, failed, isCanceled(), QString());
}

}  // namespace miacode::media
