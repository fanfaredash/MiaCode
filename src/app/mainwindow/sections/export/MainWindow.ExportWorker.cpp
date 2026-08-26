#include "MainWindow.ExportSection.h"
#include "../../MainWindowShared.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "../window/MainWindow.WindowSection.h"

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
#include "tools/video_export/VideoExportDialog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <optional>

using namespace miacode::mainwindow::shared;

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

QMessageBox::StandardButton showCenteredLocalizedMessageBox(
    QMessageBox::Icon icon,
    QWidget* parent,
    const QString& title,
    const QString& message,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton
)
{
    QMessageBox dialog(icon, title, message, QMessageBox::NoButton, UiDialogs::effectiveParentWidget(parent));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setStandardButtons(buttons);
    if (defaultButton != QMessageBox::NoButton) {
        dialog.setDefaultButton(defaultButton);
    }
    UiDialogs::prepareDialogWindow(&dialog, parent);
    UiDialogs::localizeMessageBox(&dialog);
    centerDialogOnAnchor(&dialog, parent);

    dialog.exec();
    return dialog.standardButton(dialog.clickedButton());
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

bool MainWindow::ExportSection::startVideoExportWorkerProcess(
    QProcess* process,
    const VideoExportSnapshot& snapshot,
    QString* errorMessage,
    bool forceDisableOffscreenPbo
)
{
    MC_OP("MainWindow::ExportSection::startVideoExportWorkerProcess");
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

bool MainWindow::ExportSection::runVideoExportWorkerSync(
    const VideoExportSnapshot& snapshot,
    QProgressDialog* progressDialog,
    bool* canceledByUser,
    QString* errorMessage,
    const std::function<void(int percent, const QString& rawMessage)>& progressCallback,
    const std::function<bool()>& cancellationRequested,
    const std::function<void()>& retryingCallback
)
{
    MC_OP("MainWindow::ExportSection::runVideoExportWorkerSync");
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
                    } else if (progressDialog != nullptr && !message.trimmed().isEmpty()) {
                        progressDialog->setLabelText(
                            buildExportProgressLabelTextForUiLanguage(
                                message,
                                qBound(0, percent, 100),
                                itemElapsed,
                                &smoothedEtaSeconds
                            )
                        );
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
            if ((progressDialog != nullptr && progressDialog->wasCanceled())
                || (cancellationRequested && cancellationRequested())) {
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
            } else if (progressDialog != nullptr) {
                progressDialog->setRange(0, 100);
                progressDialog->setValue(0);
                progressDialog->setLabelText(
                    UiText::text(QStringLiteral("dialog.video_export.progress.retrying_safe_mode"))
                );
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

bool MainWindow::ExportSection::launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    MC_OP("MainWindow::ExportSection::launchVideoExportWorker");
    _mc_op_.note(QStringLiteral("output=%1").arg(snapshot.outputPath));
    if (owner_.videoExportWorkerProcess_ != nullptr && owner_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.video_export.error.worker_busy"));
        }
        _mc_op_.fail(QStringLiteral("worker already running"));
        return false;
    }

    this->clearVideoExportWorkerState();

    // Panel-launched exports currently keep the same progress popup as the
    // modal path. The embedded panel's main button is still switched to
    // 取消导出 by the caller and forwards cancellation to this worker.
    if (!owner_.videoExportUseInlineProgress_) {
        auto* progress = new QProgressDialog(
            systemL10n(QStringLiteral("Preparing export..."), QStringLiteral("准备导出...")),
            systemL10n(QStringLiteral("Cancel"), QStringLiteral("取消")),
            0,
            100,
            UiDialogs::effectiveParentWidget(&owner_)
        );
        progress->setWindowTitle(systemL10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")));
        progress->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        // Never allow minimizing: over the WA_DontShowOnScreen quick-shell host
        // a minimized, parentless progress popup can never be re-raised, which
        // strands the export flow (and deadlocks the app for the modal
        // variants). Keep the popup non-minimizable everywhere.
        progress->setWindowFlag(Qt::WindowMinimizeButtonHint, false);
        progress->setWindowModality(Qt::NonModal);
        progress->setAttribute(Qt::WA_ShowWithoutActivating, true);
        progress->setLabelText(UiText::text(QStringLiteral("dialog.video_export.progress.preparing")));
        progress->setCancelButtonText(UiText::text(QStringLiteral("dialog.video_export.button.cancel")));
        progress->setWindowTitle(UiText::text(QStringLiteral("dialog.video_export.title")));
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setMinimumWidth(320);
        progress->setMaximumWidth(360);
        progress->setValue(0);
        UiDialogs::configureDialogPreviewShortcuts(progress);
        owner_.windowSection_->applySystemWindowBackdrop(progress);
        if (QLabel* label = progress->findChild<QLabel*>(); label != nullptr) {
            label->setWordWrap(true);
        }
        UiDialogs::centerDialogOnAnchor(progress, &owner_);
        progress->show();
        QTimer::singleShot(0, progress, [this, progress]() {
            if (progress == nullptr) {
                return;
            }
            progress->adjustSize();
            UiDialogs::centerDialogOnAnchor(progress, &owner_);
        });
        QTimer::singleShot(0, &owner_, [this]() {
#ifdef Q_OS_MACOS
            // MainWindow is a WA_DontShowOnScreen quick-shell host whose
            // widgets are rehosted into the QML shell; raise()/activateWindow()
            // would orderFront the empty white shell window on macOS. Windows
            // keeps this focus-restore block unchanged.
            if (owner_.testAttribute(Qt::WA_DontShowOnScreen)) {
                return;
            }
#endif
            if (!owner_.isVisible() || owner_.windowState().testFlag(Qt::WindowMinimized)) {
                return;
            }
            owner_.raise();
            owner_.activateWindow();
            if (QWidget* focusWidget = owner_.focusWidget(); focusWidget != nullptr) {
                focusWidget->setFocus(Qt::ActiveWindowFocusReason);
            } else {
                owner_.setFocus(Qt::ActiveWindowFocusReason);
            }
        });
        owner_.videoExportProgressDialog_ = progress;
        connect(progress, &QProgressDialog::canceled, &owner_, [this]() {
            this->cancelVideoExportWorker();
        });
    }

    auto* process = new QProcess(&owner_);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    owner_.videoExportWorkerProcess_ = process;
    owner_.videoExportWorkerSnapshot_ = snapshot;
    owner_.videoExportWorkerJobId_ = snapshot.jobId;
    owner_.videoExportWorkerOutputPath_ = snapshot.outputPath;
    owner_.videoExportWorkerExportLogPath_ = videoExportWorkerLogPathForUi(snapshot);
    owner_.videoExportWorkerFatalLogPath_ = videoExportWorkerFatalLogPathForUi(snapshot);
    owner_.videoExportWorkerResultMessage_.clear();
    owner_.videoExportWorkerResultDetails_.clear();
    owner_.videoExportWorkerFirstCrashDiagnostics_.clear();
    owner_.videoExportWorkerStdoutBuffer_.clear();
    owner_.videoExportWorkerStderrBuffer_.clear();
    owner_.videoExportWorkerSuccess_ = false;
    owner_.videoExportWorkerCompletionReceived_ = false;
    owner_.videoExportWorkerCancelRequested_ = false;
    owner_.videoExportWorkerLastProgressPercent_ = 0;
    owner_.videoExportWorkerLastEtaSeconds_ = -1;
    owner_.videoExportWorkerAttempt_ = 1;
    owner_.videoExportWorkerForceDisablePbo_ = false;
    owner_.videoExportWorkerElapsed_.start();

    if (owner_.videoExportUseInlineProgress_) {
        this->beginInlineExportProgress();
    }

    connect(process, &QProcess::readyReadStandardOutput, &owner_, [this]() {
        this->handleVideoExportWorkerStdout();
    });
    connect(process, &QProcess::readyReadStandardError, &owner_, [this]() {
        this->handleVideoExportWorkerStderr();
    });
    connect(process, &QProcess::finished, &owner_, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        this->handleVideoExportWorkerProcessFinished(exitCode, static_cast<int>(exitStatus));
    });

    if (!this->startVideoExportWorkerProcess(
            process,
            snapshot,
            errorMessage,
            owner_.videoExportWorkerForceDisablePbo_)) {
        this->clearVideoExportWorkerState();
        return false;
    }
    emit owner_.videoExportWorkerRunningChanged(true);
    return true;
}

void MainWindow::ExportSection::handleVideoExportWorkerStdout()
{
    if (owner_.videoExportWorkerProcess_ == nullptr) {
        return;
    }
    owner_.videoExportWorkerStdoutBuffer_.append(owner_.videoExportWorkerProcess_->readAllStandardOutput());
    while (true) {
        const int newlineIndex = owner_.videoExportWorkerStdoutBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }
        const QByteArray rawLine = owner_.videoExportWorkerStdoutBuffer_.left(newlineIndex).trimmed();
        owner_.videoExportWorkerStdoutBuffer_.remove(0, newlineIndex + 1);
        if (rawLine.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(rawLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            appendProcessDetailLine(
                &owner_.videoExportWorkerResultDetails_,
                QString::fromUtf8(rawLine),
                kVideoExportWorkerDetailsMaxChars);
            continue;
        }
        this->handleVideoExportWorkerEvent(document.object());
    }
}

void MainWindow::ExportSection::handleVideoExportWorkerStderr()
{
    if (owner_.videoExportWorkerProcess_ == nullptr) {
        return;
    }
    appendProcessOutputTail(
        &owner_.videoExportWorkerStderrBuffer_,
        owner_.videoExportWorkerProcess_->readAllStandardError(),
        kVideoExportWorkerStderrBufferMaxBytes);
}

void MainWindow::ExportSection::handleVideoExportWorkerEvent(const QJsonObject& eventObject)
{
    MC_OP("MainWindow::ExportSection::handleVideoExportWorkerEvent");
    const QString eventType = eventObject.value(QStringLiteral("event")).toString();
    _mc_op_.note(QStringLiteral("event=%1").arg(eventType));
    const bool suppressProgressUi = owner_.videoExportWorkerCancelRequested_;
    if (eventType == QLatin1String("worker_ready")) {
        if (!suppressProgressUi && owner_.videoExportProgressDialog_ != nullptr) {
            owner_.videoExportProgressDialog_->setValue(qMax(owner_.videoExportProgressDialog_->value(), 1));
            QTimer::singleShot(0, &owner_, [this]() {
                if (owner_.videoExportProgressDialog_ != nullptr) {
                    owner_.videoExportProgressDialog_->setLabelText(
                        UiText::text(QStringLiteral("dialog.video_export.progress.worker_ready"))
                    );
                }
            });
            owner_.videoExportProgressDialog_->setLabelText(
                UiText::text(QStringLiteral("dialog.video_export.progress.worker_ready"))
            );
            owner_.videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Worker ready..."),
                QStringLiteral("后台已就绪...")
            ));
        }
        if (!suppressProgressUi) {
            this->updateInlineExportProgress(
                1, UiText::text(QStringLiteral("dialog.video_export.progress.worker_ready")));
        }
        return;
    }
    if (eventType == QLatin1String("accepted")) {
        if (!suppressProgressUi && owner_.videoExportProgressDialog_ != nullptr) {
            owner_.videoExportProgressDialog_->setValue(qMax(owner_.videoExportProgressDialog_->value(), 2));
            QTimer::singleShot(0, &owner_, [this]() {
                if (owner_.videoExportProgressDialog_ != nullptr) {
                    owner_.videoExportProgressDialog_->setLabelText(
                        UiText::text(QStringLiteral("dialog.video_export.progress.starting_export"))
                    );
                }
            });
            owner_.videoExportProgressDialog_->setLabelText(
                UiText::text(QStringLiteral("dialog.video_export.progress.starting_export"))
            );
            owner_.videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Starting export..."),
                QStringLiteral("开始导出...")
            ));
        }
        if (!suppressProgressUi) {
            this->updateInlineExportProgress(
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
            owner_.videoExportWorkerLastProgressPercent_ =
                qMax(owner_.videoExportWorkerLastProgressPercent_, percent);
        }
        // Built ONCE per event — the builder advances the rolling ETA estimate.
        const QString progressLabel = buildExportProgressLabelTextForUiLanguage(
            rawMessage,
            owner_.videoExportWorkerLastProgressPercent_,
            owner_.videoExportWorkerElapsed_,
            &owner_.videoExportWorkerLastEtaSeconds_
        );
        if (owner_.videoExportProgressDialog_ != nullptr) {
            if (busyStage) {
                if (owner_.videoExportProgressDialog_->minimum() != 0 || owner_.videoExportProgressDialog_->maximum() != 0) {
                    owner_.videoExportProgressDialog_->setRange(0, 0);
                }
                owner_.videoExportProgressDialog_->setLabelText(progressLabel);
            } else {
                if (owner_.videoExportProgressDialog_->minimum() == 0 && owner_.videoExportProgressDialog_->maximum() == 0) {
                    owner_.videoExportProgressDialog_->setRange(0, 100);
                }
                owner_.videoExportProgressDialog_->setValue(owner_.videoExportWorkerLastProgressPercent_);
                owner_.videoExportProgressDialog_->setLabelText(progressLabel);
            }
        }
        this->updateInlineExportProgress(
            busyStage ? -1 : owner_.videoExportWorkerLastProgressPercent_, progressLabel);
        return;
    }
    if (eventType == QLatin1String("log")) {
        const QString message = eventObject.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            appendProcessDetailLine(
                &owner_.videoExportWorkerResultDetails_,
                message,
                kVideoExportWorkerDetailsMaxChars);
        }
        return;
    }
    if (eventType == QLatin1String("finished")) {
        owner_.videoExportWorkerCompletionReceived_ = true;
        owner_.videoExportWorkerSuccess_ = eventObject.value(QStringLiteral("success")).toBool(false);
        owner_.videoExportWorkerOutputPath_ = eventObject.value(QStringLiteral("output_path")).toString(owner_.videoExportWorkerOutputPath_);
        owner_.videoExportWorkerResultMessage_ = owner_.videoExportWorkerSuccess_
            ? QStringLiteral("ok")
            : eventObject.value(QStringLiteral("error")).toString(UiText::text(QStringLiteral("dialog.video_export.error.failed")));
        const QString details = eventObject.value(QStringLiteral("details")).toString().trimmed();
        if (!details.isEmpty()) {
            owner_.videoExportWorkerResultDetails_ =
                tailLimitedProcessText(details, kVideoExportWorkerDetailsMaxChars);
        }
        if (!suppressProgressUi && owner_.videoExportProgressDialog_ != nullptr) {
            if (owner_.videoExportProgressDialog_->minimum() == 0 && owner_.videoExportProgressDialog_->maximum() == 0) {
                owner_.videoExportProgressDialog_->setRange(0, 100);
            }
            owner_.videoExportProgressDialog_->setValue(owner_.videoExportWorkerSuccess_ ? 100 : owner_.videoExportProgressDialog_->value());
            if (owner_.videoExportWorkerSuccess_) {
                owner_.videoExportProgressDialog_->setLabelText(systemL10n(
                    QStringLiteral("Done."),
                    QStringLiteral("导出完成。")
                ));
            }
        }
        if (!suppressProgressUi && owner_.videoExportProgressDialog_ != nullptr && owner_.videoExportWorkerSuccess_) {
            owner_.videoExportProgressDialog_->setLabelText(
                UiText::text(QStringLiteral("dialog.video_export.progress.done"))
            );
        }
        if (!suppressProgressUi && owner_.videoExportWorkerSuccess_) {
            this->updateInlineExportProgress(
                100, UiText::text(QStringLiteral("dialog.video_export.progress.done")));
        }
    }
}

