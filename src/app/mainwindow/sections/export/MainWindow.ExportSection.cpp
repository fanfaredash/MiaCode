#include "MainWindow.ExportSection.h"

MainWindow::ExportSection::ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::ExportSection::applySharedExportTaskSettings(const VideoExportTask& task)
{
    owner_.applySharedExportTaskSettings(task);
}

void MainWindow::ExportSection::onExportPreviewVideo()
{
    owner_.onExportPreviewVideo();
}

void MainWindow::ExportSection::onBatchExportPreviewVideo()
{
    owner_.onBatchExportPreviewVideo();
}

bool MainWindow::ExportSection::buildVideoExportSnapshot(const VideoExportTask& requestedTask, VideoExportSnapshot* snapshot, QString* errorMessage)
{
    return owner_.buildVideoExportSnapshot(requestedTask, snapshot, errorMessage);
}

bool MainWindow::ExportSection::buildVideoExportSnapshotForChartDirectory(const QString& chartDirectory, int difficultyId, const QString& difficultyToken, const VideoExportTask& requestedTask, const QString& outputDirectory, VideoExportSnapshot* snapshot, QString* errorMessage)
{
    return owner_.buildVideoExportSnapshotForChartDirectory(chartDirectory, difficultyId, difficultyToken, requestedTask, outputDirectory, snapshot, errorMessage);
}

bool MainWindow::ExportSection::startVideoExportWorkerProcess(QProcess* process, const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    return owner_.startVideoExportWorkerProcess(process, snapshot, errorMessage);
}

bool MainWindow::ExportSection::runVideoExportWorkerSync(const VideoExportSnapshot& snapshot, QProgressDialog* progressDialog, bool* canceledByUser, QString* errorMessage, const std::function<void(int percent, const QString& rawMessage)>& progressCallback)
{
    return owner_.runVideoExportWorkerSync(snapshot, progressDialog, canceledByUser, errorMessage, progressCallback);
}

void MainWindow::ExportSection::showExportToolbarMenu()
{
    owner_.showExportToolbarMenu();
}

bool MainWindow::ExportSection::launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    return owner_.launchVideoExportWorker(snapshot, errorMessage);
}

void MainWindow::ExportSection::handleVideoExportWorkerStdout()
{
    owner_.handleVideoExportWorkerStdout();
}

void MainWindow::ExportSection::handleVideoExportWorkerStderr()
{
    owner_.handleVideoExportWorkerStderr();
}

void MainWindow::ExportSection::handleVideoExportWorkerEvent(const QJsonObject& eventObject)
{
    owner_.handleVideoExportWorkerEvent(eventObject);
}

void MainWindow::ExportSection::handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus)
{
    owner_.handleVideoExportWorkerProcessFinished(exitCode, exitStatus);
}

void MainWindow::ExportSection::cancelVideoExportWorker()
{
    owner_.cancelVideoExportWorker();
}

void MainWindow::ExportSection::clearVideoExportWorkerState()
{
    owner_.clearVideoExportWorkerState();
}

bool MainWindow::ExportSection::exportPreviewVideoFromCli(const CliVideoExportRequest& request, QString* resolvedOutputPath, QString* errorMessage, QString* details)
{
    return owner_.exportPreviewVideoFromCli(request, resolvedOutputPath, errorMessage, details);
}
