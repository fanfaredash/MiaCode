#include "runtime/export/VideoExportHost.h"
#include "app/v2/JobProgressService.h"
#include "runtime/Shared.h"
#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/shell/ShellHost.h"

#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/GpuDevicePolicy.h"
#include "common/OperationLog.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/video_export/VideoExportController.h"
#include "tools/video_export/VideoExportRuntimePolicy.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <optional>

using namespace miacode::runtime::shared;

namespace {
constexpr int kVideoExportWorkerDetailsMaxChars = 24000;
constexpr int kVideoExportWorkerStderrBufferMaxBytes = 24 * 1024;

QString appendVideoExportDiagnostics(const QString& base, const QString& extra);

QByteArray buildVideoExportWorkerStartPayload(const VideoExportSnapshot& snapshot)
{
    QJsonObject commandObject;
    commandObject.insert(QStringLiteral("cmd"), QStringLiteral("start_export"));
    commandObject.insert(QStringLiteral("protocol"), 1);
    commandObject.insert(QStringLiteral("snapshot"), snapshot.toJson());
    QByteArray payload = QJsonDocument(commandObject).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

bool shouldCurrentExportWorkerAttemptRequestPbo(bool forceDisableOffscreenPbo)
{
    if (forceDisableOffscreenPbo) {
        return false;
    }
    const std::optional<bool> enablePboOverride =
        miacode::debug_options::envOptionalFlagValue("MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO");
    const std::optional<bool> disablePboOverride =
        miacode::debug_options::envOptionalFlagValue("MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO");
    return miacode::video_export::shouldRequestOffscreenPboReadback(enablePboOverride, disablePboOverride);
}

QString combineWorkerAttemptDiagnostics(const QString& attemptDetails, const QString& workerDiagnostics)
{
    const QString trimmedAttemptDetails = attemptDetails.trimmed();
    const QString trimmedWorkerDiagnostics = workerDiagnostics.trimmed();
    if (trimmedAttemptDetails.isEmpty()) {
        return trimmedWorkerDiagnostics;
    }
    if (trimmedWorkerDiagnostics.isEmpty() || trimmedAttemptDetails == trimmedWorkerDiagnostics) {
        return trimmedAttemptDetails;
    }
    return appendVideoExportDiagnostics(trimmedAttemptDetails, trimmedWorkerDiagnostics);
}

QString buildWorkerRetryFailureDetails(
    const QString& retryNote,
    const QString& firstAttemptTitle,
    const QString& firstCrashDiagnostics,
    const QString& finalAttemptTitle,
    const QString& finalAttemptDiagnostics
)
{
    QStringList sections;
    if (!retryNote.trimmed().isEmpty()) {
        sections.append(retryNote.trimmed());
    }
    if (!firstCrashDiagnostics.trimmed().isEmpty()) {
        sections.append(QStringLiteral("%1:\n%2").arg(firstAttemptTitle.trimmed(), firstCrashDiagnostics.trimmed()));
    }
    if (!finalAttemptDiagnostics.trimmed().isEmpty()) {
        sections.append(QStringLiteral("%1:\n%2").arg(finalAttemptTitle.trimmed(), finalAttemptDiagnostics.trimmed()));
    }
    return sections.join(QStringLiteral("\n\n"));
}

QString resolvedLogPathOverrideForUi(const QString& overridePath, const QString& defaultFileName)
{
    const QString trimmed = overridePath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const bool looksLikeDirectory = trimmed.endsWith(QLatin1Char('/')) || trimmed.endsWith(QLatin1Char('\\'));
    const QString cleanPath = QDir::cleanPath(trimmed);
    const QFileInfo info(cleanPath);
    const bool looksLikeDirectoryName = !info.exists()
        && info.suffix().isEmpty()
        && !info.fileName().contains(QLatin1Char('.'));
    if (looksLikeDirectory || (info.exists() && info.isDir()) || looksLikeDirectoryName) {
        return QDir(cleanPath).filePath(defaultFileName);
    }
    return cleanPath;
}

QString projectLogDirectoryForSnapshot(const VideoExportSnapshot& snapshot)
{
    const QString chartPath = snapshot.originalChartPath.trimmed();
    if (!chartPath.isEmpty()) {
        const QString projectDataDirectoryPath = miacode::waveform::projectDataDirectoryPathForFile(chartPath);
        if (!projectDataDirectoryPath.isEmpty()) {
            return QDir(projectDataDirectoryPath).filePath(QStringLiteral("logs"));
        }
    }

    const QString projectDir = snapshot.projectDir.trimmed();
    if (!projectDir.isEmpty()) {
        return QDir(QDir::cleanPath(projectDir)).filePath(QStringLiteral(".miacode/logs"));
    }

    return QString();
}

QString workerChannelLogPathForUi(
    const VideoExportSnapshot& snapshot,
    const QString& channelOverridePath,
    const QString& defaultFileName,
    const QString& fallbackPath
)
{
    const QString overridePath = resolvedLogPathOverrideForUi(channelOverridePath, defaultFileName);
    if (!overridePath.isEmpty()) {
        return overridePath;
    }

    const QString sharedLogDirectory = qEnvironmentVariable("MIACODE_LOG_DIR").trimmed();
    if (!sharedLogDirectory.isEmpty()) {
        return QDir(QDir::cleanPath(sharedLogDirectory)).filePath(defaultFileName);
    }

    const QString projectLogDirectory = projectLogDirectoryForSnapshot(snapshot);
    if (!projectLogDirectory.isEmpty()) {
        return QDir(projectLogDirectory).filePath(defaultFileName);
    }

    return fallbackPath;
}

QString videoExportWorkerLogPathForUi(const VideoExportSnapshot& snapshot)
{
    return workerChannelLogPathForUi(
        snapshot,
        qEnvironmentVariable("MIACODE_EXPORT_LOG_PATH").trimmed(),
        QStringLiteral("miacode_video_export.log"),
        miacode::debug_log::exportLogPath()
    );
}

QString videoExportWorkerFatalLogPathForUi(const VideoExportSnapshot& snapshot)
{
    return workerChannelLogPathForUi(
        snapshot,
        qEnvironmentVariable("MIACODE_FATAL_LOG_PATH").trimmed(),
        QStringLiteral("miacode_fatal.log"),
        miacode::debug_log::fatalLogPath()
    );
}

QString qProcessExitStatusForUi(QProcess::ExitStatus status)
{
    switch (status) {
    case QProcess::NormalExit:
        return QStringLiteral("NormalExit");
    case QProcess::CrashExit:
        return QStringLiteral("CrashExit");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(status));
}

QString truncateProcessTextForUi(QString text, int maxChars = 2000)
{
    text = text.trimmed();
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars) + QStringLiteral(" ...<truncated>");
}

QString truncatedProcessTailMarker()
{
    return QStringLiteral("...[truncated earlier output]...\n");
}

QByteArray truncatedProcessTailMarkerBytes()
{
    return QByteArrayLiteral("...[truncated earlier output]...\n");
}

void trimProcessTextTail(QString* text, int maxChars)
{
    if (text == nullptr || maxChars <= 0 || text->size() <= maxChars) {
        return;
    }

    const QString marker = truncatedProcessTailMarker();
    QString body = *text;
    if (body.startsWith(marker)) {
        body.remove(0, marker.size());
    }
    const int keepChars = qMax(0, maxChars - marker.size());
    *text = marker + body.right(keepChars);
}

QString tailLimitedProcessText(QString text, int maxChars)
{
    trimProcessTextTail(&text, maxChars);
    return text;
}

void appendProcessDetailLine(QString* text, QString line, int maxChars)
{
    if (text == nullptr) {
        return;
    }
    line = line.trimmed();
    if (line.isEmpty()) {
        return;
    }
    if (!text->isEmpty()) {
        text->append(QLatin1Char('\n'));
    }
    text->append(line);
    trimProcessTextTail(text, maxChars);
}

void trimProcessOutputTail(QByteArray* buffer, int maxBytes)
{
    if (buffer == nullptr || maxBytes <= 0 || buffer->size() <= maxBytes) {
        return;
    }

    const QByteArray marker = truncatedProcessTailMarkerBytes();
    QByteArray body = *buffer;
    if (body.startsWith(marker)) {
        body.remove(0, marker.size());
    }
    const int keepBytes = qMax(0, maxBytes - marker.size());
    *buffer = marker + body.right(keepBytes);
}

void appendProcessOutputTail(QByteArray* buffer, const QByteArray& chunk, int maxBytes)
{
    if (buffer == nullptr || chunk.isEmpty()) {
        return;
    }
    buffer->append(chunk);
    trimProcessOutputTail(buffer, maxBytes);
}

QString appendVideoExportDiagnostics(const QString& base, const QString& extra)
{
    const QString trimmedBase = base.trimmed();
    const QString trimmedExtra = extra.trimmed();
    if (trimmedBase.isEmpty()) {
        return trimmedExtra;
    }
    if (trimmedExtra.isEmpty()) {
        return trimmedBase;
    }
    return trimmedBase + QStringLiteral("\n\n") + trimmedExtra;
}

QString buildWorkerProcessDiagnostics(
    int exitCode,
    QProcess::ExitStatus exitStatus,
    const QString& processError,
    const QString& stderrText,
    const QString& stdoutTailText,
    const QString& exportLogPath,
    const QString& fatalLogPath
)
{
    QStringList lines;
    lines.append(
        QStringLiteral("Worker exitCode=%1 exitStatus=%2")
            .arg(exitCode)
            .arg(qProcessExitStatusForUi(exitStatus))
    );
    if (!processError.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Process error: %1").arg(truncateProcessTextForUi(processError, 500)));
    }
    if (!stderrText.trimmed().isEmpty()) {
        lines.append(QStringLiteral("stderr: %1").arg(truncateProcessTextForUi(stderrText, 2000)));
    }
    if (!stdoutTailText.trimmed().isEmpty()) {
        lines.append(QStringLiteral("stdout_tail: %1").arg(truncateProcessTextForUi(stdoutTailText, 1000)));
    }
    if (!exportLogPath.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Export log: %1").arg(exportLogPath));
    }
    if (!fatalLogPath.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Error log: %1").arg(fatalLogPath));
    }
    return lines.join(QStringLiteral("\n"));
}

QString compactWorkerExitSummary(
    int exitCode,
    QProcess::ExitStatus exitStatus,
    const QString& fallbackMessage
)
{
    return QStringLiteral("%1 [%2, code=%3]")
        .arg(fallbackMessage)
        .arg(qProcessExitStatusForUi(exitStatus))
        .arg(exitCode);
}

QString normalizeLanguageToken(QString token)
{
    token = token.trimmed().toLower();
    token.replace('-', '_');
    return token;
}

bool systemLanguagePrefersChinese()
{
    static const bool prefersChinese = []() {
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString& language : uiLanguages) {
            const QString token = normalizeLanguageToken(language);
            if (token.startsWith(QStringLiteral("zh"))) {
                return true;
            }
            if (token.startsWith(QStringLiteral("en"))) {
                return false;
            }
        }
        return normalizeLanguageToken(QLocale::system().name()).startsWith(QStringLiteral("zh"));
    }();
    return prefersChinese;
}

