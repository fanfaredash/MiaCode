#pragma once

#include <QHash>
#include <QOpenGLFunctions>
#include <QRectF>
#include <QSize>

class QImage;

class PreviewGLRenderer : protected QOpenGLFunctions
{
public:
    void initialize();
    void shutdown();
    void beginFrame(const QSize& viewportSize, qreal devicePixelRatio);
    bool drawImageQuad(
        const QImage& image,
        const QRectF& targetRect,
        qreal angleDegrees = 0.0,
        qreal opacity = 1.0,
        const QRectF& sourceRect = QRectF()
    );

    bool isInitialized() const;
    QSize viewportSize() const;
    qreal devicePixelRatio() const;

private:
    bool ensureProgram();
    GLuint ensureTexture(const QImage& image);

    bool initialized_ = false;
    QSize viewportSize_;
    qreal devicePixelRatio_ = 1.0;
    GLuint program_ = 0;
    GLuint vertexShader_ = 0;
    GLuint fragmentShader_ = 0;
    GLuint vertexBuffer_ = 0;
    GLint positionLocation_ = -1;
    GLint uvLocation_ = -1;
    GLint textureLocation_ = -1;
    GLint opacityLocation_ = -1;
    QHash<quint64, GLuint> textureCache_;
};
