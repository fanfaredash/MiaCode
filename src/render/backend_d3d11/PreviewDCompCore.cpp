#include "render/backend_d3d11/PreviewDCompCore.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QString>

namespace miacode::preview::dcomp {

namespace {

// Convenience for logging HRESULT failures with the call site name. All
// log output goes to runtime/preview/dcomp so a single grep filter can
// follow the lifecycle of the path during diagnostics.
void logDCompError(const char* what, HRESULT hr, const QString& extra = QString())
{
#ifdef Q_OS_WIN
    QString payload = QStringLiteral("op=%1 hr=0x%2")
        .arg(QString::fromLatin1(what))
        .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/dcomp"),
        payload);
#else
    Q_UNUSED(what);
    Q_UNUSED(hr);
    Q_UNUSED(extra);
#endif
}

void logDComp(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/dcomp"),
        payload);
}

}  // namespace

PreviewDCompCore::PreviewDCompCore() = default;

PreviewDCompCore::~PreviewDCompCore()
{
    shutdown();
}

#ifdef Q_OS_WIN

bool PreviewDCompCore::initialise(HWND parentHwnd, QSize pixelSize)
{
    if (ready_) {
        // Already initialised — caller should shutdown() first if reinit is
        // needed. Treat as success because the existing setup is usable.
        return true;
    }
    if (parentHwnd == nullptr) {
        logDComp("init_failed", QStringLiteral("reason=null_parent_hwnd"));
        return false;
    }
    if (pixelSize.width() <= 0 || pixelSize.height() <= 0) {
        // DXGI rejects zero-size swap chains. The QML placeholder may
        // briefly report size (0, 0) before the layout settles — caller
        // can retry once the layout is stable.
        logDComp("init_failed",
                 QStringLiteral("reason=invalid_size width=%1 height=%2")
                     .arg(pixelSize.width()).arg(pixelSize.height()));
        return false;
    }

    parentHwnd_ = parentHwnd;
    swapChainSize_ = pixelSize;

    if (!createD3D11Device()) {
        shutdown();
        return false;
    }
    if (!createSwapChain(parentHwnd, pixelSize)) {
        shutdown();
        return false;
    }
    if (!createBackBufferRtv()) {
        shutdown();
        return false;
    }
    if (!createDCompVisualTree(parentHwnd)) {
        shutdown();
        return false;
    }

    ready_ = true;
    logDComp("initialised",
             QStringLiteral("hwnd=0x%1 width=%2 height=%3")
                 .arg(reinterpret_cast<quintptr>(parentHwnd), 0, 16)
                 .arg(pixelSize.width())
                 .arg(pixelSize.height()));
    return true;
}

bool PreviewDCompCore::createD3D11Device()
{
    // Hardware first; WARP fallback per plan §5.3. Both flow through the
    // same D3D11CreateDevice signature; only DRIVER_TYPE differs.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    // Debug layer adds validation diagnostics. Optional — silently
    // downgrades if the SDK debug layer isn't installed (the second
    // attempt below clears the flag).
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL achievedLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        d3dDevice_.GetAddressOf(),
        &achievedLevel,
        d3dContext_.GetAddressOf());

    if (FAILED(hr)) {
        // Strip the debug-layer flag and retry — common reason for HW
        // failure in dev-environments without the SDK debug runtime.
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            d3dDevice_.ReleaseAndGetAddressOf(),
            &achievedLevel,
            d3dContext_.ReleaseAndGetAddressOf());
    }

    if (FAILED(hr)) {
        // Last resort: WARP software device. Will run, just slowly. Plan
        // §5.3 requires this fallback for unusual environments (driver
        // crashes, sandboxed VMs, etc.).
        logDCompError("d3d11_hw_failed", hr);
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            d3dDevice_.ReleaseAndGetAddressOf(),
            &achievedLevel,
            d3dContext_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            logDCompError("d3d11_warp_failed", hr);
            return false;
        }
        logDComp("d3d11_using_warp",
                 QStringLiteral("feature_level=0x%1")
                     .arg(static_cast<int>(achievedLevel), 4, 16, QChar('0')));
    } else {
        logDComp("d3d11_created",
                 QStringLiteral("feature_level=0x%1")
                     .arg(static_cast<int>(achievedLevel), 4, 16, QChar('0')));
    }
    return true;
}

