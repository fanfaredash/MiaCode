#include "PreviewGLRenderer.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QByteArray>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#include <QVideoFrameFormat>
#endif
#include <QtMath>
#include <QtGlobal>

#include <cstddef>
#include <cstring>
#include <limits>

namespace {
struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};

void appendPreviewGlLog(const QString& area, const QString& payload, bool warn = false)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview_gl/%1").arg(area),
        payload
    );
    if (warn && payload.contains(QStringLiteral("failed"), Qt::CaseInsensitive)) {
        miacode::debug_log::appendFatalMessage(QStringLiteral("preview_gl/%1").arg(area), payload);
    }
}

QString surfaceProfileName(QSurfaceFormat::OpenGLContextProfile profile)
{
    switch (profile) {
    case QSurfaceFormat::NoProfile:
        return QStringLiteral("NoProfile");
    case QSurfaceFormat::CoreProfile:
        return QStringLiteral("CoreProfile");
    case QSurfaceFormat::CompatibilityProfile:
        return QStringLiteral("CompatibilityProfile");
    }
    return QStringLiteral("UnknownProfile");
}

QString renderableTypeName(QSurfaceFormat::RenderableType renderableType)
{
    switch (renderableType) {
    case QSurfaceFormat::DefaultRenderableType:
        return QStringLiteral("Default");
    case QSurfaceFormat::OpenGL:
        return QStringLiteral("DesktopGL");
    case QSurfaceFormat::OpenGLES:
        return QStringLiteral("OpenGLES");
    case QSurfaceFormat::OpenVG:
        return QStringLiteral("OpenVG");
    }
    return QStringLiteral("UnknownRenderableType");
}

QString formatSummary(const QSurfaceFormat& format)
{
    return QStringLiteral(
               "version=%1.%2 profile=%3 renderable=%4 samples=%5 depth=%6 stencil=%7 swapInterval=%8")
        .arg(format.majorVersion())
        .arg(format.minorVersion())
        .arg(surfaceProfileName(format.profile()))
        .arg(renderableTypeName(format.renderableType()))
        .arg(format.samples())
        .arg(format.depthBufferSize())
        .arg(format.stencilBufferSize())
        .arg(format.swapInterval());
}

QString glStringOrUnavailable(QOpenGLContext* context, GLenum name)
{
    QOpenGLFunctions* functions = context != nullptr ? context->functions() : nullptr;
    if (functions == nullptr) {
        return QStringLiteral("unavailable");
    }
    const GLubyte* value = functions->glGetString(name);
    if (value == nullptr) {
        return QStringLiteral("unavailable");
    }
    return QString::fromLatin1(reinterpret_cast<const char*>(value));
}

#ifdef HAVE_QT_MULTIMEDIA
bool isDirectVideoPixelFormat(QVideoFrameFormat::PixelFormat pixelFormat)
{
    bool isYv12 = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    isYv12 = pixelFormat == QVideoFrameFormat::Format_YV12;
#endif
    return pixelFormat == QVideoFrameFormat::Format_NV12
        || pixelFormat == QVideoFrameFormat::Format_YUV420P
        || isYv12;
}

QString videoFrameSummary(const QVideoFrame& frame)
{
    const QVideoFrameFormat surfaceFormat = frame.surfaceFormat();
    const QSize frameSize = surfaceFormat.frameSize();
    return QStringLiteral("valid=%1 handle=%2 pixelFormat=%3 size=%4x%5")
        .arg(frame.isValid() ? 1 : 0)
        .arg(static_cast<int>(frame.handleType()))
        .arg(static_cast<int>(surfaceFormat.pixelFormat()))
        .arg(frameSize.width())
        .arg(frameSize.height());
}
#endif
}

void PreviewGLRenderer::initialize()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (context == nullptr) {
        initialized_ = false;
        recordError(QStringLiteral("initialize called without a current OpenGL context"));
        return;
    }

    if (boundContext_ != context) {
        resetGlObjects(false);
        configureForCurrentContext(context);
        initializeOpenGLFunctions();
    }

    if (initialized_) {
        return;
    }

    if (!ensureProgram()) {
        const QString failure = lastError_.isEmpty()
            ? QStringLiteral("failed to compile base preview shader program")
            : lastError_;
        resetGlObjects(true);
        configureForCurrentContext(context);
        initializeOpenGLFunctions();
        initialized_ = false;
        recordError(failure);
        return;
    }

    initialized_ = true;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void PreviewGLRenderer::shutdown()
{
    resetGlObjects(QOpenGLContext::currentContext() != nullptr);
}

void PreviewGLRenderer::resetGlObjects(bool deleteObjects)
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = context != nullptr ? context->extraFunctions() : nullptr;
    if (deleteObjects) {
        for (auto it = textureCache_.begin(); it != textureCache_.end(); ++it) {
            if (it.value() != 0) {
                GLuint texture = it.value();
                glDeleteTextures(1, &texture);
            }
        }
        if (vertexBuffer_ != 0) {
            glDeleteBuffers(1, &vertexBuffer_);
        }
        if (vertexArray_ != 0 && extra != nullptr) {
            extra->glDeleteVertexArrays(1, &vertexArray_);
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
        if (videoProgram_ != 0) {
            glDeleteProgram(videoProgram_);
        }
        if (planarVideoProgram_ != 0) {
            glDeleteProgram(planarVideoProgram_);
        }
        if (vertexShader_ != 0) {
            glDeleteShader(vertexShader_);
        }
        if (fragmentShader_ != 0) {
            glDeleteShader(fragmentShader_);
        }
        if (videoVertexShader_ != 0) {
            glDeleteShader(videoVertexShader_);
        }
        if (videoFragmentShader_ != 0) {
            glDeleteShader(videoFragmentShader_);
        }
        if (planarVideoVertexShader_ != 0) {
            glDeleteShader(planarVideoVertexShader_);
        }
        if (planarVideoFragmentShader_ != 0) {
            glDeleteShader(planarVideoFragmentShader_);
        }
        if (videoYTexture_ != 0) {
            glDeleteTextures(1, &videoYTexture_);
        }
        if (videoUvTexture_ != 0) {
            glDeleteTextures(1, &videoUvTexture_);
        }
        if (planarVideoYTexture_ != 0) {
            glDeleteTextures(1, &planarVideoYTexture_);
        }
        if (planarVideoUTexture_ != 0) {
            glDeleteTextures(1, &planarVideoUTexture_);
        }
        if (planarVideoVTexture_ != 0) {
            glDeleteTextures(1, &planarVideoVTexture_);
        }
    }

    textureCache_.clear();
    vertexBuffer_ = 0;
    vertexArray_ = 0;
    program_ = 0;
    vertexShader_ = 0;
    fragmentShader_ = 0;
    videoProgram_ = 0;
    videoVertexShader_ = 0;
    videoFragmentShader_ = 0;
    planarVideoProgram_ = 0;
    planarVideoVertexShader_ = 0;
    planarVideoFragmentShader_ = 0;
    videoYTexture_ = 0;
    videoUvTexture_ = 0;
    planarVideoYTexture_ = 0;
    planarVideoUTexture_ = 0;
    planarVideoVTexture_ = 0;
    viewportSize_ = QSize();
    devicePixelRatio_ = 1.0;
    positionLocation_ = -1;
    uvLocation_ = -1;
    textureLocation_ = -1;
    opacityLocation_ = -1;
    videoPositionLocation_ = -1;
    videoUvLocation_ = -1;
    videoYTextureLocation_ = -1;
    videoUvTextureLocation_ = -1;
    videoOpacityLocation_ = -1;
    planarVideoPositionLocation_ = -1;
    planarVideoUvLocation_ = -1;
    planarVideoYTextureLocation_ = -1;
    planarVideoUTextureLocation_ = -1;
    planarVideoVTextureLocation_ = -1;
    planarVideoOpacityLocation_ = -1;
    videoFrameSize_ = QSize();
    planarVideoFrameSize_ = QSize();
    vaoSupported_ = false;
    initialized_ = false;
}

