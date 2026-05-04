#include "preview/runtime/PreviewQuickExportSession.h"

#include "common/DebugLog.h"
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

namespace {

inline uchar unpremultiplyChannel(uchar channel, uchar alpha)
{
    if (alpha == 0) {
        return 0;
    }
    if (alpha == 255) {
        return channel;
    }
    return static_cast<uchar>(qBound(0, (static_cast<int>(channel) * 255 + alpha / 2) / alpha, 255));
}

qsizetype readbackByteCount(const QSize& imageSize)
{
    return static_cast<qsizetype>(qMax(1, imageSize.width()))
        * static_cast<qsizetype>(qMax(1, imageSize.height()))
        * 4;
}

void appendExportSessionLog(const QString& scope, const QString& detail = QString())
{
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Export, scope, detail);
}

QString glEnumHex(GLenum value)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16);
}

bool isOpenGlAtLeast(const QSurfaceFormat& format, int major, int minor)
{
    return format.majorVersion() > major
        || (format.majorVersion() == major && format.minorVersion() >= minor);
}

}  // namespace

PreviewQuickExportSession::PreviewQuickExportSession(QObject* parent)
    : QObject(parent)
{
}

PreviewQuickExportSession::~PreviewQuickExportSession()
{
    stopConvertWorker();
    invalidate();
}

void PreviewQuickExportSession::setFrameState(const miacode::preview::scene::PreviewFrameState& state)
{
    frameState_ = state;
    applyFrameState();
}

