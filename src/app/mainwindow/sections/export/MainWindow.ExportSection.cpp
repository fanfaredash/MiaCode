#include "MainWindow.ExportSection.h"

#include "../window/MainWindow.WindowSection.h"
#include "app/qml_ui/export/QmlExportSession.h"

#include <QPointer>

MainWindow::ExportSection::ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

// ---- miacode::v2::ExportEngine ----
//
// Thin forwarders. The existing widget-side names stay because ~20 call sites
// inside MainWindow use them; the interface names are what the QML page sees.
// When the engine moves out of the window, these bodies move with it and the
// page is untouched.

VideoExportTask MainWindow::ExportSection::buildSeedTask(int difficultyId)
{
    return buildVideoExportSeedTaskPublic(difficultyId);
}

void MainWindow::ExportSection::applySharedTaskSettings(const VideoExportTask& task)
{
    applySharedExportTaskSettings(task);
}

bool MainWindow::ExportSection::startAudition(int difficultyId, const VideoExportTask& visualTask)
{
    return startQmlExportAudition(difficultyId, visualTask);
}

void MainWindow::ExportSection::stopAudition()
{
    stopQmlExportAudition();
}

bool MainWindow::ExportSection::launchVideoExport(const VideoExportTask& requestedTask,
                                                  int difficultyId, QString* errorMessage)
{
    return launchQmlVideoExport(requestedTask, difficultyId, errorMessage);
}

bool MainWindow::ExportSection::launchBatchExport(const VideoExportTask& templateTask,
                                                  const QStringList& chartDirectories,
                                                  const QList<int>& selectedDifficultyIds,
                                                  const QString& outputDirectory,
                                                  BatchResult* result,
                                                  const BatchCallbacks& callbacks,
                                                  QString* errorMessage)
{
    return launchQmlBatchExport(templateTask, chartDirectories, selectedDifficultyIds,
                                outputDirectory, result, callbacks, errorMessage);
}

void MainWindow::ExportSection::cancelVideoExport()
{
    cancelVideoExportWorker();
}

int MainWindow::resolveToolsMenuExportDifficultyId() const
{
    // While the export page is open it owns the difficulty the Tools-menu
    // export actions apply to; MainWindow keeps activeDifficultyId_ == 0 there.
    if (qmlExportSession_ != nullptr && qmlExportSession_->pageSessionActive()) {
        const int selectedDifficultyId = qmlExportSession_->selectedDifficultyId();
        if (SimaiDocument::isDifficultyId(selectedDifficultyId)
            && document_.difficulty(selectedDifficultyId) != nullptr) {
            return selectedDifficultyId;
        }
    }
    return activeDifficultyId_;
}

void MainWindow::onExportCover()
{
    emit coverExportRequested(resolveToolsMenuExportDifficultyId());
}

void MainWindow::onBatchExportPreviewVideo()
{
    const int difficultyId = resolveToolsMenuExportDifficultyId();
    exportSection_->onBatchExportPreviewVideo(difficultyId);
}

void MainWindow::onPackAsZip()
{
    exportSection_->onPackAsZip();
}

bool MainWindow::exportPreviewVideoFromCli(const CliVideoExportRequest& request, QString* resolvedOutputPath, QString* errorMessage, QString* details)
{
    return exportSection_->exportPreviewVideoFromCli(request, resolvedOutputPath, errorMessage, details);
}