QString systemL10n(const QString& en, const QString& zh)
{
    return systemLanguagePrefersChinese() ? zh : en;
}

QString formatExportRemainingDuration(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0) {
        return QStringLiteral("%1m %2s").arg(minutes).arg(secs);
    }
    return QStringLiteral("%1s").arg(secs);
}


QString localizeExportWorkerMessageForSystemLanguage(const QString& rawMessage)
{
    const QString trimmed = rawMessage.trimmed();
    if (trimmed.isEmpty()) {
        return trimmed;
    }

    static const QRegularExpression renderProgressPattern(
        QStringLiteral("^Rendering frames and encoding\\.\\.\\.\\s+(\\d+)/(\\d+)$")
    );
    const QRegularExpressionMatch renderMatch = renderProgressPattern.match(trimmed);
    if (renderMatch.hasMatch()) {
        return QStringLiteral("Rendering frames... %1/%2").arg(renderMatch.captured(1), renderMatch.captured(2));
    }

    if (trimmed == QLatin1String("Preparing SFX track...")) {
        return QStringLiteral("Preparing audio...");
    }
    if (trimmed == QLatin1String("Starting ffmpeg...")) {
        return QStringLiteral("Starting ffmpeg...");
    }
    if (trimmed == QLatin1String("Rendering frames and encoding...")) {
        return QStringLiteral("Rendering frames...");
    }
    if (trimmed == QLatin1String("Repacking MP4 for fast start...")) {
        return QStringLiteral("Finalizing video...");
    }
    if (trimmed == QLatin1String("Collecting export summary...")) {
        return QStringLiteral("Finishing up...");
    }
    if (trimmed == QLatin1String("Export completed.")) {
        return QStringLiteral("Done.");
    }
    return rawMessage;
}


