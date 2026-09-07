#include "timeline/quick/TimelineQuickTextureCache.h"

#include <QDir>
#include <QQuickWindow>
#include <QSGTexture>
#include <QStringList>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCachePolicy.h"

namespace {

// Capacity caps. These are a SAFETY NET against unbounded growth, not a working-set
// budget — the distinction decides the numbers, so it is worth stating.
//
// The first cut used 1024 on the assumption that a fat cache costs frame time. The
// field capture disproved that: over a session where the cache grew from ~18 to ~360
// entries (20x), mean updatePaintNode went DOWN, 0.334 ms -> 0.033 ms, because a
// warm cache is all hits. 995 samples, 994 of them under 3.4 ms; the single 65 ms
// outlier was the first-ever paint (cold start), not a large cache. At ~3.5 KB per
// entry the whole 360-entry cache was ~1.3 MB.
//
// So growth costs residency and nothing else, while a flush costs a full node-tree
// teardown plus a re-upload of the working set — a visible hitch. A cap tight enough
// to fire in normal use would therefore trade ~1 MB for a periodic stutter, which is
// a losing trade in an app whose open complaint is stuttering. These limits are set
// so they never fire in normal use and only catch a genuine runaway: 8192 entries is
// >20x the measured 10-switch working set, i.e. hundreds of chart switches away.
//
// The byte cap covers the shape the entry cap cannot see — few entries, each huge
// (very high DPR, oversized hold bodies).
constexpr qsizetype kTimelineCachedTextureEntryLimit = 8192;
constexpr qint64 kTimelineCachedTextureByteLimit = 64LL * 1024 * 1024;
constexpr qsizetype kTimelineTransformedPixmapEntryLimit = 8192;

QString normalizedSkinDirectory(const QString& skinDirectory)
{
    const QString trimmed = skinDirectory.trimmed();
    return trimmed.isEmpty() ? QString() : QDir::cleanPath(trimmed);
}

}  // namespace

TimelineQuickTextureCache::~TimelineQuickTextureCache()
{
    clear();
}

void TimelineQuickTextureCache::setWindow(QQuickWindow* window)
{
    if (window_ == window) {
        return;
    }
    clear();
    window_ = window;
    noteAssets_ = miacode::timeline::loadTimelineNoteAssets(skinDirectory_);
}

void TimelineQuickTextureCache::clear()
{
    qDeleteAll(textures_);
    textures_.clear();
    textureBytesByKey_.clear();
    textureBytes_ = 0;
    transformedPixmaps_.clear();
    holdPixmapParts_.clear();
}

void TimelineQuickTextureCache::invalidateAll()
{
    clear();
}

void TimelineQuickTextureCache::invalidateThemeDependent()
{
    removeTextureKeysMatching([](const QString& key) {
        return !key.startsWith(QStringLiteral("note|")) && !key.startsWith(QStringLiteral("hold_"));
    });
}

void TimelineQuickTextureCache::invalidateDprDependent()
{
    clear();
}

bool TimelineQuickTextureCache::requiresReset(QQuickWindow* window, const QString& skinDirectory) const
{
    return window_ != window || skinDirectory_ != normalizedSkinDirectory(skinDirectory);
}

TimelineQuickTextureCacheCapacity TimelineQuickTextureCache::capacitySnapshot() const
{
    return TimelineQuickTextureCacheCapacity{
        textures_.size(),
        textureBytes_,
        transformedPixmaps_.size(),
        kTimelineCachedTextureEntryLimit,
        kTimelineCachedTextureByteLimit,
        kTimelineTransformedPixmapEntryLimit,
    };
}

bool TimelineQuickTextureCache::capacityFlushRequired() const
{
    // Routed through the same snapshot the diagnostics read, so the logged limits
    // are by construction the ones the predicate actually used.
    const TimelineQuickTextureCacheCapacity capacity = capacitySnapshot();
    return miacode::timeline::quick::timelineTextureCacheFlushRequired(
        capacity.textureCount,
        capacity.textureBytes,
        capacity.pixmapCount,
        capacity.textureCountLimit,
        capacity.textureByteLimit,
        capacity.pixmapCountLimit);
}

void TimelineQuickTextureCache::setSkinDirectory(const QString& skinDirectory)
{
    const QString normalized = normalizedSkinDirectory(skinDirectory);
    if (skinDirectory_ == normalized) {
        return;
    }
    clear();
    skinDirectory_ = normalized;
    noteAssets_ = miacode::timeline::loadTimelineNoteAssets(skinDirectory_);
}

