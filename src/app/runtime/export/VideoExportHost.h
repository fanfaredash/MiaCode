#pragma once

#include "runtime/Session.h"

#include "app/v2/ExportEngine.h"

// Implements the export page's engine contract. The QML page names
// miacode::v2::ExportEngine, never this class — which is what lets the
// implementation move out of Session in stage 4 without the page noticing.
namespace miacode::runtime {

class VideoExportHost final : public miacode::v2::ExportEngine {
public:
    // The batch types are the interface's; these aliases keep the ~20 existing
    // widget-side call sites spelled as they were.
    using BatchExportResult = miacode::v2::ExportEngine::BatchResult;
    using BatchExportCallbacks = miacode::v2::ExportEngine::BatchCallbacks;

    VideoExportHost(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state);

    // ---- miacode::v2::ExportEngine ----
    VideoExportTask buildSeedTask(int difficultyId) override;
    void applySharedTaskSettings(const VideoExportTask& task) override;
    bool startAudition(int difficultyId, const VideoExportTask& visualTask) override;
    void stopAudition() override;
    bool launchVideoExport(const VideoExportTask& requestedTask, int difficultyId,
                           QString* errorMessage) override;
    bool launchBatchExport(const VideoExportTask& templateTask,
                           const QStringList& chartDirectories,
                           const QList<int>& selectedDifficultyIds,
                           const QString& outputDirectory,
                           BatchResult* result,
                           const BatchCallbacks& callbacks,
                           QString* errorMessage) override;
    void cancelVideoExport() override;
    QList<int> difficultyIds() const override;
    QString difficultyChartText(int difficultyId) const override;
    int lastOpenedDifficultyId() const override;
    MuriRenderOptions muriRenderOptions() const override;
    double currentAudioClockSecond() const override;
    void refreshIntroState() override;

    void applySharedExportTaskSettings(const VideoExportTask& task);
    // The three dialog entry slots take an explicit difficulty id; the
    // default 0 resolves to the active difficulty (the pre-export-page
    // behavior, used by the menu actions). The Export hub page passes its
    // selected badge — its stack page keeps activeDifficultyId_ == 0, so an
    // explicit id is the only way to target a difficulty from there.
    void onExportPreviewVideo(int difficultyId = 0);
    // Batch export is inherently multi-difficulty; the explicit id only
    // seeds the dialog's default difficulty token.
    void onBatchExportPreviewVideo(int difficultyId = 0);
    void onPackAsZip();
    // Continuation of onPackAsZip once the shell has answered the save picker.
    // An empty path means the pick was cancelled.
    void packChartToZipAtPath(const QString& chartText,
                              const QString& chartPath,
                              const QString& dialogTitle,
                              const QString& pickedPath);
    // ---- QML export shell (v2) ----
    VideoExportTask buildVideoExportSeedTaskPublic(int difficultyId = 0);
    bool startQmlExportAudition(int difficultyId, const VideoExportTask& visualTask);
    void stopQmlExportAudition();
    bool launchQmlVideoExport(const VideoExportTask& requestedTask, int difficultyId, QString* errorMessage);
    bool launchQmlBatchExport(
        const VideoExportTask& templateTask,
        const QStringList& chartDirectories,
        const QList<int>& selectedDifficultyIds,
        const QString& outputDirectory,
        BatchExportResult* result,
        const BatchExportCallbacks& callbacks,
        QString* errorMessage);

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
        bool* canceledByUser,
        QString* errorMessage,
        const std::function<void(int percent, const QString& rawMessage)>& progressCallback = {},
        const std::function<bool()>& cancellationRequested = {},
        const std::function<void()>& retryingCallback = {}
    );
    bool launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage);
    void handleVideoExportWorkerStdout();
    void handleVideoExportWorkerStderr();
    void handleVideoExportWorkerEvent(const QJsonObject& eventObject);
    void handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus);
    void cancelVideoExportWorker();
    void clearVideoExportWorkerState();
    bool exportPreviewVideoFromCli(
        const Session::CliVideoExportRequest& request,
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
    // active one). Callers validate the difficulty/scene_ and pause
    // playback first.
    VideoExportTask buildVideoExportSeedTask(int difficultyId = 0);
    // Parse + &first-shift note markers for an arbitrary difficulty of the
    // LIVE document, mirroring the worker-side snapshot rebuild
    // (buildVideoExportTaskFromSnapshot). Empty when the difficulty has no
    // parseable chart body.
    QVector<TimelineNoteMarker> buildParsedMarkersForDifficulty(int difficultyId) const;
    // The preview-state bracket the export page wraps around its session:
    // exportPreviewActive_ + debug-HUD suppression + chart-info HUD on begin;
    // full restore + aspect reset on end.
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
    bool runBatchExport(
        const VideoExportTask& templateTask,
        const QStringList& chartDirectories,
        const QList<int>& selectedDifficultyIds,
        const QString& outputDirectory,
        BatchExportResult* result,
        const BatchExportCallbacks& callbacks,
        QString* errorMessage);
    // ---- Inline export progress on the preview transport (A3 amended) ----
    void reportExportProgress(int percent, const QString& label);
    // percent < 0 keeps the current percent (label-only update); an empty
    // label keeps the current label.
    void endExportProgress();

    Session& session_;
    RuntimeContext::Ui& ui_;
    RuntimeContext::State& state_;
};

}  // namespace miacode::runtime
