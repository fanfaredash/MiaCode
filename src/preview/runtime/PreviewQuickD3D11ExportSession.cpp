#include "preview/runtime/PreviewQuickD3D11ExportSession.h"

#include "common/DebugLog.h"
#include "common/GpuDevicePolicy.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

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

void appendD3D11ExportSessionLog(const QString& scope, const QString& detail = QString())
{
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Export, scope, detail);
}

// Same conversion contract as the OpenGL session's
// convertBottomUpPremultipliedReadbackToStraightRgba (sync pair — the encoded
// frames from both backends must be pixel-identical), with two D3D11-specific
// differences: the mapped staging rows are TOP-DOWN (no vertical flip) and the
// source stride is the driver-chosen RowPitch, not width*4.
bool convertTopDownPremultipliedReadbackToStraightRgba(
    const uchar* sourceBytes,
    qsizetype sourceBytesPerRow,
    const QSize& imageSize,
    QImage* frame,
    QString* errorMessage)
{
    if (frame == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid D3D11 export readback output image");
        }
        return false;
    }
    if (sourceBytes == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("D3D11 export readback source bytes are null");
        }
        return false;
    }

    const QSize safeSize(qMax(1, imageSize.width()), qMax(1, imageSize.height()));
    const qsizetype expectedBytesPerRow = static_cast<qsizetype>(safeSize.width()) * 4;
    if (sourceBytesPerRow < expectedBytesPerRow) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("D3D11 export readback row pitch is smaller than RGBA output width");
        }
        return false;
    }
    // Explicit allocation when the ring slot is still shared/in flight — same
    // OOM-safety rationale as the OpenGL session's converter (see the long
    // comment there).
    if (frame->isNull()
        || frame->size() != safeSize
        || frame->format() != QImage::Format_RGBA8888
        || !frame->isDetached()) {
        *frame = QImage(safeSize, QImage::Format_RGBA8888);
    }
    if (frame->isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("failed to allocate D3D11 export straight RGBA frame buffer");
        }
        return false;
    }

    for (int row = 0; row < safeSize.height(); ++row) {
        const uchar* sourceRowBytes =
            sourceBytes + static_cast<qsizetype>(row) * sourceBytesPerRow;
        uchar* outputRow = frame->scanLine(row);
        if (outputRow == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                    "D3D11 export readback scanLine returned null (out of memory?)");
            }
            return false;
        }
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

}  // namespace

#if defined(Q_OS_WIN)

using Microsoft::WRL::ComPtr;

struct PreviewQuickD3D11ExportSession::D3dObjects {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> renderTarget;
    ComPtr<ID3D11Texture2D> staging;
    QSize textureSize;
    D3D_FEATURE_LEVEL featureLevel{};
};

namespace {

QString hresultHex(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

// Locate the DXGI adapter matching the policy-resolved LUID. Returns null (=
// default adapter / D3D_DRIVER_TYPE_HARDWARE) when the policy did not resolve
// to a concrete adapter or the LUID is no longer present (e.g. GPU removed
// between resolve and use) — device creation then behaves like the OpenGL
// path's "whatever the OS picks", which is the safe degradation.
ComPtr<IDXGIAdapter1> findAdapterByLuid(const miacode::gpu::GpuAdapterLuid& luid, QString* miss)
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        if (miss != nullptr) {
            *miss = QStringLiteral("dxgi_factory_create_failed");
        }
        return {};
    }
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) {
            continue;
        }
        if (static_cast<uint32_t>(desc.AdapterLuid.HighPart) == luid.high
            && desc.AdapterLuid.LowPart == luid.low) {
            return adapter;
        }
    }
    if (miss != nullptr) {
        *miss = QStringLiteral("adapter_luid_not_found");
    }
    return {};
}

}  // namespace

#else

// Non-Windows stub so the class compiles everywhere; initialize() reports the
// platform limitation and the caller falls back to the OpenGL session.
struct PreviewQuickD3D11ExportSession::D3dObjects {
    QSize textureSize;
};

#endif  // Q_OS_WIN

