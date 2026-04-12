#include "MainWindow.ExportSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

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

bool MainWindow::ExportSection::startVideoExportWorkerProcess(QProcess* process, const VideoExportSnapshot& snapshot, QString* errorMessage)
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

bool MainWindow::ExportSection::runVideoExportWorkerSync(
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
    if (!this->startVideoExportWorkerProcess(&process, snapshot, errorMessage)) {
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

void MainWindow::ExportSection::showExportToolbarMenu()
{
    if (owner_.exportVideoButton_ == nullptr || owner_.exportVideoMenu_ == nullptr) {
        return;
    }
    if (!owner_.exportVideoButton_->isVisible() || !owner_.exportVideoButton_->underMouse()) {
        return;
    }
    if (QApplication::mouseButtons().testAnyFlag(Qt::AllButtons)) {
        return;
    }
    if (owner_.exportVideoMenu_->isVisible()) {
        return;
    }
    const QPoint globalPos = owner_.exportVideoButton_->mapToGlobal(QPoint(0, owner_.exportVideoButton_->height()));
    owner_.exportVideoMenu_->popup(globalPos);
}

bool MainWindow::ExportSection::launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    if (owner_.videoExportWorkerProcess_ != nullptr && owner_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.worker_busy", "Another export is already running.");
        }
        return false;
    }

    this->clearVideoExportWorkerState();

    auto* progress = new QProgressDialog(
        systemL10n(QStringLiteral("Preparing export..."), QStringLiteral("准备导出...")),
        systemL10n(QStringLiteral("Cancel"), QStringLiteral("取消")),
        0,
        100,
        &owner_
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
    owner_.windowSection_->applySystemWindowBackdrop(progress);
    if (QLabel* label = progress->findChild<QLabel*>(); label != nullptr) {
        label->setWordWrap(true);
    }
    progress->show();
    owner_.videoExportProgressDialog_ = progress;

    auto* process = new QProcess(&owner_);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    owner_.videoExportWorkerProcess_ = process;
    owner_.videoExportWorkerJobId_ = snapshot.jobId;
    owner_.videoExportWorkerOutputPath_ = snapshot.outputPath;
    owner_.videoExportWorkerResultMessage_.clear();
    owner_.videoExportWorkerResultDetails_.clear();
    owner_.videoExportWorkerStdoutBuffer_.clear();
    owner_.videoExportWorkerStderrBuffer_.clear();
    owner_.videoExportWorkerSuccess_ = false;
    owner_.videoExportWorkerCompletionReceived_ = false;
    owner_.videoExportWorkerCancelRequested_ = false;
    owner_.videoExportWorkerLastProgressPercent_ = 0;
    owner_.videoExportWorkerLastEtaSeconds_ = -1;
    owner_.videoExportWorkerElapsed_.start();

    connect(progress, &QProgressDialog::canceled, &owner_, [this]() {
        this->cancelVideoExportWorker();
    });
    connect(process, &QProcess::readyReadStandardOutput, &owner_, [this]() {
        this->handleVideoExportWorkerStdout();
    });
    connect(process, &QProcess::readyReadStandardError, &owner_, [this]() {
        this->handleVideoExportWorkerStderr();
    });
    connect(process, &QProcess::finished, &owner_, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        this->handleVideoExportWorkerProcessFinished(exitCode, static_cast<int>(exitStatus));
    });

    if (!this->startVideoExportWorkerProcess(process, snapshot, errorMessage)) {
        this->clearVideoExportWorkerState();
        return false;
    }
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
            if (!owner_.videoExportWorkerResultDetails_.isEmpty()) {
                owner_.videoExportWorkerResultDetails_.append('\n');
            }
            owner_.videoExportWorkerResultDetails_.append(QString::fromUtf8(rawLine));
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
    owner_.videoExportWorkerStderrBuffer_.append(owner_.videoExportWorkerProcess_->readAllStandardError());
}

