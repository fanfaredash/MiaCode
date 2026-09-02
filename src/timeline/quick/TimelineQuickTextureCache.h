#pragma once

#include <functional>

#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QSizeF>
#include <QString>

#include "timeline/TimelineNoteAssets.h"
#include "timeline/TimelineSceneState.h"

class QQuickWindow;
class QSGTexture;

struct TimelineQuickTextureHandle {
    QSGTexture* texture = nullptr;
    QSizeF logicalSize;

    bool isValid() const
    {
        return texture != nullptr && logicalSize.isValid();
    }
};

struct TimelineQuickHoldTextureParts {
    TimelineQuickTextureHandle leftCap;
    TimelineQuickTextureHandle rightCap;
    TimelineQuickTextureHandle bodySlice;

    bool isValid() const
    {
        return leftCap.isValid() && rightCap.isValid() && bodySlice.isValid();
    }
};

// Live counts paired with the limits they are tested against. Exists so a caller
// that needs to REPORT a capacity flush (TimelineQuickItem::updatePaintNode) can
// state what tripped it without restating the limits: they stay defined exactly
// once, as file-static constants in TimelineQuickTextureCache.cpp.
struct TimelineQuickTextureCacheCapacity {
    qsizetype textureCount = 0;
    qint64 textureBytes = 0;
    qsizetype pixmapCount = 0;
    qsizetype textureCountLimit = 0;
    qint64 textureByteLimit = 0;
    qsizetype pixmapCountLimit = 0;
};

class TimelineQuickTextureCache
{
public:
    ~TimelineQuickTextureCache();

    void setWindow(QQuickWindow* window);
    void clear();
    void invalidateAll();
    void invalidateThemeDependent();
    void invalidateDprDependent();
    bool requiresReset(QQuickWindow* window, const QString& skinDirectory) const;
    // True once the cache has grown past its capacity policy and must be flushed
    // whole. Before this existed, the only invalidations that ever fired at runtime
    // were window / skin / DPR / theme — and the theme one deliberately spares the
    // `note|` and `hold_` prefixes, i.e. it spared exactly the axis that grows, so
    // note + rotation keys accumulated for the life of the process across every chart
    // switch (measured: ~30 new rotation keys per switch, no plateau after 10).
    // Callers MUST tear the node tree down before acting on this — live materials
    // hold raw QSGTexture pointers. See TimelineQuickItem::updatePaintNode.
    bool capacityFlushRequired() const;
    // The inputs capacityFlushRequired() decided on, for diagnostics. Read it BEFORE
    // acting on the flush — invalidateAll() zeroes every count, so a snapshot taken
    // afterwards describes an empty cache instead of the one that tripped the cap.
    TimelineQuickTextureCacheCapacity capacitySnapshot() const;
    void setSkinDirectory(const QString& skinDirectory);
    QSGTexture* textureForKey(const QString& key, const QImage& image);
    QSGTexture* textureForPixmapKey(const QString& key, const QPixmap& pixmap);
    QSGTexture* noteTexture(
        const QString& spriteType,
        const QSize& targetSize,
        qreal rotationDegrees = 0.0,
        bool mirrorX = false);
    QSize noteTargetSize(const QString& spriteType, qreal scale) const;
    qreal holdScaleForBaseIconScale(const QString& spriteType, qreal baseIconScale) const;
    TimelineQuickHoldTextureParts holdTextureParts(const QString& spriteType, qreal scale);

    // beta7 leak gauge (probe 3.1) — cheap read of the three uncapped caches plus the count of
    // rotation-bearing note keys (slide-track arrows) and the cumulative texture-create total.
    // A flat tex/tex_create across edit→play→pause cycles confirms this cache saturates and is
    // NOT the leak; a monotonic climb reopens it. Called at most once per pause (render thread).
    void debugCacheStats(
        int* textureCount,
        int* pixmapCount,
        int* holdPartsCount,
        int* rotatedNoteKeyCount,
        quint64* createTotal) const;

private:
    struct HoldPixmapPartsCacheEntry {
        miacode::timeline::TimelineHoldPixmapParts parts;
        QSizeF leftCapLogicalSize;
        QSizeF rightCapLogicalSize;
        QSizeF bodySliceLogicalSize;
    };

    QString noteTextureKey(
        const QString& spriteType,
        const QSize& targetSize,
        qreal rotationDegrees,
        bool mirrorX) const;
    QString holdPartsCacheKey(const QString& spriteType, qreal scale) const;
    qreal effectiveDevicePixelRatio() const;
    void removeTextureKeysMatching(const std::function<bool(const QString&)>& predicate);
    QPixmap transformedNotePixmap(
        const QString& spriteType,
        const QSize& targetSize,
        qreal rotationDegrees,
        bool mirrorX);

    QQuickWindow* window_ = nullptr;
    miacode::timeline::TimelineNoteAssetSet noteAssets_;
    QString skinDirectory_;
    QHash<QString, QSGTexture*> textures_;
    // Per-key upload size, so selective removal (removeTextureKeysMatching, used by the
    // theme invalidation) can decrement the running total instead of letting it drift.
    QHash<QString, qint64> textureBytesByKey_;
    qint64 textureBytes_ = 0;
    QHash<QString, QPixmap> transformedPixmaps_;
    QHash<QString, HoldPixmapPartsCacheEntry> holdPixmapParts_;
    // beta7 leak gauge — cumulative count of QSGTextures actually created (createTextureFromImage).
    // Monotonic by design; a flat slope once a chart saturates is the healthy signature.
    quint64 textureCreateCount_ = 0;
};
