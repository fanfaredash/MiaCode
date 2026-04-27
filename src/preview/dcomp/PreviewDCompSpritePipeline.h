#pragma once

// Phase 3 of the DirectComposition preview path. PreviewDCompSpritePipeline
// owns the D3D11 sprite-rendering machinery: HLSL shaders, input layout,
// dynamic vertex buffer, uniform buffer (per-frame projection matrix),
// sampler state, and the test texture used during Phase 3.1 to validate
// the pipeline before real assets are wired in.
//
// Phase 3.1 deliverable: render a single textured quad — drawn via the
// full sprite pipeline (HLSL VS+PS, vertex buffer, indexed/strip draw,
// sampled texture) — in place of Phase 2's solid-colour clear. Once that
// renders cleanly, Phase 3.2+ adds real frame-state plumbing and per-layer
// sprite descriptor consumption.
//
// Design notes:
//  * Owned by PreviewDCompRenderer (single-threaded use on the render
//    thread). Borrows ID3D11Device + ID3D11DeviceContext from
//    PreviewDCompCore at init() time.
//  * Shaders are compiled at runtime via D3DCompile rather than loaded
//    from precompiled .qsb files. Keeps Phase 3.1 self-contained — Phase
//    3.4+ may switch to precompiled DXBC if startup latency becomes
//    relevant, but compile-at-startup is < 5ms total for our two small
//    shaders.
//  * The vertex layout matches what the Phase 1.x QQuickRhiItem renderer
//    used (24 bytes: pos.xy + uv.xy + opacity + effect). Phase 3.3+ can
//    hand the same struct to the existing PreviewSceneStateBuilder
//    descriptors with no adapter layer.

#include "preview/dcomp/PreviewDCompCore.h"

#include <QSize>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace miacode::preview::dcomp {

// 24-byte vertex matching the existing PreviewSceneStateBuilder output
// format. Position is in logical (CSS-style) pixel coordinates with
// origin top-left; the projection matrix in the uniform buffer maps that
// to NDC. UVs are 0..1 over the source texture. Opacity is a per-vertex
// multiplier applied in the pixel shader. Effect is reserved for the
// second-tier shader paths used by track / judge_effect / firework
// layers (Phase 3.4+); zero for plain sprites.
struct PreviewDCompSpriteVertex
{
    float x;
    float y;
    float u;
    float v;
    float opacity;
    float effect;
};
static_assert(sizeof(PreviewDCompSpriteVertex) == 24,
              "PreviewDCompSpriteVertex layout must match shader input");

// Layout-compatible with std140 / cbuffer alignment. 4 floats per row, 4
// rows = 64 bytes for the projection. Phase 3 grows this as more uniforms
// are needed; cbuffer rounds up to 256 bytes anyway so headroom is free.
struct PreviewDCompSpriteUniformBlock
{
    float matrix[16];   // column-major float4x4, logical-pixel → NDC
    float opacity;      // global multiplier, default 1.0
    float wave;         // animation phase used by judge_effect (Phase 3.4+)
    float absWave;      // |wave|, supplied separately to skip ALU in PS
    float effect;       // global effect mode flag
};
static_assert(sizeof(PreviewDCompSpriteUniformBlock) == 80,
              "PreviewDCompSpriteUniformBlock layout must match cbuffer");

class PreviewDCompSpritePipeline
{
public:
    PreviewDCompSpritePipeline();
    ~PreviewDCompSpritePipeline();

    PreviewDCompSpritePipeline(const PreviewDCompSpritePipeline&) = delete;
    PreviewDCompSpritePipeline& operator=(const PreviewDCompSpritePipeline&) = delete;

#ifdef Q_OS_WIN
    // Compile shaders, create input layout / vertex buffer / uniform
    // buffer / sampler / test texture. Returns false if any step fails;
    // the caller (PreviewDCompRenderer) will fall back to Core::renderClear
    // until the pipeline is ready.
    bool initialise(ID3D11Device* device);

    void shutdown();

    bool isReady() const { return ready_; }

    // Phase 3.1 — render one centred textured quad to the supplied RTV.
    // The quad shows the test checkerboard at 256x256 logical pixels
    // centred in `rtvLogicalSize`. Phase 3.3+ replaces this with a
    // descriptor-list-driven draw.
    bool renderTestQuad(ID3D11DeviceContext* context,
                        ID3D11RenderTargetView* rtv,
                        QSize rtvLogicalSize);
#else
    bool initialise(void*) { return false; }
    void shutdown() {}
    bool isReady() const { return false; }
    bool renderTestQuad(void*, void*, QSize) { return false; }
#endif

private:
#ifdef Q_OS_WIN
    bool compileShaders(ID3D11Device* device);
    bool createInputLayout(ID3D11Device* device);
    bool createBuffers(ID3D11Device* device);
    bool createSamplerAndTexture(ID3D11Device* device);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    // Lifecycle-scoped: only held between compileShaders() and
    // createInputLayout() so the layout can be validated against the
    // VS bytecode. Released as soon as the layout is built.
    Microsoft::WRL::ComPtr<ID3DBlob> pendingVsBlob_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> uniformBuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> testTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> testTextureSrv_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;

    static constexpr int kMaxVertices = 4096;
    bool ready_ = false;
#endif
};

}  // namespace miacode::preview::dcomp