bool exportWorkerProgressUsesBusyIndicator(const QString& rawMessage)
{
    const QString trimmed = rawMessage.trimmed();
    return trimmed == QLatin1String("Finalizing encoded video stream...")
        || trimmed == QLatin1String("Repacking MP4 for fast start...")
        || trimmed == QLatin1String("Collecting export summary...")
        || trimmed == QLatin1String("Export completed.");
}

QString localizeExportWorkerMessageForUiLanguage(const QString& rawMessage)
{
    const QString trimmed = rawMessage.trimmed();
    if (trimmed.isEmpty()) {
        return trimmed;
    }

    static const QRegularExpression renderProgressPattern(
        QStringLiteral("^Rendering frames and encoding\\.\\.\\.\\s+(\\d+)/(\\d+)$")
    );
    const QRegularExpressionMatch renderMatch = renderProgressPattern.match(trimmed);
    if (renderMatch.hasMatch()) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.rendering_count"))
            .arg(renderMatch.captured(1), renderMatch.captured(2));
    }

    if (trimmed == QLatin1String("Preparing SFX track...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.preparing_audio"));
    }
    if (trimmed == QLatin1String("Starting ffmpeg...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.starting_ffmpeg"));
    }
    if (trimmed == QLatin1String("Rendering frames and encoding...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.rendering"));
    }
    if (trimmed == QLatin1String("Finalizing encoded video stream...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.finalizing_encode"));
    }
    if (trimmed == QLatin1String("Repacking MP4 for fast start...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.repacking"));
    }
    if (trimmed == QLatin1String("Collecting export summary...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.finishing"));
    }
    if (trimmed == QLatin1String("Export completed.")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.done"));
    }
    return rawMessage;
}

QString buildExportProgressLabelTextForUiLanguage(
    const QString& rawMessage,
    int percent,
    const QElapsedTimer& elapsed,
    qint64* smoothedEtaSeconds
)
{
    QString text = localizeExportWorkerMessageForUiLanguage(rawMessage.trimmed());
    if (text.isEmpty()) {
        text = UiText::text(QStringLiteral("dialog.video_export.progress.generic"));
    }
    if (exportWorkerProgressUsesBusyIndicator(rawMessage)) {
        if (smoothedEtaSeconds != nullptr) {
            *smoothedEtaSeconds = -1;
        }
        return text;
    }
    if (!elapsed.isValid() || percent < 5) {
        return text;
    }

    const qint64 elapsedMs = elapsed.elapsed();
    if (elapsedMs < 1500) {
        return text;
    }

    const double totalMs = (static_cast<double>(elapsedMs) * 100.0) / static_cast<double>(percent);
    const qint64 estimatedRemainingSeconds =
        qRound64(qMax(0.0, totalMs - static_cast<double>(elapsedMs)) / 1000.0);
    if (estimatedRemainingSeconds <= 0) {
        return text;
    }

    qint64 displayEtaSeconds = estimatedRemainingSeconds;
    if (smoothedEtaSeconds != nullptr) {
        if (*smoothedEtaSeconds >= 0) {
            displayEtaSeconds = qRound64(
                (static_cast<double>(*smoothedEtaSeconds) * 2.0 + estimatedRemainingSeconds) / 3.0
            );
        }
        *smoothedEtaSeconds = displayEtaSeconds;
    }

    const QString etaLine = UiText::text(QStringLiteral("dialog.video_export.progress.remaining"))
        .arg(formatExportRemainingDuration(displayEtaSeconds));
    return QStringLiteral("%1\n%2").arg(text, etaLine);
}


}  // namespace

