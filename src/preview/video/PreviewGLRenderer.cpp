#include "PreviewGLRenderer.h"

#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#include <QVideoFrameFormat>
#endif
#include <QtMath>
#include <QtGlobal>

#include <cstddef>

namespace {
struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};
}

void PreviewGLRenderer::initialize()
{
    if (initialized_) {
        return;
    }

    initializeOpenGLFunctions();
    initialized_ = true;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void PreviewGLRenderer::shutdown()
{
    for (auto it = textureCache_.begin(); it != textureCache_.end(); ++it) {
        if (it.value() != 0) {
            GLuint texture = it.value();
            glDeleteTextures(1, &texture);
        }
    }
    textureCache_.clear();
    if (vertexBuffer_ != 0) {
        glDeleteBuffers(1, &vertexBuffer_);
        vertexBuffer_ = 0;
    }
    if (vertexArray_ != 0) {
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
        if (extra != nullptr) {
            extra->glDeleteVertexArrays(1, &vertexArray_);
        }
        vertexArray_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (videoProgram_ != 0) {
        glDeleteProgram(videoProgram_);
        videoProgram_ = 0;
    }
    if (planarVideoProgram_ != 0) {
        glDeleteProgram(planarVideoProgram_);
        planarVideoProgram_ = 0;
    }
    if (vertexShader_ != 0) {
        glDeleteShader(vertexShader_);
        vertexShader_ = 0;
    }
    if (fragmentShader_ != 0) {
        glDeleteShader(fragmentShader_);
        fragmentShader_ = 0;
    }
    if (videoVertexShader_ != 0) {
        glDeleteShader(videoVertexShader_);
        videoVertexShader_ = 0;
    }
    if (videoFragmentShader_ != 0) {
        glDeleteShader(videoFragmentShader_);
        videoFragmentShader_ = 0;
    }
    if (planarVideoVertexShader_ != 0) {
        glDeleteShader(planarVideoVertexShader_);
        planarVideoVertexShader_ = 0;
    }
    if (planarVideoFragmentShader_ != 0) {
        glDeleteShader(planarVideoFragmentShader_);
        planarVideoFragmentShader_ = 0;
    }
    if (videoYTexture_ != 0) {
        glDeleteTextures(1, &videoYTexture_);
        videoYTexture_ = 0;
    }
    if (videoUvTexture_ != 0) {
        glDeleteTextures(1, &videoUvTexture_);
        videoUvTexture_ = 0;
    }
    if (planarVideoYTexture_ != 0) {
        glDeleteTextures(1, &planarVideoYTexture_);
        planarVideoYTexture_ = 0;
    }
    if (planarVideoUTexture_ != 0) {
        glDeleteTextures(1, &planarVideoUTexture_);
        planarVideoUTexture_ = 0;
    }
    if (planarVideoVTexture_ != 0) {
        glDeleteTextures(1, &planarVideoVTexture_);
        planarVideoVTexture_ = 0;
    }

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

    QVideoFrame mappedFrame(frame);
    QElapsedTimer mapTimer;
    mapTimer.start();
    if (!mappedFrame.map(QVideoFrame::ReadOnly)) {
        return false;
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
        return false;
    }

    if (isNv12) {
        if (!ensureVideoProgram() || !ensureVideoTextures(frameSize)) {
            mappedFrame.unmap();
            return false;
        }

        const uchar* yBits = mappedFrame.bits(0);
        const uchar* uvBits = mappedFrame.bits(1);
        const int yStride = mappedFrame.bytesPerLine(0);
        const int uvStride = mappedFrame.bytesPerLine(1);
        if (yBits == nullptr || uvBits == nullptr || yStride <= 0 || uvStride <= 0) {
            mappedFrame.unmap();
            return false;
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, yStride);
#endif
        glBindTexture(GL_TEXTURE_2D, videoYTexture_);
        if (profilingFrameActive_) {
            QElapsedTimer uploadTimer;
            uploadTimer.start();
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frameSize.width(), frameSize.height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, yBits);
            const quint64 elapsedNs = static_cast<quint64>(uploadTimer.nsecsElapsed());
            frameCpuUploadNs_ += elapsedNs;
            frameVideoUploadNs_ += elapsedNs;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frameSize.width(), frameSize.height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, yBits);
        }

#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, qMax(1, uvStride / 2));
#endif
        glBindTexture(GL_TEXTURE_2D, videoUvTexture_);
        if (profilingFrameActive_) {
            QElapsedTimer uploadTimer;
            uploadTimer.start();
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                GL_LUMINANCE_ALPHA,
                GL_UNSIGNED_BYTE,
                uvBits
            );
            const quint64 elapsedNs = static_cast<quint64>(uploadTimer.nsecsElapsed());
            frameCpuUploadNs_ += elapsedNs;
            frameVideoUploadNs_ += elapsedNs;
        } else {
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                GL_LUMINANCE_ALPHA,
                GL_UNSIGNED_BYTE,
                uvBits
            );
        }
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    } else if (isPlanar420) {
        if (!ensurePlanarVideoProgram() || !ensurePlanarVideoTextures(frameSize)) {
            mappedFrame.unmap();
            return false;
        }

        const uchar* yBits = mappedFrame.bits(0);
        const uchar* plane1Bits = mappedFrame.bits(1);
        const uchar* plane2Bits = mappedFrame.bits(2);
        const int yStride = mappedFrame.bytesPerLine(0);
        const int plane1Stride = mappedFrame.bytesPerLine(1);
        const int plane2Stride = mappedFrame.bytesPerLine(2);
        if (yBits == nullptr || plane1Bits == nullptr || plane2Bits == nullptr
            || yStride <= 0 || plane1Stride <= 0 || plane2Stride <= 0) {
            mappedFrame.unmap();
            return false;
        }

        const uchar* uBits = plane1Bits;
        const uchar* vBits = plane2Bits;
        int uStride = plane1Stride;
        int vStride = plane2Stride;
        if (isYv12) {
            uBits = plane2Bits;
            vBits = plane1Bits;
            uStride = plane2Stride;
            vStride = plane1Stride;
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, yStride);
#endif
        glBindTexture(GL_TEXTURE_2D, planarVideoYTexture_);
        if (profilingFrameActive_) {
            QElapsedTimer uploadTimer;
            uploadTimer.start();
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frameSize.width(), frameSize.height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, yBits);
            const quint64 elapsedNs = static_cast<quint64>(uploadTimer.nsecsElapsed());
            frameCpuUploadNs_ += elapsedNs;
            frameVideoUploadNs_ += elapsedNs;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frameSize.width(), frameSize.height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, yBits);
        }

