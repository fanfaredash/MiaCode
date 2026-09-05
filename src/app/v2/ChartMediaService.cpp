#include "ChartMediaService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace miacode::v2 {
namespace {

QString chartDirectory(const QString& chartPath)
{
    return chartPath.isEmpty() ? QString() : QFileInfo(chartPath).absolutePath();
}

QString existingPathForName(const QString& directoryPath, const QString& fileName)
{
    const QDir directory(directoryPath);
    for (const QString& entry : directory.entryList(QDir::Files | QDir::NoSymLinks,
                                                     QDir::Name | QDir::IgnoreCase)) {
        if (entry.compare(fileName, Qt::CaseInsensitive) == 0) {
            return QDir::cleanPath(directory.filePath(entry));
        }
    }
    return {};
}

bool samePath(const QString& left, const QString& right)
{
    return miacode::chart_media_import::pathsReferToSameFile(left, right);
}

QString timestampBackupPath(const QString& originalPath)
{
    const QFileInfo info(originalPath);
    const QString stamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString stem = info.completeBaseName()
        + QStringLiteral("_") + stamp + QStringLiteral("_bak");
    const QString suffix = info.suffix().isEmpty()
        ? QString()
        : QStringLiteral(".") + info.suffix();
    QString candidate = info.absoluteDir().filePath(stem + suffix);
    int serial = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = info.absoluteDir().filePath(
            QStringLiteral("%1_%2%3").arg(stem).arg(serial++).arg(suffix));
    }
    return QDir::cleanPath(candidate);
}

}  // namespace

ChartMediaService::Result ChartMediaService::importMedia(
    const QString& chartPath, const QString& sourcePath, Kind kind) const
{
    return miacode::chart_media_import::importToChartDirectory(
        sourcePath, chartDirectory(chartPath), kind);
}

ChartMediaService::Result ChartMediaService::removePv(
    const QString& chartPath, const QString& chartVideoFieldValue) const
{
    Result result;
    const QString directory = chartDirectory(chartPath);
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
        result.errorCode = QStringLiteral("invalid_chart_directory");
        return result;
    }

    QStringList paths = existingCandidates(chartPath, Kind::Video);
    QString overridePath = chartVideoFieldValue.trimmed();
    if (!overridePath.isEmpty()) {
        const QFileInfo overrideInfo(overridePath);
        overridePath = overrideInfo.isAbsolute()
            ? QDir::cleanPath(overridePath)
            : QDir(directory).filePath(overridePath);
        const QString resolvedOverride = existingPathForName(
            QFileInfo(overridePath).absolutePath(), QFileInfo(overridePath).fileName());
        if (!resolvedOverride.isEmpty()
            && QFileInfo(resolvedOverride).absolutePath().compare(directory, Qt::CaseInsensitive) == 0
            && !paths.contains(resolvedOverride, Qt::CaseInsensitive)) {
            paths.append(resolvedOverride);
        }
    }

    struct MovedFile {
        QString original;
        QString backup;
    };
    QVector<MovedFile> moved;
    for (const QString& path : paths) {
        const QString backup = timestampBackupPath(path);
        if (!QFile::rename(path, backup)) {
            for (auto it = moved.crbegin(); it != moved.crend(); ++it) {
                QFile::rename(it->backup, it->original);
            }
            result.errorCode = QStringLiteral("backup_failed");
            result.backupPaths.clear();
            return result;
        }
        moved.append({path, backup});
        result.backupPaths.append(backup);
    }

    result.ok = true;
    result.changed = !moved.isEmpty();
    return result;
}

QStringList ChartMediaService::existingCandidates(const QString& chartPath, Kind kind)
{
    return miacode::chart_media_import::existingCandidatePaths(
        chartDirectory(chartPath), kind);
}

bool ChartMediaService::sourceIsSupported(const QString& sourcePath, Kind kind)
{
    return miacode::chart_media_import::isSupportedSource(sourcePath, kind);
}

QString ChartMediaService::targetPath(const QString& chartPath,
                                      const QString& sourcePath,
                                      Kind kind)
{
    const QString fileName = miacode::chart_media_import::targetFileName(sourcePath, kind);
    return fileName.isEmpty() ? QString() : QDir(chartDirectory(chartPath)).filePath(fileName);
}

bool ChartMediaService::isConflictingCandidate(const QString& candidatePath,
                                               const QString& sourcePath,
                                               const QString& targetPath)
{
    return !samePath(candidatePath, sourcePath) && !samePath(candidatePath, targetPath);
}

}  // namespace miacode::v2
