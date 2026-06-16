#include "MainWindow.ExportSection.h"

#include "../window/MainWindow.WindowSection.h"
#include "tools/net/NetBatchDownloadDialog.h"

#include <QPointer>

MainWindow::ExportSection::ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::onBatchExportPreviewVideo()
{
    exportSection_->onBatchExportPreviewVideo();
}

void MainWindow::onPackAsZip()
{
    exportSection_->onPackAsZip();
}

void MainWindow::onNetBatchDownload()
{
    exportSection_->onNetBatchDownload();
}

void MainWindow::ExportSection::onNetBatchDownload()
{
    if (owner_.netBatchDownloadDialog_ != nullptr) {
        owner_.netBatchDownloadDialog_->showNormal();
        owner_.netBatchDownloadDialog_->raise();
        owner_.netBatchDownloadDialog_->activateWindow();
        return;
    }

    auto* dialog = new miacode::net::NetBatchDownloadDialog(nullptr);
    owner_.netBatchDownloadDialog_ = dialog;
    if (owner_.windowSection_ != nullptr) {
        owner_.windowSection_->applySystemWindowBackdrop(dialog);
    }
    QObject::connect(dialog, &QObject::destroyed, &owner_, [&owner = owner_]() {
        owner.netBatchDownloadDialog_ = nullptr;
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool MainWindow::exportPreviewVideoFromCli(const CliVideoExportRequest& request, QString* resolvedOutputPath, QString* errorMessage, QString* details)
{
    return exportSection_->exportPreviewVideoFromCli(request, resolvedOutputPath, errorMessage, details);
}
