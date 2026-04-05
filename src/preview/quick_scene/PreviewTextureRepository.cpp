#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QQuickWindow>
#include <QSGTexture>

namespace {

PreviewTextureLayerStats& ensureLayerStats(
    QVector<PreviewTextureLayerStats>* layerStats,
    const char* layerName
)
{
    Q_ASSERT(layerStats != nullptr);

    const QString name = QString::fromLatin1(layerName != nullptr ? layerName : "");
    for (PreviewTextureLayerStats& layerStat : *layerStats) {
        if (layerStat.name == name) {
            return layerStat;
        }
    }

    layerStats->append(PreviewTextureLayerStats{name});
    return layerStats->last();
}

}  // namespace

PreviewTextureRepository::~PreviewTextureRepository()
{
    clear();
}

void PreviewTextureRepository::setWindow(QQuickWindow* window)
{
    if (window_ == window) {
        return;
    }
    clear();
    window_ = window;
}

void PreviewTextureRepository::beginFrame()
{
    qDeleteAll(transientTextures_);
    transientTextures_.clear();
    stats_ = PreviewTextureStats();
}

QSGTexture* PreviewTextureRepository::textureForImage(const QImage& image, bool cacheable)
{
    if (window_ == nullptr || image.isNull()) {
        return nullptr;
    }

    const quint64 key = static_cast<quint64>(image.cacheKey());
    if (cacheable) {
        if (QSGTexture* existing = cachedTextures_.value(key, nullptr); existing != nullptr) {
            stats_.cachedHitCount += 1;
            return existing;
        }
        QSGTexture* texture = window_->createTextureFromImage(image);
        cachedTextures_.insert(key, texture);
        stats_.cachedCreateCount += 1;
        return texture;
    }
    if (QSGTexture* existing = transientTextures_.value(key, nullptr); existing != nullptr) {
        stats_.transientHitCount += 1;
        return existing;
    }
    QSGTexture* texture = createOwnedTexture(image);
    transientTextures_.insert(key, texture);
    stats_.transientCreateCount += 1;
    return texture;
}

QSGTexture* PreviewTextureRepository::retainedTextureForImage(const QString& slotName, const QImage& image)
{
    if (window_ == nullptr || image.isNull() || slotName.isEmpty()) {
        return nullptr;
    }

    const quint64 key = static_cast<quint64>(image.cacheKey());
    PreviewRetainedTextureEntry& entry = retainedTextures_[slotName];
    if (entry.texture != nullptr && entry.key == key) {
        return entry.texture;
    }

    delete entry.texture;
    entry.texture = createOwnedTexture(image);
    entry.key = key;
    return entry.texture;
}

void PreviewTextureRepository::releaseRetainedTexture(const QString& slotName)
{
    auto it = retainedTextures_.find(slotName);
    if (it == retainedTextures_.end()) {
        return;
    }

    delete it->texture;
    retainedTextures_.erase(it);
}

QSGTexture* PreviewTextureRepository::createOwnedTexture(const QImage& image) const
{
    if (window_ == nullptr || image.isNull()) {
        return nullptr;
    }
    return window_->createTextureFromImage(image);
}

void PreviewTextureRepository::noteSpriteBatchStats(const char* layerName, qint64 spriteCount, qint64 batchCount)
{
    stats_.spriteCount += spriteCount;
    stats_.spriteBatchCount += batchCount;
    if (layerName != nullptr && *layerName != '\0') {
        PreviewTextureLayerStats& layerStat = ensureLayerStats(&stats_.layerStats, layerName);
        layerStat.spriteCount += spriteCount;
        layerStat.spriteBatchCount += batchCount;
    }
}

void PreviewTextureRepository::setStageBackgroundFrameProfile(const PreviewStageBackgroundFrameProfile& profile)
{
    stats_.stageBackground = profile;
}

PreviewTextureStats PreviewTextureRepository::stats() const
{
    return stats_;
}

void PreviewTextureRepository::clear()
{
    qDeleteAll(cachedTextures_);
    cachedTextures_.clear();
    qDeleteAll(transientTextures_);
    transientTextures_.clear();
    for (auto it = retainedTextures_.begin(); it != retainedTextures_.end(); ++it) {
        delete it->texture;
    }
    retainedTextures_.clear();
    stats_ = PreviewTextureStats();
}
