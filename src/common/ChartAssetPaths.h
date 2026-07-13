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

inline QStringList supportedTrackFileExtensions()
{
    return {
        QStringLiteral("mp3"),
        QStringLiteral("wav"),
        QStringLiteral("flac"),
        QStringLiteral("ogg"),
    };
}

inline QStringList trackCandidateFileNames()
{
    QStringList candidates;
    for (const QString& extension : supportedTrackFileExtensions()) {
        candidates << QStringLiteral("track.%1").arg(extension);
    }
    return candidates;
}

inline QString resolveTrackPathForDirectory(const QString& directoryPath)
{
    if (directoryPath.isEmpty()) {
        return QString();
    }

    const QDir directory(directoryPath);
    for (const QString& name : trackCandidateFileNames()) {
        const QString path = directory.filePath(name);
        if (QFileInfo::exists(path)) {
            return QDir::cleanPath(path);
        }
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

// Phase 4c — chart-format `&video=` extension. Resolves the chart's
// preferred video path with `chartVideoFieldValue` (the raw value
// from `SimaiDocument::videoPath`) taking priority, and falls back to
// the existing sibling-filename heuristic (`bg.mp4`, `pv.mp4`) when
// `&video=` is empty or unresolvable.
//
// Semantics differ from `resolvePreferredBackgroundMediaPath`:
//   - That function treats `explicitPath` as a *fallback* (sibling
//     wins). Used for OS-mounted external media when no sibling exists.
//   - This function treats `chartVideoFieldValue` as an *override*
//     (explicit chart-author intent wins). Empty/missing → sibling
//     fallback. Used for the `&video=` chart field.
//
// `chartVideoFieldValue` is resolved relative to the chart file's
// directory; absolute paths are used as-is. Returns empty string when
// neither override nor fallback yields an existing supported file.
//
// Single source of truth — used by:
//   - Live preview (`PreviewStageMediaHost::setChartPath`)
//   - Warmup pre-resolution (`MainWindow.PreviewWarmupAndSettings.cpp`)
//   - Export pipeline (`MainWindow.ExportSnapshot.cpp`,
//     `VideoExportController.cpp`)
//   - Outline auto-variant (`hasBackgroundMedia` is the OK path; this
//     helper handles the explicit override).
inline QString resolveChartVideoPath(
    const QString& chartPath,
    const QString& chartVideoFieldValue)
{
    if (chartPath.isEmpty()) {
        return QString();
    }
    const QString chartDir = QFileInfo(chartPath).absolutePath();

    // Override path: `&video=` value, resolved relative to chart dir.
    const QString trimmedOverride = chartVideoFieldValue.trimmed();
    if (!trimmedOverride.isEmpty()) {
        QString candidate;
        const QFileInfo overrideInfo(trimmedOverride);
        if (overrideInfo.isAbsolute()) {
            candidate = QDir::cleanPath(trimmedOverride);
        } else {
            candidate = QDir::cleanPath(QDir(chartDir).filePath(trimmedOverride));
        }
        if (isSupportedBackgroundMediaPath(candidate, /*includeVideoCandidates=*/true)) {
            return candidate;
        }
        // Fallthrough — explicit override didn't resolve. We could
        // either return empty (strict mode) or fall back to sibling
        // (lenient mode). Lenient is friendlier for chart authors who
        // typo a path; the missing file shows up as a missing-bg
        // log entry but the chart still has its sibling background.
    }

    // Fallback: existing sibling-filename heuristic.
    return resolveBackgroundMediaPathForDirectory(chartDir, /*includeVideoCandidates=*/true);
}

// Phase 4c — extension of `hasBackgroundMedia` that also considers the
// `&video=` override. Used by the outline auto-variant logic.
inline bool hasChartBackgroundMedia(
    const QString& chartPath,
    const QString& chartVideoFieldValue)
{
    return !resolveChartVideoPath(chartPath, chartVideoFieldValue).isEmpty();
}

// Phase 4c — predicate: the resolved background path is a video (vs
// image). Lets callers decide between "render through QMediaPlayer"
// vs "image-only path" without re-stating the suffix list.
inline bool isVideoBackgroundPath(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
    return suffix == QStringLiteral("mp4");
}

}  // namespace miacode::chart_assets
