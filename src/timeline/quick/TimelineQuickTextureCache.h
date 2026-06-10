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

class TimelineQuickTextureCache
{
public:
    ~TimelineQuickTextureCache();

    void setWindow(QQuickWindow* window);
    void clear();
    void invalidateAll();
    void invalidateThemeDependent();
    void invalidateDprDependent();
    QSGTexture* textureForKey(const QString& key, const QImage& image);
    QSGTexture* textureForPixmapKey(const QString& key, const QPixmap& pixmap);
    TimelineQuickTextureHandle textTexture(const miacode::timeline::TimelineSceneTextLabel& label);
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
    QHash<QString, QSGTexture*> textures_;
    QHash<QString, QPixmap> transformedPixmaps_;
    QHash<QString, HoldPixmapPartsCacheEntry> holdPixmapParts_;
    // beta7 leak gauge — cumulative count of QSGTextures actually created (createTextureFromImage).
    // Monotonic by design; a flat slope once a chart saturates is the healthy signature.
    quint64 textureCreateCount_ = 0;
};
