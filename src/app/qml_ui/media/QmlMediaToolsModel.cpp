#include "QmlMediaToolsModel.h"

#include "app/v2/JobProgressService.h"
#include "app/v2/UiRequestService.h"
#include "mainwindow/MainWindow.h"
#include "ui/UiText.h"
#include "tools/media/PvBatchCompressionWorker.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QThread>

#include <algorithm>

namespace miacode::qml_ui {

QmlMediaToolsModel::QmlMediaToolsModel(MainWindow& backend,
                                       miacode::v2::UiRequestService& uiRequests,
                                       miacode::v2::JobProgressService& jobProgress,
                                       miacode::v2::MediaToolsEngine*& engineSlot,
                                       QObject* parent)
    : QObject(parent)
    , backend_(&backend)
    , uiRequests_(&uiRequests)
    , jobProgress_(&jobProgress)
    , engineSlot_(&engineSlot)
{
    batchSummary_ = UiText::text(QStringLiteral("media_tools.batch_pv_empty"));
}

QmlMediaToolsModel::~QmlMediaToolsModel()
{
    // The worker owns no UI, but it must not outlive this model's cancel flag.
    if (batchThread_ != nullptr) {
        batchCancelRequested_ = true;
        batchThread_->quit();
        batchThread_->wait();
    }
}

QVariantList QmlMediaToolsModel::batchJobs() const
{
    QVariantList list;
    for (int i = 0; i < jobs_.size(); ++i) {
        const auto& job = jobs_.at(i);
        QVariantMap row;
        row.insert(QStringLiteral("displayName"), job.displayName);
        row.insert(QStringLiteral("directoryPath"), job.directoryPath);
        row.insert(QStringLiteral("hasVideo"), !job.videoPath.isEmpty());
        row.insert(
            QStringLiteral("size"),
            job.originalBytes > 0 ? QLocale().formattedDataSize(job.originalBytes) : QString());
        row.insert(
            QStringLiteral("status"), i < jobStatuses_.size() ? jobStatuses_.at(i) : QString());
        list.append(row);
    }
    return list;
}

void QmlMediaToolsModel::convertTrackTo44100Hz()
{
    if (engine() != nullptr) {
        engine()->convertTrackTo44100Hz();
    }
}

void QmlMediaToolsModel::compressBackgroundVideo()
{
    if (engine() != nullptr) {
        engine()->compressBackgroundVideo();
    }
}

QVariantMap QmlMediaToolsModel::prependContext(bool isTrack)
{
    return engine() != nullptr ? engine()->mediaBlankContext(isTrack) : QVariantMap();
}

QVariantMap QmlMediaToolsModel::detectPrependTiming(bool isTrack)
{
    return engine() != nullptr ? engine()->detectMediaBlankTiming(isTrack) : QVariantMap();
}

void QmlMediaToolsModel::restorePrependBackup(bool isTrack)
{
    if (engine() != nullptr) {
        engine()->restoreMediaBlankBackup(isTrack);
    }
}

void QmlMediaToolsModel::applyPrepend(bool isTrack, double beats, double bpm)
{
    if (engine() != nullptr) {
        engine()->applyMediaBlank(isTrack, beats, bpm);
    }
}

void QmlMediaToolsModel::chooseBatchDirectory()
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
    if (requests == nullptr || batchRunning_) {
        return;
    }
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("media_tools.batch_pv_choose_folder"));
    request.startPath = batchDirectory_;
    request.selectFolder = true;
    requests->requestFile(request, [this](const QString& path) { setBatchDirectory(path); });
}

void QmlMediaToolsModel::setBatchDirectory(const QString& path)
{
    if (path.isEmpty() || path == batchDirectory_) {
        return;
    }
    batchDirectory_ = path;
    emit batchChanged();
}

void QmlMediaToolsModel::addBatchFolder()
{
    if (batchRunning_ || batchDirectory_.isEmpty()) {
        return;
    }
    rescanBatchDirectory();
}

void QmlMediaToolsModel::rescanBatchDirectory()
{
    const QList<miacode::media::PvCompressionJob> scanned =
        miacode::media::scanPvCompressionFolders(batchDirectory_);
    const int added = miacode::media::appendUniquePvCompressionJobs(&jobs_, scanned);
    while (jobStatuses_.size() < jobs_.size()) {
        jobStatuses_.append(QString());
    }
    batchSummary_ = jobs_.isEmpty()
        ? UiText::text(QStringLiteral("media_tools.batch_pv_empty"))
        : UiText::text(QStringLiteral("media_tools.batch_pv_added_1_total_2"))
              .arg(added)
              .arg(jobs_.size());
    emit batchChanged();
}

