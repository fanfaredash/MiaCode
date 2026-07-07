#include "NetBatchDownloadWorker.h"

#include "UiText.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <limits>
#include <utility>

namespace miacode::net {
namespace {

QString formatSpeed(qint64 bytes, qint64 elapsedMs)
{
    if (bytes <= 0 || elapsedMs <= 0) {
        return QStringLiteral("-");
    }
    const double bytesPerSecond = static_cast<double>(bytes) * 1000.0 / static_cast<double>(elapsedMs);
    if (bytesPerSecond >= 1024.0 * 1024.0) {
        return QStringLiteral("%1 MiB/s").arg(bytesPerSecond / (1024.0 * 1024.0), 0, 'f', 2);
    }
    return QStringLiteral("%1 KiB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 1);
}

QList<NetBatchResourceSpec> requiredResources()
{
    return {
        {QStringLiteral("track"), QStringLiteral("track.mp3"), QStringLiteral("track.mp3")},
        {QStringLiteral("image?fullImage=true"), QStringLiteral("bg.jpg"), QStringLiteral("bg.jpg")},
        {QStringLiteral("chart"), QStringLiteral("maidata.txt"), QStringLiteral("maidata.txt")},
    };
}

}  // namespace

NetBatchDownloadWorker::NetBatchDownloadWorker(
    NetBatchDownloadRequest request,
    std::atomic_bool* cancelRequested)
    : request_(std::move(request))
    , cancelRequested_(cancelRequested)
{}

void NetBatchDownloadWorker::run()
{
    NetClient client;
    int completed = 0;
    int succeeded = 0;
    int failed = 0;
    bool paused = false;
    qint64 totalBytes = 0;
    qint64 totalNetworkMs = 0;

    emit log(UiText::text(QStringLiteral("net.background_download_thread_started")));
    for (int row = 0; row < request_.jobs.size() && !isCanceled() && !paused; ++row) {
        NetDownloadJob job = request_.jobs.at(row);
        if (!job.selected) {
            emit rowStatus(row, UiText::text(QStringLiteral("net.not_selected")));
            continue;
        }

        emit rowStatus(row, UiText::text(QStringLiteral("net.downloading")));
        emit summary(UiText::text(QStringLiteral("net.downloading_1")).arg(job.chart.title));
        emit log(UiText::text(QStringLiteral("net.start_chart_1_2")).arg(job.chart.title, job.chart.id));

        QString error;
        job.outputDirectoryPath = chartDirectoryPathForTitle(request_.outputDirectory, job.chart.title, job.chart.id);
        if (!QDir().mkpath(job.outputDirectoryPath)) {
            error = UiText::text(QStringLiteral("net.could_not_create_chart_folder"));
        }

        bool resourcesOk = error.isEmpty();
        QList<NetBatchResourceStats> resourceStats;
        for (const NetBatchResourceSpec& resource : requiredResources()) {
            if (!resourcesOk || paused || isCanceled()) {
                break;
            }
            emit rowStatus(row, UiText::text(QStringLiteral("net.downloading_1_2")).arg(resource.label));
            const QString outputPath = QDir(job.outputDirectoryPath).filePath(resource.fileName);
            resourcesOk = downloadResourceToFile(client, row, job.chart.id, resource, outputPath, &error, &paused, &resourceStats);
        }

        if (paused) {
            emit rowStatus(row, UiText::text(QStringLiteral("net.paused")));
            break;
        }
        if (isCanceled()) {
            break;
        }
        if (!resourcesOk) {
            ++failed;
            job.errorMessage = error.isEmpty() ? UiText::text(QStringLiteral("net.resource_download_failed")) : error;
            emit rowStatus(row, UiText::text(QStringLiteral("net.failed_1")).arg(job.errorMessage));
        } else {
            bool chartDone = true;
            if (request_.createZip) {
                emit rowStatus(row, UiText::text(QStringLiteral("net.packaging_zip")));
                job.outputZipPath = uniqueZipPathForTitle(request_.outputDirectory, job.chart.title);
                QStringList entries;
                QElapsedTimer zipElapsed;
                zipElapsed.start();
                chartDone = packNetChartFolderZip(job.outputDirectoryPath, job.outputZipPath, &entries, &error);
                emit log(UiText::text(QStringLiteral("net.zip_package_1_2_3"))
                             .arg(job.outputDirectoryPath, job.outputZipPath)
                             .arg(zipElapsed.elapsed()));
            }
            if (chartDone) {
                ++succeeded;
                emit rowStatus(row,
                    request_.createZip ? UiText::text(QStringLiteral("net.done_folder_zip"))
                                       : UiText::text(QStringLiteral("net.done_folder")));
                emit log(UiText::text(QStringLiteral("net.chart_complete_1_2")).arg(job.chart.title, job.outputDirectoryPath));
            } else {
                ++failed;
                job.errorMessage = error;
                emit rowStatus(row, UiText::text(QStringLiteral("net.package_failed_1")).arg(error));
            }
        }

        for (const NetBatchResourceStats& stats : resourceStats) {
            totalBytes += stats.bytes;
            totalNetworkMs += stats.elapsedMs;
        }
        emitChartBottleneck(job.chart.title, resourceStats);
        ++completed;
        emit progress(completed);
        QThread::msleep(250);
    }

    if (paused) {
        emit log(UiText::text(QStringLiteral("net.queue_paused_net_cloudflare_blocked_2")));
    } else if (isCanceled()) {
        emit log(UiText::text(QStringLiteral("net.queue_canceled_1_succeeded_2")).arg(succeeded).arg(failed));
    } else {
        emit log(UiText::text(QStringLiteral("net.queue_complete_1_succeeded_2"))
                     .arg(succeeded)
                     .arg(failed)
                     .arg(totalBytes)
                     .arg(formatSpeed(totalBytes, totalNetworkMs)));
    }
    emit finished(succeeded, failed, paused, isCanceled());
}

bool NetBatchDownloadWorker::isCanceled() const
{
    return cancelRequested_ != nullptr && cancelRequested_->load();
}

bool NetBatchDownloadWorker::downloadResourceToFile(
    NetClient& client,
    int row,
    const QString& chartId,
    const NetBatchResourceSpec& resource,
    const QString& outputPath,
    QString* errorMessage,
    bool* paused,
    QList<NetBatchResourceStats>* resourceStats)
{
    for (int attempt = 0; attempt < 3 && !isCanceled(); ++attempt) {
        const QFileInfo existing(outputPath);
        if (existing.exists() && existing.size() > 0) {
            emit log(UiText::text(QStringLiteral("net.skip_existing_file_1_2"))
                         .arg(outputPath)
                         .arg(existing.size()));
            return true;
        }

        emit log(UiText::text(QStringLiteral("net.download_resource_chart_1_resource"))
                     .arg(chartId, resource.path)
                     .arg(attempt + 1));
        const NetDownloadResult result = client.downloadResourceToFile(chartId, resource.path, outputPath);
        const QString speed = formatSpeed(result.bytesWritten, result.elapsedMs);
        emit log(UiText::text(QStringLiteral("net.resource_result_1_http_2"))
                     .arg(resource.path)
                     .arg(result.statusCode)
                     .arg(result.bytesWritten)
                     .arg(result.elapsedMs)
                     .arg(speed));
        if (result.elapsedMs > 0 || result.bytesWritten > 0) {
            resourceStats->append({resource.label, result.bytesWritten, result.elapsedMs});
        }
        if (result.blockingResponse) {
            if (paused != nullptr) {
                *paused = true;
            }
            if (errorMessage != nullptr) {
                *errorMessage = result.errorMessage;
            }
            return false;
        }
        if (result.ok) {
            return true;
        }
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        emit rowStatus(row, UiText::text(QStringLiteral("net.retrying_1")).arg(resource.label));
        QThread::msleep(800);
    }
    return false;
}

void NetBatchDownloadWorker::emitChartBottleneck(
    const QString& title,
    const QList<NetBatchResourceStats>& resourceStats)
{
    if (resourceStats.isEmpty()) {
        return;
    }
    NetBatchResourceStats slowest;
    slowest.elapsedMs = std::numeric_limits<qint64>::min();
    qint64 totalBytes = 0;
    qint64 totalMs = 0;
    for (const NetBatchResourceStats& stats : resourceStats) {
        totalBytes += stats.bytes;
        totalMs += stats.elapsedMs;
        if (stats.elapsedMs > slowest.elapsedMs) {
            slowest = stats;
        }
    }
    emit log(UiText::text(QStringLiteral("net.chart_speed_summary_1_total"))
                 .arg(title)
                 .arg(totalBytes)
                 .arg(totalMs)
                 .arg(formatSpeed(totalBytes, totalMs))
                 .arg(slowest.label)
                 .arg(slowest.elapsedMs));
}

}  // namespace miacode::net
