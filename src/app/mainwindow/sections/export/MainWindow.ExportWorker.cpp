#include "../../MainWindow.h"
#include "../../MainWindowShared.h"

#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/video_export/BatchVideoExportDialog.h"
#include "tools/video_export/VideoExportController.h"
#include "tools/video_export/VideoExportDialog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {
QString videoExportBackgroundScaleModeToken(PreviewBackgroundScaleMode mode)
{
    switch (mode) {
    case PreviewBackgroundScaleMode::FitContain:
        return QStringLiteral("fit");
    case PreviewBackgroundScaleMode::FillCrop:
    default:
        return QStringLiteral("fill");
    }
}

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

QString videoExportWorkerLogPathForUi()
{
    return miacode::debug_log::exportLogPath();
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
    const QString& stdoutTailText
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
    if (miacode::debug_options::exportDebugOutputEnabled()) {
        lines.append(QStringLiteral("Debug log: %1").arg(videoExportWorkerLogPathForUi()));
    }
    lines.append(QStringLiteral("Error log: %1").arg(miacode::debug_log::fatalLogPath()));
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
        return uiText("dialog.video_export.progress.rendering_count", "Rendering frames... %1/%2")
            .arg(renderMatch.captured(1), renderMatch.captured(2));
    }

    if (trimmed == QLatin1String("Preparing SFX track...")) {
        return uiText("dialog.video_export.progress.preparing_audio", "Preparing audio...");
    }
    if (trimmed == QLatin1String("Starting ffmpeg...")) {
        return uiText("dialog.video_export.progress.starting_ffmpeg", "Starting ffmpeg...");
    }
    if (trimmed == QLatin1String("Rendering frames and encoding...")) {
        return uiText("dialog.video_export.progress.rendering", "Rendering frames...");
    }
    if (trimmed == QLatin1String("Finalizing encoded video stream...")) {
        return uiText("dialog.video_export.progress.finalizing_encode", "Finalizing video...");
    }
    if (trimmed == QLatin1String("Repacking MP4 for fast start...")) {
        return uiText("dialog.video_export.progress.repacking", "Finalizing video...");
    }
    if (trimmed == QLatin1String("Collecting export summary...")) {
        return uiText("dialog.video_export.progress.finishing", "Finishing up...");
    }
    if (trimmed == QLatin1String("Export completed.")) {
        return uiText("dialog.video_export.progress.done", "Done.");
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
        text = uiText("dialog.video_export.progress.generic", "Exporting...");
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

    const QString etaLine = uiText("dialog.video_export.progress.remaining", "About %1 remaining")
        .arg(formatExportRemainingDuration(displayEtaSeconds));
    return QStringLiteral("%1\n%2").arg(text, etaLine);
}


}  // namespace

bool MainWindow::startVideoExportWorkerProcess(QProcess* process, const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    if (process == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker process is null");
        }
        return false;
    }

    const QString executablePath = QCoreApplication::applicationFilePath();
    if (executablePath.trimmed().isEmpty() || !QFileInfo::exists(executablePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.executable_missing", "Failed to locate MiaCode executable.");
        }
        return false;
    }

    process->setProcessChannelMode(QProcess::SeparateChannels);
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
        return false;
    }

    const QByteArray payload = buildVideoExportWorkerStartPayload(snapshot);
    if (process->write(payload) != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.video_export.error.worker_write_failed",
                "Failed to send export snapshot to worker."
            );
        }
        process->kill();
        process->waitForFinished(1000);
        return false;
    }
    process->closeWriteChannel();
    return true;
}

