#pragma once

#include "../../MainWindow.h"

class MainWindow::ExportSection {
public:
    ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void applySharedExportTaskSettings(const VideoExportTask& task);
    void onExportPreviewVideo();
    void onBatchExportPreviewVideo();
    void onPackAsZip();
    bool buildVideoExportSnapshot(
        const VideoExportTask& requestedTask,
        VideoExportSnapshot* snapshot,
        QString* errorMessage
    );
    bool buildVideoExportSnapshotForChartDirectory(
        const QString& chartDirectory,
        int difficultyId,
        const QString& difficultyToken,
        const VideoExportTask& requestedTask,
        const QString& outputDirectory,
        VideoExportSnapshot* snapshot,
        QString* errorMessage
    );
    bool startVideoExportWorkerProcess(
        QProcess* process,
        const VideoExportSnapshot& snapshot,
        QString* errorMessage,
        bool forceDisableOffscreenPbo = false
    );
    bool runVideoExportWorkerSync(
        const VideoExportSnapshot& snapshot,
        QProgressDialog* progressDialog,
        bool* canceledByUser,
        QString* errorMessage,
        const std::function<void(int percent, const QString& rawMessage)>& progressCallback = {}
    );
    void showExportToolbarMenu();
    bool launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage);
    void handleVideoExportWorkerStdout();
    void handleVideoExportWorkerStderr();
    void handleVideoExportWorkerEvent(const QJsonObject& eventObject);
    void handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus);
    void cancelVideoExportWorker();
    void clearVideoExportWorkerState();
    bool exportPreviewVideoFromCli(
        const CliVideoExportRequest& request,
        QString* resolvedOutputPath,
        QString* errorMessage,
        QString* details = nullptr
    );

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
