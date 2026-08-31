#include "MainWindow.ExportSection.h"

#include "../window/MainWindow.WindowSection.h"
#include "app/qml_ui/export/QmlExportSession.h"

#include <QPointer>

MainWindow::ExportSection::ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

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