void PreviewGLRenderer::configureForCurrentContext(QOpenGLContext* context)
{
    boundContext_ = context;
    if (context == nullptr) {
        shaderLanguage_ = ShaderLanguage::DesktopLegacy;
        videoTextureUploadMode_ = VideoTextureUploadMode::LegacyLuminance;
        videoYInternalFormat_ = GL_LUMINANCE;
        videoYExternalFormat_ = GL_LUMINANCE;
        videoUvInternalFormat_ = GL_LUMINANCE_ALPHA;
        videoUvExternalFormat_ = GL_LUMINANCE_ALPHA;
        videoUvUseRgChannels_ = false;
        supportsUnpackRowLength_ = false;
        useDesktopLegacyVersion120_ = false;
        loggedFirstDirectVideoFrame_ = false;
        contextSummary_.clear();
        return;
    }

    const bool isOpenGles = context->isOpenGLES();
    const QSurfaceFormat format = context->format();
    const auto hasExtension = [context](const char* extensionName) {
        return context->hasExtension(QByteArray(extensionName));
    };
    if (isOpenGles) {
        shaderLanguage_ = format.majorVersion() >= 3 ? ShaderLanguage::Gles300 : ShaderLanguage::Gles100;
    } else if (format.profile() == QSurfaceFormat::CoreProfile) {
        shaderLanguage_ = ShaderLanguage::DesktopCore150;
    } else {
        shaderLanguage_ = ShaderLanguage::DesktopLegacy;
    }
    useDesktopLegacyVersion120_ =
        shaderLanguage_ == ShaderLanguage::DesktopLegacy
        && (format.majorVersion() > 2 || (format.majorVersion() == 2 && format.minorVersion() >= 1));

    const bool supportsTextureRgRuntime =
        !isOpenGles
        && (format.majorVersion() >= 3
            || hasExtension("GL_ARB_texture_rg")
            || hasExtension("GL_EXT_texture_rg"));
    const bool useRedGreenVideoTexturesRequested =
        shaderLanguage_ == ShaderLanguage::DesktopCore150
        || shaderLanguage_ == ShaderLanguage::Gles300
        || supportsTextureRgRuntime;
#if defined(GL_RED) && defined(GL_RG)
    const bool useRedGreenVideoTextures = useRedGreenVideoTexturesRequested;
#else
    const bool useRedGreenVideoTextures = false;
#endif
    videoTextureUploadMode_ = useRedGreenVideoTextures
        ? VideoTextureUploadMode::RedGreen
        : VideoTextureUploadMode::LegacyLuminance;
#if defined(GL_R8)
    videoYInternalFormat_ = useRedGreenVideoTextures ? GL_R8 : GL_LUMINANCE;
#elif defined(GL_RED)
    videoYInternalFormat_ = useRedGreenVideoTextures ? GL_RED : GL_LUMINANCE;
#else
    videoYInternalFormat_ = GL_LUMINANCE;
#endif
#if defined(GL_RED)
    videoYExternalFormat_ = useRedGreenVideoTextures ? GL_RED : GL_LUMINANCE;
#else
    videoYExternalFormat_ = GL_LUMINANCE;
#endif
#if defined(GL_RG8)
    videoUvInternalFormat_ = useRedGreenVideoTextures ? GL_RG8 : GL_LUMINANCE_ALPHA;
#elif defined(GL_RG)
    videoUvInternalFormat_ = useRedGreenVideoTextures ? GL_RG : GL_LUMINANCE_ALPHA;
#else
    videoUvInternalFormat_ = GL_LUMINANCE_ALPHA;
#endif
#if defined(GL_RG)
    videoUvExternalFormat_ = useRedGreenVideoTextures ? GL_RG : GL_LUMINANCE_ALPHA;
#else
    videoUvExternalFormat_ = GL_LUMINANCE_ALPHA;
#endif
    videoUvUseRgChannels_ = useRedGreenVideoTextures;
#ifdef GL_UNPACK_ROW_LENGTH
    supportsUnpackRowLength_ = isOpenGles
        ? (format.majorVersion() >= 3 || hasExtension("GL_EXT_unpack_subimage"))
        : (format.majorVersion() > 1
            || (format.majorVersion() == 1 && format.minorVersion() >= 2)
            || hasExtension("GL_EXT_unpack_subimage"));
#else
    supportsUnpackRowLength_ = false;
#endif
    loggedFirstDirectVideoFrame_ = false;
    const QString shaderLanguageName = [this]() {
        switch (shaderLanguage_) {
        case ShaderLanguage::DesktopLegacy:
            return useDesktopLegacyVersion120_
                ? QStringLiteral("desktop_legacy_120")
                : QStringLiteral("desktop_legacy_implicit");
        case ShaderLanguage::DesktopCore150:
            return QStringLiteral("desktop_core_150");
        case ShaderLanguage::Gles100:
            return QStringLiteral("gles_100");
        case ShaderLanguage::Gles300:
            return QStringLiteral("gles_300");
        }
        return QStringLiteral("unknown");
    }();
    const QString videoUploadModeName = [this]() {
        switch (videoTextureUploadMode_) {
        case VideoTextureUploadMode::LegacyLuminance:
            return QStringLiteral("legacy_luminance");
        case VideoTextureUploadMode::RedGreen:
            return QStringLiteral("red_green");
        }
        return QStringLiteral("unknown");
    }();
    const QString vendorName = glStringOrUnavailable(context, GL_VENDOR);
    const QString rendererName = glStringOrUnavailable(context, GL_RENDERER);
    const QString glVersionName = glStringOrUnavailable(context, GL_VERSION);
    contextSummary_ = QStringLiteral(
                          "%1 vendor=%2 renderer=%3 gl=%4 shader=%5 video_upload=%6 texture_rg=%7 unpack_row_length=%8")
        .arg(formatSummary(format))
        .arg(vendorName)
        .arg(rendererName)
        .arg(glVersionName)
        .arg(shaderLanguageName)
        .arg(videoUploadModeName)
        .arg(useRedGreenVideoTextures ? 1 : 0)
        .arg(supportsUnpackRowLength_ ? 1 : 0);
    lastLoggedError_.clear();
    lastLoggedVideoError_.clear();
}

void PreviewGLRenderer::recordError(const QString& message, bool videoPath)
{
    lastError_ = message;
    QString& lastLogged = videoPath ? lastLoggedVideoError_ : lastLoggedError_;
    if (lastLogged == message) {
        return;
    }
    lastLogged = message;
    appendPreviewGlLog(videoPath ? QStringLiteral("video") : QStringLiteral("renderer"), message, true);
}

void PreviewGLRenderer::restoreDefaultUnpackState()
{
#ifdef GL_UNPACK_ROW_LENGTH
    if (supportsUnpackRowLength_) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#if defined(GL_UNPACK_SKIP_PIXELS)
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
#endif
#if defined(GL_UNPACK_SKIP_ROWS)
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
    }
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

bool PreviewGLRenderer::uploadVideoPlane(
    GLuint texture,
    int width,
    int height,
    GLenum externalFormat,
    const uchar* bits,
    int strideBytes,
    int mappedBytes,
    int bytesPerPixel,
    const QString& planeLabel,
    QString* errorMessage)
{
    if (texture == 0 || bits == nullptr || width <= 0 || height <= 0 || strideBytes <= 0 || mappedBytes <= 0
        || bytesPerPixel <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                                "%1 plane upload had invalid inputs: texture=%2 size=%3x%4 stride=%5 mapped=%6 bpp=%7")
                                .arg(planeLabel)
                                .arg(texture)
                                .arg(width)
                                .arg(height)
                                .arg(strideBytes)
                                .arg(mappedBytes)
                                .arg(bytesPerPixel);
        }
        return false;
    }

    const qint64 packedRowBytes = static_cast<qint64>(width) * bytesPerPixel;
    if (strideBytes < packedRowBytes) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 plane stride=%2 was smaller than packed_row_bytes=%3")
                                .arg(planeLabel)
                                .arg(strideBytes)
                                .arg(packedRowBytes);
        }
        return false;
    }

    const qint64 minimumMappedBytes =
        static_cast<qint64>(strideBytes) * qMax(0, height - 1) + packedRowBytes;
    if (minimumMappedBytes > mappedBytes) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 plane mappedBytes=%2 was smaller than required_bytes=%3")
                                .arg(planeLabel)
                                .arg(mappedBytes)
                                .arg(minimumMappedBytes);
        }
        return false;
    }

    QByteArray tightlyPackedBytes;
    const void* uploadBits = bits;
    int rowLengthPixels = 0;
    if (strideBytes != packedRowBytes) {
        if (supportsUnpackRowLength_ && strideBytes % bytesPerPixel == 0) {
            rowLengthPixels = strideBytes / bytesPerPixel;
        } else {
            const qint64 tightlyPackedSize = packedRowBytes * height;
            if (tightlyPackedSize <= 0 || tightlyPackedSize > std::numeric_limits<int>::max()) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("%1 plane repack size=%2 was out of range")
                                        .arg(planeLabel)
                                        .arg(tightlyPackedSize);
                }
                return false;
            }
            tightlyPackedBytes.resize(static_cast<int>(tightlyPackedSize));
            char* dst = tightlyPackedBytes.data();
            for (int row = 0; row < height; ++row) {
                std::memcpy(
                    dst + static_cast<ptrdiff_t>(row) * packedRowBytes,
                    bits + static_cast<ptrdiff_t>(row) * strideBytes,
                    static_cast<size_t>(packedRowBytes)
                );
            }
            uploadBits = tightlyPackedBytes.constData();
        }
    }

    QElapsedTimer uploadTimer;
    if (profilingFrameActive_) {
        uploadTimer.start();
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef GL_UNPACK_ROW_LENGTH
    if (supportsUnpackRowLength_) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLengthPixels);