bool PreviewDCompCore::createSwapChain(HWND /*parentHwnd*/, QSize pixelSize)
{
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        logDCompError("query_dxgi_device", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(dxgiAdapter.GetAddressOf());
    if (FAILED(hr)) {
        logDCompError("get_dxgi_adapter", hr);
        return false;
    }

    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        logDCompError("get_dxgi_factory", hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(pixelSize.width());
    desc.Height = static_cast<UINT>(pixelSize.height());
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // Triple buffer — plan §6 keeps this from 927322b's TripleBuffer choice.
    desc.BufferCount = 3;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    hr = dxgiFactory_->CreateSwapChainForComposition(
        d3dDevice_.Get(),
        &desc,
        nullptr,
        swapChain_.GetAddressOf());
    if (FAILED(hr)) {
        logDCompError("create_swap_chain", hr);
        return false;
    }

    hr = swapChain_.As(&swapChain2_);
    if (FAILED(hr)) {
        logDCompError("query_swap_chain2", hr);
        return false;
    }

    frameLatencyWaitable_ = swapChain2_->GetFrameLatencyWaitableObject();
    if (frameLatencyWaitable_ == nullptr) {
        // Plan §2 frame pacing depends on this — without it Phase 2's
        // render thread can't block on vsync. Treat as init failure.
        logDComp("swap_chain_no_waitable",
                 QStringLiteral("reason=GetFrameLatencyWaitableObject_returned_null"));
        return false;
    }

    return true;
}

bool PreviewDCompCore::createBackBufferRtv()
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        logDCompError("swap_chain_get_buffer", hr);
        return false;
    }
    hr = d3dDevice_->CreateRenderTargetView(
        backBuffer.Get(), nullptr, backBufferRtv_.GetAddressOf());
    if (FAILED(hr)) {
        logDCompError("create_rtv", hr);
        return false;
    }
    return true;
}

void PreviewDCompCore::releaseBackBufferRtv()
{
    if (backBufferRtv_) {
        backBufferRtv_.Reset();
    }
}

bool PreviewDCompCore::createDCompVisualTree(HWND parentHwnd)
{
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        logDCompError("dcomp_query_dxgi_device", hr);
        return false;
    }

    hr = DCompositionCreateDevice(
        dxgiDevice.Get(),
        IID_PPV_ARGS(compDevice_.GetAddressOf()));
    if (FAILED(hr)) {
        logDCompError("dcomp_create_device", hr);
        return false;
    }

    // CreateTargetForHwnd's second parameter (TopMost = TRUE) tells DComp
    // to place our visual ABOVE the HWND's normal painted content. We
    // want this — the QML scene renders into the HWND's swap chain, our
    // visual sits on top in the compositor stack.
    hr = compDevice_->CreateTargetForHwnd(
        parentHwnd,
        TRUE,
        compTarget_.GetAddressOf());
    if (FAILED(hr)) {
        logDCompError("dcomp_create_target", hr);
        return false;
    }

    hr = compDevice_->CreateVisual(compVisual_.GetAddressOf());
    if (FAILED(hr)) {
        logDCompError("dcomp_create_visual", hr);
        return false;
    }

    hr = compVisual_->SetContent(swapChain_.Get());
    if (FAILED(hr)) {
        logDCompError("dcomp_visual_set_content", hr);
        return false;
    }

    hr = compTarget_->SetRoot(compVisual_.Get());
    if (FAILED(hr)) {
        logDCompError("dcomp_target_set_root", hr);
        return false;
    }

    hr = compDevice_->Commit();
    if (FAILED(hr)) {
        logDCompError("dcomp_initial_commit", hr);
        return false;
    }
    return true;
}

