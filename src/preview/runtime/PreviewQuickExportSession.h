#pragma once

#include <QByteArray>
#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QSurfaceFormat>
#include <QUrl>
#include <QVariantMap>
#include <QtGui/qopengl.h>

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewLayerOrder.h"

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QQmlEngine;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class PreviewQuickHudLayer;
class PreviewQuickSceneRoot;

struct PreviewQuickExportRenderStats {
    qint64 renderNs = 0;
    qint64 readbackNs = 0;
};

class PreviewQuickExportSession : public QObject
{
    Q_OBJECT

public:
    explicit PreviewQuickExportSession(QObject* parent = nullptr);
    ~PreviewQuickExportSession() override;

    void setFrameState(const miacode::preview::scene::PreviewFrameState& state);
    void applyExportFrameTick(
        double playheadSeconds,
        bool showTimestamp,
        bool showObjectStatsHud,
        bool usedGpuRendererThisFrame,
        int cpuFallbackCount,
        double fpsDisplay,
        double hudPlayheadSecondsOverride = std::numeric_limits<double>::quiet_NaN()
    );
    const miacode::preview::scene::PreviewFrameState& frameState() const { return frameState_; }
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags() const { return layerFlags_; }

    void setFrameSize(const QSize& size);
    QSize frameSize() const { return frameSize_; }

    // Intro overlay (pre-roll maimai track-start). Loads a QML scene from the
    // given qrc/file URL and mounts it above the chart + HUD. Banner data is
    // generic (no tools/video_export dependency); the caller maps its own spec
    // into the QVariantMap track + URLs. The overlay is hidden unless the most
    // recent setIntroFrame() set it active, so normal frames are unaffected.
    bool setupIntroOverlay(const QUrl& qmlUrl, QString* errorMessage = nullptr);
    // `style` carries optional IntroOverlay root properties (backdropImage url,
    // backdropBlurEnabled, cardShadowEnabled); keys are applied verbatim via
    // setProperty, so the contract stays QML-side.
    void setIntroBannerData(
        const QVariantMap& bannerTrack,
        const QVariantMap& bannerTemplate,
        const QUrl& backgroundImage,
        const QUrl& logoImage,
        const QVariantMap& style = QVariantMap());
    void setIntroFrame(int authoringFrame, bool active);
    bool introOverlayReady() const { return introItem_ != nullptr; }

    bool initialize(
        const QSurfaceFormat& requestedFormat = QSurfaceFormat(),
        QOpenGLContext* shareContext = nullptr,
        QString* errorMessage = nullptr
    );
    void invalidate();
    bool isInitialized() const;

    QImage renderFrame(QString* errorMessage = nullptr);
    bool supportsOffscreenPboReadback(QString* errorMessage = nullptr) const;
    void resetOffscreenPboReadback();
    bool renderFramePboStep(
        QImage* completedFrame,
        bool* completedFrameReady,
        bool drainOnly,
        QString* errorMessage = nullptr
    );
    const PreviewQuickExportRenderStats& lastRenderStats() const { return lastRenderStats_; }
    qint64 lastRenderNs() const { return lastRenderStats_.renderNs; }
    // P1 — actual GL renderer string captured at initialize() (OpenGL export
    // path). Empty until the session is initialized.
    const QString& lastGlRenderer() const { return lastGlRenderer_; }

private:
    bool ensureFramebuffer(QString* errorMessage);
    bool ensureDirectReadbackBuffer(const QSize& imageSize, QString* errorMessage);
    // Advance the synchronous readback staging ring and return the next slot.
    QImage* nextReadbackRingSlot();
    // Release every staging-ring buffer (called on teardown/reset).
    void resetReadbackRing();
    bool convertBottomUpPremultipliedReadbackToStraightRgba(
        const uchar* sourceBytes,
        qsizetype sourceBytesPerRow,
        const QSize& imageSize,
        QImage* frame,
        QString* errorMessage
    );
    bool ensureOffscreenReadbackPbos(const QSize& imageSize, QString* errorMessage);
    bool mapOffscreenReadbackPbo(int pboIndex, const QSize& imageSize, QImage* frame, QString* errorMessage);
    bool mapPboAndSubmitConvertJob(int pboIndex, const QSize& imageSize, QString* errorMessage);
    bool waitForPendingConvertJob(QImage* output, QString* errorMessage);
    void startConvertWorkerIfNeeded();
    void stopConvertWorker();
    void convertWorkerLoop();
    bool renderSceneIntoFramebuffer(QString* errorMessage);
    bool ensureContextCurrent(QString* errorMessage);
    void destroyFramebuffer();
    void destroyOffscreenReadbackPbos();
    void clearOffscreenReadbackPboState();
    void clearOffscreenPboCapabilityCache();
    void applyFrameSize();
    void applyIntroGeometry();
    void destroyIntroOverlay();
    void applyFrameState();
    void bindFrameStateIfNeeded();
    void applyLayerFlagsIfNeeded();
    void requestFrameRefresh();
    QSize framebufferPixelSize() const;