void PreviewQuickExportSession::applyExportFrameTick(
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud,
    bool usedGpuRendererThisFrame,
    int cpuFallbackCount,
    double fpsDisplay)
{
    frameState_.playheadSeconds = playheadSeconds;
    frameState_.render.showTimestamp = showTimestamp;
    frameState_.render.showObjectStatsHud = showObjectStatsHud;
    frameState_.usedGpuRendererThisFrame = usedGpuRendererThisFrame;
    frameState_.cpuFallbackCount = cpuFallbackCount;
    frameState_.fpsDisplay = fpsDisplay;
    requestFrameRefresh();
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
    directReadbackBuffer_.clear();
    reusableReadbackFrame_ = QImage();
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
    clearOffscreenReadbackPboState();
    clearOffscreenPboCapabilityCache();
    directReadbackBuffer_.clear();
    reusableReadbackFrame_ = QImage();
    frameStateBound_ = false;
    layerFlagsApplied_ = false;
    appliedLayerFlags_ = miacode::preview::scene::kPreviewAllRenderLayers;
    lastRenderStats_ = PreviewQuickExportRenderStats();

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
    // Force the QSG chart-render path on for the export scene graph.
    //
    // PreviewQuickSceneRoot::updatePaintNode and PreviewQuickHudLayer::paint
    // both early-return when previewDCompExclusiveEnabled() is true, because
    // for the live preview the DComp popup HWND is the authoritative chart
    // renderer and the QSG layer would just duplicate work. The export path
    // is offscreen (QQuickRenderControl + framebuffer) — there is no DComp
    // popup to render anything, so the early-return drops every chart
    // sprite from the encoded frames. Reusing the existing
    // `dcompFallbackActive` override (set by QuickShellMain.qml on the
    // secondary fullscreen surface for the same structural reason) tells
    // both items to render normally regardless of the exclusive flag.
    sceneRoot_->setDCompFallbackActive(true);

    hudLayer_ = new PreviewQuickHudLayer(rootItem_);
    hudLayer_->setZ(1.0);
    hudLayer_->setDCompFallbackActive(true);

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
    const bool needMakeCurrent =
        context_ != nullptr
        && offscreenSurface_ != nullptr
        && QOpenGLContext::currentContext() != context_;
    const bool teardownContextCurrent =
        context_ != nullptr
        && offscreenSurface_ != nullptr
        && (!needMakeCurrent || context_->makeCurrent(offscreenSurface_));
    if (teardownContextCurrent) {
        destroyOffscreenReadbackPbos();
        destroyFramebuffer();
        if (quickWindow_ != nullptr) {
            quickWindow_->releaseResources();
        }
        if (renderControl_ != nullptr) {
            renderControl_->invalidate();
        }
        if (context_ != nullptr) {
            context_->doneCurrent();
        }
    } else {
        if (framebuffer_ != nullptr
            || quickWindow_ != nullptr
            || renderControl_ != nullptr
            || offscreenReadbackPbos_[0] != 0
            || offscreenReadbackPbos_[1] != 0) {
            appendExportSessionLog(
                QStringLiteral("export_context_not_current_on_teardown"),
                QStringLiteral(
                    "trigger=invalidate makeCurrent=%1 hasContext=%2 hasSurface=%3 hasFramebuffer=%4 hasQuickWindow=%5 hasRenderControl=%6 skipExplicitGlCleanup=1"
                )
                    .arg(needMakeCurrent ? 0 : (context_ != nullptr && offscreenSurface_ != nullptr ? 1 : 0))
                    .arg(context_ != nullptr ? 1 : 0)
                    .arg(offscreenSurface_ != nullptr ? 1 : 0)
                    .arg(framebuffer_ != nullptr ? 1 : 0)
                    .arg(quickWindow_ != nullptr ? 1 : 0)
                    .arg(renderControl_ != nullptr ? 1 : 0)
            );
        }
        clearOffscreenReadbackPboState();
        framebuffer_ = nullptr;
        quickWindow_ = nullptr;
        rootItem_ = nullptr;
        sceneRoot_ = nullptr;
        hudLayer_ = nullptr;
        renderControl_ = nullptr;
        context_ = nullptr;
        offscreenSurface_ = nullptr;
        directReadbackBuffer_.clear();
        reusableReadbackFrame_ = QImage();
        clearOffscreenPboCapabilityCache();
        frameStateBound_ = false;
        layerFlagsApplied_ = false;
        appliedLayerFlags_ = miacode::preview::scene::kPreviewAllRenderLayers;
        lastRenderStats_ = PreviewQuickExportRenderStats();
        return;
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

    clearOffscreenReadbackPboState();
    clearOffscreenPboCapabilityCache();
    directReadbackBuffer_.clear();
    reusableReadbackFrame_ = QImage();
    frameStateBound_ = false;
    layerFlagsApplied_ = false;
    appliedLayerFlags_ = miacode::preview::scene::kPreviewAllRenderLayers;
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

    const QSize pixelSize = framebufferPixelSize();
    if (!ensureDirectReadbackBuffer(pixelSize, errorMessage)) {
        context_->doneCurrent();
        return QImage();
    }

    QOpenGLFunctions* gl = context_ != nullptr ? context_->functions() : nullptr;
    if (gl == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export OpenGL functions are unavailable for direct readback");
        }
        context_->doneCurrent();
        return QImage();
    }

    QElapsedTimer readbackTimer;
    readbackTimer.start();
    GLint previousPackAlignment = 4;
    gl->glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->glReadPixels(
        0,
        0,
        pixelSize.width(),
        pixelSize.height(),
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        directReadbackBuffer_.data()
    );
    gl->glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    if (!convertBottomUpPremultipliedReadbackToStraightRgba(
            reinterpret_cast<const uchar*>(directReadbackBuffer_.constData()),
            static_cast<qsizetype>(pixelSize.width()) * 4,
            pixelSize,
            &reusableReadbackFrame_,
            errorMessage)) {
        context_->doneCurrent();
        return QImage();
    }
    lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();
    context_->doneCurrent();
    return reusableReadbackFrame_;
}