PreviewQuickD3D11ExportSession::PreviewQuickD3D11ExportSession(QObject* parent)
    : QObject(parent)
{
}

PreviewQuickD3D11ExportSession::~PreviewQuickD3D11ExportSession()
{
    invalidate();
}

void PreviewQuickD3D11ExportSession::setFrameState(
    const miacode::preview::scene::PreviewFrameState& state)
{
    frameState_ = state;
    miacode::preview::scene::refreshPreviewFrameStateHudStatsSnapshot(frameState_);
    applyFrameState();
}

void PreviewQuickD3D11ExportSession::applyExportFrameTick(
    double playheadSeconds,
    bool showTimestamp,
    bool showObjectStatsHud,
    bool usedGpuRendererThisFrame,
    int cpuFallbackCount,
    double fpsDisplay,
    double hudPlayheadSecondsOverride)
{
    frameState_.playheadSeconds = playheadSeconds;
    frameState_.hudPlayheadSecondsOverride = hudPlayheadSecondsOverride;
    frameState_.render.showTimestamp = showTimestamp;
    frameState_.render.showObjectStatsHud = showObjectStatsHud;
    frameState_.usedGpuRendererThisFrame = usedGpuRendererThisFrame;
    frameState_.cpuFallbackCount = cpuFallbackCount;
    frameState_.fpsDisplay = fpsDisplay;
    miacode::preview::scene::refreshPreviewFrameStateHudStatsSnapshot(frameState_);
    requestFrameRefresh();
}

void PreviewQuickD3D11ExportSession::setLayerFlags(
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags)
{
    if (layerFlags_ == layerFlags) {
        return;
    }
    layerFlags_ = layerFlags;
    applyFrameState();
}

void PreviewQuickD3D11ExportSession::setFrameSize(const QSize& size)
{
    const QSize safeSize(qMax(1, size.width()), qMax(1, size.height()));
    if (frameSize_ == safeSize) {
        return;
    }
    frameSize_ = safeSize;
    applyFrameSize();
    destroyRenderTarget();
    resetReadbackRing();
}

bool PreviewQuickD3D11ExportSession::initialize(QString* errorMessage)
{
    if (isInitialized()) {
        return true;
    }

#if !defined(Q_OS_WIN)
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("d3d11_qrhi export session is Windows-only");
    }
    return false;
