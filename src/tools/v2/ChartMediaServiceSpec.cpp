#include "app/v2/ChartMediaService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) err << "FAIL: " << message << Qt::endl;
    return condition;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool writeImage(const QString& path, const QColor& color)
{
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(color);
    return image.save(path);
}

bool hasTransactionTemp(const QDir& directory)
{
    return !directory.entryList(QStringList{QStringLiteral(".miacode-media-transaction-*")},
                                QDir::Files | QDir::Hidden).isEmpty();
}

bool verifyImageReplacementAndRollbackNames(QTextStream& err)
{
    QTemporaryDir temp;
    bool ok = require(temp.isValid(), QStringLiteral("temporary chart folder exists"), err);
    if (!ok) return false;

    const QString chart = QDir(temp.path()).filePath(QStringLiteral("maidata.txt"));
    const QString source = QDir(temp.path()).filePath(QStringLiteral("chosen.png"));
    ok &= require(writeFile(chart, "&title=media\n"), "chart exists", err);
    ok &= require(writeImage(source, Qt::red), "source PNG is written", err);
    ok &= require(writeImage(QDir(temp.path()).filePath(QStringLiteral("BG.JPG")), Qt::blue),
                  "case-insensitive old JPG is written", err);
    ok &= require(writeImage(QDir(temp.path()).filePath(QStringLiteral("bg.jpeg")), Qt::green),
                  "second old image is written", err);

    miacode::v2::ChartMediaService service;
    const auto result = service.importMedia(
        chart, source, miacode::v2::ChartMediaService::Kind::Image);
    ok &= require(result.ok && result.changed, "image replacement succeeds", err);
    ok &= require(result.targetPath.endsWith(QStringLiteral("bg.png")),
                  "image target keeps the source extension", err);
    ok &= require(QFileInfo::exists(result.targetPath), "new image target exists", err);
    ok &= require(result.backupPaths.size() == 2, "all conflicting image candidates are backed up", err);
    ok &= require(!hasTransactionTemp(QDir(temp.path())),
                  "transaction names do not remain after success", err);
    for (const QString& backup : result.backupPaths) {
        ok &= require(QFileInfo::exists(backup), "image backup exists", err);
    }
    return ok;
}

bool verifySourceInChartFolderAndPvRemoval(QTextStream& err)
{
    QTemporaryDir temp;
    bool ok = require(temp.isValid(), QStringLiteral("PV temporary chart folder exists"), err);
    if (!ok) return false;
    const QString chart = QDir(temp.path()).filePath(QStringLiteral("maidata.txt"));
    const QString oldPv = QDir(temp.path()).filePath(QStringLiteral("BG.MP4"));
    const QString explicitPv = QDir(temp.path()).filePath(QStringLiteral("custom.mp4"));
    ok &= require(writeFile(chart, "&video=custom.mp4\n"), "PV chart exists", err);
    ok &= require(writeFile(oldPv, "old-bg"), "old bg video exists", err);
    ok &= require(writeFile(explicitPv, "old-custom"), "explicit PV exists", err);

    miacode::v2::ChartMediaService service;
    const auto imported = service.importMedia(
        chart, oldPv, miacode::v2::ChartMediaService::Kind::Video);
    ok &= require(imported.ok && QFileInfo::exists(imported.targetPath),
                  "source in chart folder imports to pv.mp4", err);
    ok &= require(imported.backupPaths.size() == 1, "source is recoverable after rename", err);
    ok &= require(!QFileInfo::exists(oldPv), "old source name is no longer active", err);

    const auto removed = service.removePv(chart, QStringLiteral("custom.mp4"));
    ok &= require(removed.ok && removed.changed, "PV removal succeeds", err);
    ok &= require(removed.backupPaths.size() == 2,
                  "both canonical and explicit chart-dir PV files are backed up", err);
    ok &= require(!QFileInfo::exists(imported.targetPath)
                      && !QFileInfo::exists(explicitPv),
                  "PV files are removed from active names", err);
    for (const QString& backup : removed.backupPaths) {
        ok &= require(QFileInfo::exists(backup)
                          && QFileInfo(backup).fileName().contains(QStringLiteral("_bak")),
                      "PV backup has a timestamped recoverable name", err);
    }
    ok &= require(!hasTransactionTemp(QDir(temp.path())),
                  "PV removal leaves no transaction temp", err);
    return ok;
}

bool verifyValidationAndNoMutationOnFailure(QTextStream& err)
{
    QTemporaryDir temp;
    bool ok = require(temp.isValid(), QStringLiteral("failure-case temporary folder exists"), err);
    if (!ok) return false;
    const QString chart = QDir(temp.path()).filePath(QStringLiteral("maidata.txt"));
    const QString invalidImage = QDir(temp.path()).filePath(QStringLiteral("bad.png"));
    ok &= require(writeFile(chart, "&title=media\n"), "failure chart exists", err);
    ok &= require(writeFile(invalidImage, "not an image"), "invalid image fixture exists", err);

    miacode::v2::ChartMediaService service;
    const auto invalid = service.importMedia(
        chart, invalidImage, miacode::v2::ChartMediaService::Kind::Image);
    ok &= require(!invalid.ok && invalid.errorCode == QStringLiteral("unreadable_image"),
                  "unreadable image is rejected before file mutation", err);
    ok &= require(QFileInfo::exists(invalidImage) && !hasTransactionTemp(QDir(temp.path())),
                  "rejected source is left untouched", err);

    const auto missingDir = service.removePv(
        QDir(temp.path()).filePath(QStringLiteral("missing/maidata.txt")), QString());
    ok &= require(!missingDir.ok && missingDir.errorCode == QStringLiteral("invalid_chart_directory"),
                  "missing chart directory is a structured failure", err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;
    ok &= verifyImageReplacementAndRollbackNames(err);
    ok &= verifySourceInChartFolderAndPvRemoval(err);
    ok &= verifyValidationAndNoMutationOnFailure(err);
    return ok ? 0 : 1;
}