bool PreviewQuickExportSession::supportsOffscreenPboReadback(QString* errorMessage) const
{
    if (offscreenPboCapabilityProbed_) {
        if (errorMessage != nullptr && !offscreenPboCapabilitySupported_) {
            *errorMessage = offscreenPboCapabilityError_;
        }
        return offscreenPboCapabilitySupported_;
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const auto finishProbe = [&](bool supported, const QString& reason, const QString& detail) {
        offscreenPboCapabilityProbed_ = true;
        offscreenPboCapabilitySupported_ = supported;
        offscreenPboCapabilityError_ = supported ? QString() : reason;
        appendExportSessionLog(QStringLiteral("pbo_capability_probe"), detail);
        if (!supported && errorMessage != nullptr) {
            *errorMessage = reason;
        }
        return supported;
    };

    if (context_ == nullptr || offscreenSurface_ == nullptr) {
        return finishProbe(
            false,
            QStringLiteral("Quick export OpenGL context is unavailable"),
            QStringLiteral(
                "version=unknown extra=0 pixelPack=0 mapRange=0 mapProc=0 extPbo=0 extMapRange=0 smoke=skipped result=disable reason=context_unavailable"
            )
        );
    }

    const bool needMakeCurrent =
        QOpenGLContext::currentContext() != context_;
    if (needMakeCurrent && !context_->makeCurrent(offscreenSurface_)) {
        return finishProbe(
            false,
            QStringLiteral("failed to make Quick export context current for PBO detection"),
            QStringLiteral(
                "version=unknown extra=0 pixelPack=0 mapRange=0 mapProc=0 extPbo=0 extMapRange=0 smoke=skipped result=disable reason=makeCurrent_failed"
            )
        );
    }

    QOpenGLFunctions* gl = context_->functions();
    QOpenGLExtraFunctions* extra = context_->extraFunctions();
    const QSurfaceFormat format = context_->format();
    const bool versionAtLeast30 = isOpenGlAtLeast(format, 3, 0);
    const bool hasPboExtension = context_->hasExtension(QByteArrayLiteral("GL_ARB_pixel_buffer_object"));
    const bool hasMapRangeExtension = context_->hasExtension(QByteArrayLiteral("GL_ARB_map_buffer_range"));
    const bool pixelPackBufferSupported = versionAtLeast30 || hasPboExtension;
    const bool mapBufferRangeSupported = versionAtLeast30 || hasMapRangeExtension;
    const bool mapBufferRangeProcAvailable =
        context_->getProcAddress("glMapBufferRange") != nullptr
        || context_->getProcAddress("glMapBufferRangeARB") != nullptr;

    QString reason;
    QString smokeProbeStatus = QStringLiteral("skipped");
    if (gl == nullptr) {
        reason = QStringLiteral("Quick export OpenGL functions are unavailable for PBO detection");
    } else if (extra == nullptr) {
        reason = QStringLiteral("OpenGL extra functions are unavailable for PBO readback");
    } else if (!pixelPackBufferSupported) {
        reason = QStringLiteral("OpenGL context does not expose pixel pack buffer support");
    } else if (!mapBufferRangeSupported) {
        reason = QStringLiteral("OpenGL context does not expose map buffer range support required for PBO readback");
    } else if (!mapBufferRangeProcAvailable) {
        reason = QStringLiteral("OpenGL context does not expose a glMapBufferRange entry point for PBO readback");
    } else {
        while (gl->glGetError() != GL_NO_ERROR) {
        }
        GLint previousPackBufferBinding = 0;
        gl->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBufferBinding);

        GLuint probeBuffer = 0;
        extra->glGenBuffers(1, &probeBuffer);
        if (probeBuffer == 0) {
            reason = QStringLiteral("failed to allocate smoke probe pixel pack buffer");
            smokeProbeStatus = QStringLiteral("glGenBuffers=0");
        } else {
            extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, probeBuffer);
            extra->glBufferData(GL_PIXEL_PACK_BUFFER, 16, nullptr, GL_STREAM_READ);
            const GLenum allocError = gl->glGetError();
            if (allocError != GL_NO_ERROR) {
                reason = QStringLiteral("failed to allocate smoke probe pixel pack buffer storage");
                smokeProbeStatus = QStringLiteral("glBufferData_error=%1").arg(glEnumHex(allocError));
            } else {
                void* mapped = extra->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 16, GL_MAP_READ_BIT);
                const GLenum mapError = gl->glGetError();
                if (mapped == nullptr || mapError != GL_NO_ERROR) {
                    reason = QStringLiteral("failed to map smoke probe pixel pack buffer");
                    smokeProbeStatus = QStringLiteral("glMapBufferRange_error=%1 mapped=%2")
                                           .arg(glEnumHex(mapError))
                                           .arg(mapped != nullptr ? 1 : 0);
                } else {
                    const bool unmapOk = extra->glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
                    const GLenum unmapError = gl->glGetError();
                    if (!unmapOk || unmapError != GL_NO_ERROR) {
                        reason = QStringLiteral("failed to unmap smoke probe pixel pack buffer");
                        smokeProbeStatus = QStringLiteral("glUnmapBuffer_error=%1 ok=%2")
                                               .arg(glEnumHex(unmapError))
                                               .arg(unmapOk ? 1 : 0);
                    } else {
                        smokeProbeStatus = QStringLiteral("pass");
                    }
                }
            }
            extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(qMax(0, previousPackBufferBinding)));
            extra->glDeleteBuffers(1, &probeBuffer);
        }
    }

    const bool supported = reason.isEmpty();
    const QString detail = QStringLiteral(
        "version=%1.%2 extra=%3 pixelPack=%4 mapRange=%5 mapProc=%6 extPbo=%7 extMapRange=%8 smoke=%9 result=%10 reason=%11"
    )
                               .arg(format.majorVersion())
                               .arg(format.minorVersion())
                               .arg(extra != nullptr ? 1 : 0)
                               .arg(pixelPackBufferSupported ? 1 : 0)
                               .arg(mapBufferRangeSupported ? 1 : 0)
                               .arg(mapBufferRangeProcAvailable ? 1 : 0)
                               .arg(hasPboExtension ? 1 : 0)
                               .arg(hasMapRangeExtension ? 1 : 0)
                               .arg(smokeProbeStatus)
                               .arg(supported ? QStringLiteral("enable") : QStringLiteral("disable"))
                               .arg(supported ? QStringLiteral("ok") : reason);
    if (needMakeCurrent) {
        context_->doneCurrent();
    }
    return finishProbe(supported, reason, detail);
}

