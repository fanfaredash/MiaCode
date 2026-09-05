#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include "PvCompressionPolicy.h"

namespace miacode::media {

struct PvCompressionJob {
    QString directoryPath;
    QString displayName;
    QString videoPath;
    qint64 originalBytes = 0;
};

QList<PvCompressionJob> scanPvCompressionFolders(const QString& rootDirectory);
int appendUniquePvCompressionJobs(
    QList<PvCompressionJob>* queue,
    const QList<PvCompressionJob>& candidates);

}  // namespace miacode::media