    miacode::preview::scene::PreviewFrameState frameState_;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
    QSize frameSize_{1, 1};
    QSurfaceFormat requestedFormat_;
    QOpenGLContext* shareContext_ = nullptr;
    QQuickRenderControl* renderControl_ = nullptr;
    QQuickWindow* quickWindow_ = nullptr;
    QQuickItem* rootItem_ = nullptr;
    PreviewQuickSceneRoot* sceneRoot_ = nullptr;
    PreviewQuickHudLayer* hudLayer_ = nullptr;
    QQmlEngine* qmlEngine_ = nullptr;
    QQuickItem* introItem_ = nullptr;
    bool introActive_ = false;
    QOffscreenSurface* offscreenSurface_ = nullptr;
    QOpenGLContext* context_ = nullptr;
    QOpenGLFramebufferObject* framebuffer_ = nullptr;
    QByteArray directReadbackBuffer_;
    // Synchronous (non-PBO) readback staging ring. The converted straight-RGBA
    // frame is handed to the FFmpeg pipe via zero-copy (the pump keeps it alive
    // through `packet.frameOwner`), so the buffer a frame was written into stays
    // referenced while it is in flight. A single reused QImage would therefore
    // be shared (refcount >= 2) on the next frame, forcing a copy-on-write
    // detach inside scanLine() — an unchecked ~14 MiB reallocation that crashes
    // on OOM and churns the heap every frame. Rotating through a small ring lets
    // the slot we cycle back to already be drained (refcount 1, isDetached) so it
    // is reused in place with no allocation; only if the pipe consumer fell
    // behind (slot still in flight) does the convert step reallocate that slot —
    // gracefully, via its checked allocation path. See
    // convertBottomUpPremultipliedReadbackToStraightRgba().
    static constexpr int kReadbackRingSize = 8;
    QImage readbackRing_[kReadbackRingSize];
    int readbackRingIndex_ = 0;
    GLuint offscreenReadbackPbos_[2] = {0, 0};
    // Per-PBO fence placed right after each glReadPixels(FBO→PBO) so
    // mapPboAndSubmitConvertJob can wait for that DMA to actually
    // finish before issuing glMapBufferRange. Without this, drivers
    // that don't strictly serialise FBO→PBO transfers against the
    // next frame's clear/draw against the same FBO can hand us a
    // partly-clobbered PBO — manifesting as horizontal-band tearing
    // on individual exported frames.
    GLsync offscreenReadbackFences_[2] = {nullptr, nullptr};
    QSize offscreenReadbackPboSize_;
    qsizetype offscreenReadbackPboBytes_ = 0;
    int offscreenReadbackPboWriteIndex_ = 0;
    int offscreenReadbackPendingIndex_ = -1;
    mutable bool offscreenPboCapabilityProbed_ = false;
    mutable bool offscreenPboCapabilitySupported_ = false;
    mutable QString offscreenPboCapabilityError_;
    bool frameStateBound_ = false;
    bool layerFlagsApplied_ = false;
    miacode::preview::scene::PreviewRenderLayerFlags appliedLayerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
    PreviewQuickExportRenderStats lastRenderStats_;
    QString lastGlRenderer_;

    // Convert worker — see the implementation comment in
    // renderFramePboStep for the 2-frame defer it introduces. The
    // staging buffer is the producer-side scratch into which the PBO
    // bytes get memcpy'd before the worker is woken up; while the
    // worker is reading from it, the producer must not overwrite it,
    // and the renderFramePboStep flow guarantees that by waiting for
    // the prior job before triggering the next memcpy.
    QByteArray pboStagingBuffer_;
    std::thread convertThread_;
    std::mutex convertMutex_;
    std::condition_variable convertSubmitCv_;
    std::condition_variable convertDoneCv_;
    std::atomic<bool> convertStop_{false};
    bool convertJobPending_ = false;
    bool convertJobDone_ = false;
    bool convertJobInFlight_ = false;
    bool convertJobSucceeded_ = false;
    const uchar* convertJobInputBytes_ = nullptr;
    qsizetype convertJobInputBytesPerRow_ = 0;
    QSize convertJobImageSize_;
    QImage convertJobOutputFrame_;
    QString convertJobErrorMessage_;
};
