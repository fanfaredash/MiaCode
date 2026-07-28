#include "MainWindow.ExportSection.h"

#include "../window/MainWindow.WindowSection.h"
#include "tools/export_page/ExportLauncherPage.h"
#include "tools/net/NetBatchDownloadDialog.h"
#include "tools/net/NetBatchUploadDialog.h"

#include <QPointer>

MainWindow::ExportSection::ExportSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

int MainWindow::resolveToolsMenuExportDifficultyId() const
{
    if (exportPage_ != nullptr) {
        const int pageDifficultyId = exportPage_->menuActionDifficultyId();
        if (SimaiDocument::isDifficultyId(pageDifficultyId)
            && document_.difficulty(pageDifficultyId) != nullptr) {
            return pageDifficultyId;
        }
        const int selectedDifficultyId = exportPage_->selectedDifficultyId();
        if (SimaiDocument::isDifficultyId(selectedDifficultyId)
            && document_.difficulty(selectedDifficultyId) != nullptr) {
            return selectedDifficultyId;
        }
    }
    return activeDifficultyId_;
}

void MainWindow::onExportCover()
{
    const int difficultyId = resolveToolsMenuExportDifficultyId();
    exportSection_->onExportCover(difficultyId);
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

void MainWindow::onNetBatchDownload()
{
    exportSection_->onNetBatchDownload();
}

void MainWindow::onNetBatchUpload()
{
    exportSection_->onNetBatchUpload();
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

void MainWindow::ExportSection::onNetBatchUpload()
{
    if (owner_.netBatchUploadDialog_ != nullptr) {
        owner_.netBatchUploadDialog_->showNormal();
        owner_.netBatchUploadDialog_->raise();
        owner_.netBatchUploadDialog_->activateWindow();
        return;
    }

    auto* dialog = new miacode::net::NetBatchUploadDialog(nullptr);
    owner_.netBatchUploadDialog_ = dialog;
    if (owner_.windowSection_ != nullptr) {
        owner_.windowSection_->applySystemWindowBackdrop(dialog);
    }
    QObject::connect(dialog, &QObject::destroyed, &owner_, [&owner = owner_]() {
        owner.netBatchUploadDialog_ = nullptr;
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool MainWindow::exportPreviewVideoFromCli(const CliVideoExportRequest& request, QString* resolvedOutputPath, QString* errorMessage, QString* details)
{
    return exportSection_->exportPreviewVideoFromCli(request, resolvedOutputPath, errorMessage, details);
}
