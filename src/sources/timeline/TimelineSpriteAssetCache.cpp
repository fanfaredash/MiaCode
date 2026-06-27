#include "sources/timeline/TimelineSpriteAssetCache.h"

#include <QDir>
#include <QPixmap>

namespace miacode::sources::timeline {

void TimelineSpriteAssetCache::ensureLoaded()
{
    if (assetsLoaded_) return;
    assets_ = miacode::timeline::loadTimelineNoteAssets(skinDirectory_);
    assetsLoaded_ = true;
}

QSharedPointer<QImage> TimelineSpriteAssetCache::lookupOrTransform(
    const QString& type,
    qreal scale,
    qreal rotationDegrees,
    bool mirrorX,
    qreal dpr,
    int fallbackPixelSize)
{
    ensureLoaded();
    if (type.isEmpty()) {
        return {};
    }
    const qreal effectiveDpr = dpr > 0.0 ? dpr : 1.0;
    // Phase 4d-fix6 — bake DPR into the rasterisation scale so the
    // pixmap's pixel dimensions match the screen's physical pixel
    // budget. The cache key incorporates `effectiveScale` so the
    // same chart at DPR=1.0 vs DPR=1.5 gets two distinct entries
    // (correctly invalidated on DPR change).
    const qreal effectiveScale = scale * effectiveDpr;
    const QString key = miacode::timeline::transformedPixmapCacheKey(
        type, effectiveScale, rotationDegrees, mirrorX);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return *it;
    }
    const QPixmap pixmap = miacode::timeline::transformNotePixmap(
        assets_, type, effectiveScale, rotationDegrees, mirrorX, fallbackPixelSize);
    if (pixmap.isNull()) {
        return {};
    }
    auto image = QSharedPointer<QImage>::create(pixmap.toImage().convertToFormat(
        QImage::Format_RGBA8888_Premultiplied));
    // Tag the image with DPR so `image->width() / image->devicePixelRatio()`
    // returns the logical (DPR-independent) target size — the sprite
    // emit code uses that to compute its draw width/height.
    image->setDevicePixelRatio(effectiveDpr);
    cache_.insert(key, image);
    return image;
}

TimelineSpriteAssetCache::HoldParts
TimelineSpriteAssetCache::lookupOrBuildHoldParts(
    const QString& type,
    qreal baseIconScale,
    qreal dpr,
    int fallbackPixelSize)
{
    ensureLoaded();
    HoldParts result;
    if (type.isEmpty()) {
        return result;
    }
    const qreal effectiveDpr = dpr > 0.0 ? dpr : 1.0;
    // Phase 4d-fix6 — bake DPR into the hold rasterisation scale,
    // matching lookupOrTransform's pattern. Without this, hold caps
    // are drawn upscaled at HiDPI → blur.
    const qreal holdScale = miacode::timeline::holdScaleForBaseIconScale(
        assets_, type, baseIconScale * effectiveDpr, fallbackPixelSize);
    const QString key = miacode::timeline::holdPixmapCacheKey(type, holdScale);
    auto it = holdCache_.find(key);
    if (it != holdCache_.end()) {
        return *it;
    }
    const miacode::timeline::TimelineHoldPixmapParts parts =
        miacode::timeline::buildHoldPixmapParts(
            assets_, type, holdScale, fallbackPixelSize);
    if (parts.leftCap.isNull() || parts.rightCap.isNull()
        || parts.bodySlice.isNull()) {
        return result;
    }
    // Logical cap size = physical_size / dpr. After we set DPR on the
    // QImage below, this matches what the QSG path computes.
    const int capLogicalWidth = qMax(
        1, qRound(parts.leftCap.width() / effectiveDpr));
    const int capLogicalHeight = qMax(
        1, qRound(parts.leftCap.height() / effectiveDpr));
    result.leftCap = QSharedPointer<QImage>::create(
        parts.leftCap.toImage().convertToFormat(
            QImage::Format_RGBA8888_Premultiplied));
    result.rightCap = QSharedPointer<QImage>::create(
        parts.rightCap.toImage().convertToFormat(
            QImage::Format_RGBA8888_Premultiplied));
    result.bodySlice = QSharedPointer<QImage>::create(
        parts.bodySlice.convertToFormat(
            QImage::Format_RGBA8888_Premultiplied));
    // Tag DPR on each piece so any downstream `width()/devicePixelRatio()`
    // calculation reports the logical (DPR-independent) size.
    result.leftCap->setDevicePixelRatio(effectiveDpr);
    result.rightCap->setDevicePixelRatio(effectiveDpr);
    result.bodySlice->setDevicePixelRatio(effectiveDpr);
    result.capLogicalWidth = capLogicalWidth;
    result.capLogicalHeight = capLogicalHeight;
    holdCache_.insert(key, result);
    return result;
}

void TimelineSpriteAssetCache::clear()
{
    cache_.clear();
    holdCache_.clear();
}

void TimelineSpriteAssetCache::setSkinDirectory(const QString& skinDirectory)
{
    const QString trimmed = skinDirectory.trimmed();
    const QString normalized = trimmed.isEmpty() ? QString() : QDir::cleanPath(trimmed);
    if (skinDirectory_ == normalized) {
        return;
    }
    skinDirectory_ = normalized;
    assets_ = miacode::timeline::TimelineNoteAssetSet();
    assetsLoaded_ = false;
    clear();
}

}  // namespace miacode::sources::timeline
