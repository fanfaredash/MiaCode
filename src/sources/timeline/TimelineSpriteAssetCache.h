#pragma once

// Phase 3d-3 — sprite asset cache for the DComp timeline path.
// TimelineSceneSprite carries a `spriteType` string ("tap_break",
// "slide_arrow_break", etc.) plus scale, rotation, and mirror flags.
// The QSG path resolves these via TimelineNoteAssets::transformNotePixmap
// which returns a QPixmap; for DComp we need a QImage so the texture
// cache can build an SRV from QImage::cacheKey.
//
// This cache wraps the QSG asset registry: lazy-loads
// loadTimelineNoteAssets() on first lookup, runs transformNotePixmap
// to produce the rotated/scaled/mirrored variant, converts to QImage,
// and stores keyed on the QSG path's existing cache key. Two timeline
// sources sharing a cache (the chart compositor never invokes timeline
// sources, so this is single-threaded GUI access throughout).

#include "timeline/TimelineNoteAssets.h"

#include <QHash>
#include <QImage>
#include <QSharedPointer>
#include <QString>

namespace miacode::sources::timeline {

class TimelineSpriteAssetCache
{
public:
    TimelineSpriteAssetCache() = default;

    // Lookup or transform. Returns the cached QSharedPointer<QImage>
    // — caller appends to snapshot.retainedImages so the render
    // thread holds a reference for the frame's lifetime.
    //
    // Phase 4d-fix6 — `dpr` is the screen device pixel ratio. The
    // cache rasterises at `scale * dpr` physical pixels and sets the
    // image's devicePixelRatio so callers get a pixmap whose
    // `pixmap.width() / pixmap.devicePixelRatio()` returns the
    // logical (DPR-independent) target size. Without this, on HiDPI
    // displays sprites are upscaled by the GPU sampler (blur) since
    // the source texture is smaller than the destination physical
    // pixel area. Mirrors TimelineQuickTextureCache.cpp:142-152.
    QSharedPointer<QImage> lookupOrTransform(
        const QString& type,
        qreal scale,
        qreal rotationDegrees,
        bool mirrorX,
        qreal dpr = 1.0,
        int fallbackPixelSize = 14);

    // Phase 9b-textured — hold-span 3-piece (left cap + body slice +
    // right cap) cache. Mirrors the QSG path's
    // TimelineQuickTextureCache::holdTextureParts. Returns shared
    // QImage pointers for each piece plus the original cap logical
    // size; the caller composes 3 PreviewSpriteDescriptors per hold
    // span and pins the QImages via snapshot.retainedImages.
    struct HoldParts {
        QSharedPointer<QImage> leftCap;
        QSharedPointer<QImage> rightCap;
        QSharedPointer<QImage> bodySlice;
        // Logical (CSS-pixel) cap width/height that the QSG path
        // would render at; the renderer stretches the QImage to
        // cover this size regardless of the QImage's pixel
        // dimensions (which are scaled up by the cap pixmap's DPR).
        int capLogicalWidth = 0;
        int capLogicalHeight = 0;
        bool isValid() const {
            return !leftCap.isNull() && !rightCap.isNull()
                && !bodySlice.isNull() && capLogicalWidth > 0
                && capLogicalHeight > 0;
        }
    };

    // Compute the asset-set-derived hold-scale for a base icon scale,
    // then build (or fetch from cache) the three QImages. `dpr` —
    // see lookupOrTransform; same DPR-aware rasterisation pattern.
    HoldParts lookupOrBuildHoldParts(
        const QString& type,
        qreal baseIconScale,
        qreal dpr = 1.0,
        int fallbackPixelSize = 14);

    void clear();
    int size() const { return cache_.size() + holdCache_.size(); }

private:
    void ensureLoaded();

    miacode::timeline::TimelineNoteAssetSet assets_;
    bool assetsLoaded_ = false;

    QHash<QString, QSharedPointer<QImage>> cache_;
    QHash<QString, HoldParts> holdCache_;
};

}  // namespace miacode::sources::timeline
