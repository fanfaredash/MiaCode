#include "MainWindow.ExportSection.h"
#include "../../MainWindowShared.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../window/MainWindow.WindowSection.h"

#include "UiText.h"
#include "app/v2/JobProgressService.h"
#include "app/v2/UiRequestService.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "tools/zip_export/ChartZipPackager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QUrl>

using namespace miacode::mainwindow::shared;

void MainWindow::ExportSection::onPackAsZip()
{
    MC_OP("MainWindow::ExportSection::onPackAsZip");

    miacode::v2::UiRequestService* const requests = owner_.uiRequestService();
    if (requests == nullptr) {
        _mc_op_.fail(QStringLiteral("ui request service unavailable"));
        return;
    }

    const QString dialogTitle = UiText::text(QStringLiteral("export.export_as_zip"));

    // Flush the in-progress editor field into the document so the packaged
    // maidata.txt matches what the user sees (same contract as save-to-path).
    if (owner_.documentSection_ != nullptr) {
        owner_.documentSection_->applyCurrentFieldToDocument();
    }

    const QString chartText = owner_.document_.toText();
    if (chartText.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty chart"));
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning,
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
    const QString defaultName =
        miacode::zip_export::sanitizedZipStem(owner_.document_.title) + QStringLiteral(".zip");

    miacode::v2::FileRequest request;
    request.title = dialogTitle;
    request.startPath = defaultDir.isEmpty() ? defaultName : QDir(defaultDir).filePath(defaultName);
    request.nameFilters = QStringList{QStringLiteral("ZIP (*.zip)")};
    request.saveMode = true;
    requests->requestFile(request, [this, chartText, chartPath, dialogTitle](const QString& picked) {
        packChartToZipAtPath(chartText, chartPath, dialogTitle, picked);
    });
}

void MainWindow::ExportSection::packChartToZipAtPath(
    const QString& chartText,
    const QString& chartPath,
    const QString& dialogTitle,
    const QString& pickedPath)
{
    MC_OP("MainWindow::ExportSection::packChartToZipAtPath");
    miacode::v2::UiRequestService* const requests = owner_.uiRequestService();
    miacode::v2::JobProgressService* const jobProgress = owner_.jobProgressService();
    if (requests == nullptr || jobProgress == nullptr || pickedPath.isEmpty()) {
        return;
    }

    QString outputPath = pickedPath;
    if (!outputPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        outputPath += QStringLiteral(".zip");
    }
    owner_.setLastOpenDirectory(outputPath);
    _mc_op_.note(QStringLiteral("out=%1").arg(outputPath));

    miacode::zip_export::ChartZipInput input;
    input.chartText = chartText;
    input.chartPath = chartPath;
    input.videoFieldValue = owner_.document_.videoPath;
    input.outputZipPath = outputPath;

    jobProgress->begin(
        dialogTitle,
        UiText::text(QStringLiteral("export.preparing_package")),
        /*cancellable=*/true);

    // The packager is synchronous and reports from the UI thread, so pump the
    // event loop at each entry: that is what repaints the overlay and delivers
    // the cancel click. Cancellation stays cooperative — we return false and the
    // packager unwinds itself.
    const auto onProgress = [jobProgress](int current, int total, const QString& entryName) -> bool {
        const int safeTotal = qMax(1, total);
        jobProgress->report(
            qRound(static_cast<double>(current - 1) * 100.0 / safeTotal),
            UiText::text(QStringLiteral("export.packaging_1_2_3"))
                .arg(current)
                .arg(total)
                .arg(entryName));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !jobProgress->cancelRequested();
    };

    const miacode::zip_export::ChartZipResult result =
        miacode::zip_export::packChartToZip(input, onProgress);
    jobProgress->end();

    if (result.canceled) {
        _mc_op_.note(QStringLiteral("canceled"));
        requests->postNotice(
            miacode::v2::NoticeSeverity::Information,
            dialogTitle,
            UiText::text(QStringLiteral("export.packaging_canceled")));
        return;
    }

    if (!result.ok) {
        _mc_op_.fail(result.errorMessage);
        requests->postNotice(
            miacode::v2::NoticeSeverity::Error,
            dialogTitle,
            UiText::text(QStringLiteral("export.packaging_failed_1")).arg(result.errorMessage));
        return;
    }

    QString details = result.includedEntries.join(QLatin1Char('\n'));
    if (details.size() > 3000) {
        details = details.left(3000) + QStringLiteral("\n...");
    }
    requests->requestNoticeAction(
        miacode::v2::NoticeSeverity::Information,
        dialogTitle,
        UiText::text(QStringLiteral("export.exported_to_1_2_file"))
            .arg(QDir::toNativeSeparators(outputPath))
            .arg(result.includedEntries.size())
            .arg(QString()),
        details,
        UiText::text(QStringLiteral("action.open_folder")),
        [outputPath](bool openFolder) {
            if (!openFolder) {
                return;
            }
            const QString dir = QFileInfo(outputPath).absoluteDir().absolutePath();
            if (!dir.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            }
        });
}
