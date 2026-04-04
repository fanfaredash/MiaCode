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
    return createOwnedTexture(image);
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
}