bool miacode::runtime::VideoExportHost::startVideoExportWorkerProcess(
    QProcess* process,
    const VideoExportSnapshot& snapshot,
    QString* errorMessage,
    bool forceDisableOffscreenPbo
)
{
    MC_OP("miacode::runtime::VideoExportHost::startVideoExportWorkerProcess");
    _mc_op_.note(QStringLiteral("output=%1 disable_pbo=%2")
                     .arg(snapshot.outputPath)
                     .arg(forceDisableOffscreenPbo ? 1 : 0));
    if (process == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker process is null");
        }
        _mc_op_.fail(QStringLiteral("null QProcess"));
        return false;
    }

    const QString executablePath = QCoreApplication::applicationFilePath();
    if (executablePath.trimmed().isEmpty() || !QFileInfo::exists(executablePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.video_export.error.executable_missing"));
        }
        _mc_op_.fail(QStringLiteral("executable not found at %1").arg(executablePath));
        return false;
    }

    process->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment workerEnvironment = QProcessEnvironment::systemEnvironment();
    if (!workerEnvironment.contains(QStringLiteral("MIACODE_LOG_DIR"))) {
        const QString projectLogDirectory = projectLogDirectoryForSnapshot(snapshot);
        if (!projectLogDirectory.isEmpty()) {
            workerEnvironment.insert(QStringLiteral("MIACODE_LOG_DIR"), projectLogDirectory);
        }
    }
    if (forceDisableOffscreenPbo) {
        workerEnvironment.insert(QStringLiteral("MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO"), QStringLiteral("1"));
    }
    // P3 — forward the resolved GPU device-policy request (raw form) so the
    // export worker resolves + logs the same high-performance adapter as the
    // GUI. Via env rather than argv because the worker's CLI parser is strict
    // about unknown options; no-op for the plain default policy.
    miacode::gpu::applyGpuPolicyToChildEnvironment(
        workerEnvironment, miacode::gpu::resolveGpuPolicyOnce());
    process->setProcessEnvironment(workerEnvironment);
    QStringList workerArgs;
    if (miacode::debug_options::debugModeEnabled()) {
        workerArgs.append(QStringLiteral("--debug"));
    }
    workerArgs.append(QStringLiteral("--export-video-worker"));
    process->start(executablePath, workerArgs, QIODevice::ReadWrite);
    if (!process->waitForStarted(5000)) {
        if (errorMessage != nullptr) {
            *errorMessage = process->errorString();
        }
        _mc_op_.fail(QStringLiteral("waitForStarted timeout: %1").arg(process->errorString()));
        return false;
    }

    const QByteArray payload = buildVideoExportWorkerStartPayload(snapshot);
    if (process->write(payload) != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.video_export.error.worker_write_failed"));
        }
        _mc_op_.fail(QStringLiteral("stdin write short: bytes=%1").arg(payload.size()));
        process->kill();
        process->waitForFinished(1000);
        return false;
    }
    process->closeWriteChannel();
    return true;
}

bool miacode::runtime::VideoExportHost::runVideoExportWorkerSync(
    const VideoExportSnapshot& snapshot,
    bool* canceledByUser,
    QString* errorMessage,
    const std::function<void(int percent, const QString& rawMessage)>& progressCallback,
    const std::function<bool()>& cancellationRequested,
    const std::function<void()>& retryingCallback
)
{
    MC_OP("miacode::runtime::VideoExportHost::runVideoExportWorkerSync");
    _mc_op_.note(QStringLiteral("output=%1 size=%2x%3 fps=%4")
                     .arg(snapshot.outputPath)
                     .arg(snapshot.outputWidth)
                     .arg(snapshot.outputHeight)
                     .arg(snapshot.fps));
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (canceledByUser != nullptr) {
        *canceledByUser = false;
    }

    QString firstCrashDiagnostics;
    bool forceDisableOffscreenPbo = false;
    for (int attempt = 1;; ++attempt) {
        QProcess process;
        if (!this->startVideoExportWorkerProcess(&process, snapshot, errorMessage, forceDisableOffscreenPbo)) {
            if (errorMessage != nullptr && !firstCrashDiagnostics.trimmed().isEmpty()) {
                *errorMessage = buildWorkerRetryFailureDetails(
                    UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_note")),
                    UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_first_attempt")),
                    firstCrashDiagnostics,
                    UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_final_attempt")),
                    *errorMessage
                );
            }
            return false;
        }

        QByteArray stdoutBuffer;
        QByteArray stderrBuffer;
        bool finishedEventReceived = false;
        bool success = false;
        QString resultMessage;
        QString resultDetails;
        QElapsedTimer itemElapsed;
        qint64 smoothedEtaSeconds = -1;
        itemElapsed.start();

        const auto parseStdoutLines = [&]() {
            while (true) {
                const int newlineIndex = stdoutBuffer.indexOf('\n');
                if (newlineIndex < 0) {
                    break;
                }
                const QByteArray rawLine = stdoutBuffer.left(newlineIndex).trimmed();
                stdoutBuffer.remove(0, newlineIndex + 1);
                if (rawLine.isEmpty()) {
                    continue;
                }
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(rawLine, &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                    appendProcessDetailLine(&resultDetails, QString::fromUtf8(rawLine), kVideoExportWorkerDetailsMaxChars);
                    continue;
                }
                const QJsonObject object = document.object();
                const QString eventType = object.value(QStringLiteral("event")).toString();
                if (eventType == QLatin1String("progress")) {
                    const int percent = object.value(QStringLiteral("percent")).toInt(-1);
                    const QString message = object.value(QStringLiteral("message")).toString();
                    if (progressCallback) {
                        progressCallback(percent, message);
                    }
                    continue;
                }
                if (eventType == QLatin1String("finished")) {
                    finishedEventReceived = true;
                    success = object.value(QStringLiteral("success")).toBool(false);
                    resultMessage = object.value(QStringLiteral("message")).toString();
                    resultDetails = tailLimitedProcessText(
                        object.value(QStringLiteral("details")).toString(),
                        kVideoExportWorkerDetailsMaxChars);
                }
            }
        };

        while (process.state() != QProcess::NotRunning) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            stdoutBuffer.append(process.readAllStandardOutput());
            appendProcessOutputTail(
                &stderrBuffer,
                process.readAllStandardError(),
                kVideoExportWorkerStderrBufferMaxBytes);
            parseStdoutLines();
            if (cancellationRequested && cancellationRequested()) {
                if (canceledByUser != nullptr) {
                    *canceledByUser = true;
                }
                process.kill();
                process.waitForFinished(2000);
                return false;
            }
            process.waitForFinished(50);
        }

        stdoutBuffer.append(process.readAllStandardOutput());
        appendProcessOutputTail(
            &stderrBuffer,
            process.readAllStandardError(),
            kVideoExportWorkerStderrBufferMaxBytes);
        parseStdoutLines();

        const QString stderrText = QString::fromUtf8(stderrBuffer).trimmed();
        const QString stdoutTailText = QString::fromUtf8(stdoutBuffer).trimmed();
        const QString processErrorText = process.errorString().trimmed();
        const QString workerExportLogPath = videoExportWorkerLogPathForUi(snapshot);
        const QString workerFatalLogPath = videoExportWorkerFatalLogPathForUi(snapshot);
        const QString workerDiagnostics = buildWorkerProcessDiagnostics(
            process.exitCode(),
            process.exitStatus(),
            processErrorText,
            stderrText,
            stdoutTailText,
            workerExportLogPath,
            workerFatalLogPath
        );
        const QString attemptDiagnostics = combineWorkerAttemptDiagnostics(resultDetails, workerDiagnostics);
        const bool shouldRetry = miacode::video_export::shouldRetryVideoExportWorkerAfterCrash(
            process.exitStatus() == QProcess::CrashExit,
            shouldCurrentExportWorkerAttemptRequestPbo(forceDisableOffscreenPbo),
            canceledByUser != nullptr && *canceledByUser,
            attempt
        );
        if (shouldRetry) {
            firstCrashDiagnostics = attemptDiagnostics;
            forceDisableOffscreenPbo = true;
            if (retryingCallback) {
                retryingCallback();
            }
            continue;
        }

        const QString combinedRetryDiagnostics = firstCrashDiagnostics.trimmed().isEmpty()
            ? QString()
            : buildWorkerRetryFailureDetails(
                  UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_note")),
                  UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_first_attempt")),
                  firstCrashDiagnostics,
                  UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_final_attempt")),
                  attemptDiagnostics
              );
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            if (errorMessage != nullptr) {
                const QString summary = !resultMessage.trimmed().isEmpty()
                    ? resultMessage
                    : (!stderrText.isEmpty()
                        ? stderrText.split('\n').constFirst().trimmed()
                        : UiText::text(QStringLiteral("dialog.batch_export.error.export_failed")));
                const QString exitSummary = compactWorkerExitSummary(process.exitCode(), process.exitStatus(), summary);
                *errorMessage = combinedRetryDiagnostics.isEmpty()
                    ? exitSummary
                    : appendVideoExportDiagnostics(exitSummary, combinedRetryDiagnostics);
            }
            return false;
        }

        if (!finishedEventReceived || !success) {
            if (errorMessage != nullptr) {
                const QString summary = !resultMessage.trimmed().isEmpty()
                    ? resultMessage
                    : (!stderrText.isEmpty()
                        ? stderrText.split('\n').constFirst().trimmed()
                        : UiText::text(QStringLiteral("dialog.batch_export.error.export_failed")));
                const bool genericFailure = resultMessage.trimmed().isEmpty() && stderrText.trimmed().isEmpty();
                if (!combinedRetryDiagnostics.isEmpty()) {
                    *errorMessage = appendVideoExportDiagnostics(summary, combinedRetryDiagnostics);
                } else {
                    *errorMessage = genericFailure
                        ? appendVideoExportDiagnostics(summary, workerDiagnostics)
                        : summary;
                }
            }
            return false;
        }

        return true;
    }
}