#else
    resetReadbackRing();
    frameStateBound_ = false;
    layerFlagsApplied_ = false;
    appliedLayerFlags_ = miacode::preview::scene::kPreviewAllRenderLayers;
    lastRenderStats_ = PreviewQuickExportRenderStats();
    adapterDescription_.clear();
    adapterLuidString_.clear();
    lastReadbackRowPitch_ = 0;

    // The QQuickRenderControl window inherits the process-global graphics API;
    // a D3D11 device import only works when that API is Direct3D11 (main.cpp
    // selects it for export processes when this backend is requested).
    const QSGRendererInterface::GraphicsApi api = QQuickWindow::graphicsApi();
    if (api != QSGRendererInterface::Direct3D11) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "process Qt Quick graphics API is not Direct3D11 (api=%1)")
                .arg(static_cast<int>(api));
        }
        return false;
    }

    // P5.1 — reuse the P3/P4 device policy for adapter selection so GUI and
    // export land on the same policy. Unlike the GUI root window there is no
    // MIACODE_GPU_BIND_HIGH_PERFORMANCE gate here: the export session renders
    // chart sprites only (no D3D11VA decode bridge — stage media is composited
    // by ffmpeg), so binding a non-default adapter carries none of the
    // video-bridge risk that keeps the GUI bind opt-in.
    const miacode::gpu::ResolvedGpuPolicy& resolved = miacode::gpu::resolveGpuPolicyOnce();
    if (resolved.resolvedKind == miacode::gpu::GpuPolicyKind::Software) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("gpu policy resolved to software; d3d11_qrhi export not applicable");
        }
        return false;
    }
    ComPtr<IDXGIAdapter1> requestedAdapter;
    QString adapterMiss;
    const bool policyHasAdapter =
        (resolved.resolvedKind == miacode::gpu::GpuPolicyKind::AutoHighPerformance
         || resolved.resolvedKind == miacode::gpu::GpuPolicyKind::AdapterLuid)
        && resolved.adapterLuid.has_value() && resolved.adapterLuid->valid;
    if (policyHasAdapter) {
        requestedAdapter = findAdapterByLuid(*resolved.adapterLuid, &adapterMiss);
    }

    d3d_ = std::make_unique<D3dObjects>();

    // BGRA support mirrors Qt RHI's own creation flags. No VIDEO_SUPPORT: this
    // device never decodes (plan P4.1 — video support only when sharing).
    const UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    const auto tryCreate = [&](bool useLevels) -> HRESULT {
        return ::D3D11CreateDevice(
            requestedAdapter.Get(),
            requestedAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            useLevels ? levels : nullptr,
            useLevels ? ARRAYSIZE(levels) : 0u,
            D3D11_SDK_VERSION,
            d3d_->device.ReleaseAndGetAddressOf(),
            &d3d_->featureLevel,
            d3d_->context.ReleaseAndGetAddressOf());
    };
    HRESULT hr = tryCreate(true);
    if (FAILED(hr)) {
        // Older drivers can reject an explicit 11_1-first list.
        hr = tryCreate(false);
    }
    if (FAILED(hr)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("D3D11CreateDevice failed (hr=%1, adapter=%2)")
                .arg(hresultHex(hr))
                .arg(requestedAdapter ? resolved.adapterLuid->toString()
                                      : QStringLiteral("default"));
        }
        invalidate();
        return false;
    }

    // Record the ACTUAL adapter from the created device (robust even when the
    // requested LUID missed and we fell back to the default adapter).
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> actualAdapter;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(d3d_->device.As(&dxgiDevice))
            && SUCCEEDED(dxgiDevice->GetAdapter(actualAdapter.GetAddressOf()))
            && SUCCEEDED(actualAdapter->GetDesc(&desc))) {
            adapterDescription_ = QString::fromWCharArray(desc.Description).trimmed();
            miacode::gpu::GpuAdapterLuid actualLuid;
            actualLuid.high = static_cast<uint32_t>(desc.AdapterLuid.HighPart);
            actualLuid.low = desc.AdapterLuid.LowPart;
            actualLuid.valid = true;
            adapterLuidString_ = actualLuid.toString();
        }
    }
    appendD3D11ExportSessionLog(
        QStringLiteral("export_d3d11_session"),
        QStringLiteral("action=device_created adapter=\"%1\" luid=%2 requested_policy=%3 "
                       "resolved_policy=%4 requested_luid=%5 adapter_miss=%6 feature_level=0x%7")
            .arg(adapterDescription_.isEmpty() ? QStringLiteral("(unknown)") : adapterDescription_)
            .arg(adapterLuidString_.isEmpty() ? QStringLiteral("(unknown)") : adapterLuidString_)
            .arg(miacode::gpu::gpuPolicyKindName(resolved.request.kind))
            .arg(miacode::gpu::gpuPolicyKindName(resolved.resolvedKind))
            .arg(policyHasAdapter ? resolved.adapterLuid->toString() : QStringLiteral("(none)"))
            .arg(adapterMiss.isEmpty() ? QStringLiteral("(none)") : adapterMiss)
            .arg(static_cast<quint32>(d3d_->featureLevel), 0, 16));

    renderControl_ = new QQuickRenderControl();
    quickWindow_ = new QQuickWindow(renderControl_);
    quickWindow_->setColor(Qt::transparent);
    quickWindow_->setPersistentGraphics(true);
    quickWindow_->setPersistentSceneGraph(true);
    quickWindow_->setGraphicsDevice(
        QQuickGraphicsDevice::fromDeviceAndContext(d3d_->device.Get(), d3d_->context.Get()));

    if (!renderControl_->initialize()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("failed to initialize D3D11 Quick export render control");
        }
        invalidate();
        return false;
    }

    rootItem_ = new QQuickItem(quickWindow_->contentItem());

    sceneRoot_ = new PreviewQuickSceneRoot(rootItem_);
    sceneRoot_->setZ(0.0);
    // Same override as the OpenGL session: the offscreen export scene graph has
    // no DComp popup, so force the QSG chart-render path regardless of the
    // exclusive flag (see the long comment in PreviewQuickExportSession.cpp).
    sceneRoot_->setDCompFallbackActive(true);

    hudLayer_ = new PreviewQuickHudLayer(rootItem_);
    hudLayer_->setZ(1.0);
    hudLayer_->setDCompFallbackActive(true);

    applyFrameSize();
    applyFrameState();

    if (!ensureRenderTarget(errorMessage)) {
        invalidate();
        return false;
    }
    return true;
