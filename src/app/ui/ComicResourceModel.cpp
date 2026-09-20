#include "ComicResourceModel.h"

#include "common/DebugLog.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr auto kFallbackResourcePath = ":/comics/fallback.jpg";

QString comicsDirectoryPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/comics"));
}

bool isWithinDirectory(const QString& path, const QString& directory)
{
    const QString canonicalDirectory = QFileInfo(directory).canonicalFilePath();
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    if (canonicalDirectory.isEmpty() || canonicalPath.isEmpty()) {
        return false;
    }
    const QString relative = QDir(canonicalDirectory).relativeFilePath(canonicalPath);
    return relative != QLatin1String("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !relative.startsWith(QStringLiteral("..\\"))
        && !QDir::isAbsolutePath(relative);
}

bool isReservedResourceName(const QString& fileName)
{
    return fileName.compare(QStringLiteral("comic_001.jpg"), Qt::CaseInsensitive) == 0
        || fileName.compare(QStringLiteral("fallback.jpg"), Qt::CaseInsensitive) == 0;
}

bool isPositiveInteger(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && number > 0.0
        && std::floor(number) == number
        && number <= static_cast<double>(std::numeric_limits<int>::max());
}

}  // namespace

namespace miacode::ui {

ComicResourceModel::ComicResourceModel(QObject* parent)
    : QObject(parent)
{
    refresh();
}

QString ComicResourceModel::fallbackUrl() const
{
    return QString::fromLatin1(kFallbackResourcePath);
}

bool ComicResourceModel::loadFallbackInfo()
{
    QImageReader reader(QString::fromLatin1(kFallbackResourcePath));
    const QSize size = reader.size();
    fallbackWidth_ = size.width();
    fallbackHeight_ = size.height();
    fallbackAvailable_ = reader.canRead() && fallbackWidth_ > 0 && fallbackHeight_ > 0;
    if (!fallbackAvailable_) {
        fallbackWidth_ = 0;
        fallbackHeight_ = 0;
    }
    return fallbackAvailable_;
}

double ComicResourceModel::fallbackAspectRatio() const
{
    return fallbackAvailable_ && fallbackHeight_ > 0
        ? static_cast<double>(fallbackWidth_) / static_cast<double>(fallbackHeight_)
        : 0.0;
}

bool ComicResourceModel::isSupportedImageName(const QString& fileName) const
{
    const QString lowerName = fileName.toLower();
    return lowerName.endsWith(QStringLiteral(".jpg"))
        || lowerName.endsWith(QStringLiteral(".jpeg"))
        || lowerName.endsWith(QStringLiteral(".png"));
}

bool ComicResourceModel::isSafeRootFile(const QString& fileName,
                                        const QString& comicsDirectory,
                                        QString* absolutePath) const
{
    if (fileName.isEmpty() || fileName == QLatin1String(".") || fileName == QLatin1String("..")
        || fileName.contains(QStringLiteral(".."))
        || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))
        || QDir::isAbsolutePath(fileName) || !isSupportedImageName(fileName)
        || isReservedResourceName(fileName)) {
        return false;
    }
    const QFileInfo info(QDir(comicsDirectory).filePath(fileName));
    if (!info.exists() || !info.isFile() || !isWithinDirectory(info.absoluteFilePath(), comicsDirectory)) {
        return false;
    }
    if (absolutePath != nullptr) {
        *absolutePath = info.absoluteFilePath();
    }
    return true;
}

bool ComicResourceModel::readResource(const QString& fileName,
                                      const QString& comicsDirectory,
                                      int expectedWidth,
                                      int expectedHeight,
                                      ResourceEntry* entry) const
{
    if (entry == nullptr
        || !isSafeRootFile(fileName, comicsDirectory, &entry->absolutePath)) {
        return false;
    }
    QImageReader reader(entry->absolutePath);
    const QSize size = reader.size();
    if (!reader.canRead() || size.width() <= 0 || size.height() <= 0
        || size.width() != expectedWidth || size.height() != expectedHeight) {
        return false;
    }
    entry->fileName = fileName;
    entry->imageUrl = QUrl::fromLocalFile(entry->absolutePath).toString();
    entry->width = size.width();
    entry->height = size.height();
    entry->aspectRatio = static_cast<double>(entry->width) / static_cast<double>(entry->height);
    return true;
}

