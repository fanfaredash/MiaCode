#include "VideoExportController.h"

#include "BassExportAudioBackend.h"
#include "LegacyExportAudioBackend.h"
#include "RawVideoPipeTransport.h"
#include "VideoExportAudioRenderPlan.h"
#include "VideoExportQuickRenderBackend.h"
#include "VideoExportRuntimePolicy.h"
#include "common/AssetPaths.h"
#include "common/ChartAssetPaths.h"
#include "common/IntroConfig.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "common/PreviewSfxTimeline.h"
#include "preview/runtime/PreviewSceneAssetLoader.h"
#include "tools/muri/MuriAnalyzer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "VideoExportControllerInternal.h"

// VideoExportPipeline.cpp — export logging helpers, image-media detection, audio-backend factory, pipe/file writers with backpressure, ffprobe summary, process-state logging, atomic output replacement, and progress-aware process waits.
//
// Definitions extracted verbatim from the original VideoExportController.cpp
// during the god-file split. All helpers live in the shared
// miacode::video_export::detail namespace (declared in
// VideoExportControllerInternal.h).
using namespace miacode::video_export::detail;

namespace miacode::video_export::detail {

QString normalizePath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

QString truncateForLog(const QString& text, int maxChars)
{
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars) + QStringLiteral(" ...<truncated>");
}

QString videoExportLogPath()
{
    return miacode::debug_log::exportLogPath();
}

bool exportDetailedLoggingEnabled()
{
    return miacode::debug_options::exportDebugOutputEnabled();
}

bool shouldWriteSummaryExportStage(const QString& stage)
{
    if (stage.startsWith(QStringLiteral("fail_")) || stage == QStringLiteral("canceled")) {
        return true;
    }

    static const QStringList kSummaryStages{
        QStringLiteral("export_begin"),
        QStringLiteral("resolve_ffmpeg"),
        QStringLiteral("resolve_ffprobe"),
        QStringLiteral("output_staging"),
        QStringLiteral("stage_static_media"),
        QStringLiteral("stage_static_media_fallback"),
        QStringLiteral("audio_backend_select"),
        QStringLiteral("audio_mix_ok"),
        QStringLiteral("encoder_select"),
        QStringLiteral("skin_bootstrap"),
        QStringLiteral("render_backend"),
        QStringLiteral("offscreen_warmup"),
        QStringLiteral("render_backend_fallback"),
        QStringLiteral("frame_timing_summary"),
        QStringLiteral("raw_pipe_summary"),
        QStringLiteral("encode_output_file"),
        QStringLiteral("ffmpeg_encode_started"),
        QStringLiteral("ffmpeg_remux_started"),
        QStringLiteral("export_file"),
        QStringLiteral("ffprobe_summary"),
        QStringLiteral("export_success"),
    };
    return kSummaryStages.contains(stage);
}

QString summarizedExportLogDetail(const QString& detail)
{
    QString normalized = detail.trimmed();
    if (normalized.isEmpty()) {
        return normalized;
    }
    normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
    normalized = normalized.simplified();
    return truncateForLog(normalized, 1200);
}

void appendVideoExportLog(const QString& stage, const QString& detail)
{
    if (exportDetailedLoggingEnabled()) {
        miacode::debug_log::appendLine(miacode::debug_log::Channel::Export, stage, detail);
    } else if (shouldWriteSummaryExportStage(stage)) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Export,
            stage,
            summarizedExportLogDetail(detail),
            true
        );
    }
    if (stage.startsWith(QStringLiteral("fail_"))) {
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/%1").arg(stage),
            detail.isEmpty() ? QStringLiteral("See export log for details.") : detail
        );
    }
}

bool isImageMediaPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("jpg")
        || suffix == QLatin1String("jpeg")
        || suffix == QLatin1String("png")
        || suffix == QLatin1String("bmp")
        || suffix == QLatin1String("webp");
}

std::unique_ptr<miacode::video_export::VideoExportAudioBackend> createExportAudioBackend(QString* errorMessage)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    auto backend = std::make_unique<miacode::video_export::BassExportAudioBackend>();
    QString reason;
    if (!backend->isSupported(&reason)) {
        if (errorMessage != nullptr) {
            *errorMessage = reason;
        }
        return {};
    }
    return backend;
#else
    auto backend = std::make_unique<miacode::video_export::LegacyExportAudioBackend>();
    QString reason;
    backend->isSupported(&reason);
    Q_UNUSED(reason);
    return backend;