void MainWindow::ExportSection::handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus)
{
    MC_OP("MainWindow::ExportSection::handleVideoExportWorkerProcessFinished");
    _mc_op_.note(QStringLiteral("exit_code=%1 status=%2").arg(exitCode).arg(exitStatus));
    Q_UNUSED(exitCode);

    const auto restorePreviewAspectIfNeeded = [this]() {
        if (!owner_.restoreSquareAfterVideoExport_) {
            return;
        }
        owner_.restoreSquareAfterVideoExport_ = false;
        owner_.setPreviewCanvasAspectRatio(1.0, false);
    };

    const QString retryNote = UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_note"));
    const QString firstAttemptTitle = UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_first_attempt"));
    const QString finalAttemptTitle = UiText::text(QStringLiteral("dialog.video_export.error.worker_retry_final_attempt"));
    const QString stderrText = QString::fromUtf8(owner_.videoExportWorkerStderrBuffer_).trimmed();
    const QString stdoutTailText = QString::fromUtf8(owner_.videoExportWorkerStdoutBuffer_).trimmed();
    const QString processErrorText = owner_.videoExportWorkerProcess_ != nullptr
        ? owner_.videoExportWorkerProcess_->errorString().trimmed()
        : QString();
    const QString workerDiagnostics = buildWorkerProcessDiagnostics(
        exitCode,
        static_cast<QProcess::ExitStatus>(exitStatus),
        processErrorText,
        stderrText,
        stdoutTailText,
        owner_.videoExportWorkerExportLogPath_,
        owner_.videoExportWorkerFatalLogPath_
    );
    const QString attemptDiagnostics =
        combineWorkerAttemptDiagnostics(owner_.videoExportWorkerResultDetails_, workerDiagnostics);
    const bool canceledOutcome =
        owner_.videoExportWorkerCancelRequested_
        && (!owner_.videoExportWorkerCompletionReceived_ || !owner_.videoExportWorkerSuccess_);
    if (canceledOutcome) {
        if (owner_.videoExportProgressDialog_ != nullptr) {
            owner_.videoExportProgressDialog_->hide();
        }
        this->endInlineExportProgress();
        restorePreviewAspectIfNeeded();
        showCenteredLocalizedMessageBox(
            QMessageBox::Information,
            &owner_,
            UiText::text(QStringLiteral("dialog.video_export.title")),
            UiText::text(QStringLiteral("dialog.video_export.message.canceled"))
        );
        this->clearVideoExportWorkerState();
        return;
    }

    bool retryRestartFailed = false;
    QString retryRestartError;
    const bool shouldRetry = miacode::video_export::shouldRetryVideoExportWorkerAfterCrash(
        exitStatus == static_cast<int>(QProcess::CrashExit),
        shouldCurrentExportWorkerAttemptRequestPbo(owner_.videoExportWorkerForceDisablePbo_),
        owner_.videoExportWorkerCancelRequested_,
        owner_.videoExportWorkerAttempt_
    );
    if (shouldRetry) {
        owner_.videoExportWorkerFirstCrashDiagnostics_ = attemptDiagnostics;
        owner_.videoExportWorkerAttempt_ += 1;
        owner_.videoExportWorkerForceDisablePbo_ = true;
        owner_.videoExportWorkerCompletionReceived_ = false;
        owner_.videoExportWorkerSuccess_ = false;
        owner_.videoExportWorkerResultMessage_.clear();
        owner_.videoExportWorkerResultDetails_.clear();
        owner_.videoExportWorkerStdoutBuffer_.clear();
        owner_.videoExportWorkerStderrBuffer_.clear();
        owner_.videoExportWorkerLastProgressPercent_ = 0;
        owner_.videoExportWorkerLastEtaSeconds_ = -1;
        owner_.videoExportWorkerElapsed_.start();
        if (owner_.videoExportProgressDialog_ != nullptr) {
            owner_.videoExportProgressDialog_->setRange(0, 100);
            owner_.videoExportProgressDialog_->setValue(0);
            owner_.videoExportProgressDialog_->setLabelText(
                UiText::text(QStringLiteral("dialog.video_export.progress.retrying_safe_mode"))
            );
            owner_.videoExportProgressDialog_->show();
        }
        this->updateInlineExportProgress(
            0,
            UiText::text(QStringLiteral("dialog.video_export.progress.retrying_safe_mode")));

        QString restartError;
        if (owner_.videoExportWorkerProcess_ != nullptr
            && this->startVideoExportWorkerProcess(
                owner_.videoExportWorkerProcess_,
                owner_.videoExportWorkerSnapshot_,
                &restartError,
                true)) {
            return;
        }

        retryRestartFailed = true;
        retryRestartError = restartError.trimmed();
    }

    if (owner_.videoExportProgressDialog_ != nullptr) {
        owner_.videoExportProgressDialog_->hide();
    }
    this->endInlineExportProgress();

    QString finalAttemptDiagnostics;
    if (retryRestartFailed) {
        owner_.videoExportWorkerSuccess_ = false;
        owner_.videoExportWorkerCompletionReceived_ = false;
        owner_.videoExportWorkerResultMessage_ =
            UiText::text(QStringLiteral("dialog.video_export.error.failed"));
        finalAttemptDiagnostics = retryRestartError;
        owner_.videoExportWorkerResultDetails_ = retryRestartError;
    } else if (!owner_.videoExportWorkerCompletionReceived_) {
        owner_.videoExportWorkerSuccess_ = false;
        owner_.videoExportWorkerResultMessage_ = exitStatus == static_cast<int>(QProcess::CrashExit)
            ? UiText::text(QStringLiteral("dialog.video_export.error.worker_crash"))
            : UiText::text(QStringLiteral("dialog.video_export.error.worker_exit"));
        finalAttemptDiagnostics = workerDiagnostics;
        owner_.videoExportWorkerResultDetails_ = finalAttemptDiagnostics;
    } else if (!owner_.videoExportWorkerSuccess_) {
        finalAttemptDiagnostics = attemptDiagnostics;
        owner_.videoExportWorkerResultDetails_ = attemptDiagnostics;
    }
    if (!owner_.videoExportWorkerSuccess_ && !owner_.videoExportWorkerFirstCrashDiagnostics_.trimmed().isEmpty()) {
        owner_.videoExportWorkerResultDetails_ = buildWorkerRetryFailureDetails(
            retryNote,
            firstAttemptTitle,
            owner_.videoExportWorkerFirstCrashDiagnostics_,
            finalAttemptTitle,
            finalAttemptDiagnostics
        );
    }
    trimProcessTextTail(&owner_.videoExportWorkerResultDetails_, kVideoExportWorkerDetailsMaxChars);
    if (!owner_.videoExportWorkerSuccess_) {
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/worker"),
            QStringLiteral("%1 | %2")
                .arg(owner_.videoExportWorkerResultMessage_.trimmed(), owner_.videoExportWorkerResultDetails_.trimmed())
        );
    }

    if (owner_.videoExportWorkerSuccess_) {
        const QFileInfo resolvedOutputInfo(owner_.videoExportWorkerOutputPath_);
        const QString resolvedOutputName = resolvedOutputInfo.fileName().trimmed().isEmpty()
            ? QDir::toNativeSeparators(owner_.videoExportWorkerOutputPath_)
            : resolvedOutputInfo.fileName();
        QMessageBox dialog(
            QMessageBox::Information,
            UiText::text(QStringLiteral("dialog.video_export.title")),
            QStringLiteral("%1\n\n%2")
                .arg(UiText::text(QStringLiteral("dialog.video_export.message.completed")))
                .arg(resolvedOutputName),
            QMessageBox::NoButton,
            UiDialogs::effectiveParentWidget(&owner_)
        );
        dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        UiDialogs::configureDialogPreviewShortcuts(&dialog);
        UiDialogs::applyDetachedParentBehavior(&dialog, &owner_);
        QPushButton* openButton = dialog.addButton(
            UiText::text(QStringLiteral("action.open")),
            QMessageBox::AcceptRole
        );
        dialog.addButton(
            UiText::text(QStringLiteral("action.close")),
            QMessageBox::RejectRole
        );
        dialog.setDefaultButton(openButton);
        centerDialogOnAnchor(&dialog, &owner_);
        dialog.exec();
        if (dialog.clickedButton() == openButton) {
            const QFileInfo outputInfo(owner_.videoExportWorkerOutputPath_);
            const QString outputDir = outputInfo.absoluteDir().absolutePath();
            if (!outputDir.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
            }
        }
    } else {
        const QString details = owner_.videoExportWorkerResultDetails_.trimmed();
        QMessageBox dialog(
            QMessageBox::Critical,
            UiText::text(QStringLiteral("dialog.video_export.error.failed_title")),
            details.isEmpty()
                ? owner_.videoExportWorkerResultMessage_
                : QStringLiteral("%1\n\n%2").arg(owner_.videoExportWorkerResultMessage_, details),
            QMessageBox::NoButton,
            UiDialogs::effectiveParentWidget(&owner_)
        );
        dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        UiDialogs::configureDialogPreviewShortcuts(&dialog);
        UiDialogs::applyDetachedParentBehavior(&dialog, &owner_);
        QPushButton* openFolderButton = nullptr;
        if (!owner_.currentFilePath_.isEmpty()) {
            openFolderButton = dialog.addButton(UiText::text(QStringLiteral("action.open_folder")), QMessageBox::ActionRole);
        }
        QPushButton* okButton = dialog.addButton(UiText::text(QStringLiteral("action.ok")), QMessageBox::AcceptRole);
        dialog.setDefaultButton(okButton);
        centerDialogOnAnchor(&dialog, &owner_);
        dialog.exec();
        if (openFolderButton != nullptr && dialog.clickedButton() == openFolderButton) {
            owner_.onOpenCurrentFolder();
        }
    }

    restorePreviewAspectIfNeeded();
    this->clearVideoExportWorkerState();
}

