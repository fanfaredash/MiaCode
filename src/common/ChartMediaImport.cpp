#include "ChartMediaImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QUuid>
#include <QVector>

namespace miacode::chart_media_import {
namespace {

QStringList directoryFileNames(const QString& directoryPath)
{
    return QDir(directoryPath).entryList(QDir::Files | QDir::NoSymLinks,
                                         QDir::Name | QDir::IgnoreCase);
}

QString existingPathForName(const QString& directoryPath, const QString& name)
{
    const QDir directory(directoryPath);
    for (const QString& fileName : directoryFileNames(directoryPath)) {
        if (fileName.compare(name, Qt::CaseInsensitive) == 0) {
            return QDir::cleanPath(directory.filePath(fileName));
        }
    }
    return {};
}

bool pathExistsCaseInsensitive(const QString& path)
{
    const QFileInfo info(path);
    return !existingPathForName(info.absolutePath(), info.fileName()).isEmpty();
}

QString normalizedExistingPath(const QString& path)
{
    const QFileInfo info(path);
    const QString existing = existingPathForName(info.absolutePath(), info.fileName());
    return existing.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : existing;
}

QString temporaryPath(const QString& originalPath)
{
    const QFileInfo info(originalPath);
    QString candidate;
    do {
        candidate = info.absoluteDir().filePath(
            QStringLiteral(".miacode-media-transaction-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    } while (pathExistsCaseInsensitive(candidate));
    return candidate;
}

bool copyFileAtomically(const QString& sourcePath, const QString& targetPath, QString* detail)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (detail != nullptr) *detail = source.errorString();
        return false;
    }

    QSaveFile target(targetPath);
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly)) {
        if (detail != nullptr) *detail = target.errorString();
        return false;
    }

    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (!source.atEnd()) {
        const qint64 read = source.read(buffer.data(), buffer.size());
        if (read < 0) {
            if (detail != nullptr) *detail = source.errorString();
            target.cancelWriting();
            return false;
        }
        if (read > 0 && target.write(buffer.constData(), read) != read) {
            if (detail != nullptr) *detail = target.errorString();
            target.cancelWriting();
            return false;
        }
    }
    if (!target.commit()) {
        if (detail != nullptr) *detail = target.errorString();
        return false;
    }
    return true;
}

struct MovedFile {
    QString original;
    QString temporary;
    QString backup;
};

bool restoreMovedFiles(QVector<MovedFile>& moved)
{
    bool restored = true;
    for (auto it = moved.crbegin(); it != moved.crend(); ++it) {
        const QString current = !it->backup.isEmpty() ? it->backup : it->temporary;
        if (current.isEmpty() || !QFileInfo::exists(current)) continue;
        QFile::remove(it->original);
        restored = QFile::rename(current, it->original) && restored;
    }
    return restored;
}

QString uniqueBackupPath(const QString& originalPath)
{
    const QFileInfo info(originalPath);
    const QString stem = info.completeBaseName() + QStringLiteral("_bak");
    const QString suffix = info.suffix().isEmpty()
        ? QString()
        : QStringLiteral(".") + info.suffix();
    QString candidate = info.absoluteDir().filePath(stem + suffix);
    for (int index = 2; pathExistsCaseInsensitive(candidate); ++index) {
        candidate = info.absoluteDir().filePath(
            QStringLiteral("%1_%2%3").arg(stem).arg(index).arg(suffix));
    }
    return QDir::cleanPath(candidate);
}

}  // namespace

QStringList candidateFileNames(Kind kind)
{
    if (kind == Kind::Video) {
        return {QStringLiteral("bg.mp4"), QStringLiteral("pv.mp4")};
    }
    return {
        QStringLiteral("bg.jpg"),
        QStringLiteral("bg.jpeg"),
        QStringLiteral("bg.png"),
    };
}