#if defined(GL_UNPACK_SKIP_PIXELS)
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
#endif
#if defined(GL_UNPACK_SKIP_ROWS)
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
    }
#endif
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        width,
        height,
        externalFormat,
        GL_UNSIGNED_BYTE,
        uploadBits
    );
    restoreDefaultUnpackState();

    if (profilingFrameActive_) {
        const quint64 elapsedNs = static_cast<quint64>(uploadTimer.nsecsElapsed());
        frameCpuUploadNs_ += elapsedNs;
        frameVideoUploadNs_ += elapsedNs;
    }
    return true;
}

void PreviewGLRenderer::beginFrame(const QSize& viewportSize, qreal devicePixelRatio)
{
    if (!initialized_) {
        return;
    }

    viewportSize_ = viewportSize;
    devicePixelRatio_ = qMax<qreal>(1.0, devicePixelRatio);
    frameCpuUploadNs_ = 0;
    frameVideoMapNs_ = 0;
    frameVideoUploadNs_ = 0;
    profilingFrameActive_ = true;

    glViewport(
        0,
        0,
        qMax(1, qRound(viewportSize.width() * devicePixelRatio_)),
        qMax(1, qRound(viewportSize.height() * devicePixelRatio_))
    );
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void PreviewGLRenderer::endFrame()
{
    profilingFrameActive_ = false;
}

void PreviewGLRenderer::prewarmTexture(const QImage& image)
{
    if (!initialized_ || image.isNull()) {
        return;
    }
    if (!ensureProgram()) {
        return;
    }
    ensureTexture(image, true);
}

bool PreviewGLRenderer::drawImageQuadBatch(
    const QImage& image,
    const QVector<QPointF>& centers,
    qreal targetWidth,
    qreal targetHeight,
    const QVector<qreal>& angleDegrees,
    qreal opacity,
    const QRectF& sourceRect,
    bool useCache
)
{
    if (opacity <= 0.0 || centers.isEmpty()) {
        return true;
    }
    if (!initialized_ || image.isNull() || viewportSize_.isEmpty() || targetWidth <= 0.0 || targetHeight <= 0.0) {
        return false;
    }
    if (!ensureProgram()) {
        return false;
    }

    const GLuint texture = ensureTexture(image, useCache);
    if (texture == 0) {
        return false;
    }

    const float width = static_cast<float>(qMax(1, viewportSize_.width()));
    const float height = static_cast<float>(qMax(1, viewportSize_.height()));
    const QRectF uvSource = sourceRect.isValid() && !sourceRect.isEmpty()
        ? sourceRect.intersected(QRectF(0.0, 0.0, image.width(), image.height()))
        : QRectF(0.0, 0.0, image.width(), image.height());
    const float u0 = static_cast<float>(uvSource.left() / qMax(1, image.width()));
    const float v0 = static_cast<float>(uvSource.top() / qMax(1, image.height()));
    const float u1 = static_cast<float>((uvSource.left() + uvSource.width()) / qMax(1, image.width()));
    const float v1 = static_cast<float>((uvSource.top() + uvSource.height()) / qMax(1, image.height()));

    const auto toNdc = [width, height](const QPointF& point) -> QPointF {
        return QPointF(
            (point.x() / width) * 2.0 - 1.0,
            1.0 - (point.y() / height) * 2.0
        );
    };

    QVector<QuadVertex> vertices;
    vertices.reserve(centers.size() * 6);

    for (int index = 0; index < centers.size(); ++index) {
        const QPointF center = centers.at(index);
        const qreal radians = qDegreesToRadians(angleDegrees.value(index));
        const qreal sinAngle = qSin(radians);
        const qreal cosAngle = qCos(radians);
        const QPointF localCorners[] = {
            QPointF(-targetWidth / 2.0, -targetHeight / 2.0),
            QPointF(-targetWidth / 2.0, targetHeight / 2.0),
            QPointF(targetWidth / 2.0, -targetHeight / 2.0),
            QPointF(targetWidth / 2.0, targetHeight / 2.0),
        };
        QPointF rotatedCorners[4];
        for (int i = 0; i < 4; ++i) {
            const QPointF& local = localCorners[i];
            rotatedCorners[i] = QPointF(
                center.x() + local.x() * cosAngle - local.y() * sinAngle,
                center.y() + local.x() * sinAngle + local.y() * cosAngle
            );
        }

        const QPointF topLeft = toNdc(rotatedCorners[0]);
        const QPointF bottomLeft = toNdc(rotatedCorners[1]);
        const QPointF topRight = toNdc(rotatedCorners[2]);
        const QPointF bottomRight = toNdc(rotatedCorners[3]);
        vertices.append({static_cast<float>(topLeft.x()), static_cast<float>(topLeft.y()), u0, v0});
        vertices.append({static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), u0, v1});
        vertices.append({static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), u1, v0});
        vertices.append({static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), u1, v0});
        vertices.append({static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), u0, v1});
        vertices.append({static_cast<float>(bottomRight.x()), static_cast<float>(bottomRight.y()), u1, v1});
    }

    if (positionLocation_ < 0 || uvLocation_ < 0 || textureLocation_ < 0 || vertices.isEmpty()) {
        return false;
    }

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(textureLocation_, 0);
    if (opacityLocation_ >= 0) {
        glUniform1f(opacityLocation_, static_cast<GLfloat>(qBound<qreal>(0.0, opacity, 1.0)));
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    if (profilingFrameActive_) {
        QElapsedTimer uploadTimer;
        uploadTimer.start();
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(vertices.size() * static_cast<int>(sizeof(QuadVertex))),
            vertices.constData(),
            GL_DYNAMIC_DRAW
        );
        frameCpuUploadNs_ += static_cast<quint64>(uploadTimer.nsecsElapsed());
    } else {
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(vertices.size() * static_cast<int>(sizeof(QuadVertex))),
            vertices.constData(),
            GL_DYNAMIC_DRAW
        );
    }
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    if (vaoSupported_ && extra != nullptr) {
        extra->glBindVertexArray(vertexArray_);
    } else {
        glEnableVertexAttribArray(static_cast<GLuint>(positionLocation_));
        glVertexAttribPointer(
            static_cast<GLuint>(positionLocation_),
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(QuadVertex),
            reinterpret_cast<const void*>(offsetof(QuadVertex, x))
        );
        glEnableVertexAttribArray(static_cast<GLuint>(uvLocation_));
        glVertexAttribPointer(
            static_cast<GLuint>(uvLocation_),
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(QuadVertex),
            reinterpret_cast<const void*>(offsetof(QuadVertex, u))
        );
    }
    glDrawArrays(GL_TRIANGLES, 0, vertices.size());

    if (vaoSupported_ && extra != nullptr) {
        extra->glBindVertexArray(0);
    } else {
        glDisableVertexAttribArray(static_cast<GLuint>(positionLocation_));
        glDisableVertexAttribArray(static_cast<GLuint>(uvLocation_));
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!useCache) {
        glDeleteTextures(1, &texture);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);

    return true;
}