void MainWindow::ExportSection::cancelVideoExportWorker()
{
    MC_OP("MainWindow::ExportSection::cancelVideoExportWorker");
    owner_.videoExportWorkerCancelRequested_ = true;
    if (owner_.videoExportProgressDialog_ != nullptr) {
        QTimer::singleShot(0, &owner_, [this]() {
            if (owner_.videoExportProgressDialog_ != nullptr) {
                owner_.videoExportProgressDialog_->setLabelText(
                    UiText::text(QStringLiteral("dialog.video_export.progress.canceling"))
                );
            }
        });
    }
    if (owner_.videoExportProgressDialog_ != nullptr) {
        owner_.videoExportProgressDialog_->setLabelText(systemL10n(
            QStringLiteral("Canceling export..."),
            QStringLiteral("正在取消导出...")
        ));
        owner_.videoExportProgressDialog_->hide();
    }
    this->updateInlineExportProgress(
        -1, UiText::text(QStringLiteral("dialog.video_export.progress.canceling")));
    if (owner_.videoExportWorkerProcess_ == nullptr || owner_.videoExportWorkerProcess_->state() == QProcess::NotRunning) {
        return;
    }

    QPointer<QProcess> processGuard(owner_.videoExportWorkerProcess_);
    owner_.videoExportWorkerProcess_->terminate();
    QTimer::singleShot(500, &owner_, [this, processGuard]() {
        if (processGuard.isNull() || processGuard != owner_.videoExportWorkerProcess_) {
            return;
        }
        if (processGuard->state() != QProcess::NotRunning) {
            processGuard->kill();
        }
    });
}

