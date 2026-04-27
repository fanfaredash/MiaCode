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
    bool resize(QSize newPixelSize);

    // Updates the visual's affine transform: position relative to the
    // parent HWND's client-area origin, scaled to `displaySize` pixels. The
    // swap chain's internal buffer size doesn't have to match — DComp
    // stretches as needed (the resize() call above is the eventual
    // catch-up).
    bool setVisualTransform(int xPx, int yPx, QSize displaySize);

    // Phase 1 deliverable: clear the back buffer to red and present. No
    // animation. Phase 2 replaces this with the real render loop.
    bool renderTestFrame();

    // True iff initialise() has succeeded and shutdown() has not been
    // called.
    bool isReady() const;

    QSize swapChainPixelSize() const;
#else
    // Non-Windows stubs so callers can compile.
    bool initialise(void*, QSize) { return false; }
    void shutdown() {}
    bool resize(QSize) { return false; }
    bool setVisualTransform(int, int, QSize) { return false; }
    bool renderTestFrame() { return false; }
    bool isReady() const { return false; }
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
    HANDLE frameLatencyWaitable_ = nullptr;
    HWND parentHwnd_ = nullptr;
    QSize swapChainSize_;
    bool ready_ = false;
#endif
};

}  // namespace miacode::preview::dcomp
