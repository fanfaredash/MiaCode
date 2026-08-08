#include "PvBatchCompressionScanner.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace miacode::media {
namespace {

QString videoPathIgnoringCase(const QDir& directory)
{
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    const QStringList candidates{QStringLiteral("bg.mp4"), QStringLiteral("pv.mp4")};
    for (const QString& candidate : candidates) {
        for (const QFileInfo& file : files) {
            if (file.fileName().compare(candidate, Qt::CaseInsensitive) == 0) {
                return file.absoluteFilePath();
            }
        }
    }
    return {};
}

void appendCompressionJob(const QString& directoryPath, QList<PvCompressionJob>* jobs)
{
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return;
    }

    const QString videoPath = videoPathIgnoringCase(directory);
    const QFileInfo videoInfo(videoPath);

    PvCompressionJob job;
    job.directoryPath = directory.absolutePath();
    job.displayName = QFileInfo(job.directoryPath).fileName();
    job.videoPath = videoPath.isEmpty() ? QString() : videoInfo.absoluteFilePath();
    job.originalBytes = videoPath.isEmpty() ? 0 : videoInfo.size();
    jobs->append(job);
}

}  // namespace

QList<PvCompressionJob> scanPvCompressionFolders(const QString& rootDirectory)
{
    QList<PvCompressionJob> jobs;
    const QDir root(rootDirectory);
    if (!root.exists()) {
        return jobs;
    }

    appendCompressionJob(root.absolutePath(), &jobs);
    const QFileInfoList children = root.entryInfoList(
        QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& child : children) {
        appendCompressionJob(child.absoluteFilePath(), &jobs);
    }
    return jobs;
}

int appendUniquePvCompressionJobs(
    QList<PvCompressionJob>* queue,
    const QList<PvCompressionJob>& candidates)
{
    if (queue == nullptr) {
        return 0;
    }
    int added = 0;
    for (const PvCompressionJob& candidate : candidates) {
        const bool duplicate = std::any_of(queue->cbegin(), queue->cend(), [&](const PvCompressionJob& existing) {
            return existing.directoryPath.compare(candidate.directoryPath, Qt::CaseInsensitive) == 0;
        });
        if (!duplicate) {
            queue->append(candidate);
            ++added;
        }
    }
    return added;
}

}  // namespace miacode::media
