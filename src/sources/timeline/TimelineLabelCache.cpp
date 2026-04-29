#include "sources/timeline/TimelineLabelCache.h"

#include <QPainter>

namespace miacode::sources::timeline {

namespace {

QByteArray makeKey(const QString& text, const QFont& font, const QColor& color)
{
    QByteArray key;
    key.reserve(text.size() * 2 + 64);
    key.append(text.toUtf8());
    key.append('|');
    key.append(font.toString().toUtf8());
    key.append('|');
    // Pack RGBA into 4 bytes; each as a single hex byte to keep the
    // key compact yet collision-free.
    const QByteArray rgba = QByteArray::number(
        (static_cast<quint32>(color.alpha()) << 24)
            | (static_cast<quint32>(color.red()) << 16)
            | (static_cast<quint32>(color.green()) << 8)
            | static_cast<quint32>(color.blue()),
        16);
    key.append(rgba);
    return key;
}

}  // namespace

QSharedPointer<QImage> TimelineLabelCache::lookupOrRasterise(
    const QString& text,
    const QFont& font,
    const QColor& color,
    const QSizeF& logicalSize,
    qreal dpr)
{
    ++frameCounter_;
    if (text.isEmpty() || logicalSize.width() <= 0.0 || logicalSize.height() <= 0.0) {
        return {};
    }

    const QByteArray key = makeKey(text, font, color);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->lastUsedFrame = frameCounter_;
        return it->image;
    }

    // Cache miss — rasterise. QImage::Format_RGBA8888_Premultiplied
    // matches the swap-chain alpha mode (DXGI_ALPHA_MODE_PREMULTIPLIED)
    // and the texture cache's normalised format, so the upload path
    // is zero-conversion.
    const qreal effectiveDpr = dpr > 0.0 ? dpr : 1.0;
    const QSize pixelSize(
        qMax(1, qRound(logicalSize.width() * effectiveDpr)),
        qMax(1, qRound(logicalSize.height() * effectiveDpr)));
    auto fresh = QSharedPointer<QImage>::create(
        pixelSize, QImage::Format_RGBA8888_Premultiplied);
    fresh->setDevicePixelRatio(effectiveDpr);
    fresh->fill(Qt::transparent);
    {
        QPainter p(fresh.data());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        QFont scaledFont = font;
        // QPainter on an RGBA image with setDevicePixelRatio honours
        // the DPR for text metrics, so we draw in logical coordinates.
        p.setFont(scaledFont);
        p.setPen(color);
        // Draw at (0,0) of the logical canvas. The sprite descriptor
        // emitted by the source positions the QImage at the label's
        // topLeft in scene coordinates.
        const QRectF logicalRect(0.0, 0.0, logicalSize.width(), logicalSize.height());
        p.drawText(logicalRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        p.end();
    }

    Entry entry;
    entry.image = fresh;
    entry.lastUsedFrame = frameCounter_;
    entries_.insert(key, entry);
    insertionOrder_.append(key);
    pruneIfFull();
    return fresh;
}

void TimelineLabelCache::pruneIfFull()
{
    // FIFO single-eviction. Same pattern PreviewDCompTextureCache
    // adopted after HANDOVER §7.4 — drop the oldest entry only when
    // we exceed the cap, never clear-all-on-full (which would UAF
    // any QSharedPointer the current frame's snapshot still holds).
    while (entries_.size() > kCacheCap && !insertionOrder_.isEmpty()) {
        const QByteArray oldest = insertionOrder_.takeFirst();
        entries_.remove(oldest);
    }
}

void TimelineLabelCache::clear()
{
    entries_.clear();
    insertionOrder_.clear();
}

}  // namespace miacode::sources::timeline
