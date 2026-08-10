#pragma once

#include <QtGlobal>

namespace miacode::timeline::quick {

// Capacity policy for TimelineQuickTextureCache.
//
// Deliberately a SEPARATE predicate from
// preview/quick_scene/PreviewTextureGenerationPolicy.h rather than a shared one:
// the timeline surface and the QSG preview are independent renderers that must not
// include each other's headers, and their pressure is not the same shape. The
// preview caches a small set of large sprite atlases, so bytes bind first. The
// timeline caches many tiny textures whose key space explodes on COUNT — slide-track
// arrow rotation is derived from on-screen pixel deltas via qAtan2 and quantised to
// 0.1 degrees, so every chart contributes a fresh batch of keys. A measured 10-switch
// session grew the rotation key set by ~30 per switch with no plateau, at roughly
// 3.5 KB per entry — i.e. a byte cap alone would never have fired.
//
// The pixmap arm exists because transformedPixmaps_ is the CPU-side twin of the note
// textures and is NOT covered by the partial theme invalidation, so it can outgrow
// textures_ on its own.
constexpr bool timelineTextureCacheFlushRequired(
    qsizetype textureCount,
    qint64 textureBytes,
    qsizetype pixmapCount,
    qsizetype textureEntryLimit,
    qint64 textureByteLimit,
    qsizetype pixmapEntryLimit
)
{
    return textureCount > textureEntryLimit
        || textureBytes > textureByteLimit
        || pixmapCount > pixmapEntryLimit;
}

}  // namespace miacode::timeline::quick