QSGTexture* TimelineQuickTextureCache::textureForKey(const QString& key, const QImage& image)
{
    if (window_ == nullptr || image.isNull() || key.isEmpty()) {
        return nullptr;
    }
    auto it = textures_.find(key);
    if (it != textures_.end()) {
        return it.value();
    }
    QSGTexture* texture = window_->createTextureFromImage(image);
    ++textureCreateCount_;
    textures_.insert(key, texture);
    const qint64 uploadBytes = qMax<qint64>(1, image.sizeInBytes());
    textureBytesByKey_.insert(key, uploadBytes);
    textureBytes_ += uploadBytes;
    return texture;
}

void TimelineQuickTextureCache::debugCacheStats(
    int* textureCount,
    int* pixmapCount,
    int* holdPartsCount,
    int* rotatedNoteKeyCount,
    quint64* createTotal) const
{
    if (textureCount != nullptr) {
        *textureCount = textures_.size();
    }
    if (pixmapCount != nullptr) {
        *pixmapCount = transformedPixmaps_.size();
    }
    if (holdPartsCount != nullptr) {
        *holdPartsCount = holdPixmapParts_.size();
    }
    if (createTotal != nullptr) {
        *createTotal = textureCreateCount_;
    }
    if (rotatedNoteKeyCount != nullptr) {
        // Note keys are "note|type|w|h|rotTenths|mirror|dpr=N" (see noteTextureKey +
        // transformedPixmapCacheKey). Field index 4 is the rotation tenths; non-"0" means a
        // slide-track arrow angle. Counting these isolates the slide-edit-driven key axis.
        int rotated = 0;
        for (auto it = textures_.cbegin(); it != textures_.cend(); ++it) {
            const QString& key = it.key();
            if (!key.startsWith(QStringLiteral("note|"))) {
                continue;
            }
            const QStringList parts = key.split(QLatin1Char('|'));
            if (parts.size() >= 6 && parts.at(4) != QStringLiteral("0")) {
                ++rotated;
            }
        }
        *rotatedNoteKeyCount = rotated;
    }
}

QSGTexture* TimelineQuickTextureCache::textureForPixmapKey(const QString& key, const QPixmap& pixmap)
{
    return pixmap.isNull() ? nullptr : textureForKey(key, pixmap.toImage());
}

QString TimelineQuickTextureCache::noteTextureKey(
    const QString& spriteType,
    const QSize& targetSize,
    qreal rotationDegrees,
    bool mirrorX) const
{
    return QStringLiteral("note|%1|dpr=%2")
        .arg(miacode::timeline::transformedPixmapCacheKey(spriteType, targetSize, rotationDegrees, mirrorX))
        .arg(qRound(effectiveDevicePixelRatio() * 100.0));
}

qreal TimelineQuickTextureCache::effectiveDevicePixelRatio() const
{
    return window_ != nullptr ? qMax<qreal>(1.0, window_->effectiveDevicePixelRatio()) : 1.0;
}

QString TimelineQuickTextureCache::holdPartsCacheKey(const QString& spriteType, qreal scale) const
{
    return QStringLiteral("%1|dpr=%2")
        .arg(miacode::timeline::holdPixmapCacheKey(spriteType, scale))
        .arg(qRound(effectiveDevicePixelRatio() * 100.0));
}

void TimelineQuickTextureCache::removeTextureKeysMatching(const std::function<bool(const QString&)>& predicate)
{
    if (!predicate) {
        return;
    }
    QList<QString> toRemove;
    toRemove.reserve(textures_.size());
    for (auto it = textures_.cbegin(); it != textures_.cend(); ++it) {
        if (predicate(it.key())) {
            toRemove.append(it.key());
        }
    }
    for (const QString& key : toRemove) {
        delete textures_.take(key);
        textureBytes_ -= textureBytesByKey_.take(key);
    }
    // Selective removal can only ever shrink the total; clamp so a missing bookkeeping
    // entry (a texture inserted before this tracking existed, or a future partial-clear
    // path that forgets to update the map) can never drive the counter negative and
    // suppress the capacity flush.
    textureBytes_ = qMax<qint64>(0, textureBytes_);
}