bool pathsReferToSameFile(const QString& left, const QString& right)
{
    if (left.isEmpty() || right.isEmpty()) return false;
    const QFileInfo leftInfo(left);
    const QFileInfo rightInfo(right);
    const QString normalizedLeft = normalizedExistingPath(left);
    const QString normalizedRight = normalizedExistingPath(right);
    if (leftInfo.exists() && rightInfo.exists()) {
        const QString leftCanonical = leftInfo.canonicalFilePath();
        const QString rightCanonical = rightInfo.canonicalFilePath();
        if (!leftCanonical.isEmpty() && !rightCanonical.isEmpty()) {
            return leftCanonical.compare(rightCanonical, Qt::CaseInsensitive) == 0;
        }
    }
    return normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
}

QString targetFileName(const QString& sourcePath, Kind kind)
{
    if (kind == Kind::Video) return QStringLiteral("pv.mp4");
    const QString suffix = QFileInfo(sourcePath).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("png")) {
        return QStringLiteral("bg.%1").arg(suffix);
    }
    return {};
}

bool isSupportedSource(const QString& sourcePath, Kind kind)
{
    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) return false;
    const QString suffix = info.suffix().trimmed().toLower();
    if (kind == Kind::Video) return suffix == QStringLiteral("mp4");
    if (suffix != QStringLiteral("jpg") && suffix != QStringLiteral("jpeg")
        && suffix != QStringLiteral("png")) {
        return false;
    }
    QImageReader reader(info.absoluteFilePath());
    return reader.canRead();
}

QStringList existingCandidatePaths(const QString& chartDirectory, Kind kind)
{
    QStringList result;
    for (const QString& candidate : candidateFileNames(kind)) {
        const QString path = existingPathForName(chartDirectory, candidate);
        if (!path.isEmpty() && !result.contains(path, Qt::CaseInsensitive)) {
            result.append(path);
        }
    }
    return result;
}

QString nextBackupPath(const QString& originalPath)
{
    return uniqueBackupPath(originalPath);
}

Result importToChartDirectory(const QString& sourcePath,
                              const QString& chartDirectory,
                              Kind kind)
{
    Result result;
    const QFileInfo directoryInfo(chartDirectory);
    if (!directoryInfo.isDir()) {
        result.errorCode = QStringLiteral("invalid_chart_directory");
        return result;
    }
    if (!isSupportedSource(sourcePath, kind)) {
        result.errorCode = kind == Kind::Image
            ? QStringLiteral("unreadable_image")
            : QStringLiteral("unsupported_source");
        return result;
    }

    const QString targetName = targetFileName(sourcePath, kind);
    result.targetPath = QDir(chartDirectory).filePath(targetName);
    const QStringList existing = existingCandidatePaths(chartDirectory, kind);
    if (existing.size() == 1 && pathsReferToSameFile(existing.constFirst(), sourcePath)
        && pathsReferToSameFile(existing.constFirst(), result.targetPath)) {
        result.ok = true;
        return result;
    }

    QVector<MovedFile> moved;
    const auto rollback = [&]() {
        QFile::remove(result.targetPath);
        restoreMovedFiles(moved);
    };

    // Move every candidate, including a selected source already in the chart
    // directory, before copying. That makes source-in-place imports and
    // cross-extension replacements follow one transaction and one rollback.
    for (const QString& path : existing) {
        MovedFile entry;
        entry.original = path;
        entry.temporary = temporaryPath(path);
        if (!QFile::rename(path, entry.temporary)) {
            rollback();
            result.errorCode = QStringLiteral("move_to_transaction_failed");
            return result;
        }
        moved.append(std::move(entry));
    }

    QString copySource = sourcePath;
    for (const MovedFile& entry : moved) {
        if (pathsReferToSameFile(entry.original, sourcePath)) {
            copySource = entry.temporary;
            break;
        }
    }
    QString copyError;
    if (!copyFileAtomically(copySource, result.targetPath, &copyError)) {
        rollback();
        result.errorCode = QStringLiteral("copy_failed");
        result.warnings.append(copyError);
        return result;
    }

    for (MovedFile& entry : moved) {
        entry.backup = uniqueBackupPath(entry.original);
        if (!QFile::rename(entry.temporary, entry.backup)) {
            rollback();
            result.errorCode = QStringLiteral("backup_failed");
            return result;
        }
        result.backupPaths.append(entry.backup);
    }

    result.ok = true;
    result.changed = true;
    return result;
}

}  // namespace miacode::chart_media_import
