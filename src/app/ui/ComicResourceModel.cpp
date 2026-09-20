#include "ComicResourceModel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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

}  // namespace

namespace miacode::ui {

ComicResourceModel::ComicResourceModel(QObject* parent)
    : QObject(parent)
{
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(0);
    connect(&refreshTimer_, &QTimer::timeout, this, &ComicResourceModel::refresh);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged,
            this, &ComicResourceModel::scheduleRefresh);
    connect(&watcher_, &QFileSystemWatcher::fileChanged,
            this, &ComicResourceModel::scheduleRefresh);
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
        || QDir::isAbsolutePath(fileName) || !isSupportedImageName(fileName)) {
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
                                      ResourceEntry* entry) const
{
    if (entry == nullptr || fileName.compare(QStringLiteral("comic_001.jpg"), Qt::CaseInsensitive) == 0
        || fileName.compare(QStringLiteral("fallback.jpg"), Qt::CaseInsensitive) == 0
        || !isSafeRootFile(fileName, comicsDirectory, &entry->absolutePath)) {
        return false;
    }
    QImageReader reader(entry->absolutePath);
    const QSize size = reader.size();
    if (!reader.canRead() || size.width() <= 0 || size.height() <= 0) {
        return false;
    }
    entry->fileName = fileName;
    entry->imageUrl = QUrl::fromLocalFile(entry->absolutePath).toString();
    entry->width = size.width();
    entry->height = size.height();
    entry->aspectRatio = static_cast<double>(entry->width) / static_cast<double>(entry->height);
    return true;
}

void ComicResourceModel::updateWatcher(const QString& comicsDirectory)
{
    const QStringList oldPaths = watcher_.directories();
    if (!oldPaths.isEmpty()) {
        watcher_.removePaths(oldPaths);
    }
    const QFileInfo directoryInfo(comicsDirectory);
    QString watchPath = directoryInfo.exists() && directoryInfo.isDir()
        ? directoryInfo.absoluteFilePath()
        : directoryInfo.absoluteDir().absolutePath();
    if (!QFileInfo::exists(watchPath)) {
        watchPath = QCoreApplication::applicationDirPath();
    }
    if (QFileInfo(watchPath).isDir()) {
        watcher_.addPath(watchPath);
    }
}

void ComicResourceModel::scheduleRefresh()
{
    if (!refreshTimer_.isActive()) {
        refreshTimer_.start();
    }
}

void ComicResourceModel::refresh()
{
    const QVector<ResourceEntry> oldResources = resources_;
    const QString oldUrl = currentImageUrl();
    const double oldAspectRatio = currentAspectRatio();
    const bool oldUsingFallback = usingFallback();
    const bool oldFallbackAvailable = fallbackAvailable_;
    const int oldCount = oldResources.size();
    const QString oldFileName = !oldUsingFallback && currentIndex_ >= 0
        && currentIndex_ < resources_.size() ? resources_.at(currentIndex_).fileName : QString();

    const QString directory = comicsDirectoryPath();
    updateWatcher(directory);
    loadFallbackInfo();

    QVector<ResourceEntry> discovered;
    QHash<QString, ResourceEntry> byName;
    bool directoryValid = true;
    const QDir comicsDir(directory);
    const QFileInfoList files = comicsDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fileInfo : files) {
        const QString lowerName = fileInfo.fileName().toLower();
        if (fileInfo.fileName() == QStringLiteral("manifest.json")) {
            continue;
        }
        if (fileInfo.isDir() || fileInfo.fileName().startsWith(QLatin1Char('.'))
            || lowerName.endsWith(QStringLiteral(".tmp"))
            || lowerName.endsWith(QStringLiteral(".part"))
            || lowerName.endsWith(QStringLiteral(".bak"))) {
            directoryValid = false;
            continue;
        }
        if (!isSupportedImageName(fileInfo.fileName())) {
            directoryValid = false;
            continue;
        }
        ResourceEntry entry;
        if (readResource(fileInfo.fileName(), directory, &entry)) {
            if (entry.fileName.compare(QStringLiteral("comic_001.jpg"), Qt::CaseInsensitive) != 0
                && entry.fileName.compare(QStringLiteral("fallback.jpg"), Qt::CaseInsensitive) != 0) {
                byName.insert(entry.fileName.toLower(), entry);
            }
        } else {
            directoryValid = false;
        }
    }

    QFile manifestFile(comicsDir.filePath(QStringLiteral("manifest.json")));
    QJsonDocument manifestDocument;
    bool manifestValid = directoryValid;
    if (manifestFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        manifestDocument = QJsonDocument::fromJson(manifestFile.readAll(), &error);
        const QJsonObject manifest = manifestDocument.object();
        const QJsonValue versionValue = manifest.value(QStringLiteral("version"));
        manifestValid = manifestValid && error.error == QJsonParseError::NoError
            && manifestDocument.isObject()
            && versionValue.isDouble()
            && std::isfinite(versionValue.toDouble())
            && std::floor(versionValue.toDouble()) == versionValue.toDouble()
            && versionValue.toInt(-1) == 1
            && manifest.value(QStringLiteral("items")).isArray();
    } else {
        manifestValid = false;
    }

    QSet<QString> manifestKeys;
    if (manifestValid) {
        const QJsonArray items = manifestDocument.object().value(QStringLiteral("items")).toArray();
        for (const QJsonValue& itemValue : items) {
            if (!itemValue.isObject()) {
                manifestValid = false;
                break;
            }
            const QJsonObject item = itemValue.toObject();
            const QString fileName = item.value(QStringLiteral("file")).toString();
            const QJsonValue widthValue = item.value(QStringLiteral("width"));
            const QJsonValue heightValue = item.value(QStringLiteral("height"));
            const auto isPositiveInteger = [](const QJsonValue& value) {
                if (!value.isDouble()) {
                    return false;
                }
                const double number = value.toDouble();
                return std::isfinite(number) && number > 0.0
                    && std::floor(number) == number
                    && number <= static_cast<double>(std::numeric_limits<int>::max());
            };
            const QString key = fileName.toLower();
            if (!isSafeRootFile(fileName, directory) || !isPositiveInteger(widthValue)
                || !isPositiveInteger(heightValue) || manifestKeys.contains(key)) {
                manifestValid = false;
                break;
            }
            const auto it = byName.constFind(key);
            if (it != byName.constEnd()) {
                const int manifestWidth = widthValue.toInt();
                const int manifestHeight = heightValue.toInt();
                if (manifestWidth != it->width || manifestHeight != it->height) {
                    manifestValid = false;
                    break;
                }
                discovered.append(it.value());
                manifestKeys.insert(key);
            } else {
                manifestValid = false;
                break;
            }
        }
    }

    if (manifestValid && manifestKeys.size() != byName.size()) {
        manifestValid = false;
    }
    if (!manifestValid) {
        discovered.clear();
        qWarning().noquote() << "Comic manifest or resource directory is invalid; using fallback:" << directory;
    } else {
        for (const ResourceEntry& entry : std::as_const(discovered)) {
            if (!isWithinDirectory(entry.absolutePath, directory)) {
                manifestValid = false;
                break;
            }
        }
    }
    if (!manifestValid) {
        discovered.clear();
    }

    resources_ = std::move(discovered);
    currentIndex_ = -1;
    if (!resources_.isEmpty()) {
        const auto currentIt = std::find_if(resources_.cbegin(), resources_.cend(),
                                            [&oldFileName](const ResourceEntry& entry) {
                                                return entry.fileName == oldFileName;
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