#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, uStride);
#endif
        glBindTexture(GL_TEXTURE_2D, planarVideoUTexture_);
        if (profilingFrameActive_) {
            QElapsedTimer uploadTimer;
            uploadTimer.start();
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                GL_LUMINANCE,
                GL_UNSIGNED_BYTE,
                uBits
            );
            const quint64 elapsedNs = static_cast<quint64>(uploadTimer.nsecsElapsed());
            frameCpuUploadNs_ += elapsedNs;
            frameVideoUploadNs_ += elapsedNs;
        } else {
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                GL_LUMINANCE,
                GL_UNSIGNED_BYTE,
                uBits
            );
        }

#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, vStride);
#endif
        glBindTexture(GL_TEXTURE_2D, planarVideoVTexture_);
        if (profilingFrameActive_) {
            QElapsedTimer uploadTimer;
            uploadTimer.start();
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                GL_LUMINANCE,
                GL_UNSIGNED_BYTE,
                vBits
            );
            const quint64 elapsedNs = static_cast<quint64>(uploadTimer.nsecsElapsed());
            frameCpuUploadNs_ += elapsedNs;
            frameVideoUploadNs_ += elapsedNs;
        } else {
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                qMax(1, frameSize.width() / 2),
                qMax(1, frameSize.height() / 2),
                GL_LUMINANCE,
                GL_UNSIGNED_BYTE,
                vBits
            );
        }
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    } else {
        mappedFrame.unmap();
        return false;
    }

    mappedFrame.unmap();

    const float width = static_cast<float>(qMax(1, viewportSize_.width()));
    const float height = static_cast<float>(qMax(1, viewportSize_.height()));
    const QPointF center = targetRect.center();
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
    Q_UNUSED(center);
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