#endif  // Q_OS_WIN
}

void PreviewQuickD3D11ExportSession::invalidate()
{
    destroyIntroOverlay();
    destroyRenderTarget();
    // Window before render control (the window's scene graph teardown runs
    // through the control); both before releasing the imported device — Qt does
    // not own an imported device, so the ComPtrs below hold the last reference.
    delete quickWindow_;
    quickWindow_ = nullptr;
    rootItem_ = nullptr;
    sceneRoot_ = nullptr;
    hudLayer_ = nullptr;

    delete renderControl_;
    renderControl_ = nullptr;

    d3d_.reset();
    resetReadbackRing();
    frameStateBound_ = false;
    layerFlagsApplied_ = false;
    appliedLayerFlags_ = miacode::preview::scene::kPreviewAllRenderLayers;
    lastRenderStats_ = PreviewQuickExportRenderStats();
    lastReadbackRowPitch_ = 0;
}

bool PreviewQuickD3D11ExportSession::isInitialized() const
{
    return renderControl_ != nullptr && quickWindow_ != nullptr && d3d_ != nullptr;
}

QImage PreviewQuickD3D11ExportSession::renderFrame(QString* errorMessage)
{
#if !defined(Q_OS_WIN)
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("d3d11_qrhi export session is Windows-only");
    }
    return QImage();
#else
    if (!isInitialized()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("D3D11 Quick export session is not initialized");
        }
        return QImage();
    }
    if (!ensureRenderTarget(errorMessage)) {
        return QImage();
    }

    applyFrameSize();
    applyFrameState();

    QElapsedTimer renderTimer;
    renderTimer.start();
    // Qt Quick clears the target to the window color (transparent) at the start
    // of its render pass, matching the OpenGL path's explicit clear.
    renderControl_->polishItems();
    renderControl_->beginFrame();
    renderControl_->sync();
    renderControl_->render();
    renderControl_->endFrame();
    lastRenderStats_.renderNs = renderTimer.nsecsElapsed();

    // P5.2 — synchronous readback. Commands on the immediate context are
    // implicitly ordered, so CopyResource sees the finished frame and
    // Map(READ) blocks until the copy lands. No fence machinery needed.
    QElapsedTimer readbackTimer;
    readbackTimer.start();
    const QSize pixelSize = renderTargetPixelSize();
    d3d_->context->CopyResource(d3d_->staging.Get(), d3d_->renderTarget.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = d3d_->context->Map(d3d_->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        const HRESULT removedReason = d3d_->device->GetDeviceRemovedReason();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "failed to map D3D11 export staging texture (hr=%1 device_removed=%2)")
                .arg(hresultHex(hr))
                .arg(removedReason != S_OK ? hresultHex(removedReason) : QStringLiteral("no"));
        }
        return QImage();
    }
    lastReadbackRowPitch_ = static_cast<qsizetype>(mapped.RowPitch);
    QImage* readbackSlot = nextReadbackRingSlot();
    QString convertError;
    const bool converted = convertTopDownPremultipliedReadbackToStraightRgba(
        static_cast<const uchar*>(mapped.pData),
        lastReadbackRowPitch_,
        pixelSize,
        readbackSlot,
        &convertError);
    d3d_->context->Unmap(d3d_->staging.Get(), 0);
    if (!converted) {
        if (errorMessage != nullptr) {
            *errorMessage = convertError;
        }
        return QImage();
    }
    lastRenderStats_.readbackNs = readbackTimer.nsecsElapsed();
    return *readbackSlot;
