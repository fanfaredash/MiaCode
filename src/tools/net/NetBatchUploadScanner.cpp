#include "NetBatchUploadScanner.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace miacode::net {
namespace {

QString filePathIgnoringCase(const QDir& directory, const QStringList& preferredNames)
{
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QString& preferred : preferredNames) {
        for (const QFileInfo& file : files) {
            if (file.fileName().compare(preferred, Qt::CaseInsensitive) == 0) {
                return file.absoluteFilePath();
            }
        }
    }
    return {};
}

void appendUploadJob(const QString& directoryPath, QList<NetUploadJob>* jobs)
{
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return;
    }

    NetUploadJob job;
    job.directoryPath = directory.absolutePath();
    job.displayName = QFileInfo(job.directoryPath).fileName();
    job.chartPath = filePathIgnoringCase(directory, {QStringLiteral("maidata.txt")});
    job.backgroundPath = filePathIgnoringCase(
        directory,
        {QStringLiteral("bg.jpg"), QStringLiteral("bg.jpeg"), QStringLiteral("bg.png")});
    job.trackPath = filePathIgnoringCase(directory, {QStringLiteral("track.mp3")});
    job.videoPath = filePathIgnoringCase(directory, {QStringLiteral("pv.mp4"), QStringLiteral("bg.mp4")});
    if (!job.chartPath.isEmpty() && !job.backgroundPath.isEmpty() && !job.trackPath.isEmpty()) {
        jobs->append(job);
    }
}

}  // namespace

QList<NetUploadJob> scanNetUploadFolders(const QString& rootDirectory)
{
    QList<NetUploadJob> jobs;
    const QDir root(rootDirectory);
    if (!root.exists()) {
        return jobs;
    }
    appendUploadJob(root.absolutePath(), &jobs);
    const QFileInfoList children = root.entryInfoList(
        QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& child : children) {
        appendUploadJob(child.absoluteFilePath(), &jobs);
    }
    return jobs;
}

int appendUniqueNetUploadJobs(QList<NetUploadJob>* queue, const QList<NetUploadJob>& candidates)
{
    if (queue == nullptr) {
        return 0;
    }
    int added = 0;
    for (const NetUploadJob& candidate : candidates) {
        const bool duplicate = std::any_of(queue->cbegin(), queue->cend(), [&](const NetUploadJob& existing) {
            return existing.directoryPath.compare(candidate.directoryPath, Qt::CaseInsensitive) == 0;
        });
        if (!duplicate) {
            queue->append(candidate);
            ++added;
        }
    }
    return added;
}

}  // namespace miacode::net
