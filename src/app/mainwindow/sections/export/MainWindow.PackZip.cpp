#include "MainWindow.ExportSection.h"
#include "../../MainWindowShared.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../window/MainWindow.WindowSection.h"

#include "DialogLocalization.h"
#include "UiText.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "tools/zip_export/ChartZipPackager.h"

#include <QtCore>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

void MainWindow::ExportSection::onPackAsZip()
{
    MC_OP("MainWindow::ExportSection::onPackAsZip");

    const QString dialogTitle = UiText::text(QStringLiteral("export.export_as_zip"));

    // Flush the in-progress editor field into the document so the packaged
    // maidata.txt matches what the user sees (same contract as save-to-path).
    if (owner_.documentSection_ != nullptr) {
        owner_.documentSection_->applyCurrentFieldToDocument();
    }

    // Prefer the live title edit when the metadata page is showing, exactly
    // like the video-export path does, so the default name tracks edits that
    // haven't been committed to a difficulty yet.
    QString chartTitle = owner_.document_.title;
    if (owner_.editorStack_ != nullptr
        && owner_.editorStack_->currentWidget() == owner_.metadataPage_
        && owner_.titleEdit_ != nullptr) {
        chartTitle = owner_.titleEdit_->text();
    }

    const QString chartText = owner_.document_.toText();
    if (chartText.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty chart"));
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            dialogTitle,
            UiText::text(QStringLiteral("export.the_chart_is_empty_there")));
        return;
    }

    const QString chartPath = owner_.currentFilePath_;
    QString defaultDir = chartPath.isEmpty()
        ? QString()
        : QFileInfo(chartPath).absolutePath();
    if (defaultDir.isEmpty() && owner_.documentSection_ != nullptr) {
        defaultDir = owner_.documentSection_->resolveInitialOpenDirectory();
    }
    const QString defaultName = miacode::zip_export::sanitizedZipStem(chartTitle) + QStringLiteral(".zip");
    const QString defaultPath = defaultDir.isEmpty()
        ? defaultName
        : QDir(defaultDir).filePath(defaultName);

    QString outputPath = QFileDialog::getSaveFileName(
        &owner_,
        dialogTitle,
        defaultPath,
        QStringLiteral("ZIP (*.zip)"));
    if (outputPath.isEmpty()) {
        return;
    }
    if (!outputPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        outputPath += QStringLiteral(".zip");
    }
    owner_.setLastOpenDirectory(outputPath);
    _mc_op_.note(QStringLiteral("out=%1").arg(outputPath));

    // Progress dialog modeled on the batch-export one: window-modal, shows
    // immediately, cancelable.
    // macOS: parent through effectiveParentWidget. MainWindow is a
    // WA_DontShowOnScreen quick-shell host, and a window-modal dialog parented
    // to it becomes a macOS sheet that orderFronts the hidden window as an
    // empty white shell. Windows keeps the original direct parenting.
#ifdef Q_OS_MACOS
    QWidget* const progressParent = UiDialogs::effectiveParentWidget(&owner_);
#else
    QWidget* const progressParent = &owner_;
#endif
    QProgressDialog progress(
        UiText::text(QStringLiteral("export.preparing_package")),
        UiText::text(QStringLiteral("action.cancel")),
        0,
        100,
        progressParent);
    progress.setWindowTitle(dialogTitle);
    progress.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    // Non-minimizable: a minimized, parentless progress popup over the
    // WA_DontShowOnScreen quick-shell host can never be re-raised and deadlocks
    // the app while it stays application-modal.
    progress.setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    progress.setWindowModality(Qt::WindowModal);
#ifdef Q_OS_MACOS
    UiDialogs::applyDetachedParentBehavior(&progress, &owner_);
#endif
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    UiDialogs::configureDialogPreviewShortcuts(&progress);
    owner_.windowSection_->applySystemWindowBackdrop(&progress);
    progress.show();

    miacode::zip_export::ChartZipInput input;
    input.chartText = chartText;
    input.chartPath = chartPath;
    input.videoFieldValue = owner_.document_.videoPath;
    input.outputZipPath = outputPath;

    const auto onProgress = [&progress](int current, int total, const QString& entryName) -> bool {
        const int safeTotal = qMax(1, total);
        progress.setValue(qBound(0, qRound(static_cast<double>(current - 1) * 100.0 / safeTotal), 100));
        progress.setLabelText(
            UiText::text(QStringLiteral("export.packaging_1_2_3"))
                .arg(current)
                .arg(total)
                .arg(entryName));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !progress.wasCanceled();
    };

    const miacode::zip_export::ChartZipResult result =
        miacode::zip_export::packChartToZip(input, onProgress);

    progress.setValue(100);
    progress.hide();

    if (result.canceled) {
        _mc_op_.note(QStringLiteral("canceled"));
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            &owner_,
            dialogTitle,
            UiText::text(QStringLiteral("export.packaging_canceled")));
        return;
    }

    if (!result.ok) {
        _mc_op_.fail(result.errorMessage);
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            dialogTitle,
            UiText::text(QStringLiteral("export.packaging_failed_1"))
                .arg(result.errorMessage));
        return;
    }

    QString details = result.includedEntries.join(QLatin1Char('\n'));
    if (details.size() > 3000) {
        details = details.left(3000) + QStringLiteral("\n...");
    }
    const QString body =
        UiText::text(QStringLiteral("export.exported_to_1_2_file"))
            .arg(QDir::toNativeSeparators(outputPath))
            .arg(result.includedEntries.size())
            .arg(details);

    QMessageBox dialog(
        QMessageBox::Information,
        dialogTitle,
        body,
        QMessageBox::NoButton,
        UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    UiDialogs::configureDialogPreviewShortcuts(&dialog);
    UiDialogs::applyDetachedParentBehavior(&dialog, &owner_);

    QPushButton* openFolderButton = dialog.addButton(
        UiText::text(QStringLiteral("action.open_folder")),
        QMessageBox::AcceptRole);
    dialog.addButton(
        UiText::text(QStringLiteral("action.close")),
        QMessageBox::RejectRole);
    dialog.setDefaultButton(openFolderButton);

    dialog.exec();

    if (dialog.clickedButton() == openFolderButton) {
        const QFileInfo zipFileInfo(outputPath);
        const QString dir = zipFileInfo.absoluteDir().absolutePath();
        if (!dir.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        }
    }
}
