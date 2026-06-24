#include "tools/cover_export/ExportCoverDialog.h"

#include "tools/cover_export/CoverStudioPanel.h"
#include "tools/video_export/VideoExportController.h"
#include "UiNativeWindowTheme.h"
#include "UiText.h"

#include <QVBoxLayout>

namespace {

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

}  // namespace

ExportCoverDialog::ExportCoverDialog(const VideoExportTask& task, const QSize& initialSize, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(l10n(QStringLiteral("Export Cover"), QStringLiteral("导出封面")));
    setModal(true);
    UiNativeWindowTheme::applyToWidget(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    studio_ = new miacode::cover_export::CoverStudioPanel(task, initialSize, this);
    layout->addWidget(studio_);
    connect(studio_, &miacode::cover_export::CoverStudioPanel::exportRequested, this, &QDialog::accept);
    connect(studio_, &miacode::cover_export::CoverStudioPanel::cancelRequested, this, &QDialog::reject);
}

miacode::cover_export::CoverExportResult ExportCoverDialog::exportCover(const QString& outputDirectory)
{
    return studio_ != nullptr ? studio_->exportCover(outputDirectory)
                              : miacode::cover_export::CoverExportResult{};
}
