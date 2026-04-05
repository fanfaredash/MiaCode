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

struct PreviewTextureStats {
    qint64 cachedHitCount = 0;
    qint64 cachedCreateCount = 0;
    qint64 transientHitCount = 0;
    qint64 transientCreateCount = 0;
    qint64 spriteCount = 0;
    qint64 spriteBatchCount = 0;
    QVector<PreviewTextureLayerStats> layerStats;
};

class PreviewTextureRepository
{
public:
    ~PreviewTextureRepository();

    void setWindow(QQuickWindow* window);
    void beginFrame();
    QSGTexture* textureForImage(const QImage& image, bool cacheable = true);
    QSGTexture* createOwnedTexture(const QImage& image) const;
    void noteSpriteBatchStats(const char* layerName, qint64 spriteCount, qint64 batchCount);
    PreviewTextureStats stats() const;
    void clear();

private:
    QQuickWindow* window_ = nullptr;
    QHash<quint64, QSGTexture*> cachedTextures_;
    QHash<quint64, QSGTexture*> transientTextures_;
    PreviewTextureStats stats_;
};