void MainWindow::ExportSection::clearVideoExportWorkerState()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    const bool hadProgressDialog = owner_.videoExportProgressDialog_ != nullptr;
    const bool hadWorkerProcess = owner_.videoExportWorkerProcess_ != nullptr;
    if (owner_.videoExportProgressDialog_ != nullptr) {
        QElapsedTimer progressDialogTimer;
        progressDialogTimer.start();
        owner_.videoExportProgressDialog_->close();
        owner_.videoExportProgressDialog_->deleteLater();
        owner_.videoExportProgressDialog_ = nullptr;
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/export_worker"),
            QStringLiteral("clear_progress_dialog"),
            progressDialogTimer.elapsed()
        );
    }
    if (owner_.videoExportWorkerProcess_ != nullptr) {
        QElapsedTimer processTimer;
        processTimer.start();
        owner_.videoExportWorkerProcess_->disconnect(&owner_);
        qint64 waitElapsedMs = 0;
        bool processWasRunning = false;
        if (owner_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
            processWasRunning = true;
            QElapsedTimer waitTimer;
            waitTimer.start();
            owner_.videoExportWorkerProcess_->kill();
            owner_.videoExportWorkerProcess_->waitForFinished(2000);
            waitElapsedMs = waitTimer.elapsed();
        }
        owner_.videoExportWorkerProcess_->deleteLater();
        owner_.videoExportWorkerProcess_ = nullptr;
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
    owner_.videoExportWorkerStdoutBuffer_.clear();
    owner_.videoExportWorkerStderrBuffer_.clear();
    owner_.videoExportWorkerSnapshot_ = VideoExportSnapshot();
    owner_.videoExportWorkerJobId_.clear();
    owner_.videoExportWorkerOutputPath_.clear();
    owner_.videoExportWorkerExportLogPath_.clear();
    owner_.videoExportWorkerFatalLogPath_.clear();
    owner_.videoExportWorkerResultMessage_.clear();
    owner_.videoExportWorkerResultDetails_.clear();
    owner_.videoExportWorkerFirstCrashDiagnostics_.clear();
    owner_.videoExportWorkerElapsed_.invalidate();
    owner_.videoExportWorkerSuccess_ = false;
    owner_.videoExportWorkerCompletionReceived_ = false;
    owner_.videoExportWorkerCancelRequested_ = false;
    owner_.restoreSquareAfterVideoExport_ = false;
    owner_.videoExportWorkerLastProgressPercent_ = 0;
    owner_.videoExportWorkerLastEtaSeconds_ = -1;
    owner_.videoExportWorkerAttempt_ = 0;
    owner_.videoExportWorkerForceDisablePbo_ = false;
    this->endInlineExportProgress();
    owner_.videoExportUseInlineProgress_ = false;
    if (hadWorkerProcess) {
        emit owner_.videoExportWorkerRunningChanged(false);
    }
    if (hadProgressDialog || hadWorkerProcess) {
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/export_worker"),
            QStringLiteral("clear_video_export_worker_state"),
            totalTimer.elapsed(),
            QStringLiteral("had_progress_dialog=%1 had_process=%2")
                .arg(hadProgressDialog ? 1 : 0)
                .arg(hadWorkerProcess ? 1 : 0)
        );
    }
}