void PreviewQuickExportSession::resetOffscreenPboReadback()
{
    const bool needMakeCurrent =
        context_ != nullptr
        && offscreenSurface_ != nullptr
        && QOpenGLContext::currentContext() != context_;
    if (context_ != nullptr
        && offscreenSurface_ != nullptr
        && (!needMakeCurrent || context_->makeCurrent(offscreenSurface_))) {
        destroyOffscreenReadbackPbos();
        if (needMakeCurrent) {
            context_->doneCurrent();
        }
        return;
    }
    if (offscreenReadbackPbos_[0] != 0 || offscreenReadbackPbos_[1] != 0) {
        appendExportSessionLog(
            QStringLiteral("pbo_cleanup_deferred"),
            QStringLiteral("trigger=reset makeCurrent=%1 skipExplicitDelete=1")
                .arg(needMakeCurrent ? 0 : (context_ != nullptr && offscreenSurface_ != nullptr ? 1 : 0))
        );
    }
    clearOffscreenReadbackPboState();
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

    // Pipeline:
    //   prior_worker_output  ──┐
    //                          │  (returned to caller this tick)
    //   prev PBO ── memcpy ────┼─→ worker (running in parallel with the
    //                          │   next render's GL command issue)
    //   render N + glReadPixels┘
    //
    // Two pending slots in steady state: one frame queued in a PBO
    // waiting for GPU readback, and one frame in the convert worker
    // waiting on the CPU. drainOnly retires both, one per call.
    QElapsedTimer readbackTimer;
    readbackTimer.start();
    QImage workerOutput;
    bool hadWorkerOutput = false;
    if (convertJobInFlight_) {
        QString workerErr;
        if (!waitForPendingConvertJob(&workerOutput, &workerErr)) {
            if (errorMessage != nullptr) {
                *errorMessage = workerErr;
            }
            context_->doneCurrent();
            return false;
        }
        hadWorkerOutput = !workerOutput.isNull();
    }

    if (drainOnly) {
        if (offscreenReadbackPendingIndex_ >= 0) {
            // Promote the queued PBO into a worker job. The next
            // drainOnly call will return this frame's converted output.
            if (!mapPboAndSubmitConvertJob(offscreenReadbackPendingIndex_, pixelSize, errorMessage)) {
                context_->doneCurrent();
                return false;
            }
            offscreenReadbackPendingIndex_ = -1;
        }
        lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();
        if (hadWorkerOutput) {
            if (completedFrame != nullptr) {
                *completedFrame = std::move(workerOutput);
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

    QOpenGLFunctions* gl = context_ != nullptr ? context_->functions() : nullptr;
    QOpenGLExtraFunctions* extra = context_ != nullptr ? context_->extraFunctions() : nullptr;
    if (gl == nullptr || extra == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("OpenGL functions are unavailable for PBO readback");
        }
        context_->doneCurrent();
        return false;
    }

    GLint previousPackAlignment = 4;
    gl->glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const int writeIndex = offscreenReadbackPboWriteIndex_;
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, offscreenReadbackPbos_[writeIndex]);
    extra->glReadPixels(0, 0, pixelSize.width(), pixelSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    if (offscreenReadbackPendingIndex_ >= 0) {
        if (!mapPboAndSubmitConvertJob(offscreenReadbackPendingIndex_, pixelSize, errorMessage)) {
            context_->doneCurrent();
            return false;
        }
    }
    lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();

    offscreenReadbackPendingIndex_ = writeIndex;
    offscreenReadbackPboWriteIndex_ = (writeIndex + 1) % 2;
    context_->doneCurrent();

    if (hadWorkerOutput) {
        if (completedFrame != nullptr) {
            *completedFrame = std::move(workerOutput);
        }
        if (completedFrameReady != nullptr) {
            *completedFrameReady = completedFrame != nullptr && !completedFrame->isNull();
        }
    }
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

bool PreviewQuickExportSession::ensureDirectReadbackBuffer(const QSize& imageSize, QString* errorMessage)
{
    const QSize safeSize(qMax(1, imageSize.width()), qMax(1, imageSize.height()));
    const qsizetype byteCount = readbackByteCount(safeSize);
    if (directReadbackBuffer_.size() != byteCount) {
        directReadbackBuffer_.resize(byteCount);
    }
    if (directReadbackBuffer_.size() != byteCount) {
        directReadbackBuffer_.clear();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to allocate Quick export direct readback buffer");
        }
        return false;
    }
    return true;
}

bool PreviewQuickExportSession::convertBottomUpPremultipliedReadbackToStraightRgba(
    const uchar* sourceBytes,
    qsizetype sourceBytesPerRow,
    const QSize& imageSize,
    QImage* frame,
    QString* errorMessage)
{
    if (frame == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid Quick export readback output image");
        }
        return false;
    }
    if (sourceBytes == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export readback source bytes are null");
        }
        return false;
    }

    const QSize safeSize(qMax(1, imageSize.width()), qMax(1, imageSize.height()));
    const qsizetype expectedBytesPerRow = static_cast<qsizetype>(safeSize.width()) * 4;
    if (sourceBytesPerRow < expectedBytesPerRow) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Quick export readback row stride is smaller than RGBA output width");
        }
        return false;
    }
    if (frame->isNull()
        || frame->size() != safeSize
        || frame->format() != QImage::Format_RGBA8888) {
        *frame = QImage(safeSize, QImage::Format_RGBA8888);
    }
    if (frame->isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to allocate Quick export straight RGBA frame buffer");
        }
        return false;
    }

    for (int row = 0; row < safeSize.height(); ++row) {
        const int sourceRow = safeSize.height() - 1 - row;
        const uchar* sourceRowBytes =
            sourceBytes + static_cast<qsizetype>(sourceRow) * sourceBytesPerRow;
        uchar* outputRow = frame->scanLine(row);
        for (int x = 0; x < safeSize.width(); ++x) {
            const uchar* src = sourceRowBytes + x * 4;
            uchar* dst = outputRow + x * 4;
            const uchar alpha = src[3];
            dst[3] = alpha;
            if (alpha == 0) {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                continue;
            }
            if (alpha == 255) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                continue;
            }
            dst[0] = unpremultiplyChannel(src[0], alpha);
            dst[1] = unpremultiplyChannel(src[1], alpha);
            dst[2] = unpremultiplyChannel(src[2], alpha);
        }
    }
    return true;
}