void ComicResourceModel::refresh()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    const QVector<ResourceEntry> oldResources = resources_;
    const QString oldUrl = currentImageUrl();
    const double oldAspectRatio = currentAspectRatio();
    const bool oldUsingFallback = usingFallback();
    const bool oldFallbackAvailable = fallbackAvailable_;
    const int oldCount = oldResources.size();
    const QString oldFileName = !oldUsingFallback && currentIndex_ >= 0
        && currentIndex_ < resources_.size() ? resources_.at(currentIndex_).fileName : QString();

    const QString directory = comicsDirectoryPath();
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    loadFallbackInfo();
    const qint64 fallbackElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QVector<ResourceEntry> discovered;
    QHash<QString, QFileInfo> filesByName;
    bool directoryValid = true;
    QString invalidReason;
    const auto invalidate = [&directoryValid, &invalidReason](const QString& reason) {
        directoryValid = false;
        if (invalidReason.isEmpty()) {
            invalidReason = reason;
        }
    };
    const QDir comicsDir(directory);
    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        invalidate(QStringLiteral("resource directory is missing"));
    } else {
        const QFileInfoList files = comicsDir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& fileInfo : files) {
            if (fileInfo.fileName() == QStringLiteral("manifest.json")) {
                continue;
            }
            const QString lowerName = fileInfo.fileName().toLower();
            if (fileInfo.isDir()) {
                invalidate(QStringLiteral("directory contains a subdirectory: %1")
                               .arg(fileInfo.fileName()));
                continue;
            }
            if (!fileInfo.isFile() || !isWithinDirectory(fileInfo.absoluteFilePath(), directory)) {
                invalidate(QStringLiteral("resource is outside the comics directory: %1")
                               .arg(fileInfo.fileName()));
                continue;
            }
            if (fileInfo.fileName().startsWith(QLatin1Char('.'))
                || lowerName.endsWith(QStringLiteral(".tmp"))
                || lowerName.endsWith(QStringLiteral(".part"))
                || lowerName.endsWith(QStringLiteral(".bak"))) {
                invalidate(QStringLiteral("directory contains a temporary resource: %1")
                               .arg(fileInfo.fileName()));
                continue;
            }
            if (!isSupportedImageName(fileInfo.fileName())) {
                invalidate(QStringLiteral("directory contains an unsupported file: %1")
                               .arg(fileInfo.fileName()));
                continue;
            }
            if (isReservedResourceName(fileInfo.fileName())) {
                invalidate(QStringLiteral("directory contains a reserved resource name: %1")
                               .arg(fileInfo.fileName()));
                continue;
            }
            const QString key = fileInfo.fileName().toLower();
            if (filesByName.contains(key)) {
                invalidate(QStringLiteral("directory contains a case-insensitive filename collision: %1")
                               .arg(fileInfo.fileName()));
                continue;
            }
            filesByName.insert(key, fileInfo);
        }
    }
    const qint64 directoryElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QFile manifestFile(comicsDir.filePath(QStringLiteral("manifest.json")));
    QJsonDocument manifestDocument;
    bool manifestValid = directoryValid;
    if (!manifestValid) {
        if (invalidReason.isEmpty()) {
            invalidReason = QStringLiteral("resource directory contract is invalid");
        }
    }
    if (manifestFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        manifestDocument = QJsonDocument::fromJson(manifestFile.readAll(), &error);
        const QJsonObject manifest = manifestDocument.object();
        const QJsonValue versionValue = manifest.value(QStringLiteral("version"));
        if (error.error != QJsonParseError::NoError || !manifestDocument.isObject()) {
            manifestValid = false;
            invalidReason = QStringLiteral("manifest JSON is invalid: %1").arg(error.errorString());
        } else if (!versionValue.isDouble() || versionValue.toInt(-1) != 1
                   || std::floor(versionValue.toDouble()) != versionValue.toDouble()) {
            manifestValid = false;
            invalidReason = QStringLiteral("manifest version must be 1");
        } else if (!manifest.value(QStringLiteral("items")).isArray()) {
            manifestValid = false;
            invalidReason = QStringLiteral("manifest items is not an array");
        }
    } else {
        manifestValid = false;
        invalidReason = QStringLiteral("manifest.json cannot be opened");
    }
    const qint64 manifestElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QSet<QString> manifestKeys;
    if (manifestValid) {
        const QJsonArray items = manifestDocument.object().value(QStringLiteral("items")).toArray();
        for (int itemIndex = 0; itemIndex < items.size(); ++itemIndex) {
            const QJsonValue itemValue = items.at(itemIndex);
            if (!itemValue.isObject()) {
                manifestValid = false;
                invalidReason = QStringLiteral("manifest item %1 is not an object").arg(itemIndex);
                break;
            }
            const QJsonObject item = itemValue.toObject();
            const QString fileName = item.value(QStringLiteral("file")).toString();
            const QJsonValue widthValue = item.value(QStringLiteral("width"));
            const QJsonValue heightValue = item.value(QStringLiteral("height"));
            const QString key = fileName.toLower();
            if (!isSafeRootFile(fileName, directory) || !isPositiveInteger(widthValue)
                || !isPositiveInteger(heightValue) || manifestKeys.contains(key)) {
                manifestValid = false;
                invalidReason = QStringLiteral("manifest item %1 has an invalid file or dimensions")
                                    .arg(itemIndex);
                break;
            }
            const auto it = filesByName.constFind(key);
            if (it == filesByName.constEnd()) {
                manifestValid = false;
                invalidReason = QStringLiteral("manifest item is missing from the directory: %1")
                                    .arg(fileName);
                break;
            }
            ResourceEntry entry;
            if (!readResource(fileName, directory, widthValue.toInt(), heightValue.toInt(), &entry)) {
                manifestValid = false;
                invalidReason = QStringLiteral("resource cannot be read or has mismatched dimensions: %1")
                                    .arg(fileName);
                break;
            }
            discovered.append(std::move(entry));
            manifestKeys.insert(key);
        }
    }

    if (manifestValid && manifestKeys.size() != filesByName.size()) {
        manifestValid = false;
        invalidReason = QStringLiteral("manifest and directory resource sets differ");
    }
    if (!manifestValid) {
        discovered.clear();
        qWarning().noquote() << "ComicResourceModel: using fallback;" << invalidReason
                             << "directory=" << directory;
    }
    const qint64 imageMetadataElapsedMs = phaseTimer.elapsed();

    resources_ = std::move(discovered);
    currentIndex_ = -1;
    if (!resources_.isEmpty()) {
        const auto currentIt = std::find_if(resources_.cbegin(), resources_.cend(),
                                            [&oldFileName](const ResourceEntry& entry) {
                                                return entry.fileName.compare(oldFileName,
                                                                              Qt::CaseInsensitive) == 0;
                                            });
        currentIndex_ = currentIt != resources_.cend()
            ? static_cast<int>(std::distance(resources_.cbegin(), currentIt))
            : 0;
    }

    bool resourceListChanged = oldResources.size() != resources_.size();
    if (!resourceListChanged) {
        for (int i = 0; i < resources_.size(); ++i) {
            const ResourceEntry& oldEntry = oldResources.at(i);
            const ResourceEntry& newEntry = resources_.at(i);
            if (oldEntry.fileName != newEntry.fileName
                || oldEntry.absolutePath != newEntry.absolutePath
                || oldEntry.width != newEntry.width
                || oldEntry.height != newEntry.height
                || oldEntry.aspectRatio != newEntry.aspectRatio) {
                resourceListChanged = true;
                break;
            }
        }
    }
    if (oldCount != resources_.size() || oldUrl != currentImageUrl()
        || oldAspectRatio != currentAspectRatio() || oldUsingFallback != usingFallback()
        || oldFallbackAvailable != fallbackAvailable_) {
        emit currentChanged();
    }
    if (resourceListChanged) {
        emit resourcesChanged();
    }

    const qint64 totalElapsedMs = totalTimer.elapsed();
    qInfo().noquote() << QStringLiteral(
        "event=comic_resources_scan resource_count=%1 valid=%2 fallback_available=%3 "
        "fallback_elapsed_ms=%4 directory_scan_elapsed_ms=%5 manifest_elapsed_ms=%6 "
        "image_metadata_elapsed_ms=%7 total_elapsed_ms=%8")
            .arg(resources_.size())
            .arg(manifestValid ? 1 : 0)
            .arg(fallbackAvailable_ ? 1 : 0)
            .arg(fallbackElapsedMs)
            .arg(directoryElapsedMs)
            .arg(manifestElapsedMs)
            .arg(imageMetadataElapsedMs)
            .arg(totalElapsedMs);
    miacode::debug_log::appendStartupTimingStage(
        QStringLiteral("ui/comic_resources_scan"), totalElapsedMs, totalElapsedMs);
}