#endif
}

bool writeAllToProcess(QProcess* process, const char* data, qint64 size, QString* failureDetail)
{
    constexpr qint64 kWriteChunkBytes = 1LL * 1024LL * 1024LL;
    constexpr qint64 kQueueHighWaterBytes = 4LL * 1024LL * 1024LL;
    constexpr qint64 kQueueLowWaterBytes = 1LL * 1024LL * 1024LL;

    if (process == nullptr || data == nullptr || size < 0) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid process write input");
        }
        return false;
    }
    qint64 writtenTotal = 0;
    while (writtenTotal < size) {
        while (process->bytesToWrite() > kQueueHighWaterBytes) {
            if (process->state() != QProcess::Running) {
                if (failureDetail != nullptr) {
                    *failureDetail = QStringLiteral("process exited while draining chunk queue after %1/%2 bytes")
                        .arg(writtenTotal)
                        .arg(size);
                }
                return false;
            }
            if (!process->waitForBytesWritten(30000)) {
                if (failureDetail != nullptr) {
                    *failureDetail = QStringLiteral("chunk queue drain timeout after %1/%2 bytes queued=%3")
                        .arg(writtenTotal)
                        .arg(size)
                        .arg(process->bytesToWrite());
                }
                return false;
            }
            if (process->bytesToWrite() <= kQueueLowWaterBytes) {
                break;
            }
        }

        const qint64 chunkBytes = qMin(kWriteChunkBytes, size - writtenTotal);
        const qint64 written = process->write(data + writtenTotal, chunkBytes);
        if (written < 0) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("write returned %1 after %2/%3 bytes")
                    .arg(written)
                    .arg(writtenTotal)
                    .arg(size);
            }
            return false;
        }
        if (written == 0) {
            if (!process->waitForBytesWritten(30000)) {
                if (failureDetail != nullptr) {
                    *failureDetail = QStringLiteral("waitForBytesWritten failed after %1/%2 bytes")
                        .arg(writtenTotal)
                        .arg(size);
                }
                return false;
            }
            continue;
        }
        writtenTotal += written;
    }
    return true;
}

ExportPipeBackpressurePlan chooseExportPipeBackpressurePlan(const QSize& frameSize)
{
    constexpr qint64 kHighWaterFloorBytes = 24LL * 1024LL * 1024LL;
    constexpr qint64 kLowWaterFloorBytes = 8LL * 1024LL * 1024LL;

    ExportPipeBackpressurePlan plan;
    const qint64 width = qMax(1, frameSize.width());
    const qint64 height = qMax(1, frameSize.height());
    plan.frameBytes = width * height * 4LL;
    plan.lowWaterBytes = qMax(kLowWaterFloorBytes, plan.frameBytes);
    plan.highWaterBytes = qMax(kHighWaterFloorBytes, plan.frameBytes * 3LL);
    if (plan.highWaterBytes <= plan.lowWaterBytes) {
        plan.highWaterBytes = plan.lowWaterBytes + plan.frameBytes;
    }
    return plan;
}

bool waitForProcessBackpressureDrain(
    QProcess* process,
    const ExportPipeBackpressurePlan& plan,
    ExportPipeBackpressureStats* stats,
    int frameIndex,
    qint64* waitNs,
    qint64* peakQueuedBytes,
    QString* failureDetail)
{
    if (waitNs != nullptr) {
        *waitNs = 0;
    }
    if (peakQueuedBytes != nullptr) {
        *peakQueuedBytes = 0;
    }
    if (process == nullptr) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid process for backpressure wait");
        }
        return false;
    }

    const qint64 initialQueuedBytes = process->bytesToWrite();
    if (initialQueuedBytes <= plan.highWaterBytes) {
        return true;
    }

    if (stats != nullptr) {
        ++stats->hitCount;
    }

    qint64 peakBytes = initialQueuedBytes;
    QElapsedTimer timer;
    timer.start();

    while (true) {
        if (process->state() != QProcess::Running) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral(
                                     "process exited during backpressure wait queued=%1 high=%2 low=%3")
                                     .arg(process->bytesToWrite())
                                     .arg(plan.highWaterBytes)
                                     .arg(plan.lowWaterBytes);
            }
            return false;
        }

        const qint64 queuedBytes = process->bytesToWrite();
        peakBytes = qMax(peakBytes, queuedBytes);
        if (queuedBytes <= plan.lowWaterBytes) {
            break;
        }

        if (timer.elapsed() >= plan.waitTimeoutMs) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral(
                                     "backpressure drain timeout queued=%1 high=%2 low=%3 timeoutMs=%4")
                                     .arg(queuedBytes)
                                     .arg(plan.highWaterBytes)
                                     .arg(plan.lowWaterBytes)
                                     .arg(plan.waitTimeoutMs);
            }
            return false;
        }

        process->waitForBytesWritten(plan.waitSliceMs);
    }

    const qint64 elapsedNs = timer.nsecsElapsed();
    if (waitNs != nullptr) {
        *waitNs = elapsedNs;
    }
    if (peakQueuedBytes != nullptr) {
        *peakQueuedBytes = peakBytes;
    }
    if (stats != nullptr) {
        stats->totalWaitNs += elapsedNs;
        if (elapsedNs > stats->maxWaitNs) {
            stats->maxWaitNs = elapsedNs;
            stats->maxWaitFrame = frameIndex;
        }
        if (peakBytes > stats->maxQueuedBytes) {
            stats->maxQueuedBytes = peakBytes;
            stats->maxQueuedFrame = frameIndex;
        }
    }
    return true;
}

