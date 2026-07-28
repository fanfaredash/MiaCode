#include "PvBatchCompressionDialog.h"

#include "DialogLocalization.h"
#include "PvBatchCompressionWorker.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

namespace miacode::media {
namespace {

QString compressionDialogStyleSheet()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return UiTheme::preferencesDialogStyleSheet()
        + QStringLiteral(
            "QTableWidget { background: %1; color: %2; gridline-color: %3; border: 1px solid %3; }"
            "QHeaderView::section { background: %4; color: %2; border: 1px solid %3; padding: 4px 6px; }"
            "QLineEdit { background: %5; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 7px; }")
              .arg(c.cardBg.name(QColor::HexRgb))
              .arg(c.textPrimary.name(QColor::HexRgb))
              .arg(c.border.name(QColor::HexRgb))
              .arg(c.panelBg.name(QColor::HexRgb))
              .arg(c.inputBg.name(QColor::HexRgb));
}

void polishButton(QPushButton* button)
{
    button->ensurePolished();
    button->setFixedHeight(qMax(button->sizeHint().height(), 30) + 4);
}

}  // namespace

PvBatchCompressionDialog::PvBatchCompressionDialog(QWidget* parent)
    : QDialog(parent)
{
    buildUi();
}

void PvBatchCompressionDialog::buildUi()
{
    setWindowTitle(UiText::text(QStringLiteral("media_tools.batch_pv_title")));
    setMinimumSize(900, 560);
    resize(900, 560);
    UiDialogs::configureDialogPreviewShortcuts(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* addRow = new QGridLayout;
    addRow->setColumnStretch(1, 1);
    addRow->setHorizontalSpacing(8);
    directoryEdit_ = new QLineEdit(this);
    browseButton_ = new QPushButton(UiText::text(QStringLiteral("net.browse")), this);
    addButton_ = new QPushButton(UiText::text(QStringLiteral("media_tools.batch_pv_add")), this);
    addRow->addWidget(new QLabel(UiText::text(QStringLiteral("media_tools.batch_pv_folder")), this), 0, 0);
    addRow->addWidget(directoryEdit_, 0, 1);
    addRow->addWidget(browseButton_, 0, 2);
    addRow->addWidget(addButton_, 0, 3);
    root->addLayout(addRow);

    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({
        UiText::text(QStringLiteral("media_tools.batch_pv_chart_folder")),
        UiText::text(QStringLiteral("media_tools.batch_pv_file")),
        UiText::text(QStringLiteral("media_tools.batch_pv_original_size")),
        UiText::text(QStringLiteral("media_tools.batch_pv_path")),
        UiText::text(QStringLiteral("net.status")),
    });
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    table_->setColumnWidth(0, 190);
    table_->setColumnWidth(4, 180);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    root->addWidget(table_, 1);

    auto* bottom = new QGridLayout;
    summaryLabel_ = new QLabel(UiText::text(QStringLiteral("media_tools.batch_pv_empty")), this);
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 1);
    progressBar_->setValue(0);
    removeSelectedButton_ = new QPushButton(
        UiText::text(QStringLiteral("media_tools.batch_pv_remove_selected")), this);
    clearQueueButton_ = new QPushButton(
        UiText::text(QStringLiteral("media_tools.batch_pv_clear_queue")), this);
    compressButton_ = new QPushButton(
        UiText::text(QStringLiteral("media_tools.batch_pv_start")), this);
    closeButton_ = new QPushButton(UiText::text(QStringLiteral("action.close")), this);
    bottom->addWidget(summaryLabel_, 0, 0, 1, 2);
    bottom->addWidget(removeSelectedButton_, 0, 2);
    bottom->addWidget(clearQueueButton_, 0, 3);
    bottom->addWidget(progressBar_, 1, 0, 1, 2);
    bottom->addWidget(compressButton_, 1, 2);
    bottom->addWidget(closeButton_, 1, 3);
    root->addLayout(bottom);

    populateTable();
    applyThemeStyles();

    connect(browseButton_, &QPushButton::clicked, this, [this]() { chooseDirectory(); });
    connect(addButton_, &QPushButton::clicked, this, [this]() { addFolder(); });
    connect(removeSelectedButton_, &QPushButton::clicked, this, [this]() { removeSelectedRows(); });
    connect(clearQueueButton_, &QPushButton::clicked, this, [this]() { clearQueue(); });
    connect(compressButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
            return;
        }
        startCompression();
    });
    connect(closeButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
            return;
        }
        close();
    });
}