bool PreviewGLRenderer::drawImageQuad(
    const QImage& image,
    const QRectF& targetRect,
    qreal angleDegrees,
    qreal opacity,
    const QRectF& sourceRect,
    bool useCache
)
{
    if (opacity <= 0.0) {
        return true;
    }
    if (!initialized_ || image.isNull() || targetRect.isEmpty() || viewportSize_.isEmpty()) {
        return false;
    }
    if (!ensureProgram()) {
        return false;
    }

    const GLuint texture = ensureTexture(image, useCache);
    if (texture == 0) {
        return false;
    }

    const float width = static_cast<float>(qMax(1, viewportSize_.width()));
    const float height = static_cast<float>(qMax(1, viewportSize_.height()));
    const QRectF uvSource = sourceRect.isValid() && !sourceRect.isEmpty()
        ? sourceRect.intersected(QRectF(0.0, 0.0, image.width(), image.height()))
        : QRectF(0.0, 0.0, image.width(), image.height());
    const float u0 = static_cast<float>(uvSource.left() / qMax(1, image.width()));
    const float v0 = static_cast<float>(uvSource.top() / qMax(1, image.height()));
    const float u1 = static_cast<float>((uvSource.left() + uvSource.width()) / qMax(1, image.width()));
    const float v1 = static_cast<float>((uvSource.top() + uvSource.height()) / qMax(1, image.height()));
    const QPointF center = targetRect.center();
    const qreal radians = qDegreesToRadians(angleDegrees);
    const qreal sinAngle = qSin(radians);
    const qreal cosAngle = qCos(radians);
    const QPointF localCorners[] = {
        QPointF(-targetRect.width() / 2.0, -targetRect.height() / 2.0),
        QPointF(-targetRect.width() / 2.0, targetRect.height() / 2.0),
        QPointF(targetRect.width() / 2.0, -targetRect.height() / 2.0),
        QPointF(targetRect.width() / 2.0, targetRect.height() / 2.0),
    };
    QPointF rotatedCorners[4];
    for (int i = 0; i < 4; ++i) {
        const QPointF& local = localCorners[i];
        rotatedCorners[i] = QPointF(
            center.x() + local.x() * cosAngle - local.y() * sinAngle,
            center.y() + local.x() * sinAngle + local.y() * cosAngle
        );
    }

    const auto toNdc = [width, height](const QPointF& point) -> QPointF {
        return QPointF(
            (point.x() / width) * 2.0 - 1.0,
            1.0 - (point.y() / height) * 2.0
        );
    };
    const QPointF topLeft = toNdc(rotatedCorners[0]);
    const QPointF bottomLeft = toNdc(rotatedCorners[1]);
    const QPointF topRight = toNdc(rotatedCorners[2]);
    const QPointF bottomRight = toNdc(rotatedCorners[3]);
    const QuadVertex vertices[] = {
        {static_cast<float>(topLeft.x()), static_cast<float>(topLeft.y()), u0, v0},
        {static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), u0, v1},
        {static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), u1, v0},
        {static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), u1, v0},
        {static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), u0, v1},
        {static_cast<float>(bottomRight.x()), static_cast<float>(bottomRight.y()), u1, v1},
    };

    if (positionLocation_ < 0 || uvLocation_ < 0 || textureLocation_ < 0) {
        return false;
    }

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(textureLocation_, 0);
    if (opacityLocation_ >= 0) {
        glUniform1f(opacityLocation_, static_cast<GLfloat>(qBound<qreal>(0.0, opacity, 1.0)));
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    if (profilingFrameActive_) {
        QElapsedTimer uploadTimer;
        uploadTimer.start();
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
        frameCpuUploadNs_ += static_cast<quint64>(uploadTimer.nsecsElapsed());
    } else {
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    }
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    if (vaoSupported_ && extra != nullptr) {
        extra->glBindVertexArray(vertexArray_);
    } else {
        glEnableVertexAttribArray(static_cast<GLuint>(positionLocation_));
        glVertexAttribPointer(
            static_cast<GLuint>(positionLocation_),
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(QuadVertex),
            reinterpret_cast<const void*>(offsetof(QuadVertex, x))
        );
        glEnableVertexAttribArray(static_cast<GLuint>(uvLocation_));
        glVertexAttribPointer(
            static_cast<GLuint>(uvLocation_),
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(QuadVertex),
            reinterpret_cast<const void*>(offsetof(QuadVertex, u))
        );
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (vaoSupported_ && extra != nullptr) {
        extra->glBindVertexArray(0);
    } else {
        glDisableVertexAttribArray(static_cast<GLuint>(positionLocation_));
        glDisableVertexAttribArray(static_cast<GLuint>(uvLocation_));
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!useCache) {
        glDeleteTextures(1, &texture);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);

    return true;
}

bool PreviewGLRenderer::drawVideoFrame(const QVideoFrame& frame, const QRectF& targetRect, qreal opacity)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(frame);
    Q_UNUSED(targetRect);
    Q_UNUSED(opacity);
    return false;
#else
    if (!initialized_ || !frame.isValid() || targetRect.isEmpty() || viewportSize_.isEmpty()) {
        return false;
    }
    if (!ensureProgram()) {
        return false;
    }

    const auto fallbackToImage = [this, &frame, &targetRect, opacity](const QString& reason) {
        const QImage converted = frame.toImage();
        if (converted.isNull()) {
            recordError(
                QStringLiteral("%1; frame.toImage() fallback failed; %2")
                    .arg(reason, videoFrameSummary(frame)),
                true
            );
            return false;
        }
        appendPreviewGlLog(
            QStringLiteral("video"),
            QStringLiteral("%1; using QVideoFrame::toImage fallback; %2")
                .arg(reason, videoFrameSummary(frame))
        );
        const bool drawn = drawImageQuad(converted, targetRect, 0.0, opacity, QRectF(), false);
        if (!drawn) {
            recordError(
                QStringLiteral("%1; toImage fallback draw failed; %2")
                    .arg(reason, videoFrameSummary(frame)),
                true
            );
            return false;
        }
        lastError_.clear();
        return true;
    };

    QVideoFrame mappedFrame(frame);
    QElapsedTimer mapTimer;
    mapTimer.start();
    if (!mappedFrame.map(QVideoFrame::ReadOnly)) {
        return fallbackToImage(QStringLiteral("failed to map QVideoFrame for direct upload"));
    }
    if (profilingFrameActive_) {
        frameVideoMapNs_ += static_cast<quint64>(mapTimer.nsecsElapsed());
    }

    const QVideoFrameFormat::PixelFormat pixelFormat = mappedFrame.surfaceFormat().pixelFormat();
    const QSize frameSize = mappedFrame.surfaceFormat().frameSize();
    const bool isNv12 = pixelFormat == QVideoFrameFormat::Format_NV12;
    bool isYv12 = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    isYv12 = pixelFormat == QVideoFrameFormat::Format_YV12;
#endif
    const bool isPlanar420 =
        pixelFormat == QVideoFrameFormat::Format_YUV420P
        || isYv12;
    if (frameSize.isEmpty()) {
        mappedFrame.unmap();
        return fallbackToImage(QStringLiteral("video frame reported an empty size"));
    }

    const auto uploadModeName = [this]() {
        switch (videoTextureUploadMode_) {
        case VideoTextureUploadMode::LegacyLuminance:
            return QStringLiteral("legacy_luminance");
        case VideoTextureUploadMode::RedGreen:
            return QStringLiteral("red_green");
        }
        return QStringLiteral("unknown");
    }();
    const auto maybeLogDirectUploadFrame = [this, &mappedFrame, &uploadModeName](
                                               const QString& directKind,
                                               const QVector<int>& strides,
                                               const QVector<int>& mappedBytes) {
        if (loggedFirstDirectVideoFrame_) {
            return;
        }
        QString planeSummary = QStringLiteral("plane_count=%1").arg(strides.size());
        for (int plane = 0; plane < strides.size() && plane < mappedBytes.size(); ++plane) {
            planeSummary += QStringLiteral(" p%1_stride=%2 p%1_mapped=%3")
                                .arg(plane)
                                .arg(strides.at(plane))
                                .arg(mappedBytes.at(plane));
        }
        appendPreviewGlLog(
            QStringLiteral("video"),
            QStringLiteral("first_direct_upload_frame; kind=%1 upload=%2 unpack_row_length=%3; %4; %5")
                .arg(directKind)
                .arg(uploadModeName)
                .arg(supportsUnpackRowLength_ ? 1 : 0)
                .arg(videoFrameSummary(mappedFrame))
                .arg(planeSummary)
        );
        loggedFirstDirectVideoFrame_ = true;
    };

    if (isNv12) {
        if (!ensureVideoProgram() || !ensureVideoTextures(frameSize)) {
            mappedFrame.unmap();
            return fallbackToImage(QStringLiteral("failed to prepare NV12 upload resources"));
        }

        const uchar* yBits = mappedFrame.bits(0);
        const uchar* uvBits = mappedFrame.bits(1);
        const int yStride = mappedFrame.bytesPerLine(0);
        const int uvStride = mappedFrame.bytesPerLine(1);
        const int yMappedBytes = mappedFrame.mappedBytes(0);
        const int uvMappedBytes = mappedFrame.mappedBytes(1);
        if (yBits == nullptr || uvBits == nullptr || yStride <= 0 || uvStride <= 0
            || yMappedBytes <= 0 || uvMappedBytes <= 0) {
            mappedFrame.unmap();
            return fallbackToImage(QStringLiteral("NV12 frame planes were unavailable after mapping"));
        }

        maybeLogDirectUploadFrame(
            QStringLiteral("nv12"),
            QVector<int>{yStride, uvStride},
            QVector<int>{yMappedBytes, uvMappedBytes}
        );

        QString uploadError;
        if (!uploadVideoPlane(
                videoYTexture_,
                frameSize.width(),
                frameSize.height(),
                videoYExternalFormat_,
                yBits,
                yStride,
                yMappedBytes,
                1,
                QStringLiteral("NV12 Y"),
                &uploadError)
            || !uploadVideoPlane(
                videoUvTexture_,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                videoUvExternalFormat_,
                uvBits,
                uvStride,
                uvMappedBytes,
                2,
                QStringLiteral("NV12 UV"),
                &uploadError)) {
            mappedFrame.unmap();
            return fallbackToImage(uploadError);
        }
    } else if (isPlanar420) {
        if (!ensurePlanarVideoProgram() || !ensurePlanarVideoTextures(frameSize)) {
            mappedFrame.unmap();
            return fallbackToImage(QStringLiteral("failed to prepare planar YUV upload resources"));
        }

        const uchar* yBits = mappedFrame.bits(0);
        const uchar* plane1Bits = mappedFrame.bits(1);
        const uchar* plane2Bits = mappedFrame.bits(2);
        const int yStride = mappedFrame.bytesPerLine(0);
        const int plane1Stride = mappedFrame.bytesPerLine(1);
        const int plane2Stride = mappedFrame.bytesPerLine(2);
        const int yMappedBytes = mappedFrame.mappedBytes(0);
        const int plane1MappedBytes = mappedFrame.mappedBytes(1);
        const int plane2MappedBytes = mappedFrame.mappedBytes(2);
        if (yBits == nullptr || plane1Bits == nullptr || plane2Bits == nullptr
            || yStride <= 0 || plane1Stride <= 0 || plane2Stride <= 0
            || yMappedBytes <= 0 || plane1MappedBytes <= 0 || plane2MappedBytes <= 0) {
            mappedFrame.unmap();
            return fallbackToImage(QStringLiteral("planar YUV frame planes were unavailable after mapping"));
        }

        const uchar* uBits = plane1Bits;
        const uchar* vBits = plane2Bits;
        int uStride = plane1Stride;
        int vStride = plane2Stride;
        int uMappedBytes = plane1MappedBytes;
        int vMappedBytes = plane2MappedBytes;
        if (isYv12) {
            uBits = plane2Bits;
            vBits = plane1Bits;
            uStride = plane2Stride;
            vStride = plane1Stride;
            uMappedBytes = plane2MappedBytes;
            vMappedBytes = plane1MappedBytes;
        }

        maybeLogDirectUploadFrame(
            isYv12 ? QStringLiteral("yv12") : QStringLiteral("yuv420p"),
            QVector<int>{yStride, uStride, vStride},
            QVector<int>{yMappedBytes, uMappedBytes, vMappedBytes}
        );

        QString uploadError;
        if (!uploadVideoPlane(
                planarVideoYTexture_,
                frameSize.width(),
                frameSize.height(),
                videoYExternalFormat_,
                yBits,
                yStride,
                yMappedBytes,
                1,
                QStringLiteral("planar Y"),
                &uploadError)
            || !uploadVideoPlane(
                planarVideoUTexture_,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                videoYExternalFormat_,
                uBits,
                uStride,
                uMappedBytes,
                1,
                QStringLiteral("planar U"),
                &uploadError)
            || !uploadVideoPlane(
                planarVideoVTexture_,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                videoYExternalFormat_,
                vBits,
                vStride,
                vMappedBytes,
                1,
                QStringLiteral("planar V"),
                &uploadError)) {
            mappedFrame.unmap();
            return fallbackToImage(uploadError);
        }
    } else {
        mappedFrame.unmap();
        return fallbackToImage(
            QStringLiteral("unsupported direct-upload video pixel format %1")
                .arg(static_cast<int>(pixelFormat))
        );
    }

    mappedFrame.unmap();

    const float width = static_cast<float>(qMax(1, viewportSize_.width()));
    const float height = static_cast<float>(qMax(1, viewportSize_.height()));
    const QPointF corners[] = {
        QPointF(targetRect.left(), targetRect.top()),
        QPointF(targetRect.left(), targetRect.bottom()),
        QPointF(targetRect.right(), targetRect.top()),
        QPointF(targetRect.right(), targetRect.bottom()),
    };
    const auto toNdc = [width, height](const QPointF& point) -> QPointF {
        return QPointF(
            (point.x() / width) * 2.0 - 1.0,
            1.0 - (point.y() / height) * 2.0
        );
    };
    const QPointF topLeft = toNdc(corners[0]);
    const QPointF bottomLeft = toNdc(corners[1]);
    const QPointF topRight = toNdc(corners[2]);
    const QPointF bottomRight = toNdc(corners[3]);
    const QuadVertex vertices[] = {
        {static_cast<float>(topLeft.x()), static_cast<float>(topLeft.y()), 0.0f, 0.0f},
        {static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), 0.0f, 1.0f},
        {static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), 1.0f, 0.0f},
        {static_cast<float>(topRight.x()), static_cast<float>(topRight.y()), 1.0f, 0.0f},
        {static_cast<float>(bottomLeft.x()), static_cast<float>(bottomLeft.y()), 0.0f, 1.0f},
        {static_cast<float>(bottomRight.x()), static_cast<float>(bottomRight.y()), 1.0f, 1.0f},
    };

    if (isNv12) {
        glUseProgram(videoProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, videoYTexture_);
        glUniform1i(videoYTextureLocation_, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, videoUvTexture_);
        glUniform1i(videoUvTextureLocation_, 1);
        if (videoOpacityLocation_ >= 0) {
            glUniform1f(videoOpacityLocation_, static_cast<GLfloat>(qBound<qreal>(0.0, opacity, 1.0)));
        }
    } else {
        glUseProgram(planarVideoProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, planarVideoYTexture_);
        glUniform1i(planarVideoYTextureLocation_, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, planarVideoUTexture_);
        glUniform1i(planarVideoUTextureLocation_, 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, planarVideoVTexture_);
        glUniform1i(planarVideoVTextureLocation_, 2);
        if (planarVideoOpacityLocation_ >= 0) {
            glUniform1f(planarVideoOpacityLocation_, static_cast<GLfloat>(qBound<qreal>(0.0, opacity, 1.0)));
        }
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    if (profilingFrameActive_) {
        QElapsedTimer uploadTimer;
        uploadTimer.start();
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
        frameCpuUploadNs_ += static_cast<quint64>(uploadTimer.nsecsElapsed());
    } else {
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    }
    const GLint activePositionLocation = isNv12 ? videoPositionLocation_ : planarVideoPositionLocation_;
    const GLint activeUvLocation = isNv12 ? videoUvLocation_ : planarVideoUvLocation_;
    glEnableVertexAttribArray(static_cast<GLuint>(activePositionLocation));
    glVertexAttribPointer(
        static_cast<GLuint>(activePositionLocation),
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(QuadVertex),
        reinterpret_cast<const void*>(offsetof(QuadVertex, x))
    );
    glEnableVertexAttribArray(static_cast<GLuint>(activeUvLocation));
    glVertexAttribPointer(
        static_cast<GLuint>(activeUvLocation),
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(QuadVertex),
        reinterpret_cast<const void*>(offsetof(QuadVertex, u))
    );
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(static_cast<GLuint>(activePositionLocation));
    glDisableVertexAttribArray(static_cast<GLuint>(activeUvLocation));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!isNv12) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);

    lastError_.clear();
    return true;