// Convert worker. Pulls a (sourceBytes, size) handoff out of the
// producer-set fields under convertMutex_, runs the existing
// convertBottomUpPremultipliedReadbackToStraightRgba into the worker's
// own QImage, and signals back. The producer guarantees exclusive
// access to the staging buffer between submit and wait, so the worker
// can read sourceBytes lock-free during the conversion itself.
void PreviewQuickExportSession::convertWorkerLoop()
{
    while (true) {
        const uchar* sourceBytes = nullptr;
        qsizetype sourceBytesPerRow = 0;
        QSize size;
        {
            std::unique_lock<std::mutex> lock(convertMutex_);
            convertSubmitCv_.wait(lock, [this]() {
                return convertJobPending_ || convertStop_.load(std::memory_order_acquire);
            });
            if (convertStop_.load(std::memory_order_acquire)) {
                return;
            }
            sourceBytes = convertJobInputBytes_;
            sourceBytesPerRow = convertJobInputBytesPerRow_;
            size = convertJobImageSize_;
            convertJobPending_ = false;
        }
        QImage output;
        QString err;
        const bool ok = convertBottomUpPremultipliedReadbackToStraightRgba(
            sourceBytes, sourceBytesPerRow, size, &output, &err);
        {
            std::lock_guard<std::mutex> lock(convertMutex_);
            convertJobOutputFrame_ = std::move(output);
            convertJobSucceeded_ = ok;
            convertJobErrorMessage_ = err;
            convertJobDone_ = true;
        }
        convertDoneCv_.notify_one();
    }
}

