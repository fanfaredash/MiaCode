#pragma once

#include <QByteArray>
#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QSurfaceFormat>
#include <QtGui/qopengl.h>

#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewLayerOrder.h"

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
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
        double fpsDisplay
    );
    const miacode::preview::scene::PreviewFrameState& frameState() const { return frameState_; }
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags);
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags() const { return layerFlags_; }

    void setFrameSize(const QSize& size);
    QSize frameSize() const { return frameSize_; }

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

private:
    bool ensureFramebuffer(QString* errorMessage);
    bool ensureDirectReadbackBuffer(const QSize& imageSize, QString* errorMessage);
    bool convertBottomUpPremultipliedReadbackToStraightRgba(
        const uchar* sourceBytes,
        qsizetype sourceBytesPerRow,
        const QSize& imageSize,
        QImage* frame,
        QString* errorMessage
    );
    bool ensureOffscreenReadbackPbos(const QSize& imageSize, QString* errorMessage);
    bool mapOffscreenReadbackPbo(int pboIndex, const QSize& imageSize, QImage* frame, QString* errorMessage);
    bool renderSceneIntoFramebuffer(QString* errorMessage);
    bool ensureContextCurrent(QString* errorMessage);
    void destroyFramebuffer();
    void destroyOffscreenReadbackPbos();
    void clearOffscreenReadbackPboState();
    void clearOffscreenPboCapabilityCache();
    void applyFrameSize();
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
    QOffscreenSurface* offscreenSurface_ = nullptr;
    QOpenGLContext* context_ = nullptr;
    QOpenGLFramebufferObject* framebuffer_ = nullptr;
    QByteArray directReadbackBuffer_;
    QImage reusableReadbackFrame_;
    GLuint offscreenReadbackPbos_[2] = {0, 0};
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
};