bool miacode::runtime::VideoExportHost::launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    MC_OP("miacode::runtime::VideoExportHost::launchVideoExportWorker");
    _mc_op_.note(QStringLiteral("output=%1").arg(snapshot.outputPath));
    if (session_.videoExportWorkerProcess_ != nullptr && session_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.video_export.error.worker_busy"));
        }
        _mc_op_.fail(QStringLiteral("worker already running"));
        return false;
    }

    this->clearVideoExportWorkerState();

    // One progress surface for every job in the shell (JobProgressOverlay).
    // The worker runs out of process, so cancellation goes through
    // cancelVideoExportWorker rather than a flag this side polls.
    if (miacode::v2::JobProgressService* const jobProgress = session_.jobProgressService();
        jobProgress != nullptr) {
        session_.videoExportJobToken_ = jobProgress->begin(
            UiText::text(QStringLiteral("dialog.video_export.title")),
            UiText::text(QStringLiteral("dialog.video_export.progress.preparing")),
            /*cancellable=*/true);
    }

    auto* process = new QProcess(&session_);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    session_.videoExportWorkerProcess_ = process;
    session_.videoExportWorkerSnapshot_ = snapshot;
    session_.videoExportWorkerJobId_ = snapshot.jobId;
    session_.videoExportWorkerOutputPath_ = snapshot.outputPath;
    session_.videoExportWorkerExportLogPath_ = videoExportWorkerLogPathForUi(snapshot);
    session_.videoExportWorkerFatalLogPath_ = videoExportWorkerFatalLogPathForUi(snapshot);
    session_.videoExportWorkerResultMessage_.clear();
    session_.videoExportWorkerResultDetails_.clear();
    session_.videoExportWorkerFirstCrashDiagnostics_.clear();
    session_.videoExportWorkerStdoutBuffer_.clear();
    session_.videoExportWorkerStderrBuffer_.clear();
    session_.videoExportWorkerSuccess_ = false;
    session_.videoExportWorkerCompletionReceived_ = false;
    session_.videoExportWorkerCancelRequested_ = false;
    session_.videoExportWorkerLastProgressPercent_ = 0;
    session_.videoExportWorkerLastEtaSeconds_ = -1;
    session_.videoExportWorkerAttempt_ = 1;
    session_.videoExportWorkerForceDisablePbo_ = false;
    session_.videoExportWorkerElapsed_.start();

    QObject::connect(process, &QProcess::readyReadStandardOutput, &session_, [this]() {
        this->handleVideoExportWorkerStdout();
    });
    QObject::connect(process, &QProcess::readyReadStandardError, &session_, [this]() {
        this->handleVideoExportWorkerStderr();
    });
    QObject::connect(process, &QProcess::finished, &session_, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        this->handleVideoExportWorkerProcessFinished(exitCode, static_cast<int>(exitStatus));
    });

    if (!this->startVideoExportWorkerProcess(
            process,
            snapshot,
            errorMessage,
            session_.videoExportWorkerForceDisablePbo_)) {
        this->clearVideoExportWorkerState();
        return false;
    }
    emit session_.videoExportWorkerRunningChanged(true);
    return true;
}

