#include "preview/runtime/PreviewQuickExportSession.h"

#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>

#include <cstring>

PreviewQuickExportSession::PreviewQuickExportSession(QObject* parent)
    : QObject(parent)
{
}

PreviewQuickExportSession::~PreviewQuickExportSession()
{
    invalidate();
}

void PreviewQuickExportSession::setFrameState(const miacode::preview::scene::PreviewFrameState& state)
{
    frameState_ = state;
    applyFrameState();
}

void PreviewQuickExportSession::setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags)
{
    if (layerFlags_ == layerFlags) {
        return;
    }
    layerFlags_ = layerFlags;
    applyFrameState();
}

void PreviewQuickExportSession::setFrameSize(const QSize& size)
{
    const QSize safeSize(qMax(1, size.width()), qMax(1, size.height()));
    if (frameSize_ == safeSize) {
        return;
    }
    frameSize_ = safeSize;
    applyFrameSize();
    destroyFramebuffer();
}

bool PreviewQuickExportSession::initialize(
    const QSurfaceFormat& requestedFormat,
    QOpenGLContext* shareContext,
    QString* errorMessage
)
{
    if (isInitialized()) {
        return true;
    }

    requestedFormat_ = requestedFormat;
    shareContext_ = shareContext;

    QSurfaceFormat surfaceFormat = requestedFormat_;
    if (shareContext_ != nullptr && shareContext_->isValid()) {
        surfaceFormat = shareContext_->format();
    }
    if (surfaceFormat.renderableType() == QSurfaceFormat::DefaultRenderableType) {
        surfaceFormat = QSurfaceFormat::defaultFormat();
    }

    offscreenSurface_ = new QOffscreenSurface();
    offscreenSurface_->setFormat(surfaceFormat);
    offscreenSurface_->create();
    if (!offscreenSurface_->isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to create Quick export offscreen surface");
        }
        invalidate();
        return false;
    }

    context_ = new QOpenGLContext();
    context_->setFormat(offscreenSurface_->format());
    if (shareContext_ != nullptr && shareContext_->isValid()) {
        context_->setShareContext(shareContext_);
    }
    if (!context_->create()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to create Quick export OpenGL context");
        }
        invalidate();
        return false;
    }
    if (!context_->makeCurrent(offscreenSurface_)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to make Quick export OpenGL context current");
        }
        invalidate();
        return false;
    }

    renderControl_ = new QQuickRenderControl();
    quickWindow_ = new QQuickWindow(renderControl_);
    quickWindow_->setColor(Qt::transparent);
    quickWindow_->setPersistentGraphics(true);
    quickWindow_->setPersistentSceneGraph(true);
    quickWindow_->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(context_));

    if (!renderControl_->initialize()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to initialize Quick export render control");
        }
        context_->doneCurrent();
        invalidate();
        return false;
    }

    rootItem_ = new QQuickItem(quickWindow_->contentItem());

    sceneRoot_ = new PreviewQuickSceneRoot(rootItem_);
    sceneRoot_->setZ(0.0);
    sceneRoot_->setFrameState(&frameState_);

    hudLayer_ = new PreviewQuickHudLayer(rootItem_);
    hudLayer_->setZ(1.0);
    hudLayer_->setFrameState(&frameState_);

    applyFrameSize();
    applyFrameState();

    const bool framebufferReady = ensureFramebuffer(errorMessage);
    context_->doneCurrent();
    if (!framebufferReady) {
        invalidate();
        return false;
    }
    return true;
}

void PreviewQuickExportSession::invalidate()
{
    if (context_ != nullptr && offscreenSurface_ != nullptr && quickWindow_ != nullptr && renderControl_ != nullptr) {
        if (context_->makeCurrent(offscreenSurface_)) {
            destroyOffscreenReadbackPbos();
            destroyFramebuffer();
            quickWindow_->releaseResources();
            renderControl_->invalidate();
            context_->doneCurrent();
        } else {
            destroyOffscreenReadbackPbos();
            destroyFramebuffer();
        }
    } else {
        destroyOffscreenReadbackPbos();
        destroyFramebuffer();
    }

    delete quickWindow_;
    quickWindow_ = nullptr;
    rootItem_ = nullptr;
    sceneRoot_ = nullptr;
    hudLayer_ = nullptr;

    delete renderControl_;
    renderControl_ = nullptr;

    delete context_;
    context_ = nullptr;

    delete offscreenSurface_;
    offscreenSurface_ = nullptr;

    lastRenderStats_ = PreviewQuickExportRenderStats();
}