bool MainWindow::runVideoExportWorkerSync(
    const VideoExportSnapshot& snapshot,
    QProgressDialog* progressDialog,
    bool* canceledByUser,
    QString* errorMessage,
    const std::function<void(int percent, const QString& rawMessage)>& progressCallback
)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (canceledByUser != nullptr) {
        *canceledByUser = false;
    }

    QProcess process;
    if (!startVideoExportWorkerProcess(&process, snapshot, errorMessage)) {
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
                resultDetails += (resultDetails.isEmpty() ? QString() : QStringLiteral("\n")) + QString::fromUtf8(rawLine);
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
                resultDetails = object.value(QStringLiteral("details")).toString();
            }
        }
    };

    while (process.state() != QProcess::NotRunning) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        stdoutBuffer.append(process.readAllStandardOutput());
        stderrBuffer.append(process.readAllStandardError());
        parseStdoutLines();
        if (progressDialog != nullptr && progressDialog->wasCanceled()) {
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
    stderrBuffer.append(process.readAllStandardError());
    parseStdoutLines();

    const QString stderrText = QString::fromUtf8(stderrBuffer).trimmed();
    const QString stdoutTailText = QString::fromUtf8(stdoutBuffer).trimmed();
    const QString processErrorText = process.errorString().trimmed();
    const QString workerDiagnostics = buildWorkerProcessDiagnostics(
        process.exitCode(),
        process.exitStatus(),
        processErrorText,
        stderrText,
        stdoutTailText
    );
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage != nullptr) {
            const QString summary = !resultMessage.trimmed().isEmpty()
                ? resultMessage
                : (!stderrText.isEmpty()
                    ? stderrText.split('\n').constFirst().trimmed()
                    : uiText("dialog.batch_export.error.export_failed", QStringLiteral("Export failed.")));
            *errorMessage = compactWorkerExitSummary(process.exitCode(), process.exitStatus(), summary);
        }
        return false;
    }

    if (!finishedEventReceived || !success) {
        if (errorMessage != nullptr) {
            const QString summary = !resultMessage.trimmed().isEmpty()
                ? resultMessage
                : (!stderrText.isEmpty()
                    ? stderrText.split('\n').constFirst().trimmed()
                    : uiText("dialog.batch_export.error.export_failed", QStringLiteral("Export failed.")));
            const bool genericFailure = resultMessage.trimmed().isEmpty() && stderrText.trimmed().isEmpty();
            *errorMessage = genericFailure
                ? appendVideoExportDiagnostics(summary, workerDiagnostics)
                : summary;
        }
        return false;
    }

    return true;
}

void MainWindow::showExportToolbarMenu()
{
    if (exportVideoButton_ == nullptr || exportVideoMenu_ == nullptr) {
        return;
    }
    if (!exportVideoButton_->isVisible() || !exportVideoButton_->underMouse()) {
        return;
    }
    if (QApplication::mouseButtons().testAnyFlag(Qt::AllButtons)) {
        return;
    }
    if (exportVideoMenu_->isVisible()) {
        return;
    }
    const QPoint globalPos = exportVideoButton_->mapToGlobal(QPoint(0, exportVideoButton_->height()));
    exportVideoMenu_->popup(globalPos);
}

bool MainWindow::launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    if (videoExportWorkerProcess_ != nullptr && videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.worker_busy", "Another export is already running.");
        }
        return false;
    }

    clearVideoExportWorkerState();

    auto* progress = new QProgressDialog(
        systemL10n(QStringLiteral("Preparing export..."), QStringLiteral("准备导出...")),
        systemL10n(QStringLiteral("Cancel"), QStringLiteral("取消")),
        0,
        100,
        this
    );
    progress->setWindowTitle(systemL10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")));
    progress->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    progress->setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    progress->setWindowModality(Qt::NonModal);
    progress->setLabelText(uiText("dialog.video_export.progress.preparing", "Preparing export..."));
    progress->setCancelButtonText(uiText("dialog.video_export.button.cancel", "Cancel"));
    progress->setWindowTitle(uiText("dialog.video_export.title", "Export Video"));
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumWidth(320);
    progress->setMaximumWidth(360);
    progress->setValue(0);
    applySystemWindowBackdrop(progress);
    if (QLabel* label = progress->findChild<QLabel*>(); label != nullptr) {
        label->setWordWrap(true);
    }
    progress->show();
    videoExportProgressDialog_ = progress;

    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    videoExportWorkerProcess_ = process;
    videoExportWorkerJobId_ = snapshot.jobId;
    videoExportWorkerOutputPath_ = snapshot.outputPath;
    videoExportWorkerResultMessage_.clear();
    videoExportWorkerResultDetails_.clear();
    videoExportWorkerStdoutBuffer_.clear();
    videoExportWorkerStderrBuffer_.clear();
    videoExportWorkerSuccess_ = false;
    videoExportWorkerCompletionReceived_ = false;
    videoExportWorkerCancelRequested_ = false;
    videoExportWorkerLastProgressPercent_ = 0;
    videoExportWorkerLastEtaSeconds_ = -1;
    videoExportWorkerElapsed_.start();

    connect(progress, &QProgressDialog::canceled, this, &MainWindow::cancelVideoExportWorker);
    connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleVideoExportWorkerStdout);
    connect(process, &QProcess::readyReadStandardError, this, &MainWindow::handleVideoExportWorkerStderr);
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        handleVideoExportWorkerProcessFinished(exitCode, static_cast<int>(exitStatus));
    });

    if (!startVideoExportWorkerProcess(process, snapshot, errorMessage)) {
        clearVideoExportWorkerState();
        return false;
    }
    return true;
}