bool PreviewGLRenderer::ensureProgram()
{
    if (program_ != 0) {
        return true;
    }

    static const char* kVertexSource =
        "attribute vec2 a_position;\n"
        "attribute vec2 a_texCoord;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
        "    v_texCoord = a_texCoord;\n"
        "}\n";
    static const char* kFragmentSource =
        "uniform sampler2D u_texture;\n"
        "uniform float u_opacity;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(u_texture, v_texCoord) * u_opacity;\n"
        "}\n";

    vertexShader_ = glCreateShader(GL_VERTEX_SHADER);
    fragmentShader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (vertexShader_ == 0 || fragmentShader_ == 0) {
        return false;
    }

    glShaderSource(vertexShader_, 1, &kVertexSource, nullptr);
    glCompileShader(vertexShader_);
    GLint compileStatus = 0;
    glGetShaderiv(vertexShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }

    glShaderSource(fragmentShader_, 1, &kFragmentSource, nullptr);
    glCompileShader(fragmentShader_);
    glGetShaderiv(fragmentShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }

    program_ = glCreateProgram();
    if (program_ == 0) {
        return false;
    }

    glAttachShader(program_, vertexShader_);
    glAttachShader(program_, fragmentShader_);
    glLinkProgram(program_);
    GLint linkStatus = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        return false;
    }

    positionLocation_ = glGetAttribLocation(program_, "a_position");
    uvLocation_ = glGetAttribLocation(program_, "a_texCoord");
    textureLocation_ = glGetUniformLocation(program_, "u_texture");
    opacityLocation_ = glGetUniformLocation(program_, "u_opacity");
    if (positionLocation_ < 0 || uvLocation_ < 0 || textureLocation_ < 0) {
        return false;
    }

    glGenBuffers(1, &vertexBuffer_);
    if (vertexBuffer_ == 0) {
        return false;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
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

    return true;
}

bool PreviewGLRenderer::ensureVideoProgram()
{
    if (videoProgram_ != 0) {
        return true;
    }

    static const char* kVertexSource =
        "attribute vec2 a_position;\n"
        "attribute vec2 a_texCoord;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
        "    v_texCoord = a_texCoord;\n"
        "}\n";
    static const char* kFragmentSource =
        "uniform sampler2D u_yTexture;\n"
        "uniform sampler2D u_uvTexture;\n"
        "uniform float u_opacity;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "    float y = texture2D(u_yTexture, v_texCoord).r;\n"
        "    vec2 uv = vec2(texture2D(u_uvTexture, v_texCoord).r, texture2D(u_uvTexture, v_texCoord).a) - vec2(0.5, 0.5);\n"
        "    y = 1.16438356 * (y - 0.0625);\n"
        "    vec3 rgb;\n"
        "    rgb.r = y + 1.79274107 * uv.y;\n"
        "    rgb.g = y - 0.21324861 * uv.x - 0.53290933 * uv.y;\n"
        "    rgb.b = y + 2.11240179 * uv.x;\n"
        "    gl_FragColor = vec4(rgb, 1.0) * u_opacity;\n"
        "}\n";

    videoVertexShader_ = glCreateShader(GL_VERTEX_SHADER);
    videoFragmentShader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (videoVertexShader_ == 0 || videoFragmentShader_ == 0) {
        return false;
    }

    glShaderSource(videoVertexShader_, 1, &kVertexSource, nullptr);
    glCompileShader(videoVertexShader_);
    GLint compileStatus = 0;
    glGetShaderiv(videoVertexShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }

    glShaderSource(videoFragmentShader_, 1, &kFragmentSource, nullptr);
    glCompileShader(videoFragmentShader_);
    glGetShaderiv(videoFragmentShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }

    videoProgram_ = glCreateProgram();
    if (videoProgram_ == 0) {
        return false;
    }

    glAttachShader(videoProgram_, videoVertexShader_);
    glAttachShader(videoProgram_, videoFragmentShader_);
    glLinkProgram(videoProgram_);
    GLint linkStatus = 0;
    glGetProgramiv(videoProgram_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        return false;
    }

    videoPositionLocation_ = glGetAttribLocation(videoProgram_, "a_position");
    videoUvLocation_ = glGetAttribLocation(videoProgram_, "a_texCoord");
    videoYTextureLocation_ = glGetUniformLocation(videoProgram_, "u_yTexture");
    videoUvTextureLocation_ = glGetUniformLocation(videoProgram_, "u_uvTexture");
    videoOpacityLocation_ = glGetUniformLocation(videoProgram_, "u_opacity");
    return videoPositionLocation_ >= 0 && videoUvLocation_ >= 0 && videoYTextureLocation_ >= 0 && videoUvTextureLocation_ >= 0;
}

