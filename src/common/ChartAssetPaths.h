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

inline bool isSupportedBackgroundMediaPath(const QString& path, bool includeVideoCandidates = true)
{
    if (path.isEmpty()) {
        return false;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    const QString suffix = info.suffix().trimmed().toLower();
    if (suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("png")) {
        return true;
    }

    return includeVideoCandidates && suffix == QStringLiteral("mp4");
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

inline QString resolvePreferredBackgroundMediaPath(
    const QString& chartPath,
    const QString& explicitPath,
    bool includeVideoCandidates = true)
{
    const QString resolvedChartMediaPath = resolveBackgroundMediaPath(chartPath, includeVideoCandidates);
    if (!resolvedChartMediaPath.isEmpty()) {
        return resolvedChartMediaPath;
    }

    const QString normalizedExplicitPath = explicitPath.isEmpty() ? QString() : QDir::cleanPath(explicitPath);
    if (!isSupportedBackgroundMediaPath(normalizedExplicitPath, includeVideoCandidates)) {
        return QString();
    }

    const QString resolvedTrackPath = resolveTrackPath(chartPath);
    if ((!resolvedTrackPath.isEmpty()
         && normalizedExplicitPath.compare(resolvedTrackPath, Qt::CaseInsensitive) == 0)
        || QFileInfo(normalizedExplicitPath).fileName().compare(trackFileName(), Qt::CaseInsensitive) == 0) {
        return QString();
    }

    return normalizedExplicitPath;
}

inline bool hasBackgroundMedia(const QString& chartPath, bool includeVideoCandidates = true)
{
    return !resolveBackgroundMediaPath(chartPath, includeVideoCandidates).isEmpty();
}

}  // namespace miacode::chart_assets