void MainWindow::ExportSection::handleVideoExportWorkerEvent(const QJsonObject& eventObject)
{
    const QString eventType = eventObject.value(QStringLiteral("event")).toString();
    const bool suppressProgressUi = owner_.videoExportWorkerCancelRequested_;
    if (eventType == QLatin1String("worker_ready")) {
        if (!suppressProgressUi && owner_.videoExportProgressDialog_ != nullptr) {
            owner_.videoExportProgressDialog_->setValue(qMax(owner_.videoExportProgressDialog_->value(), 1));
            QTimer::singleShot(0, &owner_, [this]() {
                if (owner_.videoExportProgressDialog_ != nullptr) {
                    owner_.videoExportProgressDialog_->setLabelText(
                        uiText("dialog.video_export.progress.worker_ready", "Worker ready...")
                    );
                }
            });
            owner_.videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.worker_ready", "Worker ready...")
            );
            owner_.videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Worker ready..."),
                QStringLiteral("后台已就绪...")
            ));
        }
        return;
    }
    if (eventType == QLatin1String("accepted")) {
        if (!suppressProgressUi && owner_.videoExportProgressDialog_ != nullptr) {
            owner_.videoExportProgressDialog_->setValue(qMax(owner_.videoExportProgressDialog_->value(), 2));
            QTimer::singleShot(0, &owner_, [this]() {
                if (owner_.videoExportProgressDialog_ != nullptr) {
                    owner_.videoExportProgressDialog_->setLabelText(
                        uiText("dialog.video_export.progress.starting_export", "Starting export...")
                    );
                }
            });
            owner_.videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.starting_export", "Starting export...")
            );
            owner_.videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Starting export..."),
                QStringLiteral("开始导出...")
            ));
        }
        return;
    }
    if (eventType == QLatin1String("progress")) {
        if (!suppressProgressUi && owner_.videoExportProgressDialog_ != nullptr) {
            const int percent = qBound(0, eventObject.value(QStringLiteral("percent")).toInt(), 100);
            const QString rawMessage = eventObject.value(QStringLiteral("message")).toString(
                QStringLiteral("Exporting...")
            );
            const bool busyStage = exportWorkerProgressUsesBusyIndicator(rawMessage);
            if (busyStage) {
                if (owner_.videoExportProgressDialog_->minimum() != 0 || owner_.videoExportProgressDialog_->maximum() != 0) {
                    owner_.videoExportProgressDialog_->setRange(0, 0);
                }
                owner_.videoExportProgressDialog_->setLabelText(
                    buildExportProgressLabelTextForUiLanguage(
                        rawMessage,
                        owner_.videoExportWorkerLastProgressPercent_,
                        owner_.videoExportWorkerElapsed_,
                        &owner_.videoExportWorkerLastEtaSeconds_
                    )
                );
                return;
            }
            if (owner_.videoExportProgressDialog_->minimum() == 0 && owner_.videoExportProgressDialog_->maximum() == 0) {
                owner_.videoExportProgressDialog_->setRange(0, 100);
            }
            owner_.videoExportWorkerLastProgressPercent_ = qMax(owner_.videoExportWorkerLastProgressPercent_, percent);
            owner_.videoExportProgressDialog_->setValue(owner_.videoExportWorkerLastProgressPercent_);
            owner_.videoExportProgressDialog_->setLabelText(
                buildExportProgressLabelTextForUiLanguage(
                    rawMessage,
                    owner_.videoExportWorkerLastProgressPercent_,
                    owner_.videoExportWorkerElapsed_,
                    &owner_.videoExportWorkerLastEtaSeconds_
                )
            );
        }
        return;
    }
    if (eventType == QLatin1String("log")) {
        const QString message = eventObject.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            if (!owner_.videoExportWorkerResultDetails_.isEmpty()) {
                owner_.videoExportWorkerResultDetails_.append('\n');
            }
            owner_.videoExportWorkerResultDetails_.append(message);
        }
        return;
    }
    if (eventType == QLatin1String("finished")) {
        owner_.videoExportWorkerCompletionReceived_ = true;
        owner_.videoExportWorkerSuccess_ = eventObject.value(QStringLiteral("success")).toBool(false);
        owner_.videoExportWorkerOutputPath_ = eventObject.value(QStringLiteral("output_path")).toString(owner_.videoExportWorkerOutputPath_);
        owner_.videoExportWorkerResultMessage_ = owner_.videoExportWorkerSuccess_
            ? QStringLiteral("ok")
            : eventObject.value(QStringLiteral("error")).toString(uiText("dialog.video_export.error.failed", "Export failed."));
        const QString details = eventObject.value(QStringLiteral("details")).toString().trimmed();
        if (!details.isEmpty()) {
            owner_.videoExportWorkerResultDetails_ = details;
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
                uiText("dialog.video_export.progress.done", "Done.")
            );
        }
    }
}

