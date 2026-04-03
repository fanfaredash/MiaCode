#pragma once

#include <QHash>
#include <QElapsedTimer>
#include <QPointF>
#include <QOpenGLFunctions>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

class QImage;
class QVideoFrame;

class PreviewGLRenderer : protected QOpenGLFunctions
{
public:
    void initialize();
    void shutdown();
    void beginFrame(const QSize& viewportSize, qreal devicePixelRatio);
    void endFrame();
    void prewarmTexture(const QImage& image);
    quint64 frameCpuUploadNs() const;
    quint64 frameVideoMapNs() const;
    quint64 frameVideoUploadNs() const;
    bool drawImageQuadBatch(
        const QImage& image,
        const QVector<QPointF>& centers,
        qreal targetWidth,
        qreal targetHeight,
        const QVector<qreal>& angleDegrees,
        qreal opacity = 1.0,
        const QRectF& sourceRect = QRectF(),
        bool useCache = true
    );
    bool drawImageQuad(
        const QImage& image,
        const QRectF& targetRect,
        qreal angleDegrees = 0.0,
        qreal opacity = 1.0,
        const QRectF& sourceRect = QRectF(),
        bool useCache = true
    );
    bool drawVideoFrame(
        const QVideoFrame& frame,
        const QRectF& targetRect,
        qreal opacity = 1.0
    );

    bool isInitialized() const;
    QSize viewportSize() const;
    qreal devicePixelRatio() const;
    QString lastError() const;
    QString contextSummary() const;

private:
    enum class ShaderLanguage {
        DesktopLegacy,
        DesktopCore150,
        Gles100,
        Gles300,
    };

    enum class VideoTextureUploadMode {
        LegacyLuminance,
        RedGreen,
    };

    void resetGlObjects(bool deleteObjects);
    void configureForCurrentContext(class QOpenGLContext* context);
    void recordError(const QString& message, bool videoPath = false);
    bool ensureProgram();
    bool ensureVideoProgram();
    bool ensurePlanarVideoProgram();
    void restoreDefaultUnpackState();
    bool uploadVideoPlane(
        GLuint texture,
        int width,
        int height,
        GLenum externalFormat,
        const uchar* bits,
        int strideBytes,
        int mappedBytes,
        int bytesPerPixel,
        const QString& planeLabel,
        QString* errorMessage
    );
    GLuint ensureTexture(const QImage& image, bool useCache);
    bool ensureVideoTextures(const QSize& frameSize);
    bool ensurePlanarVideoTextures(const QSize& frameSize);

    bool initialized_ = false;
    QSize viewportSize_;
    qreal devicePixelRatio_ = 1.0;
    GLuint program_ = 0;
    GLuint vertexShader_ = 0;
    GLuint fragmentShader_ = 0;
    GLuint videoProgram_ = 0;
    GLuint videoVertexShader_ = 0;
    GLuint videoFragmentShader_ = 0;
    GLuint planarVideoProgram_ = 0;
    GLuint planarVideoVertexShader_ = 0;
    GLuint planarVideoFragmentShader_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint vertexArray_ = 0;
    GLint positionLocation_ = -1;
    GLint uvLocation_ = -1;
    GLint textureLocation_ = -1;
    GLint opacityLocation_ = -1;
    GLint videoPositionLocation_ = -1;
    GLint videoUvLocation_ = -1;
    GLint videoYTextureLocation_ = -1;
    GLint videoUvTextureLocation_ = -1;
    GLint videoOpacityLocation_ = -1;
    GLint planarVideoPositionLocation_ = -1;
    GLint planarVideoUvLocation_ = -1;
    GLint planarVideoYTextureLocation_ = -1;
    GLint planarVideoUTextureLocation_ = -1;
    GLint planarVideoVTextureLocation_ = -1;
    GLint planarVideoOpacityLocation_ = -1;
    GLuint videoYTexture_ = 0;
    GLuint videoUvTexture_ = 0;
    GLuint planarVideoYTexture_ = 0;
    GLuint planarVideoUTexture_ = 0;
    GLuint planarVideoVTexture_ = 0;
    QSize videoFrameSize_;
    QSize planarVideoFrameSize_;
    QHash<quint64, GLuint> textureCache_;
    class QOpenGLContext* boundContext_ = nullptr;
    ShaderLanguage shaderLanguage_ = ShaderLanguage::DesktopLegacy;
    VideoTextureUploadMode videoTextureUploadMode_ = VideoTextureUploadMode::LegacyLuminance;
    GLenum videoYInternalFormat_ = GL_LUMINANCE;
    GLenum videoYExternalFormat_ = GL_LUMINANCE;
    GLenum videoUvInternalFormat_ = GL_LUMINANCE_ALPHA;
    GLenum videoUvExternalFormat_ = GL_LUMINANCE_ALPHA;
    bool videoUvUseRgChannels_ = false;
    bool supportsUnpackRowLength_ = false;
    bool useDesktopLegacyVersion120_ = false;
    bool loggedFirstDirectVideoFrame_ = false;
    QString lastError_;
    QString contextSummary_;
    QString lastLoggedError_;
    QString lastLoggedVideoError_;
    quint64 frameCpuUploadNs_ = 0;
    quint64 frameVideoMapNs_ = 0;
    quint64 frameVideoUploadNs_ = 0;
    bool profilingFrameActive_ = false;
    bool vaoSupported_ = false;
};
