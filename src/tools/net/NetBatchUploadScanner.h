#pragma once

#include <QList>
#include <QString>

namespace miacode::net {

struct NetUploadJob {
    QString directoryPath;
    QString displayName;
    QString chartPath;
    QString backgroundPath;
    QString trackPath;
    QString videoPath;
    bool selected = true;
};

QList<NetUploadJob> scanNetUploadFolders(const QString& rootDirectory);
int appendUniqueNetUploadJobs(QList<NetUploadJob>* queue, const QList<NetUploadJob>& candidates);

}  // namespace miacode::net