void MainWindow::ExportSection::handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus)
{
    Q_UNUSED(exitCode);

    const auto restorePreviewAspectIfNeeded = [this]() {
        if (!owner_.restoreSquareAfterVideoExport_) {
            return;
        }
        owner_.restoreSquareAfterVideoExport_ = false;
        owner_.setPreviewCanvasAspectRatio(1.0, false);
    };

    if (owner_.videoExportProgressDialog_ != nullptr) {
        owner_.videoExportProgressDialog_->hide();
    }

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
        stdoutTailText
    );
    const bool canceledOutcome =
        owner_.videoExportWorkerCancelRequested_
        && (!owner_.videoExportWorkerCompletionReceived_ || !owner_.videoExportWorkerSuccess_);
    if (canceledOutcome) {
        restorePreviewAspectIfNeeded();
        showCenteredLocalizedMessageBox(
            QMessageBox::Information,
            &owner_,
            uiText("dialog.video_export.title", "Export Video"),
            uiText("dialog.video_export.message.canceled", "Export canceled.")
        );
        this->clearVideoExportWorkerState();
        return;
    }

    if (!owner_.videoExportWorkerCompletionReceived_) {
        owner_.videoExportWorkerSuccess_ = false;
        owner_.videoExportWorkerResultMessage_ = exitStatus == static_cast<int>(QProcess::CrashExit)
            ? uiText("dialog.video_export.error.worker_crash", "Export worker crashed.")
            : uiText("dialog.video_export.error.worker_exit", "Export worker exited unexpectedly.");
        owner_.videoExportWorkerResultDetails_ = workerDiagnostics;
    } else if (!stderrText.isEmpty() && !owner_.videoExportWorkerSuccess_) {
        owner_.videoExportWorkerResultDetails_ = appendVideoExportDiagnostics(owner_.videoExportWorkerResultDetails_, workerDiagnostics);
    } else if (!owner_.videoExportWorkerSuccess_) {
        owner_.videoExportWorkerResultDetails_ = appendVideoExportDiagnostics(owner_.videoExportWorkerResultDetails_, workerDiagnostics);
    }
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
            uiText("dialog.video_export.title", "Export Video"),
            QStringLiteral("%1\n\n%2")
                .arg(uiText("dialog.video_export.message.completed", "Export completed."))
                .arg(resolvedOutputName),
            QMessageBox::NoButton,
            UiDialogs::effectiveParentWidget(&owner_)
        );
        dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        UiDialogs::applyDetachedParentBehavior(&dialog, &owner_);
        QPushButton* openButton = dialog.addButton(
            uiText("action.open", "Open"),
            QMessageBox::AcceptRole
        );
        dialog.addButton(
            uiText("action.close", "Close"),
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
        showCenteredLocalizedMessageBox(
            QMessageBox::Critical,
            &owner_,
            uiText("dialog.video_export.error.failed_title", "Export Failed"),
            details.isEmpty()
                ? owner_.videoExportWorkerResultMessage_
                : QStringLiteral("%1\n\n%2").arg(owner_.videoExportWorkerResultMessage_, details)
        );
    }

    restorePreviewAspectIfNeeded();
    this->clearVideoExportWorkerState();
}

void MainWindow::ExportSection::cancelVideoExportWorker()
{
    owner_.videoExportWorkerCancelRequested_ = true;
    if (owner_.videoExportProgressDialog_ != nullptr) {
        QTimer::singleShot(0, &owner_, [this]() {
            if (owner_.videoExportProgressDialog_ != nullptr) {
                owner_.videoExportProgressDialog_->setLabelText(
                    uiText("dialog.video_export.progress.canceling", "Canceling export...")
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
    if (owner_.videoExportProgressDialog_ != nullptr) {
        owner_.videoExportProgressDialog_->close();
        owner_.videoExportProgressDialog_->deleteLater();
        owner_.videoExportProgressDialog_ = nullptr;
    }
    if (owner_.videoExportWorkerProcess_ != nullptr) {
        owner_.videoExportWorkerProcess_->disconnect(&owner_);
        if (owner_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
            owner_.videoExportWorkerProcess_->kill();
            owner_.videoExportWorkerProcess_->waitForFinished(2000);
        }
        owner_.videoExportWorkerProcess_->deleteLater();
        owner_.videoExportWorkerProcess_ = nullptr;
    }
    owner_.videoExportWorkerStdoutBuffer_.clear();
    owner_.videoExportWorkerStderrBuffer_.clear();
    owner_.videoExportWorkerJobId_.clear();
    owner_.videoExportWorkerOutputPath_.clear();
    owner_.videoExportWorkerResultMessage_.clear();
    owner_.videoExportWorkerResultDetails_.clear();
    owner_.videoExportWorkerElapsed_.invalidate();
    owner_.videoExportWorkerSuccess_ = false;
    owner_.videoExportWorkerCompletionReceived_ = false;
    owner_.videoExportWorkerCancelRequested_ = false;
    owner_.restoreSquareAfterVideoExport_ = false;
    owner_.videoExportWorkerLastProgressPercent_ = 0;
    owner_.videoExportWorkerLastEtaSeconds_ = -1;
}

