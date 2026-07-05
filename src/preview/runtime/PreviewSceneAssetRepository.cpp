#include "preview/runtime/PreviewSceneAssetRepository.h"

#include "common/OperationLog.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

namespace miacode::preview::runtime {

PreviewSceneAssetRepository::PreviewSceneAssetRepository(QObject* parent)
    : QObject(parent)
{
    currentAssets_ = PreviewSceneAssetLoader::load(
        QString(),
        outlineVariant_,
        loadGeneration_,
        outlineImagePath_,
        outlineImageMode_);
}

void PreviewSceneAssetRepository::setStageMediaAvailable(bool hasMedia)
{
    if (stageMediaAvailable_ == hasMedia) {
        return;
    }
    stageMediaAvailable_ = hasMedia;
}

void PreviewSceneAssetRepository::setOutlineVariant(PreviewOutlineVariant variant)
{
    setOutlineSelection(variant, outlineImagePath_, outlineImageMode_);
}

void PreviewSceneAssetRepository::setOutlineImagePath(const QString& path)
{
    setOutlineSelection(outlineVariant_, path, outlineImageMode_);
}

void PreviewSceneAssetRepository::setOutlineSelection(
    PreviewOutlineVariant variant,
    const QString& path,
    PreviewOutlineImageMode imageMode)
{
    const QString normalized = path.trimmed();
    if (outlineVariant_ == variant
        && outlineImagePath_ == normalized
        && outlineImageMode_ == imageMode) {
        return;
    }
    outlineVariant_ = variant;
    outlineImagePath_ = normalized;
    outlineImageMode_ = imageMode;
    currentAssets_.assetState = PreviewSceneAssetLoader::loadAssetState(
        outlineVariant_,
        outlineImagePath_,
        outlineImageMode_);
    emit assetsChanged();
}

void PreviewSceneAssetRepository::setSkinDirectory(const QString& skinDirectory)
{
    MC_OP("PreviewSceneAssetRepository::setSkinDirectory");
    _mc_op_.note(QStringLiteral("skin=%1").arg(skinDirectory));
    skinDirectory_ = skinDirectory;
    const quint64 generation = ++loadGeneration_;
    QPointer<PreviewSceneAssetRepository> guard(this);
    QThreadPool::globalInstance()->start([
        guard,
        skinDirectory,
        outlineVariant = outlineVariant_,
        outlineImagePath = outlineImagePath_,
        outlineImageMode = outlineImageMode_,
        generation
    ]() {
        PreviewSceneAssetLoadResult result =
            PreviewSceneAssetLoader::load(
                skinDirectory,
                outlineVariant,
                generation,
                outlineImagePath,
                outlineImageMode);
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
    MC_OP("PreviewSceneAssetRepository::loadSkinDirectorySync");
    _mc_op_.note(QStringLiteral("skin=%1").arg(skinDirectory));
    skinDirectory_ = skinDirectory;
    const quint64 generation = ++loadGeneration_;
    applyLoadResult(PreviewSceneAssetLoader::load(
        skinDirectory,
        outlineVariant_,
        generation,
        outlineImagePath_,
        outlineImageMode_));
    const bool ok = hasCoreSkinAssetsLoaded();
    if (!ok) {
        _mc_op_.fail(QStringLiteral("core skin assets missing after load"));
    }
    return ok;
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
    result.assetState = PreviewSceneAssetLoader::loadAssetState(
        outlineVariant_,
        outlineImagePath_,
        outlineImageMode_);
    currentAssets_ = std::move(result);
    emit assetsChanged();
}

}  // namespace miacode::preview::runtime
