#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString readSource(const QString& relativePath)
{
    QFile file(QDir(QStringLiteral(MIACODE_SOURCE_ROOT)).filePath(relativePath));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool isSupportedImage(const QString& fileName)
{
    const QString lowerName = fileName.toLower();
    return lowerName.endsWith(QStringLiteral(".jpg"))
        || lowerName.endsWith(QStringLiteral(".jpeg"))
        || lowerName.endsWith(QStringLiteral(".png"));
}

bool isPositiveInteger(const QJsonValue& value)
{
    return value.isDouble() && value.toInt(0) > 0
        && static_cast<double>(value.toInt(0)) == value.toDouble();
}

bool verifyManifestContract(QTextStream& err)
{
    const QString root = QStringLiteral(MIACODE_SOURCE_ROOT);
    QFile manifestFile(QDir(root).filePath(QStringLiteral("resources/comics/manifest.json")));
    if (!require(manifestFile.open(QIODevice::ReadOnly),
                 QStringLiteral("the source comic manifest is readable"), err)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    bool ok = require(parseError.error == QJsonParseError::NoError && document.isObject(),
                      QStringLiteral("the source comic manifest is valid JSON"), err);
    const QJsonObject manifest = document.object();
    ok &= require(manifest.value(QStringLiteral("version")).toInt(-1) == 1,
                  QStringLiteral("the source comic manifest uses version 1"), err);
    ok &= require(manifest.value(QStringLiteral("items")).isArray(),
                  QStringLiteral("the source comic manifest has an items array"), err);
    if (!ok) {
        return false;
    }

    const QJsonArray items = manifest.value(QStringLiteral("items")).toArray();
    QSet<QString> manifestNames;
    bool itemOrderIsUnique = true;
    for (const QJsonValue& itemValue : items) {
        const QJsonObject item = itemValue.toObject();
        const QString fileName = item.value(QStringLiteral("file")).toString();
        const QString key = fileName.toLower();
        const bool safeName = !fileName.isEmpty() && !fileName.contains(QLatin1Char('/'))
            && !fileName.contains(QLatin1Char('\\')) && !fileName.contains(QStringLiteral(".."));
        const bool reserved = key == QStringLiteral("comic_001.jpg")
            || key == QStringLiteral("fallback.jpg");
        ok &= require(safeName && isSupportedImage(fileName) && !reserved,
                      QStringLiteral("manifest items contain safe supported resource names"), err);
        ok &= require(isPositiveInteger(item.value(QStringLiteral("width")))
                          && isPositiveInteger(item.value(QStringLiteral("height"))),
                      QStringLiteral("manifest items contain positive dimensions"), err);
        itemOrderIsUnique &= !manifestNames.contains(key);
        manifestNames.insert(key);
    }
    ok &= require(itemOrderIsUnique, QStringLiteral("manifest resource names are unique"), err);

    const QFileInfoList entries = QDir(QDir(root).filePath(QStringLiteral("resources/comics")))
        .entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    QSet<QString> directoryNames;
    bool directoryContractIsValid = true;
    for (const QFileInfo& entry : entries) {
        if (entry.fileName() == QStringLiteral("manifest.json")) {
            continue;
        }
        const QString key = entry.fileName().toLower();
        directoryContractIsValid &= entry.isFile() && isSupportedImage(entry.fileName())
            && key != QStringLiteral("comic_001.jpg") && key != QStringLiteral("fallback.jpg")
            && !directoryNames.contains(key);
        directoryNames.insert(key);
    }
    ok &= require(directoryContractIsValid && directoryNames == manifestNames,
                  QStringLiteral("directory resources match the manifest set"), err);
    return ok;
}

bool verifyModelLifecycleContract(QTextStream& err)
{
    const QString header = readSource(QStringLiteral("src/app/ui/ComicResourceModel.h"));
    const QString implementation = readSource(QStringLiteral("src/app/ui/ComicResourceModel.cpp"));
    bool ok = require(!header.isEmpty() && !implementation.isEmpty(),
                      QStringLiteral("the resource model sources are readable"), err);
    ok &= require(!header.contains(QStringLiteral("QFileSystemWatcher"))
                      && !header.contains(QStringLiteral("QTimer"))
                      && !header.contains(QStringLiteral("refreshTimer_"))
                      && !header.contains(QStringLiteral("scheduleRefresh"))
                      && !header.contains(QStringLiteral("updateWatcher")),
                  QStringLiteral("the header has no runtime directory watcher or deferred refresh state"), err);
    ok &= require(!implementation.contains(QStringLiteral("QFileSystemWatcher"))
                      && !implementation.contains(QStringLiteral("QTimer"))
                      && !implementation.contains(QStringLiteral("directoryChanged"))
                      && !implementation.contains(QStringLiteral("fileChanged")),
                  QStringLiteral("the implementation has no runtime directory watcher connections"), err);
    ok &= require(implementation.contains(QStringLiteral("ComicResourceModel::ComicResourceModel"))
                      && implementation.contains(QStringLiteral("    refresh();"))
                      && implementation.contains(QStringLiteral("QImageReader reader"))
                      && implementation.contains(QStringLiteral("reader.canRead()"))
                      && implementation.contains(QStringLiteral("size.width() != expectedWidth"))
                      && implementation.contains(QStringLiteral("size.height() != expectedHeight")),
                  QStringLiteral("startup refresh validates actual image metadata"), err);
    const int currentSignal = implementation.indexOf(QStringLiteral("emit currentChanged();"));
    const int resourcesSignal = implementation.indexOf(QStringLiteral("emit resourcesChanged();"));
    ok &= require(currentSignal >= 0 && resourcesSignal > currentSignal,
                  QStringLiteral("refresh publishes currentChanged before resourcesChanged"), err);
    ok &= require(implementation.contains(QStringLiteral("event=comic_resources_scan"))
                      && implementation.contains(QStringLiteral("total_elapsed_ms"))
                      && implementation.contains(QStringLiteral("resource_count")),
                  QStringLiteral("startup scan records resource count and elapsed timings"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const bool ok = verifyManifestContract(err) && verifyModelLifecycleContract(err);
    if (ok) {
        QTextStream out(stdout);
        out << "comic_resource_model_contract_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
