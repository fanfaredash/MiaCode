#pragma once

// Phase 3d-2 — CPU-rasterise text labels for the DComp timeline path.
// The chart-preview side has no font atlas, so we render each label
// to a QImage via QPainter and emit it as a sprite. The texture cache
// then deduplicates GPU uploads via QImage::cacheKey on the render
// thread.
//
// Key shape: text + font.toString() + RGBA bytes. Labels with the
// same triplet share a QImage. Typical timeline has O(50) unique
// labels (ruler ticks + lane names), so cache capacity is bounded
// by content; we add a soft cap with LRU eviction to keep memory
// in check during long sessions where the text-string pool grows
// (e.g. user scrolls the timeline far past its initial extent).

#include <QFont>
#include <QHash>
#include <QImage>
#include <QSharedPointer>
#include <QString>

namespace miacode::sources::timeline {

class TimelineLabelCache
{
public:
    TimelineLabelCache() = default;

    // Lookup or rasterise. Returns a QSharedPointer that the snapshot
    // appends to retainedImages so the render thread holds a reference
    // until the frame is done with it.
    //
    // dpr scales the rasterisation pixel size — labels rasterise at
    // (logicalSize × dpr) so HiDPI displays draw crisp text.
    QSharedPointer<QImage> lookupOrRasterise(
        const QString& text,
        const QFont& font,
        const QColor& color,
        const QSizeF& logicalSize,
        qreal dpr);

    // FIFO single-eviction at the cap, mirroring the texture-cache
    // pattern (HANDOVER §7.4). Prevents catastrophic-flush UAF when
    // the cap is reached during a frame whose snapshot still holds
    // shared pointers from this cache.
    void pruneIfFull();

    // Drop everything (e.g. on theme change so font hinting refreshes).
    void clear();

    int size() const { return entries_.size(); }

private:
    static constexpr int kCacheCap = 512;

    struct Entry {
        QSharedPointer<QImage> image;
        qint64 lastUsedFrame = 0;
    };
    QHash<QByteArray, Entry> entries_;
    QList<QByteArray> insertionOrder_;
    qint64 frameCounter_ = 0;
};

}  // namespace miacode::sources::timeline
