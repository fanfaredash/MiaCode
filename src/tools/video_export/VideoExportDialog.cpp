#include "VideoExportDialog.h"

#include "PreviewCanvas.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <QtMath>

#include <utility>

namespace {

constexpr double kExportLeadInSeconds = 3.0;
constexpr int kPreviewSliderScale = 1000;
constexpr int kPreviewTickIntervalMs = 16;

int secondToSliderValue(double second)
{
    return qMax(0, qRound(second * kPreviewSliderScale));
}

double sliderValueToSecond(int sliderValue)
{
    return qMax(0.0, static_cast<double>(sliderValue) / kPreviewSliderScale);
}

}  // namespace

VideoExportDialog::VideoExportDialog(
    const VideoExportTask& baseTask,
    PreviewCanvas* sourceCanvas,
    SeekPreviewCallback seekPreviewCallback,
    QWidget* parent
)
    : QDialog(parent)
    , baseTask_(baseTask)
    , sourceCanvas_(sourceCanvas)
    , seekPreviewCallback_(std::move(seekPreviewCallback))
    , totalDurationSeconds_(qMax(0.0, baseTask.contentDurationSeconds))
{
    setWindowTitle(QStringLiteral("Export Video"));
    setModal(true);
    resize(640, 360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto* hintLabel = new QLabel(
        QStringLiteral(
            "M3 enabled: export full chart or custom range. "
            "Range preview uses the same timeline/seek path; exported segment keeps 3-second lead-in semantics."
        ),
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

    auto* rangeGroup = new QGroupBox(QStringLiteral("Range Export (M3)"), this);
    auto* rangeGrid = new QGridLayout(rangeGroup);
    rangeGrid->setContentsMargins(10, 8, 10, 8);
    rangeGrid->setHorizontalSpacing(8);
    rangeGrid->setVerticalSpacing(6);

    startSecondSpin_ = new QDoubleSpinBox(rangeGroup);
    startSecondSpin_->setDecimals(3);
    startSecondSpin_->setRange(0.0, totalDurationSeconds_);
    startSecondSpin_->setSingleStep(0.1);
    startSecondSpin_->setSuffix(QStringLiteral(" s"));

    endSecondSpin_ = new QDoubleSpinBox(rangeGroup);
    endSecondSpin_->setDecimals(3);
    endSecondSpin_->setRange(0.0, totalDurationSeconds_);
    endSecondSpin_->setSingleStep(0.1);
    endSecondSpin_->setSuffix(QStringLiteral(" s"));

    const double defaultStart = qBound(0.0, baseTask_.exportStartSeconds, totalDurationSeconds_);
    const double defaultEnd = qBound(
        defaultStart,
        defaultStart + qMax(0.0, baseTask_.contentDurationSeconds),
        totalDurationSeconds_
    );

    startSecondSpin_->setValue(defaultStart);
    endSecondSpin_->setValue(defaultEnd);

    auto* setStartButton = new QPushButton(QStringLiteral("Set Start = Cursor"), rangeGroup);
    auto* setEndButton = new QPushButton(QStringLiteral("Set End = Cursor"), rangeGroup);

    rangeGrid->addWidget(new QLabel(QStringLiteral("Start"), rangeGroup), 0, 0);
    rangeGrid->addWidget(startSecondSpin_, 0, 1);
    rangeGrid->addWidget(setStartButton, 0, 2);
    rangeGrid->addWidget(new QLabel(QStringLiteral("End"), rangeGroup), 1, 0);
    rangeGrid->addWidget(endSecondSpin_, 1, 1);
    rangeGrid->addWidget(setEndButton, 1, 2);

    previewSlider_ = new QSlider(Qt::Horizontal, rangeGroup);
    previewSlider_->setRange(0, secondToSliderValue(totalDurationSeconds_));
    previewCursorSecond_ = defaultStart;
    previewSlider_->setValue(secondToSliderValue(previewCursorSecond_));
    rangeGrid->addWidget(new QLabel(QStringLiteral("Range Preview"), rangeGroup), 2, 0);
    rangeGrid->addWidget(previewSlider_, 2, 1, 1, 2);

    previewTimeLabel_ = new QLabel(rangeGroup);
    rangeGrid->addWidget(previewTimeLabel_, 3, 0, 1, 2);
    leadInHintLabel_ = new QLabel(rangeGroup);
    leadInHintLabel_->setWordWrap(true);
    rangeGrid->addWidget(leadInHintLabel_, 4, 0, 1, 3);

    auto* previewButtonsRow = new QWidget(rangeGroup);
    auto* previewButtonsLayout = new QHBoxLayout(previewButtonsRow);
    previewButtonsLayout->setContentsMargins(0, 0, 0, 0);
    previewButtonsLayout->setSpacing(6);
    previewRangeButton_ = new QPushButton(QStringLiteral("Preview Range"), previewButtonsRow);
    stopPreviewButton_ = new QPushButton(QStringLiteral("Stop"), previewButtonsRow);
    stopPreviewButton_->setEnabled(false);
    previewButtonsLayout->addWidget(previewRangeButton_, 0);
    previewButtonsLayout->addWidget(stopPreviewButton_, 0);
    previewButtonsLayout->addStretch(1);
    rangeGrid->addWidget(previewButtonsRow, 5, 0, 1, 3);

    layout->addWidget(rangeGroup);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    exportButton_ = buttonBox->addButton(QStringLiteral("Export"), QDialogButtonBox::AcceptRole);
    connect(exportButton_, &QPushButton::clicked, this, &VideoExportDialog::startExport);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    previewTimer_ = new QTimer(this);
    previewTimer_->setInterval(kPreviewTickIntervalMs);
    connect(previewTimer_, &QTimer::timeout, this, &VideoExportDialog::onRangePreviewTick);

    connect(startSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(endSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(previewSlider_, &QSlider::valueChanged, this, &VideoExportDialog::onPreviewSliderChanged);
    connect(setStartButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeStartFromPreview);
    connect(setEndButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeEndFromPreview);
    connect(previewRangeButton_, &QPushButton::clicked, this, &VideoExportDialog::toggleRangePreview);
    connect(stopPreviewButton_, &QPushButton::clicked, this, &VideoExportDialog::stopRangePreview);

    syncRangeUi();
    seekPreview(previewCursorSecond_);
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
    updated.exportStartSeconds = rangeStartSeconds();
    updated.contentDurationSeconds = qMax(0.0, rangeEndSeconds() - updated.exportStartSeconds);

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
            *errorMessage = QStringLiteral("Export range is empty.");
        }
        return false;
    }
    if (updated.exportStartSeconds < 0.0 || updated.exportStartSeconds > totalDurationSeconds_ + 1e-6) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Export start is out of range.");
        }
        return false;
    }

    *task = updated;
    return true;
}