void MainWindow::handleVideoExportWorkerStdout()
{
    if (videoExportWorkerProcess_ == nullptr) {
        return;
    }
    videoExportWorkerStdoutBuffer_.append(videoExportWorkerProcess_->readAllStandardOutput());
    while (true) {
        const int newlineIndex = videoExportWorkerStdoutBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }
        const QByteArray rawLine = videoExportWorkerStdoutBuffer_.left(newlineIndex).trimmed();
        videoExportWorkerStdoutBuffer_.remove(0, newlineIndex + 1);
        if (rawLine.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(rawLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (!videoExportWorkerResultDetails_.isEmpty()) {
                videoExportWorkerResultDetails_.append('\n');
            }
            videoExportWorkerResultDetails_.append(QString::fromUtf8(rawLine));
            continue;
        }
        handleVideoExportWorkerEvent(document.object());
    }
}

void MainWindow::handleVideoExportWorkerStderr()
{
    if (videoExportWorkerProcess_ == nullptr) {
        return;
    }
    videoExportWorkerStderrBuffer_.append(videoExportWorkerProcess_->readAllStandardError());
}

void MainWindow::handleVideoExportWorkerEvent(const QJsonObject& eventObject)
{
    const QString eventType = eventObject.value(QStringLiteral("event")).toString();
    const bool suppressProgressUi = videoExportWorkerCancelRequested_;
    if (eventType == QLatin1String("worker_ready")) {
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            videoExportProgressDialog_->setValue(qMax(videoExportProgressDialog_->value(), 1));
            QTimer::singleShot(0, this, [this]() {
                if (videoExportProgressDialog_ != nullptr) {
                    videoExportProgressDialog_->setLabelText(
                        uiText("dialog.video_export.progress.worker_ready", "Worker ready...")
                    );
                }
            });
            videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.worker_ready", "Worker ready...")
            );
            videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Worker ready..."),
                QStringLiteral("后台已就绪...")
            ));
        }
        return;
    }
    if (eventType == QLatin1String("accepted")) {
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            videoExportProgressDialog_->setValue(qMax(videoExportProgressDialog_->value(), 2));
            QTimer::singleShot(0, this, [this]() {
                if (videoExportProgressDialog_ != nullptr) {
                    videoExportProgressDialog_->setLabelText(
                        uiText("dialog.video_export.progress.starting_export", "Starting export...")
                    );
                }
            });
            videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.starting_export", "Starting export...")
            );
            videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Starting export..."),
                QStringLiteral("开始导出...")
            ));
        }
        return;
    }
    if (eventType == QLatin1String("progress")) {
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            const int percent = qBound(0, eventObject.value(QStringLiteral("percent")).toInt(), 100);
            const QString rawMessage = eventObject.value(QStringLiteral("message")).toString(
                QStringLiteral("Exporting...")
            );
            const bool busyStage = exportWorkerProgressUsesBusyIndicator(rawMessage);
            if (busyStage) {
                if (videoExportProgressDialog_->minimum() != 0 || videoExportProgressDialog_->maximum() != 0) {
                    videoExportProgressDialog_->setRange(0, 0);
                }
                videoExportProgressDialog_->setLabelText(
                    buildExportProgressLabelTextForUiLanguage(
                        rawMessage,
                        videoExportWorkerLastProgressPercent_,
                        videoExportWorkerElapsed_,
                        &videoExportWorkerLastEtaSeconds_
                    )
                );
                return;
            }
            if (videoExportProgressDialog_->minimum() == 0 && videoExportProgressDialog_->maximum() == 0) {
                videoExportProgressDialog_->setRange(0, 100);
            }
            videoExportWorkerLastProgressPercent_ = qMax(videoExportWorkerLastProgressPercent_, percent);
            videoExportProgressDialog_->setValue(videoExportWorkerLastProgressPercent_);
            videoExportProgressDialog_->setLabelText(
                buildExportProgressLabelTextForUiLanguage(
                    rawMessage,
                    videoExportWorkerLastProgressPercent_,
                    videoExportWorkerElapsed_,
                    &videoExportWorkerLastEtaSeconds_
                )
            );
        }
        return;
    }
    if (eventType == QLatin1String("log")) {
        const QString message = eventObject.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            if (!videoExportWorkerResultDetails_.isEmpty()) {
                videoExportWorkerResultDetails_.append('\n');
            }
            videoExportWorkerResultDetails_.append(message);
        }
        return;
    }
    if (eventType == QLatin1String("finished")) {
        videoExportWorkerCompletionReceived_ = true;
        videoExportWorkerSuccess_ = eventObject.value(QStringLiteral("success")).toBool(false);
        videoExportWorkerOutputPath_ = eventObject.value(QStringLiteral("output_path")).toString(videoExportWorkerOutputPath_);
        videoExportWorkerResultMessage_ = videoExportWorkerSuccess_
            ? QStringLiteral("ok")
            : eventObject.value(QStringLiteral("error")).toString(uiText("dialog.video_export.error.failed", "Export failed."));
        const QString details = eventObject.value(QStringLiteral("details")).toString().trimmed();
        if (!details.isEmpty()) {
            videoExportWorkerResultDetails_ = details;
        }
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            if (videoExportProgressDialog_->minimum() == 0 && videoExportProgressDialog_->maximum() == 0) {
                videoExportProgressDialog_->setRange(0, 100);
            }
            videoExportProgressDialog_->setValue(videoExportWorkerSuccess_ ? 100 : videoExportProgressDialog_->value());
            if (videoExportWorkerSuccess_) {
                videoExportProgressDialog_->setLabelText(systemL10n(
                    QStringLiteral("Done."),
                    QStringLiteral("导出完成。")
                ));
            }
        }
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr && videoExportWorkerSuccess_) {
            videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.done", "Done.")
            );
        }
    }
}