bool PreviewDCompCore::resize(QSize newPixelSize, QSize vtDisplaySizeOverride)
{
    if (!ready_) {
        return false;
    }
    // Phase 4d-fix7-final2 — pick the displaySize for post-resize VT
    // re-apply: prefer the explicit override (atomic with the resize
    // request), fall back to the recorded last value if no override.
    const QSize displaySizeForReapply =
        vtDisplaySizeOverride.isValid()
            ? vtDisplaySizeOverride
            : lastVisualTransformDisplaySize_;
    if (newPixelSize == swapChainSize_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("render/backend_d3d11/core"),
            QStringLiteral("action=resize_noop swap=%1x%2 vt_display=%3x%4 vt_valid=%5")
                .arg(swapChainSize_.width()).arg(swapChainSize_.height())
                .arg(displaySizeForReapply.width())
                .arg(displaySizeForReapply.height())
                .arg(lastVisualTransformValid_ ? 1 : 0),
            /*force=*/false);
        // Phase 4d-fix7-debug — even when the swap chain is already at
        // the target size, we may still need to refresh the visual
        // transform: the GUI thread may have set it against an *older*
        // swap-chain size (during a multi-step resize) and never
        // re-applied. Re-apply with the override displaySize (or the
        // recorded last value) against the current matching swap
        // chain size to ensure scale=1.0 post-resize-settle.
        if (lastVisualTransformValid_) {
            setVisualTransform(lastVisualTransformX_, lastVisualTransformY_,
                                displaySizeForReapply);
        }
        return true;
    }
    if (newPixelSize.width() <= 0 || newPixelSize.height() <= 0) {
        return false;
    }

    // Per IDXGISwapChain::ResizeBuffers semantics, every reference to a
    // swap chain back buffer (i.e. our RTV) must be released before the
    // resize call.
    releaseBackBufferRtv();

    HRESULT hr = swapChain_->ResizeBuffers(
        3,
        static_cast<UINT>(newPixelSize.width()),
        static_cast<UINT>(newPixelSize.height()),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (FAILED(hr)) {
        logDCompError("swap_chain_resize", hr,
                      QStringLiteral("width=%1 height=%2")
                          .arg(newPixelSize.width()).arg(newPixelSize.height()));
        return false;
    }

    if (!createBackBufferRtv()) {
        return false;
    }

    swapChainSize_ = newPixelSize;
    logDComp("resized",
             QStringLiteral("width=%1 height=%2")
                 .arg(newPixelSize.width()).arg(newPixelSize.height()));

    // Phase 4a — re-apply the last visual transform now that the swap
    // chain matches it. setVisualTransform's scale = displaySize /
    // swapChainSize_; if the GUI thread set the transform with the
    // *requested* size before the render thread resized the swap chain
    // here, the visual is currently scaled (oldDisplay/oldSwap) which
    // is wrong now. Re-applying with the same displaySize against the
    // new swap chain size gives the correct 1:1 scale.
    if (lastVisualTransformValid_) {
        // Phase 4d-fix7-final2 — use the override displaySize (paired
        // with this resize request) instead of the global recorded one.
        setVisualTransform(lastVisualTransformX_, lastVisualTransformY_,
                            displaySizeForReapply);
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("render/backend_d3d11/core"),
            QStringLiteral("action=resize_post_vt_reapply swap=%1x%2 vt_display=%3x%4")
                .arg(swapChainSize_.width()).arg(swapChainSize_.height())
                .arg(displaySizeForReapply.width())
                .arg(displaySizeForReapply.height()),
            /*force=*/false);
    } else {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("render/backend_d3d11/core"),
            QStringLiteral("action=resize_post_vt_skip reason=no_recorded_transform swap=%1x%2")
                .arg(swapChainSize_.width()).arg(swapChainSize_.height()),
            /*force=*/false);
    }
    return true;
}

bool PreviewDCompCore::setVisualTransform(int xPx, int yPx, QSize displaySize)
{
    if (!ready_) {
        return false;
    }
    if (displaySize.width() <= 0 || displaySize.height() <= 0) {
        return false;
    }

    // 2D affine transform via D2D_MATRIX_3X2_F:
    //   [ scaleX  0       offsetX ]
    //   [ 0       scaleY  offsetY ]
    // Scale factor maps the swap chain's intrinsic pixel size to the
    // display rect — DComp/DWM stretch with a linear filter, so a
    // mismatch is visually fine until resize() catches up.
    const float scaleX = static_cast<float>(displaySize.width())
                         / static_cast<float>(swapChainSize_.width());
    const float scaleY = static_cast<float>(displaySize.height())
                         / static_cast<float>(swapChainSize_.height());
    D2D_MATRIX_3X2_F matrix{};
    matrix._11 = scaleX;
    matrix._12 = 0.0f;
    matrix._21 = 0.0f;
    matrix._22 = scaleY;
    matrix._31 = static_cast<float>(xPx);
    matrix._32 = static_cast<float>(yPx);

    // IDCompositionVisual::SetTransform with a D2D_MATRIX_3X2_F overload —
    // simpler than building an IDCompositionMatrixTransform interface for
    // a static visual. We'll move to IDCompositionMatrixTransform3D in
    // Phase 4 if animations need it.
    HRESULT hr = compVisual_->SetTransform(matrix);
    if (FAILED(hr)) {
        logDCompError("visual_set_transform", hr);
        return false;
    }
    hr = compDevice_->Commit();
    if (FAILED(hr)) {
        logDCompError("commit_after_transform", hr);
        return false;
    }
    // Remember the requested transform so resize() can re-apply once
    // the swap chain catches up — see the explanation on
    // lastVisualTransformValid_ in the header.
    lastVisualTransformValid_ = true;
    lastVisualTransformX_ = xPx;
    lastVisualTransformY_ = yPx;
    lastVisualTransformDisplaySize_ = displaySize;
    return true;
}

