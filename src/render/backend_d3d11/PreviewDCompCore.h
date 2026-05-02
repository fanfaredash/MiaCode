#pragma once

// Phase 1 of the DirectComposition preview path. PreviewDCompCore owns the
// D3D11 device, the DXGI swap chain (created via CreateSwapChainForComposition
// + FRAME_LATENCY_WAITABLE_OBJECT), and the DComp device/target/visual that
// place the swap chain into the system compositor's visual tree under a
// specific parent HWND. See docs/PREVIEW_FRAME_PACING_FEASIBILITY_AND_IMPLEMENTATION_PLAN_ZH.md
// — this is the §3 Phase 1 deliverable.
//
// Phase 1 scope: render a static red rectangle, demonstrate the visual is
// correctly attached to the parent HWND, demonstrate resize handling works.
// All rendering happens on the GUI thread when render() is called; the
// dedicated render thread + frame-pacing waitable arrives in Phase 2.
//
// Windows-only: every member is guarded by Q_OS_WIN. On other platforms the
// class compiles to an empty stub so callers can include it unconditionally.

#include <QSize>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX  // Block <windows.h>'s min/max macros — they break
                  // std::min/std::max in any TU that pulls this header in.
#endif
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#endif

namespace miacode::preview::dcomp {

class PreviewDCompCore
{
public:
    PreviewDCompCore();
    ~PreviewDCompCore();

    PreviewDCompCore(const PreviewDCompCore&) = delete;
    PreviewDCompCore& operator=(const PreviewDCompCore&) = delete;

#ifdef Q_OS_WIN
    // Creates the D3D11 device, swap chain, and DComp visual tree. Attaches
    // the visual tree to `parentHwnd` so the OS compositor knows where to
    // composite our visual. Initial swap chain size is `pixelSize`. Returns
    // false if any step fails (HRESULT logged via debug_log under
    // runtime/preview/dcomp); on failure the caller should fall back to the
    // legacy QSG path per plan §5.3.
    bool initialise(HWND parentHwnd, QSize pixelSize);

    // Releases all D3D11/DXGI/DComp resources in the correct order.
    // Idempotent — safe to call from the destructor or repeatedly.
    void shutdown();

    // Asks the swap chain to resize to `newPixelSize`. Cheap (uses
    // ResizeBuffers, which keeps the existing back-buffer pool). Plan §4.5
    // discusses why this is decoupled from the visual transform.
    // Phase 4d-fix7-final2 — `vtDisplaySizeOverride` (optional) lets
    // the caller specify the displaySize to use when re-applying the
    // visual transform after the swap chain resize completes. If
    // invalid (default-constructed QSize), the post-resize re-apply
    // falls back to the global lastVisualTransformDisplaySize_.
    // Used by the render thread to pass the displaySize that was
    // captured atomically with the resize request, avoiding races
    // with concurrent GUI-thread setVisualTransform updates.
    bool resize(QSize newPixelSize, QSize vtDisplaySizeOverride = QSize());

    // Updates the visual's affine transform: position relative to the
    // parent HWND's client-area origin, scaled to `displaySize` pixels. The
    // swap chain's internal buffer size doesn't have to match — DComp
    // stretches as needed (the resize() call above is the eventual
    // catch-up).
    bool setVisualTransform(int xPx, int yPx, QSize displaySize);

    // Phase 1 deliverable: clear the back buffer to red and present. No
    // animation. Phase 2 replaces this with the real render loop.
    bool renderTestFrame();

    // Phase 2: parameterised clear+present, called from the render
    // thread on every wake-up. The alpha component must be 1.0 with the
    // DXGI_ALPHA_MODE_PREMULTIPLIED swap chain we configured in
    // createSwapChain — anything less and DComp would alpha-blend our
    // visual with the QML scene below, which Phase 1 doesn't want.
    // Thread safety: caller must ensure only the render thread calls
    // this. The immediate D3D11 context is not safe to share.
    bool renderClear(float r, float g, float b, float a);