void PreviewQuickExportSession::startConvertWorkerIfNeeded()
{
    if (convertThread_.joinable()) {
        return;
    }
    convertStop_.store(false, std::memory_order_release);
    convertJobPending_ = false;
    convertJobDone_ = false;
    convertJobInFlight_ = false;
    convertJobSucceeded_ = false;
    convertJobErrorMessage_.clear();
    convertJobOutputFrame_ = QImage();
    convertThread_ = std::thread([this]() { convertWorkerLoop(); });
}

void PreviewQuickExportSession::stopConvertWorker()
{
    if (!convertThread_.joinable()) {
        return;
    }
    convertStop_.store(true, std::memory_order_release);
    convertSubmitCv_.notify_all();
    convertThread_.join();
    convertJobPending_ = false;
    convertJobDone_ = false;
    convertJobInFlight_ = false;
    convertJobOutputFrame_ = QImage();
}

// Producer-side: map the queued PBO into CPU memory, copy its bytes
// into the worker's staging buffer (the GL mapping must be released
// before the next renderFramePboStep so the GPU can reuse the PBO),
// and submit the convert job. The worker runs in parallel with the
// next frame's render-command issue + glReadPixels enqueue, and the
// producer waits on its result one iteration later. Net effect: the
// per-frame readback path becomes max(render, convert) instead of
// render+convert, with one extra frame of pipeline depth.
bool PreviewQuickExportSession::mapPboAndSubmitConvertJob(
    int pboIndex,
    const QSize& imageSize,
    QString* errorMessage)
{
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
    const qsizetype byteCount = bytesPerRow * static_cast<qsizetype>(safeSize.height());
    if (pboStagingBuffer_.size() < byteCount) {
        pboStagingBuffer_.resize(byteCount);
        if (pboStagingBuffer_.size() < byteCount) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("failed to allocate Quick export PBO staging buffer");
            }
            return false;
        }
    }

    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, offscreenReadbackPbos_[pboIndex]);
    void* mapped = extra->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, offscreenReadbackPboBytes_, GL_MAP_READ_BIT);
    if (mapped == nullptr) {
        extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to map Quick export readback PBO");
        }
        return false;
    }
    std::memcpy(pboStagingBuffer_.data(), mapped, static_cast<size_t>(byteCount));
    const bool unmapOk = extra->glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!unmapOk) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to unmap Quick export readback PBO");
        }
        return false;
    }

    startConvertWorkerIfNeeded();
    {
        std::lock_guard<std::mutex> lock(convertMutex_);
        convertJobInputBytes_ = reinterpret_cast<const uchar*>(pboStagingBuffer_.constData());
        convertJobInputBytesPerRow_ = bytesPerRow;
        convertJobImageSize_ = safeSize;
        convertJobOutputFrame_ = QImage();
        convertJobSucceeded_ = false;
        convertJobErrorMessage_.clear();
        convertJobPending_ = true;
        convertJobDone_ = false;
        convertJobInFlight_ = true;
    }
    convertSubmitCv_.notify_one();
    return true;
}