QPixmap TimelineQuickTextureCache::transformedNotePixmap(
    const QString& spriteType,
    const QSize& targetSize,
    qreal rotationDegrees,
    bool mirrorX)
{
    const QString key = noteTextureKey(spriteType, targetSize, rotationDegrees, mirrorX);
    auto it = transformedPixmaps_.find(key);
    if (it != transformedPixmaps_.end()) {
        return it.value();
    }

    if (!targetSize.isValid()) {
        return QPixmap();
    }
    const qreal dpr = effectiveDevicePixelRatio();
    const QSize pixelSize(
        qMax(1, qRound(static_cast<qreal>(targetSize.width()) * dpr)),
        qMax(1, qRound(static_cast<qreal>(targetSize.height()) * dpr)));
    QPixmap transformed = miacode::timeline::transformNotePixmapToTargetSize(
        noteAssets_,
        spriteType,
        pixelSize,
        rotationDegrees,
        mirrorX);
    if (!transformed.isNull()) {
        transformed.setDevicePixelRatio(dpr);
    }
    transformedPixmaps_.insert(key, transformed);
    return transformed;
}

QSGTexture* TimelineQuickTextureCache::noteTexture(
    const QString& spriteType,
    const QSize& targetSize,
    qreal rotationDegrees,
    bool mirrorX)
{
    const QPixmap pixmap = transformedNotePixmap(spriteType, targetSize, rotationDegrees, mirrorX);
    if (pixmap.isNull()) {
        return nullptr;
    }
    return textureForKey(noteTextureKey(spriteType, targetSize, rotationDegrees, mirrorX), pixmap.toImage());
}

QSize TimelineQuickTextureCache::noteTargetSize(const QString& spriteType, qreal scale) const
{
    return miacode::timeline::targetSizeForNoteType(noteAssets_, spriteType, scale);
}

qreal TimelineQuickTextureCache::holdScaleForBaseIconScale(const QString& spriteType, qreal baseIconScale) const
{
    return miacode::timeline::holdScaleForBaseIconScale(noteAssets_, spriteType, baseIconScale);
}

TimelineQuickHoldTextureParts TimelineQuickTextureCache::holdTextureParts(
    const QString& spriteType,
    qreal scale)
{
    const QString key = holdPartsCacheKey(spriteType, scale);
    auto it = holdPixmapParts_.constFind(key);
    if (it != holdPixmapParts_.constEnd()) {
        return TimelineQuickHoldTextureParts{
            TimelineQuickTextureHandle{
                textureForPixmapKey(QStringLiteral("hold_left_cap|%1").arg(key), it->parts.leftCap),
                it->leftCapLogicalSize,
            },
            TimelineQuickTextureHandle{
                textureForPixmapKey(QStringLiteral("hold_right_cap|%1").arg(key), it->parts.rightCap),
                it->rightCapLogicalSize,
            },
            TimelineQuickTextureHandle{
                textureForKey(QStringLiteral("hold_body|%1").arg(key), it->parts.bodySlice),
                it->bodySliceLogicalSize,
            },
        };
    }

    const qreal dpr = effectiveDevicePixelRatio();
    const QSize logicalTargetSize = noteTargetSize(spriteType, scale);
    const QSize pixelTargetSize(
        qMax(1, qRound(static_cast<qreal>(logicalTargetSize.width()) * dpr)),
        qMax(1, qRound(static_cast<qreal>(logicalTargetSize.height()) * dpr)));
    HoldPixmapPartsCacheEntry entry;
    entry.parts = miacode::timeline::buildHoldPixmapPartsForTargetSize(noteAssets_, spriteType, pixelTargetSize);
    if (!entry.parts.leftCap.isNull()) {
        entry.leftCapLogicalSize = QSizeF(
            static_cast<qreal>(entry.parts.leftCap.width()) / dpr,
            static_cast<qreal>(entry.parts.leftCap.height()) / dpr);
    }
    if (!entry.parts.rightCap.isNull()) {
        entry.rightCapLogicalSize = QSizeF(
            static_cast<qreal>(entry.parts.rightCap.width()) / dpr,
            static_cast<qreal>(entry.parts.rightCap.height()) / dpr);
    }
    if (!entry.parts.bodySlice.isNull()) {
        entry.bodySliceLogicalSize = QSizeF(
            static_cast<qreal>(entry.parts.bodySlice.width()) / dpr,
            static_cast<qreal>(entry.parts.bodySlice.height()) / dpr);
    }
    holdPixmapParts_.insert(key, entry);
    return TimelineQuickHoldTextureParts{
        TimelineQuickTextureHandle{
            textureForPixmapKey(QStringLiteral("hold_left_cap|%1").arg(key), entry.parts.leftCap),
            entry.leftCapLogicalSize,
        },
        TimelineQuickTextureHandle{
            textureForPixmapKey(QStringLiteral("hold_right_cap|%1").arg(key), entry.parts.rightCap),
            entry.rightCapLogicalSize,
        },
        TimelineQuickTextureHandle{
            textureForKey(QStringLiteral("hold_body|%1").arg(key), entry.parts.bodySlice),
            entry.bodySliceLogicalSize,
        },
    };
}
