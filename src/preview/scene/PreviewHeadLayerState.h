#pragma once

#include <QColor>
#include <QHash>
#include <QSharedPointer>

#include "preview/scene/PreviewActiveMarkerView.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

namespace miacode::preview::scene {

struct PreviewHeadRenderAssetCacheKey {
    quint64 baseKey = 0;
    quint64 overlayKey = 0;
    QRgb tintRgba = 0;

    bool operator==(const PreviewHeadRenderAssetCacheKey& other) const
    {
        return baseKey == other.baseKey
            && overlayKey == other.overlayKey
            && tintRgba == other.tintRgba;
    }
};

size_t qHash(const PreviewHeadRenderAssetCacheKey& key, size_t seed = 0) noexcept;

class PreviewHeadRenderAssetCache
{
public:
    void synchronizeSkinAssets(const PreviewSkinAssets& skin);
    const QImage* composedImage(
        const QImage& baseImage,
        const QImage& overlayImage,
        const QColor& tint,
        qreal baseMix,
        qreal overlayAlphaMix);
    void clear();

    qint64 hitCount() const { return hitCount_; }
    qint64 missCount() const { return missCount_; }
    qsizetype entryCount() const { return entries_.size(); }

private:
    QHash<PreviewHeadRenderAssetCacheKey, QSharedPointer<QImage>> entries_;
    QVector<quint64> skinAssetKeys_;
    qint64 hitCount_ = 0;
    qint64 missCount_ = 0;
};

struct PreviewHeadLayerState {
    PreviewSpriteDescriptors sprites;
    QVector<QSharedPointer<QImage>> ownedImages;
};

PreviewHeadLayerState buildPreviewHeadLayerState(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect,
    PreviewHeadRenderAssetCache* renderAssetCache = nullptr
);

}  // namespace miacode::preview::scene