#endif  // Q_OS_WIN
}

bool PreviewQuickD3D11ExportSession::ensureRenderTarget(QString* errorMessage)
{
#if !defined(Q_OS_WIN)
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("d3d11_qrhi export session is Windows-only");
    }
    return false;
#else
    if (!isInitialized()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("D3D11 Quick export session is not initialized");
        }
        return false;
    }
    const QSize pixelSize = renderTargetPixelSize();
    if (pixelSize.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("D3D11 Quick export frame size is invalid");
        }
        return false;
    }
    if (d3d_->renderTarget && d3d_->staging && d3d_->textureSize == pixelSize) {
        return true;
    }

    destroyRenderTarget();

    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width = static_cast<UINT>(pixelSize.width());
    targetDesc.Height = static_cast<UINT>(pixelSize.height());
    targetDesc.MipLevels = 1;
    targetDesc.ArraySize = 1;
    // Non-sRGB RGBA8, matching the OpenGL path's GL_RGBA8 FBO — the mapped
    // bytes share the R,G,B,A memory order of the GL_RGBA readback.
    targetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = d3d_->device->CreateTexture2D(
        &targetDesc, nullptr, d3d_->renderTarget.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to create D3D11 export render target (hr=%1)")
                .arg(hresultHex(hr));
        }
        destroyRenderTarget();
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = d3d_->device->CreateTexture2D(
        &stagingDesc, nullptr, d3d_->staging.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to create D3D11 export staging texture (hr=%1)")
                .arg(hresultHex(hr));
        }
        destroyRenderTarget();
        return false;
    }

    // Depth/stencil is left to Qt (plan P5.1 spike guidance): QQuickRenderTarget
    // auto-creates a matching depth-stencil buffer for the scene graph renderer.
    QQuickRenderTarget target = QQuickRenderTarget::fromD3D11Texture(
        d3d_->renderTarget.Get(),
        static_cast<uint>(targetDesc.Format),
        pixelSize,
        1);
    target.setDevicePixelRatio(1.0);
    quickWindow_->setRenderTarget(target);
    d3d_->textureSize = pixelSize;
    return true;
#endif  // Q_OS_WIN
}

void PreviewQuickD3D11ExportSession::destroyRenderTarget()
{
#if defined(Q_OS_WIN)
    if (d3d_ == nullptr) {
        return;
    }
    if (quickWindow_ != nullptr && d3d_->renderTarget) {
        quickWindow_->setRenderTarget(QQuickRenderTarget());
    }
    d3d_->staging.Reset();
    d3d_->renderTarget.Reset();
    d3d_->textureSize = QSize();
#endif
}

QImage* PreviewQuickD3D11ExportSession::nextReadbackRingSlot()
{
    QImage* slot = &readbackRing_[readbackRingIndex_];
    readbackRingIndex_ = (readbackRingIndex_ + 1) % kReadbackRingSize;
    return slot;
}

void PreviewQuickD3D11ExportSession::resetReadbackRing()
{
    for (QImage& slot : readbackRing_) {
        slot = QImage();
    }
    readbackRingIndex_ = 0;
}

