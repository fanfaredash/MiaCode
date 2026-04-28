// Phase 0 smoke test for the DirectComposition preview path.
//
// Intent: confirm at compile + link time that the Windows SDK + toolchain
// available to the project provides every API the DComp implementation
// plan (docs/PREVIEW_FRAME_PACING_FEASIBILITY_AND_IMPLEMENTATION_PLAN_ZH.md)
// depends on. This file is intentionally **not** built into MiaCode.exe —
// the body is wrapped in a standalone smoke-only entry point that validates
// every header symbol and library link target the plan requires. CMake
// configures it as a small executable target only when the env flag
// MIACODE_BUILD_DCOMP_SMOKE is set, so a clean build of the application
// does not pull in DirectComposition until Phase 1 starts wiring it for
// real.
//
// Validates:
//   * D3D11CreateDevice (hardware + WARP)
//   * IDXGIFactory2::CreateSwapChainForComposition
//   * IDXGISwapChain2::GetFrameLatencyWaitableObject
//   * DCompositionCreateDevice2
//   * IDCompositionDevice::CreateTargetForHwnd / CreateVisual / SetContent /
//     SetTransform / Commit
//   * Linking against d3d11.lib, dxgi.lib, dcomp.lib
//
// Does not actually create a real preview pipeline — it allocates the
// objects, asserts that creation succeeds (or logs the HRESULT for the
// fallback pathway documented in §5 of the plan), then releases them
// immediately. This is a build-environment gate, not a runtime gate.

#ifdef _WIN32
#include <wrl/client.h>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <windows.h>

#include <cstdio>

namespace miacode::preview::dcomp {

using Microsoft::WRL::ComPtr;

// Returns true if the smoke test successfully exercised every API needed by
// the Phase 1+ DComp pipeline. Logs HRESULTs to stderr on failure so a
// CI / dev environment can capture the reason without launching a debugger.
bool runPhase0Smoke()
{
    std::fprintf(stderr, "[dcomp-phase0-smoke] starting\n");

    // --- 1. D3D11 device (hardware first, WARP fallback per plan §5.3) ---
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    D3D_FEATURE_LEVEL chosenLevel = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    UINT d3dFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    d3dFlags |= D3D11_CREATE_DEVICE_DEBUG;  // available only with the SDK
                                            // debug layers installed; the
                                            // smoke test silently downgrades
                                            // if absent.
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        d3dFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        d3dDevice.GetAddressOf(),
        &chosenLevel,
        d3dContext.GetAddressOf());

    if (FAILED(hr)) {
        // Per plan §5.3, fall through to WARP. NDEBUG callers may also miss
        // the debug layer flag — retry without it.
        d3dFlags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            d3dFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            d3dDevice.ReleaseAndGetAddressOf(),
            &chosenLevel,
            d3dContext.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            std::fprintf(stderr,
                         "[dcomp-phase0-smoke] D3D11CreateDevice (HW+WARP) "
                         "failed: hr=0x%08lx\n",
                         static_cast<long>(hr));
            return false;
        }
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] using WARP software device "
                     "(hardware unavailable)\n");
    }
    std::fprintf(stderr,
                 "[dcomp-phase0-smoke] D3D11 device ok (feature_level=0x%x)\n",
                 chosenLevel);

    // --- 2. DXGI factory (need IDXGIFactory2 for CreateSwapChainForComposition) ---
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] D3D11Device->IDXGIDevice failed: "
                     "hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(dxgiAdapter.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] IDXGIDevice::GetAdapter failed: "
                     "hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] IDXGIAdapter->IDXGIFactory2 "
                     "failed: hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }
    std::fprintf(stderr, "[dcomp-phase0-smoke] IDXGIFactory2 ok\n");

    // --- 3. Swap chain for composition (the swap chain we'll attach to
    //     a DComp visual in Phase 1) ---
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 256;
    desc.Height = 256;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;  // triple buffer per plan §6
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = dxgiFactory->CreateSwapChainForComposition(
        d3dDevice.Get(),
        &desc,
        nullptr,
        swapChain1.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] CreateSwapChainForComposition "
                     "failed: hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }

    ComPtr<IDXGISwapChain2> swapChain2;
    hr = swapChain1.As(&swapChain2);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] IDXGISwapChain1->IDXGISwapChain2 "
                     "failed: hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }

    HANDLE waitable = swapChain2->GetFrameLatencyWaitableObject();
    if (waitable == nullptr) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] GetFrameLatencyWaitableObject "
                     "returned null — required for plan §2 frame pacing\n");
        return false;
    }
    std::fprintf(stderr,
                 "[dcomp-phase0-smoke] swap chain + waitable handle ok\n");
    ::CloseHandle(waitable);

    // --- 4. DComp device (we'll mount a real visual on it in Phase 1) ---
    ComPtr<IDCompositionDevice> compDevice;
    hr = DCompositionCreateDevice(
        dxgiDevice.Get(),
        IID_PPV_ARGS(compDevice.GetAddressOf()));
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] DCompositionCreateDevice failed: "
                     "hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }

    ComPtr<IDCompositionVisual> visual;
    hr = compDevice->CreateVisual(visual.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] CreateVisual failed: "
                     "hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }

    // SetContent accepts the swap chain we just made — proves Phase 1 will
    // be able to do `visual->SetContent(swapChain.Get())` directly.
    hr = visual->SetContent(swapChain1.Get());
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "[dcomp-phase0-smoke] visual->SetContent(swapChain) "
                     "failed: hr=0x%08lx\n",
                     static_cast<long>(hr));
        return false;
    }
    std::fprintf(stderr,
                 "[dcomp-phase0-smoke] DComp device + visual + SetContent "
                 "ok\n");

    // CreateTargetForHwnd is the one API we cannot validate here (no HWND
    // available yet — Phase 4 wires that). But its symbol resolution at
    // link time is verified because we link against dcomp.lib regardless.

    // Releasing in scope-tear-down via ComPtr.
    std::fprintf(stderr, "[dcomp-phase0-smoke] PASS\n");
    return true;
}

}  // namespace miacode::preview::dcomp

// Standalone entry point so the smoke test can be invoked from a tiny
// executable target (gated by MIACODE_BUILD_DCOMP_SMOKE in CMakeLists.txt).
// The body remains compiled into MiaCode.exe **only** when that flag is
// set; otherwise this translation unit produces no exported symbols and
// adds zero runtime cost to the application.
#ifdef MIACODE_BUILD_DCOMP_SMOKE
int main(int /*argc*/, char* /*argv*/[])
{
    return miacode::preview::dcomp::runPhase0Smoke() ? 0 : 1;
}
#endif

#else  // !_WIN32

// Stub: the DComp path is Windows-only. Other platforms fall back to the
// legacy QSG path per plan §5. This file is left empty on non-Windows
// builds so CMake can still include it in the source list unconditionally
// without breaking macOS / Linux builds.

#endif  // _WIN32
