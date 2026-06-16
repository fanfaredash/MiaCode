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

QString trText(const char* zh, const char* en)
{
    return UiText::isChineseUi() ? QString::fromUtf8(zh) : QString::fromLatin1(en);
}

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

    emit log(trText("后台下载线程已启动。", "Background download thread started."));
    for (int row = 0; row < request_.jobs.size() && !isCanceled() && !paused; ++row) {
        NetDownloadJob job = request_.jobs.at(row);
        if (!job.selected) {
            emit rowStatus(row, trText("未选中", "Not selected"));
            continue;
        }

        emit rowStatus(row, trText("下载中...", "Downloading..."));
        emit summary(trText("正在下载：%1", "Downloading: %1").arg(job.chart.title));
        emit log(trText("开始谱面：%1 [%2]", "Start chart: %1 [%2]").arg(job.chart.title, job.chart.id));

        QString error;
        job.outputDirectoryPath = chartDirectoryPathForTitle(request_.outputDirectory, job.chart.title, job.chart.id);
        if (!QDir().mkpath(job.outputDirectoryPath)) {
            error = trText("无法创建谱面文件夹。", "Could not create chart folder.");
        }

        bool resourcesOk = error.isEmpty();
        QList<NetBatchResourceStats> resourceStats;
        for (const NetBatchResourceSpec& resource : requiredResources()) {
            if (!resourcesOk || paused || isCanceled()) {
                break;
            }
            emit rowStatus(row, trText("下载中：%1", "Downloading: %1").arg(resource.label));
            const QString outputPath = QDir(job.outputDirectoryPath).filePath(resource.fileName);
            resourcesOk = downloadResourceToFile(client, row, job.chart.id, resource, outputPath, &error, &paused, &resourceStats);
        }

        if (paused) {
            emit rowStatus(row, trText("已暂停", "Paused"));
            break;
        }
        if (isCanceled()) {
            break;
        }
        if (!resourcesOk) {
            ++failed;
            job.errorMessage = error.isEmpty() ? trText("资源下载失败。", "Resource download failed.") : error;
            emit rowStatus(row, trText("失败：%1", "Failed: %1").arg(job.errorMessage));
        } else {
            bool chartDone = true;
            if (request_.createZip) {
                emit rowStatus(row, trText("正在打包 ZIP...", "Packaging ZIP..."));
                job.outputZipPath = uniqueZipPathForTitle(request_.outputDirectory, job.chart.title);
                QStringList entries;
                QElapsedTimer zipElapsed;
                zipElapsed.start();
                chartDone = packNetChartFolderZip(job.outputDirectoryPath, job.outputZipPath, &entries, &error);
                emit log(trText("ZIP 打包：%1 -> %2（%3 ms）", "ZIP package: %1 -> %2 (%3 ms)")
                             .arg(job.outputDirectoryPath, job.outputZipPath)
                             .arg(zipElapsed.elapsed()));
            }
            if (chartDone) {
                ++succeeded;
                emit rowStatus(row, request_.createZip ? trText("完成（文件夹 + ZIP）", "Done (folder + ZIP)") : trText("完成（文件夹）", "Done (folder)"));
                emit log(trText("谱面完成：%1 -> %2", "Chart complete: %1 -> %2").arg(job.chart.title, job.outputDirectoryPath));
            } else {
                ++failed;
                job.errorMessage = error;
                emit rowStatus(row, trText("打包失败：%1", "Package failed: %1").arg(error));
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
        emit log(trText("队列暂停：Net/Cloudflare 阻断了请求。", "Queue paused: Net/Cloudflare blocked a request."));
    } else if (isCanceled()) {
        emit log(trText("队列取消：成功 %1，失败 %2。", "Queue canceled: %1 succeeded, %2 failed.").arg(succeeded).arg(failed));
    } else {
        emit log(trText("队列完成：成功 %1，失败 %2，网络总量 %3 bytes，平均 %4。", "Queue complete: %1 succeeded, %2 failed, network total %3 bytes, average %4.")
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
            emit log(trText("跳过已有文件：%1（%2 bytes）", "Skip existing file: %1 (%2 bytes)")
                         .arg(outputPath)
                         .arg(existing.size()));
            return true;
        }

        emit log(trText("下载资源：chart=%1 resource=%2 attempt=%3", "Download resource: chart=%1 resource=%2 attempt=%3")
                     .arg(chartId, resource.path)
                     .arg(attempt + 1));
        const NetDownloadResult result = client.downloadResourceToFile(chartId, resource.path, outputPath);
        const QString speed = formatSpeed(result.bytesWritten, result.elapsedMs);
        emit log(trText("资源结果：%1 HTTP=%2 bytes=%3 elapsed=%4ms speed=%5", "Resource result: %1 HTTP=%2 bytes=%3 elapsed=%4ms speed=%5")
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
        emit rowStatus(row, trText("重试：%1", "Retrying: %1").arg(resource.label));
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
    emit log(trText("谱面速度汇总：%1 total=%2 bytes network=%3ms avg=%4 slowest=%5/%6ms",
                    "Chart speed summary: %1 total=%2 bytes network=%3ms avg=%4 slowest=%5/%6ms")
                 .arg(title)
                 .arg(totalBytes)
                 .arg(totalMs)
                 .arg(formatSpeed(totalBytes, totalMs))
                 .arg(slowest.label)
                 .arg(slowest.elapsedMs));
}

}  // namespace miacode::net
