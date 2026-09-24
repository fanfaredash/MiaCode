#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

enum class VideoExportOutputMode {
    Mp4,
    Wav,
    Mp4AndWav,
};

inline QString videoExportOutputModeToken(VideoExportOutputMode mode)
{
    switch (mode) {
    case VideoExportOutputMode::Wav:
        return QStringLiteral("wav");
    case VideoExportOutputMode::Mp4AndWav:
        return QStringLiteral("mp4_and_wav");
    case VideoExportOutputMode::Mp4:
    default:
        return QStringLiteral("mp4");
    }
}

inline VideoExportOutputMode videoExportOutputModeFromToken(
    const QString& token,
    VideoExportOutputMode fallback = VideoExportOutputMode::Mp4)
{
    if (token.compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
        return VideoExportOutputMode::Wav;
    }
    if (token.compare(QStringLiteral("mp4_and_wav"), Qt::CaseInsensitive) == 0) {
        return VideoExportOutputMode::Mp4AndWav;
    }
    if (token.compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0) {
        return VideoExportOutputMode::Mp4;
    }
    return fallback;
}

inline bool videoExportProducesMp4(VideoExportOutputMode mode)
{
    return mode != VideoExportOutputMode::Wav;
}

inline bool videoExportProducesWav(VideoExportOutputMode mode)
{
    return mode != VideoExportOutputMode::Mp4;
}

inline QString videoExportPathWithSuffix(const QString& path, const QString& suffix)
{
    QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (cleaned.isEmpty()) {
        return {};
    }
    if (cleaned.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)
        || cleaned.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        cleaned.chop(4);
    }
    return cleaned + QLatin1Char('.') + suffix;
}

inline QString videoExportPrimaryOutputPath(const QString& path, VideoExportOutputMode mode)
{
    return videoExportPathWithSuffix(
        path,
        mode == VideoExportOutputMode::Wav ? QStringLiteral("wav") : QStringLiteral("mp4"));
}

inline QStringList videoExportOutputPaths(const QString& primaryPath, VideoExportOutputMode mode)
{
    QStringList paths;
    if (videoExportProducesMp4(mode)) {
        paths.append(videoExportPathWithSuffix(primaryPath, QStringLiteral("mp4")));
    }
    if (videoExportProducesWav(mode)) {
        paths.append(videoExportPathWithSuffix(primaryPath, QStringLiteral("wav")));
    }
    paths.removeAll(QString());
    return paths;
}

inline QString makeUniqueVideoExportOutputPath(
    const QString& outputPath,
    VideoExportOutputMode mode)
{
    const QString normalizedPath = videoExportPrimaryOutputPath(outputPath, mode);
    if (normalizedPath.isEmpty()) {
        return {};
    }

    const QFileInfo outputInfo(normalizedPath);
    const QDir outputDir = outputInfo.absoluteDir();
    QString fileStem = outputInfo.completeBaseName().trimmed();
    if (fileStem.isEmpty()) {
        fileStem = QStringLiteral("out");
    }

    for (int duplicateIndex = 0;; ++duplicateIndex) {
        const QString candidateStem = duplicateIndex <= 0
            ? fileStem
            : QStringLiteral("%1(%2)").arg(fileStem).arg(duplicateIndex);
        const QString candidate = outputDir.filePath(
            candidateStem + (mode == VideoExportOutputMode::Wav
                ? QStringLiteral(".wav")
                : QStringLiteral(".mp4")));
        bool available = true;
        for (const QString& path : videoExportOutputPaths(candidate, mode)) {
            if (QFileInfo::exists(path)) {
                available = false;
                break;
            }
        }
        if (available) {
            return QDir::cleanPath(candidate);
        }
    }
}

inline QString resolveVideoExportOutputPath(
    const QString& requestedOutputPath,
    const QString& defaultDirectory,
    const QString& defaultOutputName,
    VideoExportOutputMode mode)
{
    const QString baseDirectory = defaultDirectory.trimmed().isEmpty()
        ? QDir::currentPath()
        : QDir::cleanPath(QDir::fromNativeSeparators(defaultDirectory.trimmed()));
    const QString normalizedDefaultName = videoExportPrimaryOutputPath(
        defaultOutputName.trimmed().isEmpty() ? QStringLiteral("out") : defaultOutputName,
        mode);

    const QString trimmedRequestedOutput = requestedOutputPath.trimmed();
    QString resolvedOutputPath = QDir::fromNativeSeparators(trimmedRequestedOutput);
    if (resolvedOutputPath.isEmpty()) {
        resolvedOutputPath = QDir(baseDirectory).filePath(normalizedDefaultName);
    } else {
        const bool trailingSeparator = trimmedRequestedOutput.endsWith('/')
            || trimmedRequestedOutput.endsWith('\\');
        const QFileInfo requestedInfo(resolvedOutputPath);
        if (requestedInfo.isRelative()) {
            resolvedOutputPath = QDir(baseDirectory).absoluteFilePath(resolvedOutputPath);
        }
        const QFileInfo absoluteInfo(resolvedOutputPath);
        if ((absoluteInfo.exists() && absoluteInfo.isDir()) || trailingSeparator) {
            const QString outputDirPath = absoluteInfo.exists() && absoluteInfo.isDir()
                ? absoluteInfo.absoluteFilePath()
                : resolvedOutputPath;
            resolvedOutputPath = QDir(outputDirPath).filePath(normalizedDefaultName);
        }
    }
    return makeUniqueVideoExportOutputPath(resolvedOutputPath, mode);
}
