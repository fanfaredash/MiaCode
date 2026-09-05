#pragma once

#include <QString>
#include <QStringList>

namespace miacode::chart_media_import {

enum class Kind {
    Image,
    Video,
};

struct Result {
    bool ok = false;
    bool changed = false;
    QString targetPath;
    QStringList backupPaths;
    QStringList warnings;
    QString errorCode;
};

QStringList candidateFileNames(Kind kind);
bool pathsReferToSameFile(const QString& left, const QString& right);
QString targetFileName(const QString& sourcePath, Kind kind);
bool isSupportedSource(const QString& sourcePath, Kind kind);
QStringList existingCandidatePaths(const QString& chartDirectory, Kind kind);
QString nextBackupPath(const QString& originalPath);

Result importToChartDirectory(const QString& sourcePath,
                              const QString& chartDirectory,
                              Kind kind);

}  // namespace miacode::chart_media_import
