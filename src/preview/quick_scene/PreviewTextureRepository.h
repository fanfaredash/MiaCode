#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

class QQuickWindow;
class QSGTexture;

struct PreviewTextureLayerStats {
    QString name;
    qint64 spriteCount = 0;
    qint64 spriteBatchCount = 0;
    qint64 spriteRunCount = 0;
    qint64 spriteGeometryReallocCount = 0;
    qint64 spriteVerticesRequired = 0;
    qint64 spriteVerticesCapacity = 0;
    qint64 spriteScratchReserveGrowCount = 0;
    qint64 candidateCount = 0;
    qint64 activeCount = 0;
    double buildMs = 0.0;
};

struct PreviewSpriteBatchFrameProfile {
    qint64 spriteCount = 0;
    qint64 batchCount = 0;
    qint64 runCount = 0;
    qint64 geometryReallocCount = 0;
    qint64 verticesRequired = 0;
    qint64 verticesCapacity = 0;
    qint64 scratchReserveGrowCount = 0;
};

struct PreviewStageBackgroundFrameProfile {
    double mediaToImageMs = 0.0;
    double mediaTextureMs = 0.0;
    double dimUniformUpdateMs = 0.0;
    double nodeUpdateMs = 0.0;
    qint64 mediaFrameCount = 0;
    qint64 dimFrameCount = 0;
    qint64 videoFrameCount = 0;
    qint64 staticImageFrameCount = 0;
    qint64 dimUniformUpdateCount = 0;
};

struct PreviewTextureStats {
    qint64 cachedHitCount = 0;
    qint64 cachedCreateCount = 0;
    qint64 transientHitCount = 0;
    qint64 transientCreateCount = 0;
    qint64 spriteCount = 0;
    qint64 spriteBatchCount = 0;
    QVector<PreviewTextureLayerStats> layerStats;
    PreviewStageBackgroundFrameProfile stageBackground;
};

struct PreviewRetainedTextureEntry {
    quint64 key = 0;
    QSGTexture* texture = nullptr;
};

class PreviewTextureRepository
{
public:
    ~PreviewTextureRepository();

    void setWindow(QQuickWindow* window);
    void beginFrame();
    QSGTexture* textureForImage(const QImage& image, bool cacheable = true);
    QSGTexture* retainedTexture(const QString& slotName) const;
    QSGTexture* retainedTextureForImage(const QString& slotName, const QImage& image);
    void releaseRetainedTexture(const QString& slotName);
    QSGTexture* createOwnedTexture(const QImage& image) const;
    void noteSpriteBatchStats(const char* layerName, const PreviewSpriteBatchFrameProfile& profile);
    void setStageBackgroundFrameProfile(const PreviewStageBackgroundFrameProfile& profile);
    PreviewTextureStats stats() const;
    void clear();

private:
    void clearCachedTextures();

    QQuickWindow* window_ = nullptr;
    // L1 cache: keyed by image.cacheKey() (fast path for re-using the same QImage instance).
    // Maps the fast cache key to the de-duplicated content-fingerprint key in cachedTextures_.
    QHash<quint64, quint64> cachedKeyToFingerprint_;
    // L2 cache: keyed by content fingerprint (collapses visually-identical QImage instances
    // that originate from different sources but represent the same texture).
    QHash<quint64, QSGTexture*> cachedTextures_;
    QHash<quint64, qint64> cachedTextureBytesByKey_;
    QHash<quint64, QSGTexture*> transientTextures_;
    QHash<QString, PreviewRetainedTextureEntry> retainedTextures_;
    PreviewTextureStats stats_;
    qint64 cachedTextureBytes_ = 0;
    bool cachedTextureFlushPending_ = false;
};
