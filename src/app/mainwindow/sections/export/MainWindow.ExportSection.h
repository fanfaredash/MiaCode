#pragma once

#include "../../MainWindow.h"

class MainWindow::ExportSection {
public:
    ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void applySharedExportTaskSettings(const VideoExportTask& task);
    void onExportPreviewVideo();
    // Toolbar Export dropdown → 导出封面: opens the cover composer directly
    // (no video-export dialog in between).
    void onExportCover();
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
    // Banner-card payload (title/artist/designer/level/difficulty/bpm/mode/jacket)
    // for the currently active difficulty — seeds VideoExportTask::intro so the
    // cover composer can render the card without a full snapshot.
    IntroBannerSpec buildActiveDifficultyIntroBannerSpec() const;
    // Seed task shared by the export dialog and the direct cover export (markers
    // + muri + render settings + skin/outline + chart metadata + intro payload).
    // Callers validate hasActiveDifficulty()/previewCanvas_ and pause playback.
    VideoExportTask buildVideoExportSeedTask();

    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