QString ComicResourceModel::currentImageUrl() const
{
    return currentIndex_ >= 0 && currentIndex_ < resources_.size()
        ? resources_.at(currentIndex_).imageUrl : fallbackUrl();
}

double ComicResourceModel::currentAspectRatio() const
{
    return currentIndex_ >= 0 && currentIndex_ < resources_.size()
        ? resources_.at(currentIndex_).aspectRatio : fallbackAspectRatio();
}

bool ComicResourceModel::usingFallback() const
{
    return currentIndex_ < 0 || currentIndex_ >= resources_.size();
}

bool ComicResourceModel::fallbackAvailable() const
{
    return fallbackAvailable_;
}

int ComicResourceModel::resourceCount() const
{
    return resources_.size();
}

QString ComicResourceModel::imageUrlAt(int index) const
{
    return index >= 0 && index < resources_.size() ? resources_.at(index).imageUrl : QString();
}

double ComicResourceModel::aspectRatioAt(int index) const
{
    return index >= 0 && index < resources_.size() ? resources_.at(index).aspectRatio : 0.0;
}

void ComicResourceModel::useFallback()
{
    if (!usingFallback()) {
        currentIndex_ = -1;
        emit currentChanged();
    }
}

void ComicResourceModel::selectNextResource()
{
    if (resources_.isEmpty()) {
        return;
    }
    const int nextIndex = usingFallback() ? 0 : (currentIndex_ + 1) % resources_.size();
    if (currentIndex_ != nextIndex) {
        currentIndex_ = nextIndex;
        emit currentChanged();
    }
}

void ComicResourceModel::selectPreviousResource()
{
    if (resources_.isEmpty()) {
        return;
    }
    const int previousIndex = usingFallback() ? resources_.size() - 1
                                               : (currentIndex_ + resources_.size() - 1) % resources_.size();
    if (currentIndex_ != previousIndex) {
        currentIndex_ = previousIndex;
        emit currentChanged();
    }
}

void ComicResourceModel::selectRandomResource()
{
    if (resources_.size() <= 1) {
        return;
    }
    const int currentIndex = usingFallback() ? -1 : currentIndex_;
    int randomIndex = QRandomGenerator::global()->bounded(resources_.size());
    if (currentIndex >= 0) {
        const int randomOffset = QRandomGenerator::global()->bounded(resources_.size() - 1);
        randomIndex = randomOffset >= currentIndex ? randomOffset + 1 : randomOffset;
    }
    if (currentIndex_ != randomIndex) {
        currentIndex_ = randomIndex;
        emit currentChanged();
    }
}

}  // namespace miacode::ui