void PvBatchCompressionDialog::applyThemeStyles()
{
    setPalette(UiTheme::applicationPalette());
    setStyleSheet(compressionDialogStyleSheet());
    for (QPushButton* button : {
             browseButton_, addButton_, removeSelectedButton_, clearQueueButton_, compressButton_, closeButton_}) {
        if (button != nullptr) {
            polishButton(button);
        }
    }
}

void PvBatchCompressionDialog::closeEvent(QCloseEvent* event)
{
    if (busy_) {
        cancelRequested_ = true;
        summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void PvBatchCompressionDialog::chooseDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        UiText::text(QStringLiteral("media_tools.batch_pv_choose_folder")),
        directoryEdit_->text().trimmed());
    if (!directory.isEmpty()) {
        directoryEdit_->setText(QDir::toNativeSeparators(directory));
        addFolder();
    }
}

void PvBatchCompressionDialog::addFolder()
{
    const QString directory = QDir::fromNativeSeparators(directoryEdit_->text().trimmed());
    if (directory.isEmpty() || !QDir(directory).exists()) {
        QMessageBox::warning(
            this,
            windowTitle(),
            UiText::text(QStringLiteral("media_tools.batch_pv_invalid_folder")));
        return;
    }

    const QList<PvCompressionJob> scanned = scanPvCompressionFolders(directory);
    const int added = appendUniquePvCompressionJobs(&jobs_, scanned);
    populateTable();
    summaryLabel_->setText(
        UiText::text(QStringLiteral("media_tools.batch_pv_added_1_total_2"))
            .arg(added)
            .arg(jobs_.size()));
}

void PvBatchCompressionDialog::populateTable()
{
    table_->setRowCount(jobs_.size());
    for (int row = 0; row < jobs_.size(); ++row) {
        const PvCompressionJob& job = jobs_.at(row);
        table_->setItem(row, 0, new QTableWidgetItem(job.displayName));
        const bool hasVideo = !job.videoPath.isEmpty();
        table_->setItem(row, 1, new QTableWidgetItem(hasVideo ? QFileInfo(job.videoPath).fileName() : QStringLiteral("-")));
        table_->setItem(row, 2, new QTableWidgetItem(hasVideo ? QLocale().formattedDataSize(job.originalBytes) : QStringLiteral("-")));
        table_->setItem(row, 3, new QTableWidgetItem(QDir::toNativeSeparators(job.directoryPath)));
        const QString statusKey = !hasVideo
            ? QStringLiteral("media_tools.batch_pv_no_video")
            : (job.originalBytes <= kPvCompressionTargetBytes
                   ? QStringLiteral("media_tools.batch_pv_already_small")
                   : QStringLiteral("media_tools.batch_pv_pending"));
        table_->setItem(row, 4, new QTableWidgetItem(UiText::text(statusKey)));
    }
    const bool hasJobs = !jobs_.isEmpty();
    const bool hasCompressibleVideo = std::any_of(jobs_.cbegin(), jobs_.cend(), [](const PvCompressionJob& job) {
        return !job.videoPath.isEmpty() && job.originalBytes > kPvCompressionTargetBytes;
    });
    removeSelectedButton_->setEnabled(!busy_ && hasJobs);
    clearQueueButton_->setEnabled(!busy_ && hasJobs);
    compressButton_->setEnabled(hasCompressibleVideo);
}

void PvBatchCompressionDialog::removeSelectedRows()
{
    QModelIndexList rows = table_->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& lhs, const QModelIndex& rhs) {
        return lhs.row() > rhs.row();
    });
    for (const QModelIndex& index : rows) {
        jobs_.removeAt(index.row());
    }
    populateTable();
    summaryLabel_->setText(
        jobs_.isEmpty()
            ? UiText::text(QStringLiteral("media_tools.batch_pv_empty"))
            : UiText::text(QStringLiteral("media_tools.batch_pv_queue_total_1")).arg(jobs_.size()));
}