// ---- Inline export progress on the preview-area transport (E-C, A3 as
// amended 2026-06-11): the panel-launched export rides the PLAYBACK slider —
// the handle advances through chart time (exportStart + percent × duration)
// exactly like a playback, range untouched, seeking disabled (the
// TimelineSection position updater is gated on
// videoExportInlineProgressSecond_ >= 0). Stage/ETA text goes to the status
// bar (shared by both shells); the quick shell additionally reads the chart
// time via shellVideoExportProgressSeconds() for its QML transport. ----

void MainWindow::ExportSection::beginInlineExportProgress()
{
    // Progress is reported in the STATUS BAR only (2026-06-13): the preview
    // transport stays entirely the user's — playback and a running export are
    // NOT mutually exclusive. videoExportInlineProgressSecond_ >= 0 just marks
    // that an inline (panel-launched) export is in flight (cleared on end).
    owner_.videoExportInlineProgressSecond_ =
        qMax(0.0, owner_.videoExportWorkerSnapshot_.exportStartSeconds);
    owner_.statusBar()->showMessage(
        UiText::text(QStringLiteral("dialog.video_export.progress.preparing")));
}

void MainWindow::ExportSection::updateInlineExportProgress(int percent, const QString& label)
{
    if (owner_.videoExportInlineProgressSecond_ < 0.0) {
        return;  // inline progress not active (popup-dialog launch)
    }
    // Status-bar only: never drive the preview slider (it belongs to the user
    // now). Fold the percent into the stage/ETA label.
    QString message = label;
    if (percent >= 0) {
        const QString percentText = QStringLiteral("%1%").arg(qBound(0, percent, 100));
        message = message.isEmpty()
            ? percentText
            : QStringLiteral("%1 · %2").arg(percentText, message);
    }
    if (!message.isEmpty()) {
        owner_.statusBar()->showMessage(message);
    }
}

void MainWindow::ExportSection::endInlineExportProgress()
{
    const bool inlineProgressActive = owner_.videoExportInlineProgressSecond_ >= 0.0;
    if (!owner_.embeddedVideoExportPanel_.isNull()) {
        owner_.embeddedVideoExportPanel_->setEmbeddedExportRunning(false);
    }
    if (!inlineProgressActive) {
        return;
    }
    owner_.videoExportInlineProgressSecond_ = -1.0;
    owner_.statusBar()->clearMessage();
    // Hand the slider back to the playback clock.
    if (owner_.timelineSection_ != nullptr) {
        owner_.timelineSection_->updatePreviewSliderPosition(owner_.qtPreviewPauseSecond_);
    }
}
