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
    QSharedPointer<QImage> lookupOrTransform(
        const QString& type,
        qreal scale,
        qreal rotationDegrees,
        bool mirrorX,
        int fallbackPixelSize = 14);

    void clear();
    int size() const { return cache_.size(); }

private:
    void ensureLoaded();

    miacode::timeline::TimelineNoteAssetSet assets_;
    bool assetsLoaded_ = false;

    QHash<QString, QSharedPointer<QImage>> cache_;
};

}  // namespace miacode::sources::timeline