void QmlMediaToolsModel::removeBatchJob(int index)
{
    if (batchRunning_ || index < 0 || index >= jobs_.size()) {
        return;
    }
    jobs_.removeAt(index);
    if (index < jobStatuses_.size()) {
        jobStatuses_.removeAt(index);
    }
    emit batchChanged();
}

void QmlMediaToolsModel::clearBatchQueue()
{
    if (batchRunning_) {
        return;
    }
    jobs_.clear();
    jobStatuses_.clear();
    batchSummary_ = UiText::text(QStringLiteral("media_tools.batch_pv_empty"));
    emit batchChanged();
}

void QmlMediaToolsModel::setRowStatus(int row, const QString& status)
{
    if (row < 0 || row >= jobStatuses_.size()) {
        return;
    }
    jobStatuses_[row] = status;
    emit batchChanged();
}

void QmlMediaToolsModel::startBatchCompression()
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
    miacode::v2::JobProgressService* const jobProgress = jobProgress_;
    if (requests == nullptr || jobProgress == nullptr || batchRunning_ || jobs_.isEmpty()) {
        return;
    }
    const bool hasCompressibleVideo = std::any_of(
        jobs_.cbegin(), jobs_.cend(), [](const miacode::media::PvCompressionJob& job) {
            return !job.videoPath.isEmpty()
                && job.originalBytes > miacode::media::kPvCompressionTargetBytes;
        });
    if (!hasCompressibleVideo) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Information,
            UiText::text(QStringLiteral("media_tools.batch_pv_title")),
            UiText::text(QStringLiteral("media_tools.batch_pv_already_small")));
        return;
    }

    batchCancelRequested_ = false;
    batchRunning_ = true;
    jobStatuses_ = QStringList(jobs_.size());
    emit batchRunningChanged();
    emit batchChanged();

    batchJobToken_ = jobProgress->begin(
        UiText::text(QStringLiteral("media_tools.batch_pv_title")),
        UiText::text(QStringLiteral("media_tools.batch_pv_title")),
        /*cancellable=*/true);

    auto* thread = new QThread(this);
    auto* worker = new miacode::media::PvBatchCompressionWorker(jobs_, &batchCancelRequested_);
    worker->moveToThread(thread);
    batchThread_ = thread;

    const int total = jobs_.size();
    connect(thread, &QThread::started, worker, &miacode::media::PvBatchCompressionWorker::run);
    connect(worker, &miacode::media::PvBatchCompressionWorker::rowStatus, this,
            &QmlMediaToolsModel::setRowStatus);
    connect(worker, &miacode::media::PvBatchCompressionWorker::progress, this,
            [this, jobProgress, total](int completed) {
                if (jobProgress->token() != batchJobToken_) {
                    return;
                }
                jobProgress->report(
                    total > 0 ? completed * 100 / total : 0,
                    UiText::text(QStringLiteral("media_tools.batch_pv_queue_total_1")).arg(total));
            });
    connect(worker, &miacode::media::PvBatchCompressionWorker::summary, this,
            [this](const QString& message) {
                batchSummary_ = message;
                emit batchChanged();
            });
    connect(worker, &miacode::media::PvBatchCompressionWorker::finished, this,
            &QmlMediaToolsModel::finishBatch);
    connect(worker, &miacode::media::PvBatchCompressionWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this]() { batchThread_ = nullptr; });

    // The worker polls the atomic flag between files, so the shell's cancel
    // stops it at the next boundary rather than mid-encode.
    connect(jobProgress, &miacode::v2::JobProgressService::cancellationRequested, this,
            [this](quint64 token) {
                if (token == batchJobToken_) {
                    batchCancelRequested_ = true;
                }
            });
    thread->start();
}

void QmlMediaToolsModel::finishBatch(
    int succeeded, int failed, bool canceled, const QString& fatalError)
{
    miacode::v2::JobProgressService* const jobProgress = jobProgress_;
    if (jobProgress != nullptr && jobProgress->token() == batchJobToken_) {
        jobProgress->end();
    }
    batchJobToken_ = 0;
    batchRunning_ = false;
    emit batchRunningChanged();

    if (!fatalError.isEmpty()) {
        batchSummary_ = fatalError;
    } else if (canceled) {
        batchSummary_ = UiText::text(QStringLiteral("media_tools.batch_pv_canceled"));
    } else {
        batchSummary_ = UiText::text(QStringLiteral("media_tools.batch_pv_complete_1_2"))
                            .arg(succeeded)
                            .arg(failed);
    }
    emit batchChanged();
}

}  // namespace miacode::qml_ui
