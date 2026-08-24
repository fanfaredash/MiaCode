#pragma once

#include "../../MainWindow.h"

class MainWindow::ExportSection {
public:
    ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void applySharedExportTaskSettings(const VideoExportTask& task);
    // The three dialog entry slots take an explicit difficulty id; the
    // default 0 resolves to the active difficulty (the pre-export-page
    // behavior, used by the menu actions). The Export hub page passes its
    // selected badge — its stack page keeps activeDifficultyId_ == 0, so an
    // explicit id is the only way to target a difficulty from there.
    void onExportPreviewVideo(int difficultyId = 0);
    // Export-page card / Tools menu → 导出封面: opens the cover composer
    // directly (no video-export dialog in between).
    void onExportCover(int difficultyId = 0);
    // Batch export is inherently multi-difficulty; the explicit id only
    // seeds the dialog's default difficulty token.
    void onBatchExportPreviewVideo(int difficultyId = 0);
    void onPackAsZip();
    void onNetBatchDownload();
    void onNetBatchUpload();
    // ---- E-C embedded video panel (hosted by the Export hub page) ----
    // Builds a VideoExportDialog in embedded panel mode seeded with the given
    // difficulty, begins the export-preview session (exportPreviewActive_,
    // debug-HUD suppression, chart-info HUD), and wires exportConfirmed to a
    // snapshot + worker launch with the normal progress popup.
    // Returns nullptr when the difficulty/preview isn't
    // available. The host inserts the widget into its layout; ownership stays
    // with this section (destroyEmbeddedVideoExportPanel deletes it).
    QWidget* createEmbeddedVideoExportPanel(int difficultyId, QWidget* parent);
    // Finalizes the embedded panel session (range-preview stop + live-preview
    // restore + export-preview session end) and deletes the panel. Idempotent;
    // a running worker is NOT cancelled.
    void destroyEmbeddedVideoExportPanel();
    QWidget* createEmbeddedBatchExportPanel(int difficultyId, QWidget* parent);
    void updateEmbeddedBatchExportPreviewDifficulty(int difficultyId);
    void destroyEmbeddedBatchExportPanel();
    bool buildVideoExportSnapshot(
        const VideoExportTask& requestedTask,
        VideoExportSnapshot* snapshot,
        QString* errorMessage,
        int difficultyId = 0
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
    // for the given difficulty — seeds VideoExportTask::intro so the cover
    // composer can render the card without a full snapshot.
    IntroBannerSpec buildIntroBannerSpecForDifficulty(int difficultyId) const;
    // Seed task shared by the export dialog and the direct cover export (markers
    // + muri + render settings + skin/outline + chart metadata + intro payload).
    // difficultyId 0 = active difficulty; an explicit non-active id parses
    // that difficulty's chart directly (the live timeline belongs to the
    // active one). Callers validate the difficulty/previewCanvas_ and pause
    // playback first.
    VideoExportTask buildVideoExportSeedTask(int difficultyId = 0);
    // Parse + &first-shift note markers for an arbitrary difficulty of the
    // LIVE document, mirroring the worker-side snapshot rebuild
    // (buildVideoExportTaskFromSnapshot). Empty when the difficulty has no
    // parseable chart body.
    QVector<TimelineNoteMarker> buildParsedMarkersForDifficulty(int difficultyId) const;
    // Constructs a VideoExportDialog wired with the full set of live-preview
    // callbacks + the owner-wired Gameplay injection. Shared by the modal
    // (Tools menu) path and the embedded panel; caller owns the returned
    // dialog.
    VideoExportDialog* buildConfiguredVideoExportDialog(const VideoExportTask& task, QWidget* parent);
    // The preview-state bracket both paths wrap around the dialog/panel
    // lifetime: exportPreviewActive_ + debug-HUD suppression + chart-info
    // HUD on begin; full restore + aspect reset on end.
    void beginExportPreviewSession(const VideoExportTask& task);
    void endExportPreviewSession();
    // Pushes the seed task's per-difficulty metadata into the preview canvas'
    // chart-info HUD. Shared by beginExportPreviewSession and the batch page's
    // in-place difficulty retarget (which keeps the session open), so the two
    // can never disagree about what the HUD shows.
    void applyExportPreviewChartInfo(const VideoExportTask& task);
    // Export-page preview audition: install the badge-selected difficulty as a
    // real, playable preview source (markers + bottom-timeline + slider + SFX),
    // so the normal transport plays/seeks it even though activeDifficultyId_==0.
    // Mirrors the latency page's installSandboxScene. Teardown clears the flag
    // and invalidates the snapshot so the next difficulty switch rebuilds.
    void installExportPreviewAuditionScene(int difficultyId);
    void teardownExportPreviewAuditionScene();
    void handleEmbeddedExportConfirmed();
    void handleBatchExportConfirmed();
    // ---- Inline export progress on the preview transport (A3 amended) ----
    void beginInlineExportProgress();
    // percent < 0 keeps the current percent (label-only update); an empty
    // label keeps the current label.
    void updateInlineExportProgress(int percent, const QString& label);
    void endInlineExportProgress();

    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
