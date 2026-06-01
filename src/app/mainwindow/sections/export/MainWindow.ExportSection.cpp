#include "MainWindow.ExportSection.h"

MainWindow::ExportSection::ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::onExportPreviewVideo()
{
    exportSection_->onExportPreviewVideo();
}

void MainWindow::onBatchExportPreviewVideo()
{
    exportSection_->onBatchExportPreviewVideo();
}

void MainWindow::onPackAsZip()
{
    exportSection_->onPackAsZip();
}

bool MainWindow::exportPreviewVideoFromCli(const CliVideoExportRequest& request, QString* resolvedOutputPath, QString* errorMessage, QString* details)
{
    return exportSection_->exportPreviewVideoFromCli(request, resolvedOutputPath, errorMessage, details);
}
