#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QQuickWindow>
#include <QSGTexture>

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
}

QSGTexture* PreviewTextureRepository::textureForImage(const QImage& image, bool cacheable)
{
    if (window_ == nullptr || image.isNull()) {
        return nullptr;
    }

    const quint64 key = static_cast<quint64>(image.cacheKey());
    if (cacheable) {
        if (QSGTexture* existing = cachedTextures_.value(key, nullptr); existing != nullptr) {
            return existing;
        }
        QSGTexture* texture = window_->createTextureFromImage(image);
        cachedTextures_.insert(key, texture);
        return texture;
    }
    if (QSGTexture* existing = transientTextures_.value(key, nullptr); existing != nullptr) {
        return existing;
    }
    QSGTexture* texture = createOwnedTexture(image);
    transientTextures_.insert(key, texture);
    return texture;
}

QSGTexture* PreviewTextureRepository::createOwnedTexture(const QImage& image) const
{
    if (window_ == nullptr || image.isNull()) {
        return nullptr;
    }
    return window_->createTextureFromImage(image);
}

void PreviewTextureRepository::clear()
{
    qDeleteAll(cachedTextures_);
    cachedTextures_.clear();
    qDeleteAll(transientTextures_);
    transientTextures_.clear();
}
