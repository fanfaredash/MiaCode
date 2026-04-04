#include "preview/runtime/PreviewSceneAssetRepository.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace miacode::preview::runtime {

PreviewSceneAssetRepository::PreviewSceneAssetRepository(QObject* parent)
    : QObject(parent)
{
    currentAssets_ = PreviewSceneAssetLoader::load(QString(), stageMediaAvailable_, loadGeneration_);
}

void PreviewSceneAssetRepository::setStageMediaAvailable(bool hasMedia)
{
    if (stageMediaAvailable_ == hasMedia) {
        return;
    }
    stageMediaAvailable_ = hasMedia;
    currentAssets_.assetState = PreviewSceneAssetLoader::loadAssetState(stageMediaAvailable_);
    emit assetsChanged();
}

void PreviewSceneAssetRepository::setSkinDirectory(const QString& skinDirectory)
{
    skinDirectory_ = skinDirectory;
    const quint64 generation = ++loadGeneration_;
    QPointer<PreviewSceneAssetRepository> guard(this);
    QThreadPool::globalInstance()->start([guard, skinDirectory, stageMedia = stageMediaAvailable_, generation]() {
        PreviewSceneAssetLoadResult result = PreviewSceneAssetLoader::load(skinDirectory, stageMedia, generation);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) {
                    return;
                }
                guard->applyLoadResult(std::move(result));
            },
            Qt::QueuedConnection);
    });
}

bool PreviewSceneAssetRepository::loadSkinDirectorySync(const QString& skinDirectory)
{
    skinDirectory_ = skinDirectory;
    const quint64 generation = ++loadGeneration_;
    applyLoadResult(PreviewSceneAssetLoader::load(skinDirectory, stageMediaAvailable_, generation));
    return hasCoreSkinAssetsLoaded();
}

bool PreviewSceneAssetRepository::hasCoreSkinAssetsLoaded() const
{
    return !currentAssets_.skinAssets.tapImage.isNull()
        && !currentAssets_.skinAssets.holdImage.isNull()
        && !currentAssets_.skinAssets.starImage.isNull();
}

void PreviewSceneAssetRepository::applyLoadResult(PreviewSceneAssetLoadResult&& result)
{
    if (result.generation != loadGeneration_) {
        return;
    }
    result.assetState = PreviewSceneAssetLoader::loadAssetState(stageMediaAvailable_);
    currentAssets_ = std::move(result);
    emit assetsChanged();
}

}  // namespace miacode::preview::runtime