void PvBatchCompressionDialog::clearQueue()
{
    jobs_.clear();
    populateTable();
    summaryLabel_->setText(UiText::text(QStringLiteral("media_tools.batch_pv_empty")));
}

void PvBatchCompressionDialog::setRowStatus(int row, const QString& status)
{
    if (row >= 0 && row < table_->rowCount() && table_->item(row, 4) != nullptr) {
        table_->item(row, 4)->setText(status);
    }
}

void PvBatchCompressionDialog::setBusy(bool busy)
{
    busy_ = busy;
    directoryEdit_->setEnabled(!busy);
    browseButton_->setEnabled(!busy);
    addButton_->setEnabled(!busy);
    table_->setEnabled(!busy);
    removeSelectedButton_->setEnabled(!busy && !jobs_.isEmpty());
    clearQueueButton_->setEnabled(!busy && !jobs_.isEmpty());
    compressButton_->setText(
        busy
            ? UiText::text(QStringLiteral("media_tools.batch_pv_cancel"))
            : UiText::text(QStringLiteral("media_tools.batch_pv_start")));
    const bool hasCompressibleVideo = std::any_of(jobs_.cbegin(), jobs_.cend(), [](const PvCompressionJob& job) {
        return !job.videoPath.isEmpty() && job.originalBytes > kPvCompressionTargetBytes;
    });
    compressButton_->setEnabled(hasCompressibleVideo);
    closeButton_->setText(
        busy ? UiText::text(QStringLiteral("action.cancel"))
             : UiText::text(QStringLiteral("action.close")));
}

void PvBatchCompressionDialog::startCompression()
{
    const int videoCount = static_cast<int>(std::count_if(
        jobs_.cbegin(), jobs_.cend(), [](const PvCompressionJob& job) {
            return !job.videoPath.isEmpty() && job.originalBytes > kPvCompressionTargetBytes;
        }));
    if (videoCount == 0) {
        return;
    }
    if (QMessageBox::question(
            this,
            windowTitle(),
            UiText::text(QStringLiteral("media_tools.batch_pv_confirm_1")).arg(videoCount))
        != QMessageBox::Yes) {
        return;
    }

    cancelRequested_ = false;
    progressBar_->setRange(0, jobs_.size());
    progressBar_->setValue(0);
    setBusy(true);

    auto* worker = new PvBatchCompressionWorker(jobs_, &cancelRequested_);
    workerThread_ = new QThread(this);
    worker->moveToThread(workerThread_);
    connect(workerThread_, &QThread::started, worker, &PvBatchCompressionWorker::run);
    connect(worker, &PvBatchCompressionWorker::rowStatus, this, [this](int row, const QString& status) {
        setRowStatus(row, status);
    });
    connect(worker, &PvBatchCompressionWorker::progress, progressBar_, &QProgressBar::setValue);
    connect(worker, &PvBatchCompressionWorker::summary, summaryLabel_, &QLabel::setText);
    connect(worker, &PvBatchCompressionWorker::finished, this,
        [this](int succeeded, int failed, bool canceled, const QString& fatalError) {
            if (!fatalError.isEmpty()) {
                summaryLabel_->setText(fatalError);
                QMessageBox::warning(this, windowTitle(), fatalError);
            } else if (canceled) {
                summaryLabel_->setText(UiText::text(QStringLiteral("media_tools.batch_pv_canceled")));
            } else {
                summaryLabel_->setText(
                    UiText::text(QStringLiteral("media_tools.batch_pv_complete_1_2"))
                        .arg(succeeded)
                        .arg(failed));
            }
        });
    connect(worker, &PvBatchCompressionWorker::finished, workerThread_, &QThread::quit);
    connect(worker, &PvBatchCompressionWorker::finished, worker, &QObject::deleteLater);
    connect(workerThread_, &QThread::finished, this, [this]() {
        setBusy(false);
        workerThread_ = nullptr;
    });
    connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);
    workerThread_->start();
}

}  // namespace miacode::media
