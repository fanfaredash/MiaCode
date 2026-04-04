#pragma once

#include <QHash>
#include <QImage>

class QQuickWindow;
class QSGTexture;

class PreviewTextureRepository
{
public:
    ~PreviewTextureRepository();

    void setWindow(QQuickWindow* window);
    QSGTexture* textureForImage(const QImage& image, bool cacheable = true);
    QSGTexture* createOwnedTexture(const QImage& image) const;
    void clear();

private:
    QQuickWindow* window_ = nullptr;
    QHash<quint64, QSGTexture*> cachedTextures_;
};
