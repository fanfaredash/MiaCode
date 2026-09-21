#include "ComicResourceModel.h"

#include "common/AssetPaths.h"
#include "common/DebugLog.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

constexpr auto kComicsAssetSubdirectory = "comics";

// Resolved through the shared assets root so the app finds the comics wherever
// the package put it (next to the executable, or Contents/Resources on macOS).
QString comicsDirectoryPath()
{
    return miacode::assets::assetPath(QString::fromLatin1(kComicsAssetSubdirectory));
}

bool isSupportedImageName(const QString& fileName)
{
    const QString lowerName = fileName.toLower();
    return lowerName.endsWith(QStringLiteral(".jpg"))
        || lowerName.endsWith(QStringLiteral(".jpeg"))
        || lowerName.endsWith(QStringLiteral(".png"));
}

}  // namespace

namespace miacode::ui {

// Construction deliberately does not scan: refresh() runs when the chart-export
// comic area is first shown, so application startup never probes the comic
// images. Until then the model reports an empty resource set.
ComicResourceModel::ComicResourceModel(QObject* parent)
    : QObject(parent)
{
}

void ComicResourceModel::refresh()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    const QVector<ResourceEntry> oldResources = resources_;
    const QString oldUrl = currentImageUrl();
    const QString oldFileName = currentIndex_ >= 0 && currentIndex_ < resources_.size()
        ? resources_.at(currentIndex_).fileName
        : QString();

    QVector<ResourceEntry> discovered;
    const QDir comicsDir(comicsDirectoryPath());
    if (comicsDir.exists()) {
        const QFileInfoList files = comicsDir.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& fileInfo : files) {
            if (!isSupportedImageName(fileInfo.fileName())) {
                continue;
            }
            const QString absolutePath = fileInfo.absoluteFilePath();
            QImageReader reader(absolutePath);
            if (!reader.canRead()) {
                qWarning().noquote() << "ComicResourceModel: skipping unreadable comic image"
                                     << absolutePath;
                continue;
            }
            ResourceEntry entry;
            entry.fileName = fileInfo.fileName();
            entry.imageUrl = QUrl::fromLocalFile(absolutePath).toString();
            discovered.append(std::move(entry));
        }
    }

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
        for (int index = 0; index < resources_.size(); ++index) {
            if (oldResources.at(index).imageUrl != resources_.at(index).imageUrl) {
                resourceListChanged = true;
                break;
            }
        }
    }
    if (oldUrl != currentImageUrl()) {
        emit currentChanged();
    }
    if (resourceListChanged) {
        emit resourcesChanged();
    }

    const qint64 totalElapsedMs = totalTimer.elapsed();
    qInfo().noquote() << QStringLiteral(
        "event=comic_resources_scan resource_count=%1 total_elapsed_ms=%2")
            .arg(resources_.size())
            .arg(totalElapsedMs);
    miacode::debug_log::appendStartupTimingStage(
        QStringLiteral("ui/comic_resources_scan"), totalElapsedMs, totalElapsedMs);
}

QString ComicResourceModel::currentImageUrl() const
{
    return currentIndex_ >= 0 && currentIndex_ < resources_.size()
        ? resources_.at(currentIndex_).imageUrl
        : QString();
}

int ComicResourceModel::resourceCount() const
{
    return resources_.size();
}

QString ComicResourceModel::imageUrlAt(int index) const
{
    return index >= 0 && index < resources_.size() ? resources_.at(index).imageUrl : QString();
}

void ComicResourceModel::selectResource(int index)
{
    if (index < 0 || index >= resources_.size() || currentIndex_ == index) {
        return;
    }
    currentIndex_ = index;
    emit currentChanged();
}

void ComicResourceModel::selectRandomResource()
{
    if (resources_.size() <= 1) {
        return;
    }
    // Draw from the other candidates and shift past the current image so a
    // rotation never repeats the picture already on screen.
    int randomIndex = QRandomGenerator::global()->bounded(resources_.size());
    if (currentIndex_ >= 0) {
        const int randomOffset = QRandomGenerator::global()->bounded(resources_.size() - 1);
        randomIndex = randomOffset >= currentIndex_ ? randomOffset + 1 : randomOffset;
    }
    if (currentIndex_ != randomIndex) {
        currentIndex_ = randomIndex;
        emit currentChanged();
    }
}

}  // namespace miacode::ui
