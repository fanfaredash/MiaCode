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

#include "render/backend_d3d11/PreviewDCompCore.h"
#include "render/backend_d3d11/PreviewDCompFrameStateSnapshot.h"
#include "render/backend_d3d11/PreviewDCompTextureCache.h"

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

// Phase 3.5c — firework material constant buffer. Layout matches the
// legacy GLSL fragment shader's std140 block in
// src/preview/quick_scene/shaders/PreviewFireworkMaterial.frag (16-byte
// alignment, vec4-grouped scalars). Updated once per firework draw —
// typical chart has ≤1 active firework per frame.
struct PreviewDCompFireworkUniformBlock
{
    float matrix[16];           // 64 bytes: projection (logical-pixel → NDC)
    float opacity;              //  4
    float quadRadius;           //  4
    float clipRadius;           //  4
    float outerRadius;          //  4
    float fireworkAndHole[4];   // 16: alpha, rotation°, holeRadius, holeMaskRadius
    float colorBallSmall[4];    // 16: ballRadius, ballAlpha, sourceAspect, fallbackBallRadius
    float colorBallBig[4];      // 16: bigBallRadius, bigBallAlpha, fallbackBallAlpha, renderFlags
    float sourceRect[4];        // 16: texture sub-region (x, y, w, h) in [0,1]
    // Phase 3.5c-fix: clip-circle offset (clipCenter - fireworkCenter) in
    // scene pixels. The legacy QSG path renders the firework inside a
    // separate QSGClipNode; D3D11 has no direct equivalent for circular
    // clips, so the PS computes distance from localPos to clipOffset.xy
    // and discards fragments outside clipRadius. zw padding to keep the
    // 16-byte alignment.
    float clipOffset[4];        // 16: x, y, _pad, _pad
};
static_assert(sizeof(PreviewDCompFireworkUniformBlock) == 160,
              "PreviewDCompFireworkUniformBlock must match cbuffer layout");

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
    // Phase 3.2 — quad horizontal position is offset by the snapshot's
    // playheadSeconds so playback motion is visually verifiable. The
    // quad shows the test checkerboard at 200x200 logical pixels.
    // Phase 3.3+ replaces this with a descriptor-list-driven draw.
    bool renderTestQuad(ID3D11DeviceContext* context,
                        ID3D11RenderTargetView* rtv,
                        QSize rtvLogicalSize,
                        const PreviewDCompFrameStateSnapshot& snapshot);

    // Phase 3.3c — render sprites from the snapshot descriptor list.
    // Each descriptor's QImage* is resolved to an ID3D11ShaderResourceView
    // through `textureCache`. Sprites grouped by SRV produce one draw
    // call per group. The RTV is cleared to opaque black before drawing
    // so the previous frame's content is gone and DComp blending starts
    // from a clean slate. Returns false if not ready / null inputs.
    bool renderSnapshot(ID3D11DeviceContext* context,
                        ID3D11Device* device,
                        ID3D11RenderTargetView* rtv,
                        QSize rtvLogicalSize,
                        const PreviewDCompFrameStateSnapshot& snapshot,
                        PreviewDCompTextureCache& textureCache);
#else
    bool initialise(void*) { return false; }
    void shutdown() {}
    bool isReady() const { return false; }
    bool renderTestQuad(void*, void*, QSize,
                         const PreviewDCompFrameStateSnapshot&) { return false; }
    bool renderSnapshot(void*, void*, void*, QSize,
                         const PreviewDCompFrameStateSnapshot&,
                         PreviewDCompTextureCache&) { return false; }
#endif

private:
#ifdef Q_OS_WIN
    bool compileShaders(ID3D11Device* device);
    bool createInputLayout(ID3D11Device* device);
    bool createBuffers(ID3D11Device* device);
    bool createSamplerAndTexture(ID3D11Device* device);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    // Phase 3.5a — second pixel shader for circle batches. Same VS +
    // same input layout; the PS reads the colour from the vertex's
    // (uv.x, uv.y, opacity, effect) slots — repurposed as RGBA — and
    // ignores the texture sampler entirely.
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psSolid_;
    // Phase 3.5c — firework PS. Same VS + same input layout; the PS
    // ignores the per-vertex opacity/effect and pulls all rendering
    // parameters from a dedicated constant buffer. Implements the
    // procedural sector + color-ball logic ported from the legacy
    // GLSL shader.
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psFirework_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> fireworkUniformBuffer_;
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

    // Phase 3.5a expanded the budget to accommodate circle tessellation
    // alongside sprite quads. A 32-segment filled circle is 96 vertices;
    // a 32-segment stroke ring is 192 vertices (six per segment quad).
    // Worst-case ~10 circles ≈ 2880 verts + ~100 sprite quads ≈ 600
    // verts = ~3.5k, comfortably under 8192.
    static constexpr int kMaxVertices = 8192;
    bool ready_ = false;
#endif
};

}  // namespace miacode::preview::dcomp