#endif
}

bool PreviewGLRenderer::isInitialized() const
{
    return initialized_;
}

QSize PreviewGLRenderer::viewportSize() const
{
    return viewportSize_;
}

qreal PreviewGLRenderer::devicePixelRatio() const
{
    return devicePixelRatio_;
}

QString PreviewGLRenderer::lastError() const
{
    return lastError_;
}

QString PreviewGLRenderer::contextSummary() const
{
    return contextSummary_;
}

bool PreviewGLRenderer::ensureProgram()
{
    if (program_ != 0) {
        return true;
    }

    const QByteArray desktopLegacyPreamble =
        useDesktopLegacyVersion120_ ? QByteArray("#version 120\n") : QByteArray();
    const QByteArray vertexSource = [this, &desktopLegacyPreamble]() {
        switch (shaderLanguage_) {
        case ShaderLanguage::DesktopCore150:
            return QByteArray(
                "#version 150\n"
                "in vec2 a_position;\n"
                "in vec2 a_texCoord;\n"
                "out vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::Gles300:
            return QByteArray(
                "#version 300 es\n"
                "precision mediump float;\n"
                "in vec2 a_position;\n"
                "in vec2 a_texCoord;\n"
                "out vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::Gles100:
            return QByteArray(
                "precision mediump float;\n"
                "attribute vec2 a_position;\n"
                "attribute vec2 a_texCoord;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::DesktopLegacy:
        default:
            return desktopLegacyPreamble + QByteArray(
                "attribute vec2 a_position;\n"
                "attribute vec2 a_texCoord;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        }
    }();
    const QByteArray fragmentSource = [this, &desktopLegacyPreamble]() {
        switch (shaderLanguage_) {
        case ShaderLanguage::DesktopCore150:
            return QByteArray(
                "#version 150\n"
                "uniform sampler2D u_texture;\n"
                "uniform float u_opacity;\n"
                "in vec2 v_texCoord;\n"
                "out vec4 fragColor;\n"
                "void main() {\n"
                "    fragColor = texture(u_texture, v_texCoord) * u_opacity;\n"
                "}\n"
            );
        case ShaderLanguage::Gles300:
            return QByteArray(
                "#version 300 es\n"
                "precision mediump float;\n"
                "uniform sampler2D u_texture;\n"
                "uniform float u_opacity;\n"
                "in vec2 v_texCoord;\n"
                "out vec4 fragColor;\n"
                "void main() {\n"
                "    fragColor = texture(u_texture, v_texCoord) * u_opacity;\n"
                "}\n"
            );
        case ShaderLanguage::Gles100:
            return QByteArray(
                "precision mediump float;\n"
                "uniform sampler2D u_texture;\n"
                "uniform float u_opacity;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_FragColor = texture2D(u_texture, v_texCoord) * u_opacity;\n"
                "}\n"
            );
        case ShaderLanguage::DesktopLegacy:
        default:
            return desktopLegacyPreamble + QByteArray(
                "uniform sampler2D u_texture;\n"
                "uniform float u_opacity;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_FragColor = texture2D(u_texture, v_texCoord) * u_opacity;\n"
                "}\n"
            );
        }
    }();
    const auto shaderInfoLog = [this](GLuint shader) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength <= 1) {
            return QString();
        }
        QByteArray log(logLength, '\0');
        GLsizei actualLength = 0;
        glGetShaderInfoLog(shader, logLength, &actualLength, log.data());
        return QString::fromUtf8(log.constData(), qMax(0, static_cast<int>(actualLength)));
    };
    const auto programInfoLog = [this](GLuint program) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength <= 1) {
            return QString();
        }
        QByteArray log(logLength, '\0');
        GLsizei actualLength = 0;
        glGetProgramInfoLog(program, logLength, &actualLength, log.data());
        return QString::fromUtf8(log.constData(), qMax(0, static_cast<int>(actualLength)));
    };
    const auto compileShader = [this, &shaderInfoLog](GLuint shader, const QByteArray& source, const QString& label) {
        const char* sourcePtr = source.constData();
        glShaderSource(shader, 1, &sourcePtr, nullptr);
        glCompileShader(shader);
        GLint compileStatus = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus == GL_FALSE) {
            const QString log = shaderInfoLog(shader);
            recordError(
                QStringLiteral("%1 compile failed; %2; %3")
                    .arg(label, contextSummary_, log.isEmpty() ? QStringLiteral("no shader log") : log)
            );
            return false;
        }
        return true;
    };

    vertexShader_ = glCreateShader(GL_VERTEX_SHADER);
    fragmentShader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (vertexShader_ == 0 || fragmentShader_ == 0) {
        recordError(QStringLiteral("failed to allocate base shader objects; %1").arg(contextSummary_));
        return false;
    }

    if (!compileShader(vertexShader_, vertexSource, QStringLiteral("preview image vertex shader"))) {
        return false;
    }
    if (!compileShader(fragmentShader_, fragmentSource, QStringLiteral("preview image fragment shader"))) {
        return false;
    }

    program_ = glCreateProgram();
    if (program_ == 0) {
        recordError(QStringLiteral("failed to allocate base preview shader program; %1").arg(contextSummary_));
        return false;
    }

    glAttachShader(program_, vertexShader_);
    glAttachShader(program_, fragmentShader_);
    glLinkProgram(program_);
    GLint linkStatus = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        const QString log = programInfoLog(program_);
        recordError(
            QStringLiteral("preview image shader link failed; %1; %2")
                .arg(contextSummary_, log.isEmpty() ? QStringLiteral("no program log") : log)
        );
        return false;
    }

    positionLocation_ = glGetAttribLocation(program_, "a_position");
    uvLocation_ = glGetAttribLocation(program_, "a_texCoord");
    textureLocation_ = glGetUniformLocation(program_, "u_texture");
    opacityLocation_ = glGetUniformLocation(program_, "u_opacity");
    if (positionLocation_ < 0 || uvLocation_ < 0 || textureLocation_ < 0) {
        recordError(QStringLiteral("preview image shader is missing required attributes/uniforms; %1").arg(contextSummary_));
        return false;
    }

    glGenBuffers(1, &vertexBuffer_);
    if (vertexBuffer_ == 0) {
        recordError(QStringLiteral("failed to allocate preview vertex buffer; %1").arg(contextSummary_));
        return false;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    vaoSupported_ = false;
    if (extra != nullptr) {
        extra->glGenVertexArrays(1, &vertexArray_);
        if (vertexArray_ != 0) {
            extra->glBindVertexArray(vertexArray_);
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
            glEnableVertexAttribArray(static_cast<GLuint>(positionLocation_));
            glVertexAttribPointer(
                static_cast<GLuint>(positionLocation_),
                2,
                GL_FLOAT,
                GL_FALSE,
                sizeof(QuadVertex),
                reinterpret_cast<const void*>(offsetof(QuadVertex, x))
            );
            glEnableVertexAttribArray(static_cast<GLuint>(uvLocation_));
            glVertexAttribPointer(
                static_cast<GLuint>(uvLocation_),
                2,
                GL_FLOAT,
                GL_FALSE,
                sizeof(QuadVertex),
                reinterpret_cast<const void*>(offsetof(QuadVertex, u))
            );
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            extra->glBindVertexArray(0);
            vaoSupported_ = true;
        }
    }

    lastError_.clear();
    return true;
}