bool writeAllToFile(QFile* file, const char* data, qint64 size)
{
    if (file == nullptr || data == nullptr || size < 0) {
        return false;
    }
    qint64 writtenTotal = 0;
    while (writtenTotal < size) {
        const qint64 written = file->write(data + writtenTotal, size - writtenTotal);
        if (written <= 0) {
            return false;
        }
        writtenTotal += written;
    }
    return true;
}

QString probeExportedVideoSummary(const QString& ffprobePath, const QString& outputPath)
{
    if (ffprobePath.isEmpty()) {
        return QStringLiteral("ffprobe_missing");
    }
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args{
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("v:0"),
        QStringLiteral("-show_entries"),
        QStringLiteral("stream=codec_name,pix_fmt,width,height,r_frame_rate,avg_frame_rate,time_base,nb_frames,duration"),
        QStringLiteral("-show_entries"),
        QStringLiteral("format=duration,size,bit_rate"),
        QStringLiteral("-of"),
        QStringLiteral("default=noprint_wrappers=1"),
        outputPath
    };
    probe.start(ffprobePath, args, QIODevice::ReadOnly);
    if (!probe.waitForStarted(5000)) {
        return QStringLiteral("ffprobe_start_failed error=%1").arg(probe.errorString());
    }
    constexpr int kProbeSliceMs = 100;
    constexpr int kProbeTimeoutMs = 5000;
    int elapsedMs = 0;
    while (probe.state() != QProcess::NotRunning && elapsedMs < kProbeTimeoutMs) {
        probe.waitForFinished(kProbeSliceMs);
        elapsedMs += kProbeSliceMs;
        QCoreApplication::processEvents();
    }
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished(2000);
        return QStringLiteral("ffprobe_timeout_or_failed error=%1").arg(probe.errorString());
    }
    const QString output = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        const QString exitInfo = QStringLiteral("ffprobe_nonzero status=%1 code=%2")
            .arg(static_cast<int>(probe.exitStatus()))
            .arg(probe.exitCode());
        if (output.isEmpty()) {
            return exitInfo;
        }
        return exitInfo + QStringLiteral(" output=") + truncateForLog(output, 2000);
    }
    return truncateForLog(output, 4000);
}

QString ffmpegBaseArgsLog(const QString& ffmpegPath, const QStringList& args)
{
    return QString("%1 %2").arg(ffmpegPath, args.join(' '));
}

QString qProcessStateForLog(QProcess::ProcessState state)
{
    switch (state) {
    case QProcess::NotRunning:
        return QStringLiteral("NotRunning");
    case QProcess::Starting:
        return QStringLiteral("Starting");
    case QProcess::Running:
        return QStringLiteral("Running");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(state));
}

QString qProcessExitStatusForLog(QProcess::ExitStatus status)
{
    switch (status) {
    case QProcess::NormalExit:
        return QStringLiteral("NormalExit");
    case QProcess::CrashExit:
        return QStringLiteral("CrashExit");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(status));
}

