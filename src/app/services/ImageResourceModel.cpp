#include "ImageResourceModel.h"

#include "common/AssetPaths.h"
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QUrl>

namespace {

bool isSupportedImageName(const QString& fileName)
{
    const QString lowerName = fileName.toLower();
    return lowerName.endsWith(QStringLiteral(".jpg"))
        || lowerName.endsWith(QStringLiteral(".jpeg"))
        || lowerName.endsWith(QStringLiteral(".png"));
}

}  // namespace

namespace miacode::ui {

ImageResourceModel::ImageResourceModel(const QString& assetSubdirectory, QObject* parent)
    : QObject(parent)
    , assetSubdirectory_(assetSubdirectory)
{
}

void ImageResourceModel::refresh()
{
    QVector<QString> discovered;
    const QDir imageDir(miacode::assets::assetPath(assetSubdirectory_));
    if (imageDir.exists()) {
        const QFileInfoList files = imageDir.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& fileInfo : files) {
            if (!isSupportedImageName(fileInfo.fileName())) {
                continue;
            }
            discovered.append(QUrl::fromLocalFile(fileInfo.absoluteFilePath()).toString());
        }
    }

    for (int index = discovered.size() - 1; index > 0; --index) {
        discovered.swapItemsAt(index, QRandomGenerator::global()->bounded(index + 1));
    }

    const bool resourceCountChanged = resources_.size() != discovered.size();
    resources_ = discovered;
    currentIndex_ = resources_.isEmpty() ? -1 : 0;
    if (resourceCountChanged) {
        emit resourcesChanged();
    }
    emit currentChanged();
}

QString ImageResourceModel::currentImageUrl() const
{
    return currentIndex_ >= 0 && currentIndex_ < resources_.size()
        ? resources_.at(currentIndex_)
        : QString();
}

int ImageResourceModel::resourceCount() const
{
    return resources_.size();
}

void ImageResourceModel::selectNextResource()
{
    if (resources_.size() < 2) {
        return;
    }
    currentIndex_ = (currentIndex_ + 1) % resources_.size();
    emit currentChanged();
}

void ImageResourceModel::selectPreviousResource()
{
    if (resources_.size() < 2) {
        return;
    }
    currentIndex_ = (currentIndex_ - 1 + resources_.size()) % resources_.size();
    emit currentChanged();
}

}  // namespace miacode::ui