bool PreviewGLRenderer::ensureVideoProgram()
{
    if (videoProgram_ != 0) {
        return true;
    }

    const QByteArray desktopLegacyPreamble =
        useDesktopLegacyVersion120_ ? QByteArray("#version 120\n") : QByteArray();
    const QByteArray vertexSource = [this, &desktopLegacyPreamble]() {
        switch (shaderLanguage_) {
        case ShaderLanguage::DesktopCore150:
            return QByteArray(
                "#version 150\n"
                "in vec2 a_position;\n"
                "in vec2 a_texCoord;\n"
                "out vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::Gles300:
            return QByteArray(
                "#version 300 es\n"
                "precision mediump float;\n"
                "in vec2 a_position;\n"
                "in vec2 a_texCoord;\n"
                "out vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::Gles100:
            return QByteArray(
                "precision mediump float;\n"
                "attribute vec2 a_position;\n"
                "attribute vec2 a_texCoord;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::DesktopLegacy:
        default:
            return desktopLegacyPreamble + QByteArray(
                "attribute vec2 a_position;\n"
                "attribute vec2 a_texCoord;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        }
    }();
    const QByteArray fragmentSource = [this, &desktopLegacyPreamble]() {
        const QString uvExpr = videoUvUseRgChannels_
            ? QStringLiteral("texture(u_uvTexture, v_texCoord).rg")
            : QStringLiteral("vec2(texture(u_uvTexture, v_texCoord).r, texture(u_uvTexture, v_texCoord).a)");
        const QString uvExprLegacy = videoUvUseRgChannels_
            ? QStringLiteral("vec2(texture2D(u_uvTexture, v_texCoord).r, texture2D(u_uvTexture, v_texCoord).g)")
            : QStringLiteral("vec2(texture2D(u_uvTexture, v_texCoord).r, texture2D(u_uvTexture, v_texCoord).a)");
        switch (shaderLanguage_) {
        case ShaderLanguage::DesktopCore150:
            return QString(
                "#version 150\n"
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uvTexture;\n"
                "uniform float u_opacity;\n"
                "in vec2 v_texCoord;\n"
                "out vec4 fragColor;\n"
                "void main() {\n"
                "    float y = texture(u_yTexture, v_texCoord).r;\n"
                "    vec2 uv = %1 - vec2(0.5, 0.5);\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * uv.y;\n"
                "    rgb.g = y - 0.21324861 * uv.x - 0.53290933 * uv.y;\n"
                "    rgb.b = y + 2.11240179 * uv.x;\n"
                "    fragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            ).arg(uvExpr).toUtf8();
        case ShaderLanguage::Gles300:
            return QString(
                "#version 300 es\n"
                "precision mediump float;\n"
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uvTexture;\n"
                "uniform float u_opacity;\n"
                "in vec2 v_texCoord;\n"
                "out vec4 fragColor;\n"
                "void main() {\n"
                "    float y = texture(u_yTexture, v_texCoord).r;\n"
                "    vec2 uv = %1 - vec2(0.5, 0.5);\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * uv.y;\n"
                "    rgb.g = y - 0.21324861 * uv.x - 0.53290933 * uv.y;\n"
                "    rgb.b = y + 2.11240179 * uv.x;\n"
                "    fragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            ).arg(uvExpr).toUtf8();
        case ShaderLanguage::Gles100:
            return QString(
                "precision mediump float;\n"
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uvTexture;\n"
                "uniform float u_opacity;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    float y = texture2D(u_yTexture, v_texCoord).r;\n"
                "    vec2 uv = %1 - vec2(0.5, 0.5);\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * uv.y;\n"
                "    rgb.g = y - 0.21324861 * uv.x - 0.53290933 * uv.y;\n"
                "    rgb.b = y + 2.11240179 * uv.x;\n"
                "    gl_FragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            ).arg(uvExprLegacy).toUtf8();
        case ShaderLanguage::DesktopLegacy:
        default:
            return (desktopLegacyPreamble + QString(
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uvTexture;\n"
                "uniform float u_opacity;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    float y = texture2D(u_yTexture, v_texCoord).r;\n"
                "    vec2 uv = %1 - vec2(0.5, 0.5);\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * uv.y;\n"
                "    rgb.g = y - 0.21324861 * uv.x - 0.53290933 * uv.y;\n"
                "    rgb.b = y + 2.11240179 * uv.x;\n"
                "    gl_FragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            ).arg(uvExprLegacy).toUtf8());
        }
    }();
    const auto shaderInfoLog = [this](GLuint shader) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength <= 1) {
            return QString();
        }
        QByteArray log(logLength, '\0');
        GLsizei actualLength = 0;
        glGetShaderInfoLog(shader, logLength, &actualLength, log.data());
        return QString::fromUtf8(log.constData(), qMax(0, static_cast<int>(actualLength)));
    };
    const auto programInfoLog = [this](GLuint program) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength <= 1) {
            return QString();
        }
        QByteArray log(logLength, '\0');
        GLsizei actualLength = 0;
        glGetProgramInfoLog(program, logLength, &actualLength, log.data());
        return QString::fromUtf8(log.constData(), qMax(0, static_cast<int>(actualLength)));
    };
    const auto compileShader = [this, &shaderInfoLog](GLuint shader, const QByteArray& source, const QString& label) {
        const char* sourcePtr = source.constData();
        glShaderSource(shader, 1, &sourcePtr, nullptr);
        glCompileShader(shader);
        GLint compileStatus = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus == GL_FALSE) {
            const QString log = shaderInfoLog(shader);
            recordError(
                QStringLiteral("%1 compile failed; %2; %3")
                    .arg(label, contextSummary_, log.isEmpty() ? QStringLiteral("no shader log") : log),
                true
            );
            return false;
        }
        return true;
    };

    videoVertexShader_ = glCreateShader(GL_VERTEX_SHADER);
    videoFragmentShader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (videoVertexShader_ == 0 || videoFragmentShader_ == 0) {
        recordError(QStringLiteral("failed to allocate video shader objects; %1").arg(contextSummary_), true);
        return false;
    }

    if (!compileShader(videoVertexShader_, vertexSource, QStringLiteral("preview video vertex shader"))) {
        return false;
    }
    if (!compileShader(videoFragmentShader_, fragmentSource, QStringLiteral("preview video fragment shader"))) {
        return false;
    }

    videoProgram_ = glCreateProgram();
    if (videoProgram_ == 0) {
        recordError(QStringLiteral("failed to allocate video shader program; %1").arg(contextSummary_), true);
        return false;
    }

    glAttachShader(videoProgram_, videoVertexShader_);
    glAttachShader(videoProgram_, videoFragmentShader_);
    glLinkProgram(videoProgram_);
    GLint linkStatus = 0;
    glGetProgramiv(videoProgram_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        const QString log = programInfoLog(videoProgram_);
        recordError(
            QStringLiteral("preview video shader link failed; %1; %2")
                .arg(contextSummary_, log.isEmpty() ? QStringLiteral("no program log") : log),
            true
        );
        return false;
    }

    videoPositionLocation_ = glGetAttribLocation(videoProgram_, "a_position");
    videoUvLocation_ = glGetAttribLocation(videoProgram_, "a_texCoord");
    videoYTextureLocation_ = glGetUniformLocation(videoProgram_, "u_yTexture");
    videoUvTextureLocation_ = glGetUniformLocation(videoProgram_, "u_uvTexture");
    videoOpacityLocation_ = glGetUniformLocation(videoProgram_, "u_opacity");
    if (videoPositionLocation_ < 0 || videoUvLocation_ < 0 || videoYTextureLocation_ < 0 || videoUvTextureLocation_ < 0) {
        recordError(QStringLiteral("preview video shader is missing required attributes/uniforms; %1").arg(contextSummary_), true);
        return false;
    }
    lastError_.clear();
    return true;
}

bool PreviewGLRenderer::ensurePlanarVideoProgram()
{
    if (planarVideoProgram_ != 0) {
        return true;
    }

    const QByteArray desktopLegacyPreamble =
        useDesktopLegacyVersion120_ ? QByteArray("#version 120\n") : QByteArray();
    const QByteArray vertexSource = [this, &desktopLegacyPreamble]() {
        switch (shaderLanguage_) {
        case ShaderLanguage::DesktopCore150:
            return QByteArray(
                "#version 150\n"
                "in vec2 a_position;\n"
                "in vec2 a_texCoord;\n"
                "out vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::Gles300:
            return QByteArray(
                "#version 300 es\n"
                "precision mediump float;\n"
                "in vec2 a_position;\n"
                "in vec2 a_texCoord;\n"
                "out vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::Gles100:
            return QByteArray(
                "precision mediump float;\n"
                "attribute vec2 a_position;\n"
                "attribute vec2 a_texCoord;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        case ShaderLanguage::DesktopLegacy:
        default:
            return desktopLegacyPreamble + QByteArray(
                "attribute vec2 a_position;\n"
                "attribute vec2 a_texCoord;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                "    v_texCoord = a_texCoord;\n"
                "}\n"
            );
        }
    }();
    const QByteArray fragmentSource = [this, &desktopLegacyPreamble]() {
        switch (shaderLanguage_) {
        case ShaderLanguage::DesktopCore150:
            return QByteArray(
                "#version 150\n"
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uTexture;\n"
                "uniform sampler2D u_vTexture;\n"
                "uniform float u_opacity;\n"
                "in vec2 v_texCoord;\n"
                "out vec4 fragColor;\n"
                "void main() {\n"
                "    float y = texture(u_yTexture, v_texCoord).r;\n"
                "    float u = texture(u_uTexture, v_texCoord).r - 0.5;\n"
                "    float v = texture(u_vTexture, v_texCoord).r - 0.5;\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * v;\n"
                "    rgb.g = y - 0.21324861 * u - 0.53290933 * v;\n"
                "    rgb.b = y + 2.11240179 * u;\n"
                "    fragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            );
        case ShaderLanguage::Gles300:
            return QByteArray(
                "#version 300 es\n"
                "precision mediump float;\n"
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uTexture;\n"
                "uniform sampler2D u_vTexture;\n"
                "uniform float u_opacity;\n"
                "in vec2 v_texCoord;\n"
                "out vec4 fragColor;\n"
                "void main() {\n"
                "    float y = texture(u_yTexture, v_texCoord).r;\n"
                "    float u = texture(u_uTexture, v_texCoord).r - 0.5;\n"
                "    float v = texture(u_vTexture, v_texCoord).r - 0.5;\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * v;\n"
                "    rgb.g = y - 0.21324861 * u - 0.53290933 * v;\n"
                "    rgb.b = y + 2.11240179 * u;\n"
                "    fragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            );
        case ShaderLanguage::Gles100:
            return QByteArray(
                "precision mediump float;\n"
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uTexture;\n"
                "uniform sampler2D u_vTexture;\n"
                "uniform float u_opacity;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    float y = texture2D(u_yTexture, v_texCoord).r;\n"
                "    float u = texture2D(u_uTexture, v_texCoord).r - 0.5;\n"
                "    float v = texture2D(u_vTexture, v_texCoord).r - 0.5;\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * v;\n"
                "    rgb.g = y - 0.21324861 * u - 0.53290933 * v;\n"
                "    rgb.b = y + 2.11240179 * u;\n"
                "    gl_FragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            );
        case ShaderLanguage::DesktopLegacy:
        default:
            return desktopLegacyPreamble + QByteArray(
                "uniform sampler2D u_yTexture;\n"
                "uniform sampler2D u_uTexture;\n"
                "uniform sampler2D u_vTexture;\n"
                "uniform float u_opacity;\n"
                "varying vec2 v_texCoord;\n"
                "void main() {\n"
                "    float y = texture2D(u_yTexture, v_texCoord).r;\n"
                "    float u = texture2D(u_uTexture, v_texCoord).r - 0.5;\n"
                "    float v = texture2D(u_vTexture, v_texCoord).r - 0.5;\n"
                "    y = 1.16438356 * (y - 0.0625);\n"
                "    vec3 rgb;\n"
                "    rgb.r = y + 1.79274107 * v;\n"
                "    rgb.g = y - 0.21324861 * u - 0.53290933 * v;\n"
                "    rgb.b = y + 2.11240179 * u;\n"
                "    gl_FragColor = vec4(rgb, 1.0) * u_opacity;\n"
                "}\n"
            );
        }
    }();
    const auto shaderInfoLog = [this](GLuint shader) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength <= 1) {
            return QString();
        }
        QByteArray log(logLength, '\0');
        GLsizei actualLength = 0;
        glGetShaderInfoLog(shader, logLength, &actualLength, log.data());
        return QString::fromUtf8(log.constData(), qMax(0, static_cast<int>(actualLength)));
    };
    const auto programInfoLog = [this](GLuint program) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength <= 1) {
            return QString();
        }
        QByteArray log(logLength, '\0');
        GLsizei actualLength = 0;
        glGetProgramInfoLog(program, logLength, &actualLength, log.data());
        return QString::fromUtf8(log.constData(), qMax(0, static_cast<int>(actualLength)));
    };
    const auto compileShader = [this, &shaderInfoLog](GLuint shader, const QByteArray& source, const QString& label) {
        const char* sourcePtr = source.constData();
        glShaderSource(shader, 1, &sourcePtr, nullptr);
        glCompileShader(shader);
        GLint compileStatus = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus == GL_FALSE) {
            const QString log = shaderInfoLog(shader);
            recordError(
                QStringLiteral("%1 compile failed; %2; %3")
                    .arg(label, contextSummary_, log.isEmpty() ? QStringLiteral("no shader log") : log),
                true
            );
            return false;
        }
        return true;
    };

    planarVideoVertexShader_ = glCreateShader(GL_VERTEX_SHADER);
    planarVideoFragmentShader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (planarVideoVertexShader_ == 0 || planarVideoFragmentShader_ == 0) {
        recordError(QStringLiteral("failed to allocate planar video shader objects; %1").arg(contextSummary_), true);
        return false;
    }
    if (!compileShader(planarVideoVertexShader_, vertexSource, QStringLiteral("preview planar video vertex shader"))) {
        return false;
    }
    if (!compileShader(planarVideoFragmentShader_, fragmentSource, QStringLiteral("preview planar video fragment shader"))) {
        return false;
    }

    planarVideoProgram_ = glCreateProgram();
    if (planarVideoProgram_ == 0) {
        recordError(QStringLiteral("failed to allocate planar video shader program; %1").arg(contextSummary_), true);
        return false;
    }
    glAttachShader(planarVideoProgram_, planarVideoVertexShader_);
    glAttachShader(planarVideoProgram_, planarVideoFragmentShader_);
    glLinkProgram(planarVideoProgram_);
    GLint linkStatus = 0;
    glGetProgramiv(planarVideoProgram_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        const QString log = programInfoLog(planarVideoProgram_);
        recordError(
            QStringLiteral("preview planar video shader link failed; %1; %2")
                .arg(contextSummary_, log.isEmpty() ? QStringLiteral("no program log") : log),
            true
        );
        return false;
    }

    planarVideoPositionLocation_ = glGetAttribLocation(planarVideoProgram_, "a_position");
    planarVideoUvLocation_ = glGetAttribLocation(planarVideoProgram_, "a_texCoord");
    planarVideoYTextureLocation_ = glGetUniformLocation(planarVideoProgram_, "u_yTexture");
    planarVideoUTextureLocation_ = glGetUniformLocation(planarVideoProgram_, "u_uTexture");
    planarVideoVTextureLocation_ = glGetUniformLocation(planarVideoProgram_, "u_vTexture");
    planarVideoOpacityLocation_ = glGetUniformLocation(planarVideoProgram_, "u_opacity");
    if (planarVideoPositionLocation_ < 0
        || planarVideoUvLocation_ < 0
        || planarVideoYTextureLocation_ < 0
        || planarVideoUTextureLocation_ < 0
        || planarVideoVTextureLocation_ < 0) {
        recordError(QStringLiteral("preview planar video shader is missing required attributes/uniforms; %1").arg(contextSummary_), true);
        return false;
    }
    lastError_.clear();
    return true;
}

bool PreviewGLRenderer::ensureVideoTextures(const QSize& frameSize)
{
    if (frameSize.isEmpty()) {
        return false;
    }
    const bool resizeNeeded = videoFrameSize_ != frameSize || videoYTexture_ == 0 || videoUvTexture_ == 0;
    if (!resizeNeeded) {
        return true;
    }

    if (videoYTexture_ == 0) {
        glGenTextures(1, &videoYTexture_);
    }
    if (videoUvTexture_ == 0) {
        glGenTextures(1, &videoUvTexture_);
    }
    if (videoYTexture_ == 0 || videoUvTexture_ == 0) {
        recordError(QStringLiteral("failed to allocate NV12 textures; %1").arg(contextSummary_), true);
        return false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_2D, videoYTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        videoYInternalFormat_,
        frameSize.width(),
        frameSize.height(),
        0,
        videoYExternalFormat_,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, videoUvTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        videoUvInternalFormat_,
        qMax(1, frameSize.width() / 2),
        qMax(1, frameSize.height() / 2),
        0,
        videoUvExternalFormat_,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    restoreDefaultUnpackState();
    videoFrameSize_ = frameSize;
    return true;
}

bool PreviewGLRenderer::ensurePlanarVideoTextures(const QSize& frameSize)
{
    if (frameSize.isEmpty()) {
        return false;
    }
    const bool resizeNeeded =
        planarVideoFrameSize_ != frameSize
        || planarVideoYTexture_ == 0
        || planarVideoUTexture_ == 0
        || planarVideoVTexture_ == 0;
    if (!resizeNeeded) {
        return true;
    }

    if (planarVideoYTexture_ == 0) {
        glGenTextures(1, &planarVideoYTexture_);
    }
    if (planarVideoUTexture_ == 0) {
        glGenTextures(1, &planarVideoUTexture_);
    }
    if (planarVideoVTexture_ == 0) {
        glGenTextures(1, &planarVideoVTexture_);
    }
    if (planarVideoYTexture_ == 0 || planarVideoUTexture_ == 0 || planarVideoVTexture_ == 0) {
        recordError(QStringLiteral("failed to allocate planar YUV textures; %1").arg(contextSummary_), true);
        return false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_2D, planarVideoYTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        videoYInternalFormat_,
        frameSize.width(),
        frameSize.height(),
        0,
        videoYExternalFormat_,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, planarVideoUTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        videoYInternalFormat_,
        qMax(1, frameSize.width() / 2),
        qMax(1, frameSize.height() / 2),
        0,
        videoYExternalFormat_,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, planarVideoVTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        videoYInternalFormat_,
        qMax(1, frameSize.width() / 2),
        qMax(1, frameSize.height() / 2),
        0,
        videoYExternalFormat_,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    restoreDefaultUnpackState();
    planarVideoFrameSize_ = frameSize;
    return true;
}

GLuint PreviewGLRenderer::ensureTexture(const QImage& image, bool useCache)
{
    const quint64 cacheKey = useCache ? image.cacheKey() : 0;
    if (useCache) {
        const auto cached = textureCache_.constFind(cacheKey);
        if (cached != textureCache_.cend() && cached.value() != 0) {
            return cached.value();
        }
    }

    const QImage textureImage = image.convertToFormat(QImage::Format_RGBA8888);
    if (textureImage.isNull()) {
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    // Apply mipmaps only for cached static textures to avoid per-frame cost on media frames.
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        useCache ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR
    );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (profilingFrameActive_) {
        QElapsedTimer uploadTimer;
        uploadTimer.start();
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            textureImage.width(),
            textureImage.height(),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            textureImage.constBits()
        );
        frameCpuUploadNs_ += static_cast<quint64>(uploadTimer.nsecsElapsed());
    } else {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            textureImage.width(),
            textureImage.height(),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            textureImage.constBits()
        );
    }
    if (useCache) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    restoreDefaultUnpackState();
    glBindTexture(GL_TEXTURE_2D, 0);

    if (useCache) {
        textureCache_.insert(cacheKey, texture);
    }
    return texture;
}

quint64 PreviewGLRenderer::frameCpuUploadNs() const
{
    return frameCpuUploadNs_;
}

quint64 PreviewGLRenderer::frameVideoMapNs() const
{
    return frameVideoMapNs_;
}

quint64 PreviewGLRenderer::frameVideoUploadNs() const
{
    return frameVideoUploadNs_;
}