QString describeProcessForLog(const QProcess& process)
{
    return QStringLiteral("state=%1 exitStatus=%2 exitCode=%3 processError=%4 pid=%5")
        .arg(qProcessStateForLog(process.state()))
        .arg(qProcessExitStatusForLog(process.exitStatus()))
        .arg(process.exitCode())
        .arg(truncateForLog(process.errorString().trimmed(), 400))
        .arg(QString::number(process.processId()));
}

QString makeRemuxStageOutputPath(const QString& finalOutputPath)
{
    const QFileInfo outputInfo(finalOutputPath);
    const QString baseName = outputInfo.completeBaseName().isEmpty()
        ? QStringLiteral("miacode_export")
        : outputInfo.completeBaseName();
    const QString suffix = outputInfo.completeSuffix().isEmpty()
        ? QStringLiteral("mp4")
        : outputInfo.completeSuffix();
    return QDir(outputInfo.absolutePath()).filePath(
        QStringLiteral("%1.miacode_remux_%2.%3")
            .arg(baseName)
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
            .arg(suffix)
    );
}

QString processOutputAndErrorForLog(QProcess& process, int maxChars)
{
    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QString errorOutput = QString::fromUtf8(process.readAllStandardError()).trimmed();
    QStringList parts;
    if (!output.isEmpty()) {
        parts.append(QStringLiteral("stdout: %1").arg(truncateForLog(output, maxChars)));
    }
    if (!errorOutput.isEmpty()) {
        parts.append(QStringLiteral("stderr: %1").arg(truncateForLog(errorOutput, maxChars)));
    }
    if (parts.isEmpty()) {
        const QString processError = process.errorString().trimmed();
        if (!processError.isEmpty()) {
            parts.append(QStringLiteral("process_error: %1").arg(truncateForLog(processError, maxChars)));
        }
    }
    return parts.join(QStringLiteral("\n"));
}

bool waitForProcessWithProgress(
    QProcess& process,
    const QString& beginStage,
    const QString& doneStage,
    const QString& progressText,
    int progressPercent,
    const std::function<bool(int, const QString&)>& setProgressPercent,
    const QString& cancelDetail,
    VideoExportResult* result
)
{
    QElapsedTimer waitTimer;
    waitTimer.start();
    appendVideoExportLog(beginStage);
    while (process.state() != QProcess::NotRunning) {
        if (setProgressPercent(progressPercent, progressText)) {
            process.kill();
            process.waitForFinished(2000);
            if (result != nullptr) {
                result->message = QStringLiteral("canceled");
                result->details = withExportLogPath(result->details);
            }
            appendVideoExportLog(QStringLiteral("canceled"), cancelDetail);
            return false;
        }
        process.waitForFinished(80);
    }
    appendVideoExportLog(
        doneStage,
        QStringLiteral("exitStatus=%1 exitCode=%2 elapsedMs=%3")
            .arg(static_cast<int>(process.exitStatus()))
            .arg(process.exitCode())
            .arg(waitTimer.elapsed())
    );
    return true;
}

bool replaceOutputFileAtomicallyBestEffort(
    const QString& stagedPath,
    const QString& finalPath,
    QString* errorMessage
)
{
    if (stagedPath.isEmpty() || finalPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("staged or final output path is empty");
        }
        return false;
    }
    if (!QFileInfo::exists(stagedPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("staged remux output does not exist");
        }
        return false;
    }
    const QString backupPath = finalPath + QStringLiteral(".miacode_backup");
    QFile::remove(backupPath);
    const bool hadExistingFinal = QFileInfo::exists(finalPath);
    QFile finalFile(finalPath);
    QFile stagedFile(stagedPath);
    if (hadExistingFinal && !finalFile.rename(backupPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to move existing output aside");
        }
        return false;
    }
    if (stagedFile.rename(finalPath)) {
        QFile::remove(backupPath);
        return true;
    }
    const QString renameError = QStringLiteral("failed to promote staged remux output");
    if (hadExistingFinal) {
        QFile backupFile(backupPath);
        backupFile.rename(finalPath);
    }
    if (errorMessage != nullptr) {
        *errorMessage = renameError;
    }
    return false;
}


QString withExportLogPath(const QString& details)
{
    QStringList lines;
    if (!details.trimmed().isEmpty()) {
        lines.append(details);
    }
    lines.append(QStringLiteral("Export log: %1").arg(videoExportLogPath()));
    lines.append(QStringLiteral("Error log: %1").arg(miacode::debug_log::fatalLogPath()));
    return lines.join(QStringLiteral("\n"));
}


}  // namespace miacode::video_export::detail