bool PreviewDCompCore::renderTestFrame()
{
    return renderClear(1.0f, 0.0f, 0.0f, 1.0f);
}

bool PreviewDCompCore::renderClear(float r, float g, float b, float a)
{
    if (!ready_) {
        return false;
    }
    if (!backBufferRtv_) {
        return false;
    }
    const float clearColor[4] = { r, g, b, a };
    d3dContext_->ClearRenderTargetView(backBufferRtv_.Get(), clearColor);
    return present();
}

bool PreviewDCompCore::present(unsigned int syncInterval)
{
    if (!ready_) {
        return false;
    }
    // Present(N, 0) — block until the Nth-next vsync. N=1 (default) is
    // the historical "lock to display rate" behaviour; N>=2 caps the
    // present rate to display_rate/N (e.g., N=2 on a 120 Hz display
    // gives a precise 60 FPS, N=3 gives 40 FPS). Skipping presents in
    // software is incompatible with the FRAME_LATENCY_WAITABLE_OBJECT
    // pacing — the waitable signals once per Present, so a stuck Present
    // would freeze the render loop.
    HRESULT hr = swapChain_->Present(syncInterval, 0);
    if (FAILED(hr)) {
        logDCompError("present", hr);
        return false;
    }
    return true;
}

HANDLE PreviewDCompCore::frameLatencyWaitable() const
{
    return frameLatencyWaitable_;
}

ID3D11Device* PreviewDCompCore::device() const
{
    return d3dDevice_.Get();
}

ID3D11DeviceContext* PreviewDCompCore::context() const
{
    return d3dContext_.Get();
}

ID3D11RenderTargetView* PreviewDCompCore::backBufferRtv() const
{
    return backBufferRtv_.Get();
}

void PreviewDCompCore::shutdown()
{
    if (!ready_ && d3dDevice_ == nullptr && compDevice_ == nullptr) {
        return;  // never initialised
    }
    if (compTarget_) {
        compTarget_->SetRoot(nullptr);
    }
    if (compDevice_) {
        // Ensure pending changes are flushed before tear-down.
        compDevice_->Commit();
    }

    compVisual_.Reset();
    compTarget_.Reset();
    compDevice_.Reset();
    backBufferRtv_.Reset();
    if (frameLatencyWaitable_ != nullptr) {
        ::CloseHandle(frameLatencyWaitable_);
        frameLatencyWaitable_ = nullptr;
    }
    swapChain2_.Reset();
    swapChain_.Reset();
    dxgiFactory_.Reset();
    if (d3dContext_) {
        d3dContext_->Flush();
        d3dContext_->ClearState();
    }
    d3dContext_.Reset();
    d3dDevice_.Reset();
    parentHwnd_ = nullptr;
    ready_ = false;
    logDComp("shutdown");
}

bool PreviewDCompCore::isReady() const
{
    return ready_;
}

bool PreviewDCompCore::isDeviceRemoved() const
{
    if (!d3dDevice_) {
        return false;  // No device to be "removed".
    }
    // GetDeviceRemovedReason returns S_OK while the device is alive and
    // an HRESULT (DXGI_ERROR_DEVICE_REMOVED / _RESET / _HUNG / driver
    // internal error) once it is removed. Cheap call — it just reads a
    // flag the runtime sets when removal is detected. Safe to call
    // from any thread because it queries device state, not anything
    // requiring the immediate context.
    const HRESULT hr = d3dDevice_->GetDeviceRemovedReason();
    return FAILED(hr);
}

QSize PreviewDCompCore::swapChainPixelSize() const
{
    return swapChainSize_;
}

#else  // !Q_OS_WIN

// Non-Windows: stubs already inline in the header. Nothing to define here.

#endif  // Q_OS_WIN

}  // namespace miacode::preview::dcomp
