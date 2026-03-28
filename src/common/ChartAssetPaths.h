#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace miacode::chart_assets {

inline QString trackFileName()
{
    return QStringLiteral("track.mp3");
}

inline QString resolveTrackPathForDirectory(const QString& directoryPath)
{
    if (directoryPath.isEmpty()) {
        return QString();
    }

    const QString path = QDir(directoryPath).filePath(trackFileName());
    if (QFileInfo::exists(path)) {
        return QDir::cleanPath(path);
    }
    return QString();
}

inline QString resolveTrackPath(const QString& chartPath)
{
    if (chartPath.isEmpty()) {
        return QString();
    }
    return resolveTrackPathForDirectory(QFileInfo(chartPath).absolutePath());
}

inline QStringList backgroundMediaCandidateFileNames(bool includeVideoCandidates = true)
{
    QStringList candidates;
    if (includeVideoCandidates) {
        candidates << QStringLiteral("bg.mp4")
                   << QStringLiteral("pv.mp4");
    }
    candidates << QStringLiteral("bg.jpg")
               << QStringLiteral("bg.png")
               << QStringLiteral("bg.jpeg");
    return candidates;
}

inline QString resolveBackgroundMediaPathForDirectory(const QString& directoryPath, bool includeVideoCandidates = true)
{
    if (directoryPath.isEmpty()) {
        return QString();
    }

    const QDir directory(directoryPath);
    for (const QString& name : backgroundMediaCandidateFileNames(includeVideoCandidates)) {
        const QString path = directory.filePath(name);
        if (QFileInfo::exists(path)) {
            return QDir::cleanPath(path);
        }
    }
    return QString();
}

inline QString resolveBackgroundMediaPath(const QString& chartPath, bool includeVideoCandidates = true)
{
    if (chartPath.isEmpty()) {
        return QString();
    }
    return resolveBackgroundMediaPathForDirectory(QFileInfo(chartPath).absolutePath(), includeVideoCandidates);
}

inline bool hasBackgroundMedia(const QString& chartPath, bool includeVideoCandidates = true)
{
    return !resolveBackgroundMediaPath(chartPath, includeVideoCandidates).isEmpty();
}

}  // namespace miacode::chart_assets