void PreviewQuickD3D11ExportSession::applyFrameSize()
{
    if (quickWindow_ == nullptr || rootItem_ == nullptr || sceneRoot_ == nullptr
        || hudLayer_ == nullptr) {
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

    applyIntroGeometry();
}

void PreviewQuickD3D11ExportSession::applyIntroGeometry()
{
    if (introItem_ == nullptr) {
        return;
    }
    // Same 16:9 centre-crop contract as the OpenGL session (sync pair).
    constexpr double kNativeW = 1920.0;
    constexpr double kNativeH = 1080.0;
    const double frameW = qMax(1, frameSize_.width());
    const double frameH = qMax(1, frameSize_.height());
    introItem_->setWidth(kNativeW);
    introItem_->setHeight(kNativeH);
    introItem_->setTransformOrigin(QQuickItem::Center);
    introItem_->setScale(frameH / kNativeH);
    introItem_->setX(frameW / 2.0 - kNativeW / 2.0);
    introItem_->setY(frameH / 2.0 - kNativeH / 2.0);
}

void PreviewQuickD3D11ExportSession::destroyIntroOverlay()
{
    // introItem_ lives in the quickWindow_ scene tree and is deleted with the
    // window during invalidate(); here we only drop our references.
    introItem_ = nullptr;
    introActive_ = false;
}

bool PreviewQuickD3D11ExportSession::setupIntroOverlay(const QUrl& qmlUrl, QString* errorMessage)
{
    if (rootItem_ == nullptr || quickWindow_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export session not initialized before intro setup");
        }
        return false;
    }
    if (introItem_ != nullptr) {
        return true;
    }
    if (qmlEngine_ == nullptr) {
        qmlEngine_ = new QQmlEngine(this);
    }

    QQmlComponent component(qmlEngine_, qmlUrl);
    if (component.isError()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("intro QML load failed: %1").arg(component.errorString().trimmed());
        }
        return false;
    }
    QObject* object = component.create();
    if (object == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("intro QML create returned null: %1")
                .arg(component.errorString().trimmed());
        }
        return false;
    }
    introItem_ = qobject_cast<QQuickItem*>(object);
    if (introItem_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("intro QML root is not a QQuickItem");
        }
        delete object;
        return false;
    }
    introItem_->setParentItem(rootItem_);
    introItem_->setParent(rootItem_);
    introItem_->setZ(2.0);
    introItem_->setVisible(false);
    applyIntroGeometry();
    return true;
}

void PreviewQuickD3D11ExportSession::setIntroBannerData(
    const QVariantMap& bannerTrack,
    const QVariantMap& bannerTemplate,
    const QUrl& backgroundImage,
    const QUrl& logoImage,
    const QVariantMap& style)
{
    if (introItem_ == nullptr) {
        return;
    }
    if (!bannerTemplate.isEmpty()) {
        introItem_->setProperty("bannerTemplateData", bannerTemplate);
    }
    if (!logoImage.isEmpty()) {
        introItem_->setProperty("logoImage", logoImage);
    }
    introItem_->setProperty("backgroundImage", backgroundImage);
    introItem_->setProperty("bannerTrack", bannerTrack);
    for (auto it = style.constBegin(); it != style.constEnd(); ++it) {
        introItem_->setProperty(it.key().toUtf8().constData(), it.value());
    }
}

void PreviewQuickD3D11ExportSession::setIntroFrame(int authoringFrame, bool active)
{
    introActive_ = active;
    if (introItem_ == nullptr) {
        return;
    }
    if (active) {
        introItem_->setProperty("frame", authoringFrame);
    }
    introItem_->setVisible(active);
}

void PreviewQuickD3D11ExportSession::applyFrameState()
{
    bindFrameStateIfNeeded();
    applyLayerFlagsIfNeeded();
    requestFrameRefresh();
}

void PreviewQuickD3D11ExportSession::bindFrameStateIfNeeded()
{
    if (frameStateBound_ || sceneRoot_ == nullptr || hudLayer_ == nullptr) {
        return;
    }

    sceneRoot_->setFrameState(&frameState_);
    hudLayer_->setFrameState(&frameState_);
    frameStateBound_ = true;
}

void PreviewQuickD3D11ExportSession::applyLayerFlagsIfNeeded()
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

void PreviewQuickD3D11ExportSession::requestFrameRefresh()
{
    if (sceneRoot_ != nullptr) {
        sceneRoot_->update();
    }
    if (hudLayer_ != nullptr) {
        hudLayer_->update();
    }
}

QSize PreviewQuickD3D11ExportSession::renderTargetPixelSize() const
{
    return QSize(qMax(1, frameSize_.width()), qMax(1, frameSize_.height()));
}