    // Phase 3: split present from the clear, so the sprite pipeline can
    // do its own clear (or skip clearing entirely) and Core just flips
    // the swap chain. Returns false if !ready_ or Present fails.
    // Thread safety: render thread only, same as renderClear.
    //
    // syncInterval is forwarded to ::Present(syncInterval, 0). 1 = wait
    // for next vsync (legacy default); 2 = wait for two vsyncs (half
    // rate, e.g. 60 FPS on a 120 Hz display). The renderer computes
    // this from the user's FPS-cap option vs the display refresh rate.
    bool present(unsigned int syncInterval = 1);

    // Returns the FRAME_LATENCY_WAITABLE_OBJECT handle the render thread
    // blocks on (WaitForSingleObject). Lifetime is tied to this Core —
    // caller must not CloseHandle it. Returns nullptr if the swap chain
    // hasn't been created yet (i.e. before initialise() succeeds).
    HANDLE frameLatencyWaitable() const;

    // Phase 3 accessors — borrowed by PreviewDCompSpritePipeline so it
    // can build buffers, compile shaders, and issue draw calls without
    // owning the device. Caller must use these only from the render
    // thread (single-thread rule for the immediate D3D11 context).
    // Lifetime tied to this Core.
    ID3D11Device* device() const;
    ID3D11DeviceContext* context() const;
    ID3D11RenderTargetView* backBufferRtv() const;

    // True iff initialise() has succeeded and shutdown() has not been
    // called.
    bool isReady() const;

    // True if the D3D11 device has been removed (driver crash, GPU
    // reset, TDR timeout, OOM removal, etc.). Wraps
    // ID3D11Device::GetDeviceRemovedReason — if this returns FAILED the
    // device is unrecoverable and the swap chain will fail every
    // subsequent Present forever. The render thread polls this each
    // iteration so it can exit cleanly instead of looping on a stuck
    // FRAME_LATENCY_WAITABLE_OBJECT (which stops signalling once the
    // device is removed).
    //
    // Observed in user logs at 06:08:30.623 (rapid progress-bar drag):
    // [preview/dcomp] op=present hr=0x887a0005 (DXGI_ERROR_DEVICE_REMOVED).
    // Without this check the render thread blocked on
    // WaitForSingleObject(waitable, INFINITE) forever, freezing the
    // entire DComp pipeline until the user killed the app.
    bool isDeviceRemoved() const;

    QSize swapChainPixelSize() const;
#else
    // Non-Windows stubs so callers can compile.
    bool initialise(void*, QSize) { return false; }
    void shutdown() {}
    bool resize(QSize) { return false; }
    bool setVisualTransform(int, int, QSize) { return false; }
    bool renderTestFrame() { return false; }
    bool renderClear(float, float, float, float) { return false; }
    bool present() { return false; }
    void* frameLatencyWaitable() const { return nullptr; }
    void* device() const { return nullptr; }
    void* context() const { return nullptr; }
    void* backBufferRtv() const { return nullptr; }
    bool isReady() const { return false; }
    bool isDeviceRemoved() const { return false; }
    QSize swapChainPixelSize() const { return {}; }
#endif

private:
#ifdef Q_OS_WIN
    bool createD3D11Device();
    bool createSwapChain(HWND parentHwnd, QSize pixelSize);
    bool createDCompVisualTree(HWND parentHwnd);
    bool createBackBufferRtv();
    void releaseBackBufferRtv();

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backBufferRtv_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> compTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> compVisual_;

    // Phase 4a — remembered visual transform args. setVisualTransform
    // computes the visual's scale as displaySize / swapChainSize_; when
    // a resize is requested from the GUI thread immediately followed by
    // setVisualTransform, the swap chain hasn't yet been resized (the
    // render thread does that between presents), so the scale is wrong
    // and *stays wrong* until the GUI thread happens to call
    // setVisualTransform again. Storing the requested displaySize lets
    // resize() re-apply the transform once the swap chain catches up,
    // collapsing the scale back to 1.0.
    bool lastVisualTransformValid_ = false;
    int lastVisualTransformX_ = 0;
    int lastVisualTransformY_ = 0;
    QSize lastVisualTransformDisplaySize_;
    HANDLE frameLatencyWaitable_ = nullptr;
    HWND parentHwnd_ = nullptr;
    QSize swapChainSize_;
    bool ready_ = false;
#endif
};

}  // namespace miacode::preview::dcomp