void MainWindow::handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus)
{
    Q_UNUSED(exitCode);

    const auto restorePreviewAspectIfNeeded = [this]() {
        if (!restoreSquareAfterVideoExport_) {
            return;
        }
        restoreSquareAfterVideoExport_ = false;
        setPreviewCanvasAspectRatio(1.0, false);
    };

    if (videoExportProgressDialog_ != nullptr) {
        videoExportProgressDialog_->hide();
    }

    const QString stderrText = QString::fromUtf8(videoExportWorkerStderrBuffer_).trimmed();
    const QString stdoutTailText = QString::fromUtf8(videoExportWorkerStdoutBuffer_).trimmed();
    const QString processErrorText = videoExportWorkerProcess_ != nullptr
        ? videoExportWorkerProcess_->errorString().trimmed()
        : QString();
    const QString workerDiagnostics = buildWorkerProcessDiagnostics(
        exitCode,
        static_cast<QProcess::ExitStatus>(exitStatus),
        processErrorText,
        stderrText,
        stdoutTailText
    );
    const bool canceledOutcome =
        videoExportWorkerCancelRequested_
        && (!videoExportWorkerCompletionReceived_ || !videoExportWorkerSuccess_);
    if (canceledOutcome) {
        restorePreviewAspectIfNeeded();
        showCenteredLocalizedMessageBox(
            QMessageBox::Information,
            this,
            uiText("dialog.video_export.title", "Export Video"),
            uiText("dialog.video_export.message.canceled", "Export canceled.")
        );
        clearVideoExportWorkerState();
        return;
    }

    if (!videoExportWorkerCompletionReceived_) {
        videoExportWorkerSuccess_ = false;
        videoExportWorkerResultMessage_ = exitStatus == static_cast<int>(QProcess::CrashExit)
            ? uiText("dialog.video_export.error.worker_crash", "Export worker crashed.")
            : uiText("dialog.video_export.error.worker_exit", "Export worker exited unexpectedly.");
        videoExportWorkerResultDetails_ = workerDiagnostics;
    } else if (!stderrText.isEmpty() && !videoExportWorkerSuccess_) {
        videoExportWorkerResultDetails_ = appendVideoExportDiagnostics(videoExportWorkerResultDetails_, workerDiagnostics);
    } else if (!videoExportWorkerSuccess_) {
        videoExportWorkerResultDetails_ = appendVideoExportDiagnostics(videoExportWorkerResultDetails_, workerDiagnostics);
    }
    if (!videoExportWorkerSuccess_) {
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/worker"),
            QStringLiteral("%1 | %2")
                .arg(videoExportWorkerResultMessage_.trimmed(), videoExportWorkerResultDetails_.trimmed())
        );
    }

    if (videoExportWorkerSuccess_) {
        const QFileInfo resolvedOutputInfo(videoExportWorkerOutputPath_);
        const QString resolvedOutputName = resolvedOutputInfo.fileName().trimmed().isEmpty()
            ? QDir::toNativeSeparators(videoExportWorkerOutputPath_)
            : resolvedOutputInfo.fileName();
        QMessageBox dialog(
            QMessageBox::Information,
            uiText("dialog.video_export.title", "Export Video"),
            QStringLiteral("%1\n\n%2")
                .arg(uiText("dialog.video_export.message.completed", "Export completed."))
                .arg(resolvedOutputName),
            QMessageBox::NoButton,
            UiDialogs::effectiveParentWidget(this)
        );
        dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        UiDialogs::applyDetachedParentBehavior(&dialog, this);
        QPushButton* openButton = dialog.addButton(
            uiText("action.open", "Open"),
            QMessageBox::AcceptRole
        );
        dialog.addButton(
            uiText("action.close", "Close"),
            QMessageBox::RejectRole
        );
        dialog.setDefaultButton(openButton);
        centerDialogOnAnchor(&dialog, this);
        dialog.exec();
        if (dialog.clickedButton() == openButton) {
            const QFileInfo outputInfo(videoExportWorkerOutputPath_);
            const QString outputDir = outputInfo.absoluteDir().absolutePath();
            if (!outputDir.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
            }
        }
    } else {
        const QString details = videoExportWorkerResultDetails_.trimmed();
        showCenteredLocalizedMessageBox(
            QMessageBox::Critical,
            this,
            uiText("dialog.video_export.error.failed_title", "Export Failed"),
            details.isEmpty()
                ? videoExportWorkerResultMessage_
                : QStringLiteral("%1\n\n%2").arg(videoExportWorkerResultMessage_, details)
        );
    }

    restorePreviewAspectIfNeeded();
    clearVideoExportWorkerState();
}