bool PreviewQuickExportSession::isInitialized() const
{
    return renderControl_ != nullptr
        && quickWindow_ != nullptr
        && context_ != nullptr
        && offscreenSurface_ != nullptr;
}

QImage PreviewQuickExportSession::renderFrame(QString* errorMessage)
{
    if (!ensureContextCurrent(errorMessage)) {
        return QImage();
    }
    if (!ensureFramebuffer(errorMessage)) {
        context_->doneCurrent();
        return QImage();
    }
    if (!renderSceneIntoFramebuffer(errorMessage)) {
        context_->doneCurrent();
        return QImage();
    }

    QElapsedTimer readbackTimer;
    readbackTimer.start();
    const QImage frame = framebuffer_->toImage(true);
    lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();
    context_->doneCurrent();
    return frame;
}

bool PreviewQuickExportSession::supportsOffscreenPboReadback(QString* errorMessage) const
{
    const bool needMakeCurrent =
        context_ != nullptr
        && offscreenSurface_ != nullptr
        && QOpenGLContext::currentContext() != context_;
    if (needMakeCurrent && !context_->makeCurrent(offscreenSurface_)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to make Quick export context current for PBO detection");
        }
        return false;
    }

    QOpenGLContext* activeContext = context_ != nullptr ? context_ : QOpenGLContext::currentContext();
    if (activeContext == nullptr) {
        if (needMakeCurrent) {
            context_->doneCurrent();
        }
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export OpenGL context is unavailable");
        }
        return false;
    }

    const QSurfaceFormat format = activeContext->format();
    const bool versionSupported =
        format.majorVersion() > 2 || (format.majorVersion() == 2 && format.minorVersion() >= 1);
    if (versionSupported || activeContext->hasExtension(QByteArrayLiteral("GL_ARB_pixel_buffer_object"))) {
        if (needMakeCurrent) {
            context_->doneCurrent();
        }
        return true;
    }
    if (needMakeCurrent) {
        context_->doneCurrent();
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("OpenGL context does not expose pixel pack buffer support");
    }
    return false;
}

void PreviewQuickExportSession::resetOffscreenPboReadback()
{
    if (context_ != nullptr && offscreenSurface_ != nullptr && context_->makeCurrent(offscreenSurface_)) {
        destroyOffscreenReadbackPbos();
        context_->doneCurrent();
        return;
    }
    destroyOffscreenReadbackPbos();
}

bool PreviewQuickExportSession::renderFramePboStep(
    QImage* completedFrame,
    bool* completedFrameReady,
    bool drainOnly,
    QString* errorMessage)
{
    if (completedFrame != nullptr) {
        *completedFrame = QImage();
    }
    if (completedFrameReady != nullptr) {
        *completedFrameReady = false;
    }
    lastRenderStats_ = PreviewQuickExportRenderStats();

    if (!ensureContextCurrent(errorMessage)) {
        return false;
    }
    const QSize pixelSize = framebufferPixelSize();
    if (!ensureFramebuffer(errorMessage) || !ensureOffscreenReadbackPbos(pixelSize, errorMessage)) {
        context_->doneCurrent();
        return false;
    }

    if (drainOnly) {
        if (offscreenReadbackPendingIndex_ >= 0) {
            QElapsedTimer readbackTimer;
            readbackTimer.start();
            QImage drainedFrame;
            if (!mapOffscreenReadbackPbo(offscreenReadbackPendingIndex_, pixelSize, &drainedFrame, errorMessage)) {
                context_->doneCurrent();
                return false;
            }
            lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();
            offscreenReadbackPendingIndex_ = -1;
            if (completedFrame != nullptr) {
                *completedFrame = std::move(drainedFrame);
            }
            if (completedFrameReady != nullptr) {
                *completedFrameReady = completedFrame != nullptr && !completedFrame->isNull();
            }
        }
        context_->doneCurrent();
        return true;
    }

    if (!renderSceneIntoFramebuffer(errorMessage)) {
        context_->doneCurrent();
        return false;
    }

    QOpenGLExtraFunctions* extra = context_ != nullptr ? context_->extraFunctions() : nullptr;
    if (extra == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("OpenGL extra functions are unavailable for PBO readback");
        }
        context_->doneCurrent();
        return false;
    }

    const int writeIndex = offscreenReadbackPboWriteIndex_;
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, offscreenReadbackPbos_[writeIndex]);
    extra->glReadPixels(0, 0, pixelSize.width(), pixelSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    if (offscreenReadbackPendingIndex_ >= 0) {
        QElapsedTimer readbackTimer;
        readbackTimer.start();
        QImage readyFrame;
        if (!mapOffscreenReadbackPbo(offscreenReadbackPendingIndex_, pixelSize, &readyFrame, errorMessage)) {
            context_->doneCurrent();
            return false;
        }
        lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();
        if (completedFrame != nullptr) {
            *completedFrame = std::move(readyFrame);
        }
        if (completedFrameReady != nullptr) {
            *completedFrameReady = completedFrame != nullptr && !completedFrame->isNull();
        }
    }

    offscreenReadbackPendingIndex_ = writeIndex;
    offscreenReadbackPboWriteIndex_ = (writeIndex + 1) % 2;
    context_->doneCurrent();
    return true;
}

