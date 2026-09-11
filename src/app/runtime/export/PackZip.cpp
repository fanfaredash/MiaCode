#include "runtime/export/VideoExportHost.h"
#include "runtime/Shared.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/shell/ShellHost.h"

#include "app/services/JobProgressService.h"
#include "app/services/UiRequestService.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "tools/zip_export/ChartZipPackager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QUrl>

using namespace miacode::runtime::shared;

void miacode::runtime::VideoExportHost::onPackAsZip()
{
    MC_OP("miacode::runtime::VideoExportHost::onPackAsZip");

    miacode::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        _mc_op_.fail(QStringLiteral("ui request service unavailable"));
        return;
    }

    const QString dialogTitle = qtTrId("export.export_as_zip");

    // Flush the in-progress editor field into the document so the packaged
    // maidata.txt matches what the user sees (same contract as save-to-path).
    if (session_.documents_ != nullptr) {
        session_.documents_->applyCurrentFieldToDocument();
    }

    const QString chartText = session_.applicationServices_.workspace().document().toText();
    if (chartText.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty chart"));
        requests->postNotice(
            miacode::NoticeSeverity::Warning,
            dialogTitle,
            qtTrId("export.the_chart_is_empty_there"));
        return;
    }

    const QString chartPath = session_.currentFilePath_;
    QString defaultDir = chartPath.isEmpty()
        ? QString()
        : QFileInfo(chartPath).absolutePath();
    if (defaultDir.isEmpty() && session_.documents_ != nullptr) {
        defaultDir = session_.documents_->resolveInitialOpenDirectory();
    }
    const QString defaultName =
        miacode::zip_export::sanitizedZipStem(session_.applicationServices_.workspace().document().title) + QStringLiteral(".zip");

    miacode::FileRequest request;
    request.title = dialogTitle;
    request.startPath = defaultDir.isEmpty() ? defaultName : QDir(defaultDir).filePath(defaultName);
    request.nameFilters = QStringList{QStringLiteral("ZIP (*.zip)")};
    request.saveMode = true;
    requests->requestFile(request, [this, chartText, chartPath, dialogTitle](const QString& picked) {
        packChartToZipAtPath(chartText, chartPath, dialogTitle, picked);
    });
}

void miacode::runtime::VideoExportHost::packChartToZipAtPath(
    const QString& chartText,
    const QString& chartPath,
    const QString& dialogTitle,
    const QString& pickedPath)
{
    MC_OP("miacode::runtime::VideoExportHost::packChartToZipAtPath");
    miacode::UiRequestService* const requests = session_.uiRequestService();
    miacode::JobProgressService* const jobProgress = session_.jobProgressService();
    if (requests == nullptr || jobProgress == nullptr || pickedPath.isEmpty()) {
        return;
    }

    QString outputPath = pickedPath;
    if (!outputPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        outputPath += QStringLiteral(".zip");
    }
    session_.setLastOpenDirectory(outputPath);
    _mc_op_.note(QStringLiteral("out=%1").arg(outputPath));

    miacode::zip_export::ChartZipInput input;
    input.chartText = chartText;
    input.chartPath = chartPath;
    input.videoFieldValue = session_.applicationServices_.workspace().document().videoPath;
    input.outputZipPath = outputPath;

    jobProgress->begin(
        dialogTitle,
        qtTrId("export.preparing_package"),
        /*cancellable=*/true);

    // The packager is synchronous and reports from the UI thread, so pump the
    // event loop at each entry: that is what repaints the overlay and delivers
    // the cancel click. Cancellation stays cooperative — we return false and the
    // packager unwinds itself.
    const auto onProgress = [jobProgress](int current, int total, const QString& entryName) -> bool {
        const int safeTotal = qMax(1, total);
        jobProgress->report(
            qRound(static_cast<double>(current - 1) * 100.0 / safeTotal),
            qtTrId("export.packaging_1_2_3")
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
            miacode::NoticeSeverity::Information,
            dialogTitle,
            qtTrId("export.packaging_canceled"));
        return;
    }

    if (!result.ok) {
        _mc_op_.fail(result.errorMessage);
        requests->postNotice(
            miacode::NoticeSeverity::Error,
            dialogTitle,
            qtTrId("export.packaging_failed_1").arg(result.errorMessage));
        return;
    }

    QString details = result.includedEntries.join(QLatin1Char('\n'));
    if (details.size() > 3000) {
        details = details.left(3000) + QStringLiteral("\n...");
    }
    requests->requestNoticeAction(
        miacode::NoticeSeverity::Information,
        dialogTitle,
        qtTrId("export.exported_to_1_2_file")
            .arg(QDir::toNativeSeparators(outputPath))
            .arg(result.includedEntries.size())
            .arg(QString()),
        details,
        qtTrId("action.open_folder"),
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
