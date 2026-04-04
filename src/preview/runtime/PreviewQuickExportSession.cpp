#include "preview/runtime/PreviewQuickExportSession.h"

#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>

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
            destroyFramebuffer();
            quickWindow_->releaseResources();
            renderControl_->invalidate();
            context_->doneCurrent();
        } else {
            destroyFramebuffer();
        }
    } else {
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
    if (!isInitialized()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export session is not initialized");
        }
        return QImage();
    }
    if (!context_->makeCurrent(offscreenSurface_)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to make Quick export context current for rendering");
        }
        return QImage();
    }
    if (!ensureFramebuffer(errorMessage)) {
        context_->doneCurrent();
        return QImage();
    }

    applyFrameSize();
    applyFrameState();

    if (!framebuffer_->bind()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to bind Quick export framebuffer");
        }
        context_->doneCurrent();
        return QImage();
    }
    QOpenGLFunctions* gl = context_->functions();
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

    QElapsedTimer readbackTimer;
    readbackTimer.start();
    const QImage frame = framebuffer_->toImage(true);
    lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();
    context_->doneCurrent();
    return frame;
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

void PreviewQuickExportSession::destroyFramebuffer()
{
    if (quickWindow_ != nullptr && framebuffer_ != nullptr) {
        quickWindow_->setRenderTarget(QQuickRenderTarget());
    }
    delete framebuffer_;
    framebuffer_ = nullptr;
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