bool PreviewQuickExportSession::ensureFramebuffer(QString* errorMessage)
{
    if (!isInitialized()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export session is not initialized");
        }
        return false;
    }

    const QSize pixelSize = framebufferPixelSize();
    if (pixelSize.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export frame size is invalid");
        }
        return false;
    }
    if (framebuffer_ != nullptr && framebuffer_->size() == pixelSize) {
        return true;
    }

    destroyFramebuffer();

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setInternalTextureFormat(GL_RGBA8);
    framebuffer_ = new QOpenGLFramebufferObject(pixelSize, format);
    if (framebuffer_ == nullptr || !framebuffer_->isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to allocate Quick export framebuffer");
        }
        destroyFramebuffer();
        return false;
    }

    QQuickRenderTarget target = QQuickRenderTarget::fromOpenGLTexture(framebuffer_->texture(), pixelSize);
    target.setDevicePixelRatio(1.0);
    quickWindow_->setRenderTarget(target);
    return true;
}

bool PreviewQuickExportSession::ensureOffscreenReadbackPbos(const QSize& imageSize, QString* errorMessage)
{
    const QSize safeSize(qMax(1, imageSize.width()), qMax(1, imageSize.height()));
    const qsizetype byteCount = static_cast<qsizetype>(safeSize.width()) * safeSize.height() * 4;
    if (offscreenReadbackPbos_[0] != 0
        && offscreenReadbackPbos_[1] != 0
        && offscreenReadbackPboSize_ == safeSize
        && offscreenReadbackPboBytes_ == byteCount) {
        return true;
    }
    if (!supportsOffscreenPboReadback(errorMessage)) {
        return false;
    }

    QOpenGLExtraFunctions* extra = context_ != nullptr ? context_->extraFunctions() : nullptr;
    if (extra == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("OpenGL extra functions are unavailable for PBO readback");
        }
        return false;
    }

    destroyOffscreenReadbackPbos();
    extra->glGenBuffers(2, offscreenReadbackPbos_);
    if (offscreenReadbackPbos_[0] == 0 || offscreenReadbackPbos_[1] == 0) {
        destroyOffscreenReadbackPbos();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to allocate pixel pack buffers");
        }
        return false;
    }

    for (GLuint pboId : offscreenReadbackPbos_) {
        extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, pboId);
        extra->glBufferData(GL_PIXEL_PACK_BUFFER, byteCount, nullptr, GL_STREAM_READ);
    }
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    offscreenReadbackPboSize_ = safeSize;
    offscreenReadbackPboBytes_ = byteCount;
    offscreenReadbackPboWriteIndex_ = 0;
    offscreenReadbackPendingIndex_ = -1;
    return true;
}

bool PreviewQuickExportSession::mapOffscreenReadbackPbo(
    int pboIndex,
    const QSize& imageSize,
    QImage* frame,
    QString* errorMessage)
{
    if (frame == nullptr) {
        return false;
    }
    if (pboIndex < 0 || pboIndex >= 2 || offscreenReadbackPbos_[pboIndex] == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid Quick export readback PBO index");
        }
        return false;
    }

    QOpenGLExtraFunctions* extra = context_ != nullptr ? context_->extraFunctions() : nullptr;
    if (extra == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("OpenGL extra functions are unavailable while mapping PBO");
        }
        return false;
    }

    const QSize safeSize(qMax(1, imageSize.width()), qMax(1, imageSize.height()));
    const qsizetype bytesPerRow = static_cast<qsizetype>(safeSize.width()) * 4;
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, offscreenReadbackPbos_[pboIndex]);
    void* mapped = extra->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, offscreenReadbackPboBytes_, GL_MAP_READ_BIT);
    if (mapped == nullptr) {
        extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to map Quick export readback PBO");
        }
        return false;
    }

    QImage output(safeSize, QImage::Format_RGBA8888);
    const uchar* sourceBytes = static_cast<const uchar*>(mapped);
    for (int row = 0; row < safeSize.height(); ++row) {
        const int sourceRow = safeSize.height() - 1 - row;
        std::memcpy(
            output.scanLine(row),
            sourceBytes + (static_cast<qsizetype>(sourceRow) * bytesPerRow),
            bytesPerRow
        );
    }
    extra->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    *frame = std::move(output);
    return true;
}