bool PreviewQuickExportSession::waitForPendingConvertJob(QImage* output, QString* errorMessage)
{
    std::unique_lock<std::mutex> lock(convertMutex_);
    if (!convertJobInFlight_) {
        if (output != nullptr) {
            *output = QImage();
        }
        return true;
    }
    convertDoneCv_.wait(lock, [this]() { return convertJobDone_; });
    convertJobDone_ = false;
    convertJobInFlight_ = false;
    if (!convertJobSucceeded_) {
        if (errorMessage != nullptr) {
            *errorMessage = convertJobErrorMessage_;
        }
        if (output != nullptr) {
            *output = QImage();
        }
        return false;
    }
    if (output != nullptr) {
        *output = std::move(convertJobOutputFrame_);
    }
    convertJobOutputFrame_ = QImage();
    return true;
}

bool PreviewQuickExportSession::ensureOffscreenReadbackPbos(const QSize& imageSize, QString* errorMessage)
{
    const QSize safeSize(qMax(1, imageSize.width()), qMax(1, imageSize.height()));
    const qsizetype byteCount = readbackByteCount(safeSize);
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

    const uchar* sourceBytes = static_cast<const uchar*>(mapped);
    const bool normalized = convertBottomUpPremultipliedReadbackToStraightRgba(
        sourceBytes,
        bytesPerRow,
        safeSize,
        &reusableReadbackFrame_,
        errorMessage
    );
    const bool unmapOk = extra->glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
    extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!normalized) {
        return false;
    }
    if (!unmapOk) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to unmap Quick export readback PBO");
        }
        return false;
    }
    *frame = reusableReadbackFrame_;
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
    if (extra != nullptr
        && QOpenGLContext::currentContext() == context_
        && (offscreenReadbackPbos_[0] != 0 || offscreenReadbackPbos_[1] != 0)) {
        extra->glDeleteBuffers(2, offscreenReadbackPbos_);
    }
    clearOffscreenReadbackPboState();
}

void PreviewQuickExportSession::clearOffscreenReadbackPboState()
{
    offscreenReadbackPbos_[0] = 0;
    offscreenReadbackPbos_[1] = 0;
    offscreenReadbackPboSize_ = QSize();
    offscreenReadbackPboBytes_ = 0;
    offscreenReadbackPboWriteIndex_ = 0;
    offscreenReadbackPendingIndex_ = -1;
}

void PreviewQuickExportSession::clearOffscreenPboCapabilityCache()
{
    offscreenPboCapabilityProbed_ = false;
    offscreenPboCapabilitySupported_ = false;
    offscreenPboCapabilityError_.clear();
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
    bindFrameStateIfNeeded();
    applyLayerFlagsIfNeeded();
    requestFrameRefresh();
}

void PreviewQuickExportSession::bindFrameStateIfNeeded()
{
    if (frameStateBound_ || sceneRoot_ == nullptr || hudLayer_ == nullptr) {
        return;
    }

    sceneRoot_->setFrameState(&frameState_);
    hudLayer_->setFrameState(&frameState_);
    frameStateBound_ = true;
}

void PreviewQuickExportSession::applyLayerFlagsIfNeeded()
{
    if (sceneRoot_ == nullptr || hudLayer_ == nullptr) {
        return;
    }
    if (layerFlagsApplied_ && appliedLayerFlags_ == layerFlags_) {
        return;
    }

    sceneRoot_->setLayerFlags(layerFlags_);
    hudLayer_->setLayerFlags(layerFlags_);
    appliedLayerFlags_ = layerFlags_;
    layerFlagsApplied_ = true;
}

void PreviewQuickExportSession::requestFrameRefresh()
{
    if (sceneRoot_ != nullptr) {
        sceneRoot_->update();
    }
    if (hudLayer_ != nullptr) {
        hudLayer_->update();
    }
}

QSize PreviewQuickExportSession::framebufferPixelSize() const
{
    return QSize(qMax(1, frameSize_.width()), qMax(1, frameSize_.height()));
}
