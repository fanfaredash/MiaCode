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
    double buildMs = 0.0;
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
    void noteSpriteBatchStats(const char* layerName, qint64 spriteCount, qint64 batchCount);
    void setStageBackgroundFrameProfile(const PreviewStageBackgroundFrameProfile& profile);
    PreviewTextureStats stats() const;
    void clear();

private:
    QQuickWindow* window_ = nullptr;
    QHash<quint64, QSGTexture*> cachedTextures_;
    QHash<quint64, QSGTexture*> transientTextures_;
    QHash<QString, PreviewRetainedTextureEntry> retainedTextures_;
    PreviewTextureStats stats_;
};
