#pragma once

#include "tools/video_export/VideoExportController.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace miacode::v2 {

// What the export page needs the export engine to do.
//
// Stage 3.5 item 2: QmlExportSession used to reach `MainWindow::videoExport_`
// — a private member holding a 3,600-line partial-class section — which was the
// last direct private-member read anywhere in src/app/qml_ui. That is not a
// coupling an accessor can honestly fix: the QML page was depending on a
// concrete piece of the window's insides.
//
// This is the seam instead. The page depends on these seven operations; the
// engine depends on nothing here. Today MainWindow::ExportSection implements it
// and installs itself into ApplicationServices, so the engine still physically
// lives in the window — moving 3,600 lines of preview-canvas, timeline, SFX,
// HUD and worker-process code is stage 4's job. What changed is the direction:
// when the engine does move, only the implementation side changes, because the
// page has never named it.
//
// Deliberately Qt Widgets-free. `export_engine_spec` links Qt6::Core+Gui only,
// so a QtWidgets type appearing in this contract fails to link.
class ExportEngine
{
public:
    // Outcome of one batch run. `canceled` means the user stopped it, which is
    // not a failure — the successes before the stop still stand.
    struct BatchResult {
        bool canceled = false;
        int successCount = 0;
        QStringList failedCharts;
        QStringList exportedFiles;
    };

    // Batch runs synchronously on the UI thread, so it reports and asks about
    // cancellation through the caller rather than returning at the end.
    struct BatchCallbacks {
        std::function<void(int percent, const QString& label)> progressChanged;
        std::function<bool()> cancellationRequested;
        std::function<void()> retrying;
    };

    virtual ~ExportEngine() = default;

    // The export task a difficulty starts from: markers, muri, render settings,
    // skin/outline, chart metadata and the intro payload. difficultyId 0 means
    // the active difficulty.
    virtual VideoExportTask buildSeedTask(int difficultyId) = 0;

    // Push the page's live edits onto the on-screen preview so what the user is
    // watching matches what the export will render.
    virtual void applySharedTaskSettings(const VideoExportTask& task) = 0;

    // Install the badge-selected difficulty as a real, playable preview source,
    // so the normal transport plays it even when it is not the active
    // difficulty. Returns false when it could not be installed.
    virtual bool startAudition(int difficultyId, const VideoExportTask& visualTask) = 0;
    virtual void stopAudition() = 0;

    // Start the single-chart export. Returns false and fills `errorMessage`
    // when the job could not be launched at all; a job that starts and later
    // fails reports through the engine's own progress path.
    virtual bool launchVideoExport(const VideoExportTask& requestedTask, int difficultyId,
                                   QString* errorMessage) = 0;

    virtual bool launchBatchExport(const VideoExportTask& templateTask,
                                   const QStringList& chartDirectories,
                                   const QList<int>& selectedDifficultyIds,
                                   const QString& outputDirectory,
                                   BatchResult* result,
                                   const BatchCallbacks& callbacks,
                                   QString* errorMessage) = 0;

    // Stops the single-chart export worker. Batch cancellation goes through
    // BatchCallbacks::cancellationRequested instead, because that run is
    // synchronous and has no worker process to signal.
    virtual void cancelVideoExport() = 0;

    // What the page needs about the document to build its difficulty list and
    // pick a default. These read the same copy the export snapshot is built
    // from, which is why they belong here rather than on ChartWorkspace — see
    // the deferred-sync hazard recorded in
    // docs/specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md.
    virtual QList<int> difficultyIds() const = 0;
    virtual QString difficultyChartText(int difficultyId) const = 0;
    // Which difficulty this project was last opened on; seeds the badge.
    virtual int lastOpenedDifficultyId() const = 0;

    // The 无理 overlay options the export task is seeded from.
    virtual MuriRenderOptions muriRenderOptions() const = 0;

    // Where the live playhead is, for seeding the export range from the
    // current position.
    virtual double currentAudioClockSecond() const = 0;
    // Re-derives the negative-time intro region after an intro edit.
    virtual void refreshIntroState() = 0;

protected:
    ExportEngine() = default;
    ExportEngine(const ExportEngine&) = default;
    ExportEngine& operator=(const ExportEngine&) = default;
};

}  // namespace miacode::v2
