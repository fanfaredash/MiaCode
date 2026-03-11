#include "VideoExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

VideoExportDialog::VideoExportDialog(const VideoExportTask& baseTask, const PreviewCanvas* sourceCanvas, QWidget* parent)
    : QDialog(parent)
    , baseTask_(baseTask)
    , sourceCanvas_(sourceCanvas)
{
    setWindowTitle(QStringLiteral("Export Video"));
    setModal(true);
    resize(520, 220);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto* hintLabel = new QLabel(
        QStringLiteral("Current scope: full export (M1+M2), 60 fps, 3-second lead-in, offline frame rendering."),
        this
    );
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    auto* outputRow = new QWidget(this);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(6);
    outputPathEdit_ = new QLineEdit(outputRow);
    outputPathEdit_->setText(baseTask_.outputPath);
    auto* browseButton = new QPushButton(QStringLiteral("Browse..."), outputRow);
    connect(browseButton, &QPushButton::clicked, this, &VideoExportDialog::browseOutputPath);
    outputLayout->addWidget(outputPathEdit_, 1);
    outputLayout->addWidget(browseButton, 0);
    form->addRow(QStringLiteral("Output"), outputRow);

    resolutionCombo_ = new QComboBox(this);
    const QList<int> presets{512, 768, 1024, 1280, 1536};
    for (int preset : presets) {
        resolutionCombo_->addItem(QString("%1x%1").arg(preset), preset);
    }
    const int currentPresetIndex = resolutionCombo_->findData(baseTask_.resolution);
    resolutionCombo_->setCurrentIndex(currentPresetIndex >= 0 ? currentPresetIndex : 2);
    form->addRow(QStringLiteral("Resolution"), resolutionCombo_);

    auto* fpsLabel = new QLabel(QStringLiteral("60 fps (fixed)"), this);
    form->addRow(QStringLiteral("FPS"), fpsLabel);

    showTimestampCheck_ = new QCheckBox(QStringLiteral("Show bottom-left timestamp"), this);
    showTimestampCheck_->setChecked(baseTask_.showTimestamp);
    form->addRow(QStringLiteral("Options"), showTimestampCheck_);

    layout->addLayout(form);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    exportButton_ = buttonBox->addButton(QStringLiteral("Export"), QDialogButtonBox::AcceptRole);
    connect(exportButton_, &QPushButton::clicked, this, &VideoExportDialog::startExport);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

void VideoExportDialog::browseOutputPath()
{
    const QString initial = outputPathEdit_ != nullptr ? outputPathEdit_->text().trimmed() : QString();
    const QString selected = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Video"),
        initial,
        QStringLiteral("MP4 Video (*.mp4)")
    );
    if (selected.isEmpty() || outputPathEdit_ == nullptr) {
        return;
    }
    outputPathEdit_->setText(selected);
}

bool VideoExportDialog::applyUiToTask(VideoExportTask* task, QString* errorMessage) const
{
    if (task == nullptr) {
        return false;
    }
    VideoExportTask updated = baseTask_;
    const QString outputPath = outputPathEdit_ != nullptr ? outputPathEdit_->text().trimmed() : QString();
    if (outputPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Please choose an output path.");
        }
        return false;
    }
    updated.outputPath = outputPath;
    updated.resolution = resolutionCombo_ != nullptr ? resolutionCombo_->currentData().toInt() : updated.resolution;
    updated.fps = 60;
    updated.showTimestamp = showTimestampCheck_ != nullptr ? showTimestampCheck_->isChecked() : true;

    const QFileInfo outputInfo(updated.outputPath);
    const QDir outputDir = outputInfo.absoluteDir();
    if (!outputDir.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Output directory does not exist.");
        }
        return false;
    }
    if (updated.resolution <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Resolution is invalid.");
        }
        return false;
    }
    if (updated.contentDurationSeconds <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No exportable timeline in current difficulty.");
        }
        return false;
    }

    *task = updated;
    return true;
}

void VideoExportDialog::startExport()
{
    VideoExportTask task;
    QString errorMessage;
    if (!applyUiToTask(&task, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("Export Video"), errorMessage);
        return;
    }
    if (sourceCanvas_ == nullptr) {
        QMessageBox::warning(this, QStringLiteral("Export Video"), QStringLiteral("Preview canvas is not initialized."));
        return;
    }

    QProgressDialog progress(QStringLiteral("Preparing export..."), QStringLiteral("Cancel"), 0, 100, this);
    progress.setWindowTitle(QStringLiteral("Export Video"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    exportButton_->setEnabled(false);
    VideoExportResult result = VideoExportController::exportFullPreview(task, sourceCanvas_, &progress);
    exportButton_->setEnabled(true);

    if (!result.success) {
        if (result.message == QLatin1String("canceled")) {
            QMessageBox::information(this, QStringLiteral("Export Video"), QStringLiteral("Export canceled."));
            return;
        }
        const QString details = result.details.trimmed();
        QMessageBox::critical(
            this,
            QStringLiteral("Export Failed"),
            details.isEmpty() ? result.message : QString("%1\n\n%2").arg(result.message, details)
        );
        return;
    }

    QMessageBox::information(this, QStringLiteral("Export Video"), QStringLiteral("Export completed."));
    accept();
}