void MainWindow::cancelVideoExportWorker()
{
    videoExportWorkerCancelRequested_ = true;
    if (videoExportProgressDialog_ != nullptr) {
        QTimer::singleShot(0, this, [this]() {
            if (videoExportProgressDialog_ != nullptr) {
                videoExportProgressDialog_->setLabelText(
                    uiText("dialog.video_export.progress.canceling", "Canceling export...")
                );
            }
        });
    }
    if (videoExportProgressDialog_ != nullptr) {
        videoExportProgressDialog_->setLabelText(systemL10n(
            QStringLiteral("Canceling export..."),
            QStringLiteral("正在取消导出...")
        ));
        videoExportProgressDialog_->hide();
    }
    if (videoExportWorkerProcess_ == nullptr || videoExportWorkerProcess_->state() == QProcess::NotRunning) {
        return;
    }

    QPointer<QProcess> processGuard(videoExportWorkerProcess_);
    videoExportWorkerProcess_->terminate();
    QTimer::singleShot(500, this, [this, processGuard]() {
        if (processGuard.isNull() || processGuard != videoExportWorkerProcess_) {
            return;
        }
        if (processGuard->state() != QProcess::NotRunning) {
            processGuard->kill();
        }
    });
}

void MainWindow::clearVideoExportWorkerState()
{
    if (videoExportProgressDialog_ != nullptr) {
        videoExportProgressDialog_->close();
        videoExportProgressDialog_->deleteLater();
        videoExportProgressDialog_ = nullptr;
    }
    if (videoExportWorkerProcess_ != nullptr) {
        videoExportWorkerProcess_->disconnect(this);
        if (videoExportWorkerProcess_->state() != QProcess::NotRunning) {
            videoExportWorkerProcess_->kill();
            videoExportWorkerProcess_->waitForFinished(2000);
        }
        videoExportWorkerProcess_->deleteLater();
        videoExportWorkerProcess_ = nullptr;
    }
    videoExportWorkerStdoutBuffer_.clear();
    videoExportWorkerStderrBuffer_.clear();
    videoExportWorkerJobId_.clear();
    videoExportWorkerOutputPath_.clear();
    videoExportWorkerResultMessage_.clear();
    videoExportWorkerResultDetails_.clear();
    videoExportWorkerElapsed_.invalidate();
    videoExportWorkerSuccess_ = false;
    videoExportWorkerCompletionReceived_ = false;
    videoExportWorkerCancelRequested_ = false;
    restoreSquareAfterVideoExport_ = false;
    videoExportWorkerLastProgressPercent_ = 0;
    videoExportWorkerLastEtaSeconds_ = -1;
}