void miacode::runtime::VideoExportHost::handleVideoExportWorkerStdout()
{
    if (session_.videoExportWorkerProcess_ == nullptr) {
        return;
    }
    session_.videoExportWorkerStdoutBuffer_.append(session_.videoExportWorkerProcess_->readAllStandardOutput());
    while (true) {
        const int newlineIndex = session_.videoExportWorkerStdoutBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }
        const QByteArray rawLine = session_.videoExportWorkerStdoutBuffer_.left(newlineIndex).trimmed();
        session_.videoExportWorkerStdoutBuffer_.remove(0, newlineIndex + 1);
        if (rawLine.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(rawLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            appendProcessDetailLine(
                &session_.videoExportWorkerResultDetails_,
                QString::fromUtf8(rawLine),
                kVideoExportWorkerDetailsMaxChars);
            continue;
        }
        this->handleVideoExportWorkerEvent(document.object());
    }
}

void miacode::runtime::VideoExportHost::handleVideoExportWorkerStderr()
{
    if (session_.videoExportWorkerProcess_ == nullptr) {
        return;
    }
    appendProcessOutputTail(
        &session_.videoExportWorkerStderrBuffer_,
        session_.videoExportWorkerProcess_->readAllStandardError(),
        kVideoExportWorkerStderrBufferMaxBytes);
}

void miacode::runtime::VideoExportHost::handleVideoExportWorkerEvent(const QJsonObject& eventObject)
{
    MC_OP("miacode::runtime::VideoExportHost::handleVideoExportWorkerEvent");
    const QString eventType = eventObject.value(QStringLiteral("event")).toString();
    _mc_op_.note(QStringLiteral("event=%1").arg(eventType));
    const bool suppressProgressUi = session_.videoExportWorkerCancelRequested_;
    if (eventType == QLatin1String("worker_ready")) {
        if (!suppressProgressUi) {
            this->reportExportProgress(
                1, UiText::text(QStringLiteral("dialog.video_export.progress.worker_ready")));
        }
        return;
    }
    if (eventType == QLatin1String("accepted")) {
        if (!suppressProgressUi) {
            this->reportExportProgress(
                2, UiText::text(QStringLiteral("dialog.video_export.progress.starting_export")));
        }
        return;
    }
    if (eventType == QLatin1String("progress")) {
        if (suppressProgressUi) {
            return;
        }
        const int percent = qBound(0, eventObject.value(QStringLiteral("percent")).toInt(), 100);
        const QString rawMessage = eventObject.value(QStringLiteral("message")).toString(
            QStringLiteral("Exporting...")
        );
        const bool busyStage = exportWorkerProgressUsesBusyIndicator(rawMessage);
        if (!busyStage) {
            session_.videoExportWorkerLastProgressPercent_ =
                qMax(session_.videoExportWorkerLastProgressPercent_, percent);
        }
        // Built ONCE per event — the builder advances the rolling ETA estimate.
        const QString progressLabel = buildExportProgressLabelTextForUiLanguage(
            rawMessage,
            session_.videoExportWorkerLastProgressPercent_,
            session_.videoExportWorkerElapsed_,
            &session_.videoExportWorkerLastEtaSeconds_
        );
        this->reportExportProgress(
            busyStage ? -1 : session_.videoExportWorkerLastProgressPercent_, progressLabel);
        return;
    }
    if (eventType == QLatin1String("log")) {
        const QString message = eventObject.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            appendProcessDetailLine(
                &session_.videoExportWorkerResultDetails_,
                message,
                kVideoExportWorkerDetailsMaxChars);
        }
        return;
    }
    if (eventType == QLatin1String("finished")) {
        session_.videoExportWorkerCompletionReceived_ = true;
        session_.videoExportWorkerSuccess_ = eventObject.value(QStringLiteral("success")).toBool(false);
        session_.videoExportWorkerOutputPath_ = eventObject.value(QStringLiteral("output_path")).toString(session_.videoExportWorkerOutputPath_);
        session_.videoExportWorkerResultMessage_ = session_.videoExportWorkerSuccess_
            ? QStringLiteral("ok")
            : eventObject.value(QStringLiteral("error")).toString(UiText::text(QStringLiteral("dialog.video_export.error.failed")));
        const QString details = eventObject.value(QStringLiteral("details")).toString().trimmed();
        if (!details.isEmpty()) {
            session_.videoExportWorkerResultDetails_ =
                tailLimitedProcessText(details, kVideoExportWorkerDetailsMaxChars);
        }
        if (!suppressProgressUi && session_.videoExportWorkerSuccess_) {
            this->reportExportProgress(
                100, UiText::text(QStringLiteral("dialog.video_export.progress.done")));
        }
    }
}

