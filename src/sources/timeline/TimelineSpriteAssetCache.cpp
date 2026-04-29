#include "sources/timeline/TimelineSpriteAssetCache.h"

#include <QPixmap>

namespace miacode::sources::timeline {

void TimelineSpriteAssetCache::ensureLoaded()
{
    if (assetsLoaded_) return;
    assets_ = miacode::timeline::loadTimelineNoteAssets();
    assetsLoaded_ = true;
}

QSharedPointer<QImage> TimelineSpriteAssetCache::lookupOrTransform(
    const QString& type,
    qreal scale,
    qreal rotationDegrees,
    bool mirrorX,
    int fallbackPixelSize)
{
    ensureLoaded();
    if (type.isEmpty()) {
        return {};
    }
    const QString key = miacode::timeline::transformedPixmapCacheKey(
        type, scale, rotationDegrees, mirrorX);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return *it;
    }
    const QPixmap pixmap = miacode::timeline::transformNotePixmap(
        assets_, type, scale, rotationDegrees, mirrorX, fallbackPixelSize);
    if (pixmap.isNull()) {
        return {};
    }
    auto image = QSharedPointer<QImage>::create(pixmap.toImage().convertToFormat(
        QImage::Format_RGBA8888_Premultiplied));
    cache_.insert(key, image);
    return image;
}

void TimelineSpriteAssetCache::clear()
{
    cache_.clear();
}

}  // namespace miacode::sources::timeline
