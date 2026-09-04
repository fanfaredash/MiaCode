#include "MainEntrypoints.h"

#include "tools/video_export/VideoExportSnapshot.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/WaveformCache.h"

#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QTextStream>

namespace {

QString videoExportWorkerProjectLogDirectory(const VideoExportSnapshot& snapshot)
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

void writeWorkerJsonLine(const QJsonObject& object)
{
    QTextStream out(stdout);
    out << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
    out.flush();
}

}  // namespace

namespace miacode::app::entry {

int runCliVideoExportWorker(QGuiApplication& app, QString* errorMessage)
{
    MC_OP("runCliVideoExportWorker");
    QString workerJobId;
    try {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MiaCode export worker"));
    parser.addHelpOption();
    parser.addVersionOption();
    addSharedCliDebugOption(parser);
    parser.addOption(QCommandLineOption(
        QStringLiteral("export-video-worker"),
        QStringLiteral("Run background export worker and exit.")
    ));

    if (!parser.parse(app.arguments())) {
        if (errorMessage != nullptr) {
            *errorMessage = parser.errorText();
        }
        return 2;
    }
    if (!parser.isSet(QStringLiteral("export-video-worker"))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("internal CLI dispatch error: --export-video-worker not set");
        }
        return 2;
    }

    writeWorkerJsonLine(QJsonObject{
        {QStringLiteral("event"), QStringLiteral("worker_ready")},
        {QStringLiteral("protocol"), 1},
    });

    QFile stdinFile;
    if (!stdinFile.open(stdin, QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to open stdin for export worker");
        }
        return 1;
    }

    const QList<QByteArray> inputLines = stdinFile.readAll().split('\n');
    QByteArray rawCommand;
    for (const QByteArray& line : inputLines) {
        if (!line.trimmed().isEmpty()) {
            rawCommand = line.trimmed();
            break;
        }
    }
    if (rawCommand.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker received empty command payload");
        }
        return 1;
    }

    QJsonParseError parseError;
    const QJsonDocument commandDocument = QJsonDocument::fromJson(rawCommand, &parseError);
    if (parseError.error != QJsonParseError::NoError || !commandDocument.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker failed to parse command JSON");
        }
        return 1;
    }

    const QJsonObject commandObject = commandDocument.object();
    if (commandObject.value(QStringLiteral("cmd")).toString() != QLatin1String("start_export")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unsupported export worker command");
        }
        return 1;
    }

    VideoExportSnapshot snapshot;
    QString snapshotError;
    if (!VideoExportSnapshot::fromJson(commandObject.value(QStringLiteral("snapshot")).toObject(), &snapshot, &snapshotError)) {
        if (errorMessage != nullptr) {
            *errorMessage = snapshotError;
        }
        return 1;
    }
    workerJobId = snapshot.jobId;
    miacode::debug_log::setSessionProjectLogDirectory(videoExportWorkerProjectLogDirectory(snapshot));
    // Co-locate the startup beacon and op-chain shadow with the worker's
    // runtime/export logs under the project's .miacode/logs/.
    {
        const QString projectLogDir = videoExportWorkerProjectLogDirectory(snapshot);
        if (!projectLogDir.isEmpty()) {
            miacode::oplog::relocateLogs(projectLogDir);
        }
    }
    // Re-emit the P0/P2/P3 startup diagnostics into the worker's now-bound
    // project log so the export-worker's GPU policy (which the GUI forwards via
    // env) is visible alongside the GUI's in the same collected .miacode/logs/.
    logProcessStartupDiagnostics(QStringLiteral("log_dir_rebound"));

    writeWorkerJsonLine(QJsonObject{
        {QStringLiteral("event"), QStringLiteral("accepted")},
        {QStringLiteral("job_id"), snapshot.jobId},
    });

    VideoExportTask task;
    QString taskError;
    if (!buildVideoExportTaskFromSnapshot(snapshot, &task, &taskError)) {
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("finished")},
            {QStringLiteral("job_id"), snapshot.jobId},
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("Failed to prepare export task.")},
            {QStringLiteral("details"), taskError},
        });
        return 1;
    }

    const auto progressCallback = [&snapshot](int percent, const QString& text) {
        if (percent < 0 && text.isEmpty()) {
            return false;
        }
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("progress")},
            {QStringLiteral("job_id"), snapshot.jobId},
            {QStringLiteral("stage"), QStringLiteral("render")},
            {QStringLiteral("percent"), percent},
            {QStringLiteral("message"), text},
        });
        QCoreApplication::processEvents();
        return false;
    };

    const VideoExportResult result = VideoExportController::exportPreparedTask(task, progressCallback);

    QJsonObject finishedObject{
        {QStringLiteral("event"), QStringLiteral("finished")},
        {QStringLiteral("job_id"), snapshot.jobId},
        {QStringLiteral("success"), result.success},
    };
    if (result.success) {
        finishedObject.insert(QStringLiteral("output_path"), task.outputPath);
    } else {
        finishedObject.insert(QStringLiteral("error"), result.message);
        finishedObject.insert(QStringLiteral("details"), result.details);
    }
    writeWorkerJsonLine(finishedObject);
    return result.success ? 0 : 1;
    } catch (...) {
        const QString detail = currentExceptionDetail();
        const QString error = QStringLiteral("Unhandled export worker exception.");
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/worker_exception"),
            QStringLiteral("%1 details=%2").arg(error, detail)
        );
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("finished")},
            {QStringLiteral("job_id"), workerJobId},
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), error},
            {QStringLiteral("details"), detail},
        });
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 %2").arg(error, detail);
        }
        return 1;
    }
}

}  // namespace miacode::app::entry
