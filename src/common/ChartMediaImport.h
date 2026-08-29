#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QUuid>

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
    QString error;
    QStringList cleanupWarnings;
};

inline QStringList candidateFileNames(Kind kind)
{
    if (kind == Kind::Video) {
        return {QStringLiteral("bg.mp4"), QStringLiteral("pv.mp4")};
    }
    return {
        QStringLiteral("bg.jpg"),
        QStringLiteral("bg.png"),
        QStringLiteral("bg.jpeg"),
    };
}

inline bool pathsReferToSameFile(const QString& left, const QString& right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }
    const QFileInfo leftInfo(left);
    const QFileInfo rightInfo(right);
    QString normalizedLeft = leftInfo.exists() ? leftInfo.canonicalFilePath() : leftInfo.absoluteFilePath();
    QString normalizedRight = rightInfo.exists() ? rightInfo.canonicalFilePath() : rightInfo.absoluteFilePath();
    normalizedLeft = QDir::cleanPath(normalizedLeft);
    normalizedRight = QDir::cleanPath(normalizedRight);
#ifdef Q_OS_WIN
    return normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
#else
    return normalizedLeft == normalizedRight;
#endif
}

inline QString targetFileName(const QString& sourcePath, Kind kind)
{
    if (kind == Kind::Video) {
        return QStringLiteral("pv.mp4");
    }
    const QString suffix = QFileInfo(sourcePath).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("png")) {
        return QStringLiteral("bg.%1").arg(suffix);
    }
    return QString();
}

inline bool isSupportedSource(const QString& sourcePath, Kind kind)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return false;
    }
    const QString suffix = sourceInfo.suffix().trimmed().toLower();
    if (kind == Kind::Video) {
        return suffix == QStringLiteral("mp4");
    }
    return suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("png");
}

inline QStringList existingCandidatePaths(const QString& chartDirectory, Kind kind)
{
    QStringList paths;
    const QDir directory(chartDirectory);
    for (const QString& fileName : candidateFileNames(kind)) {
        const QString path = directory.filePath(fileName);
        if (QFileInfo::exists(path)) {
            paths.append(QDir::cleanPath(path));
        }
    }
    return paths;
}

inline QString nextBackupPath(const QString& originalPath)
{
    const QFileInfo info(originalPath);
    const QString suffix = info.suffix();
    const QString baseName = info.completeBaseName() + QStringLiteral("_bak");
    const QString suffixPart = suffix.isEmpty() ? QString() : QStringLiteral(".%1").arg(suffix);
    QDir directory = info.absoluteDir();
    QString candidate = directory.filePath(baseName + suffixPart);
    for (int copyIndex = 2; QFileInfo::exists(candidate); ++copyIndex) {
        candidate = directory.filePath(
            QStringLiteral("%1_%2%3").arg(baseName).arg(copyIndex).arg(suffixPart));
    }
    return QDir::cleanPath(candidate);
}

inline bool copyFileAtomically(const QString& sourcePath, const QString& targetPath, QString* error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = source.errorString();
        }
        return false;
    }

    QSaveFile target(targetPath);
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = target.errorString();
        }
        return false;
    }

    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (!source.atEnd()) {
        const qint64 read = source.read(buffer.data(), buffer.size());
        if (read < 0) {
            if (error != nullptr) {
                *error = source.errorString();
            }
            target.cancelWriting();
            return false;
        }
        if (read > 0 && target.write(buffer.constData(), read) != read) {
            if (error != nullptr) {
                *error = target.errorString();
            }
            target.cancelWriting();
            return false;
        }
    }

    if (!target.commit()) {
        if (error != nullptr) {
            *error = target.errorString();
        }
        return false;
    }
    return true;
}

inline Result importToChartDirectory(const QString& sourcePath, const QString& chartDirectory, Kind kind)
{
    Result result;
    if (!isSupportedSource(sourcePath, kind)) {
        result.error = QStringLiteral("unsupported_source");
        return result;
    }
    if (chartDirectory.isEmpty() || !QFileInfo(chartDirectory).isDir()) {
        result.error = QStringLiteral("invalid_chart_directory");
        return result;
    }

    const QString fileName = targetFileName(sourcePath, kind);
    if (fileName.isEmpty()) {
        result.error = QStringLiteral("unsupported_source");
        return result;
    }
    result.targetPath = QDir(chartDirectory).filePath(fileName);

    struct Backup {
        QString original;
        QString temporary;
    };
    QList<Backup> backups;
    const QStringList existing = existingCandidatePaths(chartDirectory, kind);
    const bool sourceIsTarget = pathsReferToSameFile(sourcePath, result.targetPath);

    const auto restoreBackups = [&backups]() {
        for (auto it = backups.crbegin(); it != backups.crend(); ++it) {
            QFile::remove(it->original);
            QFile::rename(it->temporary, it->original);
        }
    };

    // Move every old candidate except the selected source aside first. This
    // prevents a higher-priority bg.* sibling from shadowing the new file and
    // gives failed copies a complete rollback path.
    for (const QString& path : existing) {
        if (pathsReferToSameFile(path, sourcePath)) {
            continue;
        }
        const QString backupPath = path
            + QStringLiteral(".miacode-import-backup-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QFile::rename(path, backupPath)) {
            restoreBackups();
            result.error = QStringLiteral("backup_failed:%1").arg(path);
            return result;
        }
        backups.append({path, backupPath});
    }

    if (!sourceIsTarget) {
        QString copyError;
        if (!copyFileAtomically(sourcePath, result.targetPath, &copyError)) {
            QFile::remove(result.targetPath);
            restoreBackups();
            result.error = QStringLiteral("copy_failed:%1").arg(copyError);
            return result;
        }
        result.changed = true;
    }

    // When the selected source itself is an old sibling with another canonical
    // name (for example bg.mp4 -> pv.mp4), preserve it as a normal backup only
    // after the new target has committed.
    if (!sourceIsTarget && QFileInfo(sourcePath).absolutePath() == QFileInfo(result.targetPath).absolutePath()) {
        for (const QString& candidate : existing) {
            if (!pathsReferToSameFile(candidate, sourcePath)) {
                continue;
            }
            const QString backupPath = nextBackupPath(candidate);
            if (QFile::rename(candidate, backupPath)) {
                result.backupPaths.append(backupPath);
                result.changed = true;
            } else {
                result.cleanupWarnings.append(candidate);
            }
        }
    }

    for (const Backup& backup : backups) {
        const QString backupPath = nextBackupPath(backup.original);
        if (!QFile::rename(backup.temporary, backupPath)) {
            result.cleanupWarnings.append(backup.temporary);
        } else {
            result.backupPaths.append(backupPath);
            result.changed = true;
        }
    }

    result.ok = true;
    return result;
}

}  // namespace miacode::chart_media_import