bool PreviewGLRenderer::ensurePlanarVideoProgram()
{
    if (planarVideoProgram_ != 0) {
        return true;
    }

    static const char* kVertexSource =
        "attribute vec2 a_position;\n"
        "attribute vec2 a_texCoord;\n"
        "varying vec2 v_texCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
        "    v_texCoord = a_texCoord;\n"
        "}\n";
    static const char* kFragmentSource =
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
        "}\n";

    planarVideoVertexShader_ = glCreateShader(GL_VERTEX_SHADER);
    planarVideoFragmentShader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (planarVideoVertexShader_ == 0 || planarVideoFragmentShader_ == 0) {
        return false;
    }
    glShaderSource(planarVideoVertexShader_, 1, &kVertexSource, nullptr);
    glCompileShader(planarVideoVertexShader_);
    GLint compileStatus = 0;
    glGetShaderiv(planarVideoVertexShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }
    glShaderSource(planarVideoFragmentShader_, 1, &kFragmentSource, nullptr);
    glCompileShader(planarVideoFragmentShader_);
    glGetShaderiv(planarVideoFragmentShader_, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        return false;
    }

    planarVideoProgram_ = glCreateProgram();
    if (planarVideoProgram_ == 0) {
        return false;
    }
    glAttachShader(planarVideoProgram_, planarVideoVertexShader_);
    glAttachShader(planarVideoProgram_, planarVideoFragmentShader_);
    glLinkProgram(planarVideoProgram_);
    GLint linkStatus = 0;
    glGetProgramiv(planarVideoProgram_, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        return false;
    }

    planarVideoPositionLocation_ = glGetAttribLocation(planarVideoProgram_, "a_position");
    planarVideoUvLocation_ = glGetAttribLocation(planarVideoProgram_, "a_texCoord");
    planarVideoYTextureLocation_ = glGetUniformLocation(planarVideoProgram_, "u_yTexture");
    planarVideoUTextureLocation_ = glGetUniformLocation(planarVideoProgram_, "u_uTexture");
    planarVideoVTextureLocation_ = glGetUniformLocation(planarVideoProgram_, "u_vTexture");
    planarVideoOpacityLocation_ = glGetUniformLocation(planarVideoProgram_, "u_opacity");
    return planarVideoPositionLocation_ >= 0
        && planarVideoUvLocation_ >= 0
        && planarVideoYTextureLocation_ >= 0
        && planarVideoUTextureLocation_ >= 0
        && planarVideoVTextureLocation_ >= 0;
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
        return false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_2D, videoYTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frameSize.width(), frameSize.height(), 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, videoUvTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_LUMINANCE_ALPHA,
        qMax(1, frameSize.width() / 2),
        qMax(1, frameSize.height() / 2),
        0,
        GL_LUMINANCE_ALPHA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
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
        return false;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_2D, planarVideoYTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frameSize.width(), frameSize.height(), 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, planarVideoUTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_LUMINANCE,
        qMax(1, frameSize.width() / 2),
        qMax(1, frameSize.height() / 2),
        0,
        GL_LUMINANCE,
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
        GL_LUMINANCE,
        qMax(1, frameSize.width() / 2),
        qMax(1, frameSize.height() / 2),
        0,
        GL_LUMINANCE,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
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
