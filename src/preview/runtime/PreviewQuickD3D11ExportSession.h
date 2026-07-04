#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <limits>
#include <memory>

#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewLayerOrder.h"
#include "preview/runtime/PreviewQuickExportSession.h"  // PreviewQuickExportRenderStats

class QQmlEngine;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class PreviewQuickHudLayer;
class PreviewQuickSceneRoot;

// P5 — D3D11/QRhi export session (plan P5.1/P5.2), parallel to the OpenGL
// PreviewQuickExportSession (which stays the default + fallback path).
//
// Same scene contract as the OpenGL session: a QQuickRenderControl-driven
// QQuickWindow hosting PreviewQuickSceneRoot + PreviewQuickHudLayer (+ the
// optional intro overlay), fed by a PreviewFrameState, rendered offscreen one
// frame at a time on the calling thread. Differences are confined to the
// graphics plumbing:
//   - MiaCode creates the ID3D11Device itself on the adapter resolved by the
//     P3/P4 GPU device policy (miacode::gpu::resolveGpuPolicyOnce) and imports
//     it via QQuickGraphicsDevice::fromDeviceAndContext. The export session has
//     no video-decode bridge (stage media is composited by ffmpeg), so unlike
//     the GUI surfaces it can bind a non-default adapter safely.
//   - The render target is an ID3D11Texture2D (R8G8B8A8_UNORM) handed to Qt
//     Quick via QQuickRenderTarget::fromD3D11Texture.
//   - Readback is synchronous: CopyResource into a staging texture + Map(READ)
//     (row-pitch aware, top-down — no vertical flip, unlike GL glReadPixels),
//     then the same premultiplied→straight RGBA8888 conversion as the GL path.
//     No PBO/async pipeline in this first version (plan P5.2).
//
// Requires the process-global Qt Quick graphics API to be Direct3D11 (main.cpp
// selects it for export processes when MIACODE_EXPORT_RENDER_BACKEND requests
// this backend). initialize() fails cleanly otherwise so the caller can fall
// back to the OpenGL session. Windows-only: on other platforms initialize()
// always fails with a clear reason.
class PreviewQuickD3D11ExportSession : public QObject
{
    Q_OBJECT

public:
    explicit PreviewQuickD3D11ExportSession(QObject* parent = nullptr);
    ~PreviewQuickD3D11ExportSession() override;

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

    // Intro overlay — same contract as PreviewQuickExportSession (sync pair).
    bool setupIntroOverlay(const QUrl& qmlUrl, QString* errorMessage = nullptr);
    void setIntroBannerData(
        const QVariantMap& bannerTrack,
        const QVariantMap& bannerTemplate,
        const QUrl& backgroundImage,
        const QUrl& logoImage,
        const QVariantMap& style = QVariantMap());
    void setIntroFrame(int authoringFrame, bool active);
    bool introOverlayReady() const { return introItem_ != nullptr; }

    bool initialize(QString* errorMessage = nullptr);
    void invalidate();
    bool isInitialized() const;

    QImage renderFrame(QString* errorMessage = nullptr);
    const PreviewQuickExportRenderStats& lastRenderStats() const { return lastRenderStats_; }
    qint64 lastRenderNs() const { return lastRenderStats_.renderNs; }

    // P5.3 summary-log fields (empty/zero until initialize() succeeds).
    const QString& adapterDescriptionForDebug() const { return adapterDescription_; }
    const QString& adapterLuidForDebug() const { return adapterLuidString_; }
    QString renderTargetFormatNameForDebug() const { return QStringLiteral("R8G8B8A8_UNORM"); }
    qsizetype lastReadbackRowPitchForDebug() const { return lastReadbackRowPitch_; }

private:
    struct D3dObjects;  // Windows-only COM state (device/context/textures), pimpl

    bool ensureRenderTarget(QString* errorMessage);
    void destroyRenderTarget();
    // Advance the synchronous readback staging ring and return the next slot
    // (same in-flight zero-copy rationale as the OpenGL session's ring — see
    // the readbackRing_ comment in PreviewQuickExportSession.h).
    QImage* nextReadbackRingSlot();
    void resetReadbackRing();
    void applyFrameSize();
    void applyIntroGeometry();
    void destroyIntroOverlay();
    void applyFrameState();
    void bindFrameStateIfNeeded();
    void applyLayerFlagsIfNeeded();
    void requestFrameRefresh();
    QSize renderTargetPixelSize() const;

    miacode::preview::scene::PreviewFrameState frameState_;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
    QSize frameSize_{1, 1};
    QQuickRenderControl* renderControl_ = nullptr;
    QQuickWindow* quickWindow_ = nullptr;
    QQuickItem* rootItem_ = nullptr;
    PreviewQuickSceneRoot* sceneRoot_ = nullptr;
    PreviewQuickHudLayer* hudLayer_ = nullptr;
    QQmlEngine* qmlEngine_ = nullptr;
    QQuickItem* introItem_ = nullptr;
    bool introActive_ = false;
    std::unique_ptr<D3dObjects> d3d_;
    static constexpr int kReadbackRingSize = 8;
    QImage readbackRing_[kReadbackRingSize];
    int readbackRingIndex_ = 0;
    bool frameStateBound_ = false;
    bool layerFlagsApplied_ = false;
    miacode::preview::scene::PreviewRenderLayerFlags appliedLayerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;
    PreviewQuickExportRenderStats lastRenderStats_;
    QString adapterDescription_;
    QString adapterLuidString_;
    qsizetype lastReadbackRowPitch_ = 0;
};