void VideoExportDialog::onRangeSpinChanged()
{
    if (syncingRangeUi_) {
        return;
    }
    const double start = rangeStartSeconds();
    const double end = rangeEndSeconds();
    if (end <= start && totalDurationSeconds_ > 0.0) {
        const bool fromStart = sender() == startSecondSpin_;
        syncingRangeUi_ = true;
        if (fromStart && endSecondSpin_ != nullptr) {
            endSecondSpin_->setValue(qMin(totalDurationSeconds_, start + 0.001));
        } else if (startSecondSpin_ != nullptr) {
            startSecondSpin_->setValue(qMax(0.0, end - 0.001));
        }
        syncingRangeUi_ = false;
    }
    syncRangeUi();
}

void VideoExportDialog::onPreviewSliderChanged(int sliderValue)
{
    if (syncingRangeUi_) {
        return;
    }
    previewCursorSecond_ = qBound(0.0, sliderValueToSecond(sliderValue), totalDurationSeconds_);
    seekPreview(previewCursorSecond_);
    syncRangeUi();
}

void VideoExportDialog::setRangeStartFromPreview()
{
    if (startSecondSpin_ == nullptr) {
        return;
    }
    startSecondSpin_->setValue(previewCursorSecond_);
}

void VideoExportDialog::setRangeEndFromPreview()
{
    if (endSecondSpin_ == nullptr) {
        return;
    }
    endSecondSpin_->setValue(previewCursorSecond_);
}

void VideoExportDialog::toggleRangePreview()
{
    if (previewTimer_ == nullptr) {
        return;
    }
    if (previewTimer_->isActive()) {
        stopRangePreview();
        return;
    }
    if (rangeEndSeconds() <= rangeStartSeconds()) {
        return;
    }
    previewCursorSecond_ = leadInStartSeconds();
    seekPreview(previewCursorSecond_);
    previewElapsedTimer_.restart();
    previewTimer_->start();
    if (previewRangeButton_ != nullptr) {
        previewRangeButton_->setText(QStringLiteral("Pause Preview"));
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(true);
    }
    syncRangeUi();
}