bool PreviewQuickExportSession::renderSceneIntoFramebuffer(QString* errorMessage)
{
    applyFrameSize();
    applyFrameState();

    if (!framebuffer_->bind()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to bind Quick export framebuffer");
        }
        return false;
    }

    QOpenGLFunctions* gl = context_ != nullptr ? context_->functions() : nullptr;
    if (gl == nullptr) {
        framebuffer_->release();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export OpenGL functions are unavailable");
        }
        return false;
    }

    gl->glViewport(0, 0, framebuffer_->width(), framebuffer_->height());
    gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    framebuffer_->release();

    QElapsedTimer renderTimer;
    renderTimer.start();
    renderControl_->polishItems();
    renderControl_->beginFrame();
    renderControl_->sync();
    renderControl_->render();
    renderControl_->endFrame();
    gl->glFlush();
    lastRenderStats_.renderNs = renderTimer.nsecsElapsed();
    return true;
}

bool PreviewQuickExportSession::ensureContextCurrent(QString* errorMessage)
{
    if (!isInitialized()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export session is not initialized");
        }
        return false;
    }
    if (!context_->makeCurrent(offscreenSurface_)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to make Quick export context current for rendering");
        }
        return false;
    }
    return true;
}

void PreviewQuickExportSession::destroyFramebuffer()
{
    if (quickWindow_ != nullptr && framebuffer_ != nullptr) {
        quickWindow_->setRenderTarget(QQuickRenderTarget());
    }
    delete framebuffer_;
    framebuffer_ = nullptr;
}

void PreviewQuickExportSession::destroyOffscreenReadbackPbos()
{
    QOpenGLExtraFunctions* extra = context_ != nullptr ? context_->extraFunctions() : nullptr;
    if (extra != nullptr && (offscreenReadbackPbos_[0] != 0 || offscreenReadbackPbos_[1] != 0)) {
        extra->glDeleteBuffers(2, offscreenReadbackPbos_);
    }
    offscreenReadbackPbos_[0] = 0;
    offscreenReadbackPbos_[1] = 0;
    offscreenReadbackPboSize_ = QSize();
    offscreenReadbackPboBytes_ = 0;
    offscreenReadbackPboWriteIndex_ = 0;
    offscreenReadbackPendingIndex_ = -1;
}

void PreviewQuickExportSession::applyFrameSize()
{
    if (quickWindow_ == nullptr || rootItem_ == nullptr || sceneRoot_ == nullptr || hudLayer_ == nullptr) {
        return;
    }

    quickWindow_->setGeometry(0, 0, frameSize_.width(), frameSize_.height());
    quickWindow_->contentItem()->setSize(QSizeF(frameSize_));

    rootItem_->setPosition(QPointF(0.0, 0.0));
    rootItem_->setSize(QSizeF(frameSize_));

    sceneRoot_->setPosition(QPointF(0.0, 0.0));
    sceneRoot_->setSize(QSizeF(frameSize_));
    sceneRoot_->update();

    hudLayer_->setPosition(QPointF(0.0, 0.0));
    hudLayer_->setSize(QSizeF(frameSize_));
    hudLayer_->update();
}

void PreviewQuickExportSession::applyFrameState()
{
    if (sceneRoot_ != nullptr) {
        sceneRoot_->setFrameState(&frameState_);
        sceneRoot_->setLayerFlags(layerFlags_);
    }
    if (hudLayer_ != nullptr) {
        hudLayer_->setFrameState(&frameState_);
        hudLayer_->setLayerFlags(layerFlags_);
    }
}

QSize PreviewQuickExportSession::framebufferPixelSize() const
{
    return QSize(qMax(1, frameSize_.width()), qMax(1, frameSize_.height()));
}