void miacode::runtime::VideoExportHost::handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus)
{
    MC_OP("miacode::runtime::VideoExportHost::handleVideoExportWorkerProcessFinished");
    _mc_op_.note(QStringLiteral("exit_code=%1 status=%2").arg(exitCode).arg(exitStatus));
    Q_UNUSED(exitCode);

    const auto restorePreviewAspectIfNeeded = [this]() {
        if (!session_.restoreSquareAfterVideoExport_) {
            return;
        }
        session_.restoreSquareAfterVideoExport_ = false;
        session_.setPreviewCanvasAspectRatio(1.0, false);
    };

    const QString retryNote = UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_note"));
    const QString firstAttemptTitle = UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_first_attempt"));
    const QString finalAttemptTitle = UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_final_attempt"));
    const QString stderrText = QString::fromUtf8(session_.videoExportWorkerStderrBuffer_).trimmed();
    const QString stdoutTailText = QString::fromUtf8(session_.videoExportWorkerStdoutBuffer_).trimmed();
    const QString processErrorText = session_.videoExportWorkerProcess_ != nullptr
        ? session_.videoExportWorkerProcess_->errorString().trimmed()
        : QString();
    const QString workerDiagnostics = buildWorkerProcessDiagnostics(
        exitCode,
        static_cast<QProcess::ExitStatus>(exitStatus),
        processErrorText,
        stderrText,
        stdoutTailText,
        session_.videoExportWorkerExportLogPath_,
        session_.videoExportWorkerFatalLogPath_
    );
    const QString attemptDiagnostics =
        combineWorkerAttemptDiagnostics(session_.videoExportWorkerResultDetails_, workerDiagnostics);
    const bool canceledOutcome =
        session_.videoExportWorkerCancelRequested_
        && (!session_.videoExportWorkerCompletionReceived_ || !session_.videoExportWorkerSuccess_);
    if (canceledOutcome) {
        this->endExportProgress();
        restorePreviewAspectIfNeeded();
        // Through the QML request boundary, like every other export outcome.
        // This one used to be a QMessageBox parented to the hidden window, so a
        // v2 user cancelling a single export got a Qt-styled box while
        // cancelling a BATCH export got the app's own notice — same action, two
        // different dialogs.
        session_.applicationServices_.uiRequests().postNotice(
            miacode::v2::NoticeSeverity::Information,
            UiText::text(QStringLiteral("dialog.video_export.title")),
            UiText::text(QStringLiteral("dialog.video_export.message.canceled")));
        this->clearVideoExportWorkerState();
        return;
    }

    bool retryRestartFailed = false;
    QString retryRestartError;
    const bool shouldRetry = miacode::video_export::shouldRetryVideoExportWorkerAfterCrash(
        exitStatus == static_cast<int>(QProcess::CrashExit),
        shouldCurrentExportWorkerAttemptRequestPbo(session_.videoExportWorkerForceDisablePbo_),
        session_.videoExportWorkerCancelRequested_,
        session_.videoExportWorkerAttempt_
    );
    if (shouldRetry) {
        session_.videoExportWorkerFirstCrashDiagnostics_ = attemptDiagnostics;
        session_.videoExportWorkerAttempt_ += 1;
        session_.videoExportWorkerForceDisablePbo_ = true;
        session_.videoExportWorkerCompletionReceived_ = false;
        session_.videoExportWorkerSuccess_ = false;
        session_.videoExportWorkerResultMessage_.clear();
        session_.videoExportWorkerResultDetails_.clear();
        session_.videoExportWorkerStdoutBuffer_.clear();
        session_.videoExportWorkerStderrBuffer_.clear();
        session_.videoExportWorkerLastProgressPercent_ = 0;
        session_.videoExportWorkerLastEtaSeconds_ = -1;
        session_.videoExportWorkerElapsed_.start();
        this->reportExportProgress(
            0,
            UiText::text(QStringLiteral("dialog.video_export.progress.retrying_safe_mode")));

        QString restartError;
        if (session_.videoExportWorkerProcess_ != nullptr
            && this->startVideoExportWorkerProcess(
                session_.videoExportWorkerProcess_,
                session_.videoExportWorkerSnapshot_,
                &restartError,
                true)) {
            return;
        }

        retryRestartFailed = true;
        retryRestartError = restartError.trimmed();
    }

    this->endExportProgress();

    QString finalAttemptDiagnostics;
    if (retryRestartFailed) {
        session_.videoExportWorkerSuccess_ = false;
        session_.videoExportWorkerCompletionReceived_ = false;
        session_.videoExportWorkerResultMessage_ =
            UiText::text(QStringLiteral("dialog.video_export.error.failed"));
        finalAttemptDiagnostics = retryRestartError;
        session_.videoExportWorkerResultDetails_ = retryRestartError;
    } else if (!session_.videoExportWorkerCompletionReceived_) {
        session_.videoExportWorkerSuccess_ = false;
        session_.videoExportWorkerResultMessage_ = exitStatus == static_cast<int>(QProcess::CrashExit)
            ? UiText::text(QStringLiteral("dialog.video_export.error.worker_crash"))
            : UiText::text(QStringLiteral("dialog.video_export.error.worker_exit"));
        finalAttemptDiagnostics = workerDiagnostics;
        session_.videoExportWorkerResultDetails_ = finalAttemptDiagnostics;
    } else if (!session_.videoExportWorkerSuccess_) {
        finalAttemptDiagnostics = attemptDiagnostics;
        session_.videoExportWorkerResultDetails_ = attemptDiagnostics;
    }
    if (!session_.videoExportWorkerSuccess_ && !session_.videoExportWorkerFirstCrashDiagnostics_.trimmed().isEmpty()) {
        session_.videoExportWorkerResultDetails_ = buildWorkerRetryFailureDetails(
            retryNote,
            firstAttemptTitle,
            session_.videoExportWorkerFirstCrashDiagnostics_,
            finalAttemptTitle,
            finalAttemptDiagnostics
        );
    }
    trimProcessTextTail(&session_.videoExportWorkerResultDetails_, kVideoExportWorkerDetailsMaxChars);
    if (!session_.videoExportWorkerSuccess_) {
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/worker"),
            QStringLiteral("%1 | %2")
                .arg(session_.videoExportWorkerResultMessage_.trimmed(), session_.videoExportWorkerResultDetails_.trimmed())
        );
    }

    if (session_.videoExportWorkerSuccess_) {
        const QFileInfo resolvedOutputInfo(session_.videoExportWorkerOutputPath_);
        const QString resolvedOutputName = resolvedOutputInfo.fileName().trimmed().isEmpty()
            ? QDir::toNativeSeparators(session_.videoExportWorkerOutputPath_)
            : resolvedOutputInfo.fileName();
        // 打开 opens the containing folder, which is what the Widgets button
        // did — the label says 打开 but it has always been the folder.
        const QString outputPath = session_.videoExportWorkerOutputPath_;
        session_.applicationServices_.uiRequests().requestNoticeAction(
            miacode::v2::NoticeSeverity::Information,
            UiText::text(QStringLiteral("dialog.video_export.title")),
            QStringLiteral("%1\n\n%2")
                .arg(UiText::text(QStringLiteral("dialog.video_export.message.completed")))
                .arg(resolvedOutputName),
            QString(),
            UiText::text(QStringLiteral("action.open")),
            [outputPath](bool actionChosen) {
                if (!actionChosen) {
                    return;
                }
                const QString outputDir = QFileInfo(outputPath).absoluteDir().absolutePath();
                if (!outputDir.isEmpty()) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
                }
            });
    } else {
        const QString details = session_.videoExportWorkerResultDetails_.trimmed();
        const QString failureTitle =
            UiText::text(QStringLiteral("dialog.video_export.error.failed_title"));
        const QString failureText = session_.videoExportWorkerResultMessage_;
        if (session_.currentFilePath_.isEmpty()) {
            // No chart on disk, so there is no folder to offer.
            session_.applicationServices_.uiRequests().postNotice(
                miacode::v2::NoticeSeverity::Error, failureTitle, failureText, details);
        } else {
            session_.applicationServices_.uiRequests().requestNoticeAction(
                miacode::v2::NoticeSeverity::Error, failureTitle, failureText, details,
                UiText::text(QStringLiteral("action.open_folder")),
                [this](bool actionChosen) {
                    if (actionChosen) {
                        session_.onOpenCurrentFolder();
                    }
                });
        }
    }

    restorePreviewAspectIfNeeded();
    this->clearVideoExportWorkerState();
}