void VideoExportDialog::stopRangePreview()
{
    if (previewTimer_ != nullptr && previewTimer_->isActive()) {
        previewTimer_->stop();
    }
    if (previewRangeButton_ != nullptr) {
        previewRangeButton_->setText(QStringLiteral("Preview Range"));
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(false);
    }
}

void VideoExportDialog::onRangePreviewTick()
{
    if (previewTimer_ == nullptr || !previewTimer_->isActive()) {
        return;
    }
    qint64 elapsedMs = previewElapsedTimer_.restart();
    if (elapsedMs <= 0) {
        return;
    }
    elapsedMs = qMin<qint64>(elapsedMs, 120);
    previewCursorSecond_ += static_cast<double>(elapsedMs) / 1000.0;

    const double end = rangeEndSeconds();
    if (previewCursorSecond_ >= end) {
        previewCursorSecond_ = end;
        seekPreview(previewCursorSecond_);
        stopRangePreview();
        syncRangeUi();
        return;
    }

    seekPreview(previewCursorSecond_);
    syncRangeUi();
}

void VideoExportDialog::syncRangeUi()
{
    if (syncingRangeUi_) {
        return;
    }
    syncingRangeUi_ = true;

    const double clampedStart = qBound(0.0, rangeStartSeconds(), totalDurationSeconds_);
    const double clampedEnd = qBound(clampedStart, rangeEndSeconds(), totalDurationSeconds_);
    previewCursorSecond_ = qBound(0.0, previewCursorSecond_, totalDurationSeconds_);

    if (startSecondSpin_ != nullptr) {
        const QSignalBlocker blocker(*startSecondSpin_);
        startSecondSpin_->setValue(clampedStart);
    }
    if (endSecondSpin_ != nullptr) {
        const QSignalBlocker blocker(*endSecondSpin_);
        endSecondSpin_->setValue(clampedEnd);
    }
    if (previewSlider_ != nullptr) {
        const QSignalBlocker blocker(*previewSlider_);
        previewSlider_->setValue(secondToSliderValue(previewCursorSecond_));
    }
    if (previewTimeLabel_ != nullptr) {
        previewTimeLabel_->setText(
            QStringLiteral("Cursor %1 / %2    Range [%3, %4]")
                .arg(formatSecond(previewCursorSecond_))
                .arg(formatSecond(totalDurationSeconds_))
                .arg(formatSecond(clampedStart))
                .arg(formatSecond(clampedEnd))
        );
    }
    if (leadInHintLabel_ != nullptr) {
        leadInHintLabel_->setText(
            QStringLiteral("Lead-in preview starts at %1 (start - 3.000s, clamped to >= 0)")
                .arg(formatSecond(leadInStartSeconds()))
        );
    }

    syncingRangeUi_ = false;
}

void VideoExportDialog::seekPreview(double second)
{
    const double clamped = qBound(0.0, second, totalDurationSeconds_);
    previewCursorSecond_ = clamped;
    if (previewSlider_ != nullptr) {
        const QSignalBlocker blocker(*previewSlider_);
        previewSlider_->setValue(secondToSliderValue(clamped));
    }

    if (seekPreviewCallback_) {
        seekPreviewCallback_(clamped);
        return;
    }
    if (sourceCanvas_ != nullptr) {
        sourceCanvas_->setPlayheadSeconds(clamped);
        sourceCanvas_->update();
    }
}

double VideoExportDialog::rangeStartSeconds() const
{
    return startSecondSpin_ != nullptr ? startSecondSpin_->value() : 0.0;
}

double VideoExportDialog::rangeEndSeconds() const
{
    return endSecondSpin_ != nullptr ? endSecondSpin_->value() : totalDurationSeconds_;
}

double VideoExportDialog::leadInStartSeconds() const
{
    return qMax(0.0, rangeStartSeconds() - kExportLeadInSeconds);
}

QString VideoExportDialog::formatSecond(double second) const
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(second * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 sec = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

void VideoExportDialog::startExport()
{
    stopRangePreview();

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
            details.isEmpty() ? result.message : QStringLiteral("%1\n\n%2").arg(result.message, details)
        );
        return;
    }

    QMessageBox::information(this, QStringLiteral("Export Video"), QStringLiteral("Export completed."));
    accept();
}

void VideoExportDialog::closeEvent(QCloseEvent* event)
{
    stopRangePreview();
    QDialog::closeEvent(event);
}