void miacode::runtime::VideoExportHost::cancelVideoExportWorker()
{
    MC_OP("miacode::runtime::VideoExportHost::cancelVideoExportWorker");
    session_.videoExportWorkerCancelRequested_ = true;
    // Stay on the surface with a busy indicator: terminating the worker takes a
    // moment and hiding progress here would look like the export had finished.
    this->reportExportProgress(
        -1, UiText::text(QStringLiteral("dialog.video_export.progress.canceling")));
    if (session_.videoExportWorkerProcess_ == nullptr || session_.videoExportWorkerProcess_->state() == QProcess::NotRunning) {
        return;
    }

    QPointer<QProcess> processGuard(session_.videoExportWorkerProcess_);
    session_.videoExportWorkerProcess_->terminate();
    QTimer::singleShot(500, &session_, [this, processGuard]() {
        if (processGuard.isNull() || processGuard != session_.videoExportWorkerProcess_) {
            return;
        }
        if (processGuard->state() != QProcess::NotRunning) {
            processGuard->kill();
        }
    });
}

void miacode::runtime::VideoExportHost::clearVideoExportWorkerState()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    const bool hadWorkerProcess = session_.videoExportWorkerProcess_ != nullptr;
    if (session_.videoExportWorkerProcess_ != nullptr) {
        QElapsedTimer processTimer;
        processTimer.start();
        session_.videoExportWorkerProcess_->disconnect(&session_);
        qint64 waitElapsedMs = 0;
        bool processWasRunning = false;
        if (session_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
            processWasRunning = true;
            QElapsedTimer waitTimer;
            waitTimer.start();
            session_.videoExportWorkerProcess_->kill();
            session_.videoExportWorkerProcess_->waitForFinished(2000);
            waitElapsedMs = waitTimer.elapsed();
        }
        session_.videoExportWorkerProcess_->deleteLater();
        session_.videoExportWorkerProcess_ = nullptr;
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/export_worker"),
            QStringLiteral("clear_worker_process"),
            processTimer.elapsed(),
            QStringLiteral("was_running=%1 wait_ms=%2")
                .arg(processWasRunning ? 1 : 0)
                .arg(waitElapsedMs)
        );
    }
    session_.videoExportWorkerStdoutBuffer_.clear();
    session_.videoExportWorkerStderrBuffer_.clear();
    session_.videoExportWorkerSnapshot_ = VideoExportSnapshot();
    session_.videoExportWorkerJobId_.clear();
    session_.videoExportWorkerOutputPath_.clear();
    session_.videoExportWorkerExportLogPath_.clear();
    session_.videoExportWorkerFatalLogPath_.clear();
    session_.videoExportWorkerResultMessage_.clear();
    session_.videoExportWorkerResultDetails_.clear();
    session_.videoExportWorkerFirstCrashDiagnostics_.clear();
    session_.videoExportWorkerElapsed_.invalidate();
    session_.videoExportWorkerSuccess_ = false;
    session_.videoExportWorkerCompletionReceived_ = false;
    session_.videoExportWorkerCancelRequested_ = false;
    session_.restoreSquareAfterVideoExport_ = false;
    session_.videoExportWorkerLastProgressPercent_ = 0;
    session_.videoExportWorkerLastEtaSeconds_ = -1;
    session_.videoExportWorkerAttempt_ = 0;
    session_.videoExportWorkerForceDisablePbo_ = false;
    this->endExportProgress();
    if (hadWorkerProcess) {
        emit session_.videoExportWorkerRunningChanged(false);
    }
    if (hadWorkerProcess) {
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/export_worker"),
            QStringLiteral("clear_video_export_worker_state"),
            totalTimer.elapsed(),
            QStringLiteral("had_process=%1").arg(hadWorkerProcess ? 1 : 0)
        );
    }
}

// The export worker owns the shared progress surface between begin and end.
// A negative percent means the stage has no measurable progress.
void miacode::runtime::VideoExportHost::reportExportProgress(int percent, const QString& label)
{
    miacode::v2::JobProgressService* const jobProgress = session_.jobProgressService();
    if (jobProgress == nullptr || jobProgress->token() != session_.videoExportJobToken_) {
        return;
    }
    if (percent < 0) {
        jobProgress->reportIndeterminate(label);
    } else {
        jobProgress->report(percent, label);
    }
}

void miacode::runtime::VideoExportHost::endExportProgress()
{
    miacode::v2::JobProgressService* const jobProgress = session_.jobProgressService();
    if (jobProgress == nullptr || session_.videoExportJobToken_ == 0) {
        return;
    }
    // Only clear our own job: a later job may already own the surface.
    if (jobProgress->token() == session_.videoExportJobToken_) {
        jobProgress->end();
    }
    session_.videoExportJobToken_ = 0;
}
