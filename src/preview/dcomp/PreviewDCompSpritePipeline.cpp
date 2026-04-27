#include "preview/dcomp/PreviewDCompSpritePipeline.h"

#include "common/DebugLog.h"

#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3dcompiler.h>
#include <windows.h>

#include <array>
#include <cstring>
#include <vector>
#endif

namespace miacode::preview::dcomp {

namespace {

void logPipeline(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/dcomp/pipeline"),
        payload);
}

#ifdef Q_OS_WIN

void logHr(const char* op, HRESULT hr, const QString& extra = QString())
{
    QString payload = QStringLiteral("op=%1 hr=0x%2")
        .arg(QString::fromLatin1(op))
        .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/dcomp/pipeline"),
        payload);
}

// Compact HLSL shader source. The vertex layout + uniform layout match
// the C++ structs PreviewDCompSpriteVertex / PreviewDCompSpriteUniformBlock
// — keep them in sync if either changes.
//
// Premultiplied-alpha output: the swap chain is configured with
// DXGI_ALPHA_MODE_PREMULTIPLIED (Phase 1 §4.5), and DComp blends our
// visual against the parent QML scene using the same convention. We
// multiply RGB by alpha in the pixel shader before returning.
constexpr const char* kSpriteVS = R"HLSL(
cbuffer SpriteUniforms : register(b0)
{
    float4x4 projection;
    float    globalOpacity;
    float    wave;
    float    absWave;
    float    effect;
};

struct VSInput
{
    float2 pos      : POSITION;
    float2 uv       : TEXCOORD0;
    float  opacity  : TEXCOORD1;
    float  effectIn : TEXCOORD2;
};

struct PSInput
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float  opacity  : TEXCOORD1;
    float  effectIn : TEXCOORD2;
};

PSInput main(VSInput input)
{
    PSInput o;
    o.pos = mul(projection, float4(input.pos, 0.0, 1.0));
    o.uv = input.uv;
    o.opacity = input.opacity * globalOpacity;
    o.effectIn = input.effectIn;
    return o;
}
)HLSL";

constexpr const char* kSpritePS = R"HLSL(
Texture2D    spriteTexture : register(t0);
SamplerState spriteSampler : register(s0);

struct PSInput
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float  opacity  : TEXCOORD1;
    float  effectIn : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 c = spriteTexture.Sample(spriteSampler, input.uv);
    c *= input.opacity;
    return c;
}
)HLSL";

// Phase 3.5a — pixel shader for solid-colour primitives (circles in
// 3.5a, arcs/fireworks in 3.5b/c). The vertex's (uv.x, uv.y, opacity,
// effect) slots are repurposed as straight (R, G, B, A). Output is
// premultiplied to match the swap chain's DXGI_ALPHA_MODE_PREMULTIPLIED
// the same way the sprite path does (sprite path multiplies the sampled
// premultiplied texture by opacity; we multiply RGB by A here so a pure
// red half-transparent fill stores as `(0.5, 0, 0, 0.5)` in the RTV).
constexpr const char* kSolidPS = R"HLSL(
struct PSInput
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float  opacity  : TEXCOORD1;
    float  effectIn : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET
{
    float r = input.uv.x;
    float g = input.uv.y;
    float b = input.opacity;
    float a = input.effectIn;
    return float4(r * a, g * a, b * a, a);
}
)HLSL";

// Build a 64x64 RGBA8 checkerboard pattern with two contrasting colours
// + a magenta border so the test texture is unmistakable. Returned as
// a row-major byte array suitable for ID3D11Device::CreateTexture2D
// initial data.
std::vector<unsigned char> buildCheckerboardPixels(int side)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(side * side * 4), 0);
    constexpr int kCellPx = 8;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int i = (y * side + x) * 4;
            const bool border = (x == 0 || y == 0 || x == side - 1 || y == side - 1);
            if (border) {
                pixels[i + 0] = 255;  // R
                pixels[i + 1] = 0;    // G
                pixels[i + 2] = 255;  // B (magenta)
                pixels[i + 3] = 255;  // A
                continue;
            }
            const bool dark = ((x / kCellPx) + (y / kCellPx)) & 1;
            const unsigned char v = dark ? 64 : 220;
            pixels[i + 0] = v;
            pixels[i + 1] = v;
            pixels[i + 2] = v;
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

// Column-major 4x4 orthographic projection mapping logical (CSS-style)
// pixel coordinates with origin at top-left to NDC. Output is stored in
// the cbuffer's `projection` field (which the HLSL declares as a
// float4x4 — D3D11 default packing is column-major).
void writeOrthoTopLeftPixelMatrix(float* mat, int widthPx, int heightPx)
{
    const float w = static_cast<float>(widthPx > 0 ? widthPx : 1);
    const float h = static_cast<float>(heightPx > 0 ? heightPx : 1);
    // Column 0
    mat[0]  = 2.0f / w;
    mat[1]  = 0.0f;
    mat[2]  = 0.0f;
    mat[3]  = 0.0f;
    // Column 1
    mat[4]  = 0.0f;
    mat[5]  = -2.0f / h;     // Y flipped: top-left origin → NDC top is +1
    mat[6]  = 0.0f;
    mat[7]  = 0.0f;
    // Column 2
    mat[8]  = 0.0f;
    mat[9]  = 0.0f;
    mat[10] = 1.0f;
    mat[11] = 0.0f;
    // Column 3 (translation)
    mat[12] = -1.0f;
    mat[13] = 1.0f;
    mat[14] = 0.0f;
    mat[15] = 1.0f;
}

#endif  // Q_OS_WIN

}  // namespace

PreviewDCompSpritePipeline::PreviewDCompSpritePipeline() = default;

PreviewDCompSpritePipeline::~PreviewDCompSpritePipeline()
{
    shutdown();
}

#ifdef Q_OS_WIN

bool PreviewDCompSpritePipeline::initialise(ID3D11Device* device)
{
    if (ready_) {
        return true;
    }
    if (device == nullptr) {
        logPipeline("init_failed", QStringLiteral("reason=null_device"));
        return false;
    }
    if (!compileShaders(device)) {
        shutdown();
        return false;
    }
    if (!createInputLayout(device)) {
        shutdown();
        return false;
    }
    if (!createBuffers(device)) {
        shutdown();
        return false;
    }
    if (!createSamplerAndTexture(device)) {
        shutdown();
        return false;
    }

    // Premultiplied-alpha blend state. D3D11_RENDER_TARGET_BLEND_DESC
    // defaults aren't quite right for premultiplied; build it explicitly.
    D3D11_BLEND_DESC bd{};
    bd.AlphaToCoverageEnable = FALSE;
    bd.IndependentBlendEnable = FALSE;
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr = device->CreateBlendState(&bd, blendState_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_blend_state", hr);
        shutdown();
        return false;
    }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = FALSE;
    rd.MultisampleEnable = FALSE;
    rd.AntialiasedLineEnable = FALSE;
    hr = device->CreateRasterizerState(&rd, rasterizerState_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_rasterizer_state", hr);
        shutdown();
        return false;
    }

    ready_ = true;
    logPipeline("initialised");
    return true;
}

bool PreviewDCompSpritePipeline::compileShaders(ID3D11Device* device)
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        kSpriteVS, std::strlen(kSpriteVS), "PreviewDCompSpriteVS",
        nullptr, nullptr, "main", "vs_5_0",
        compileFlags, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        const QString errStr = errorBlob
            ? QString::fromUtf8(static_cast<const char*>(errorBlob->GetBufferPointer()),
                                static_cast<int>(errorBlob->GetBufferSize()))
            : QStringLiteral("(no error blob)");
        logHr("compile_vs", hr, QStringLiteral("err=%1").arg(errStr.left(400)));
        return false;
    }
    hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        nullptr, vs_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_vs", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    hr = D3DCompile(
        kSpritePS, std::strlen(kSpritePS), "PreviewDCompSpritePS",
        nullptr, nullptr, "main", "ps_5_0",
        compileFlags, 0, psBlob.GetAddressOf(), errorBlob.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        const QString errStr = errorBlob
            ? QString::fromUtf8(static_cast<const char*>(errorBlob->GetBufferPointer()),
                                static_cast<int>(errorBlob->GetBufferSize()))
            : QStringLiteral("(no error blob)");
        logHr("compile_ps", hr, QStringLiteral("err=%1").arg(errStr.left(400)));
        return false;
    }
    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
        nullptr, ps_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_ps", hr);
        return false;
    }

    // Phase 3.5a — solid-colour PS for non-textured primitives.
    Microsoft::WRL::ComPtr<ID3DBlob> psSolidBlob;
    hr = D3DCompile(
        kSolidPS, std::strlen(kSolidPS), "PreviewDCompSolidPS",
        nullptr, nullptr, "main", "ps_5_0",
        compileFlags, 0, psSolidBlob.GetAddressOf(), errorBlob.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        const QString errStr = errorBlob
            ? QString::fromUtf8(static_cast<const char*>(errorBlob->GetBufferPointer()),
                                static_cast<int>(errorBlob->GetBufferSize()))
            : QStringLiteral("(no error blob)");
        logHr("compile_ps_solid", hr, QStringLiteral("err=%1").arg(errStr.left(400)));
        return false;
    }
    hr = device->CreatePixelShader(
        psSolidBlob->GetBufferPointer(), psSolidBlob->GetBufferSize(),
        nullptr, psSolid_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_ps_solid", hr);
        return false;
    }

    // Stash the VS blob on the pipeline so createInputLayout can validate
    // against the actual bytecode — CreateInputLayout requires the VS
    // bytecode to verify semantic bindings, and a fresh recompile would
    // double the (already-tiny) shader compile cost. The blob is released
    // once the input layout has been built.
    pendingVsBlob_ = vsBlob;
    return true;
}

bool PreviewDCompSpritePipeline::createInputLayout(ID3D11Device* device)
{
    if (!pendingVsBlob_) {
        logPipeline("create_input_layout_failed",
                    QStringLiteral("reason=no_vs_blob"));
        return false;
    }
    const D3D11_INPUT_ELEMENT_DESC layoutElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,    0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,    0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    HRESULT hr = device->CreateInputLayout(
        layoutElements, ARRAYSIZE(layoutElements),
        pendingVsBlob_->GetBufferPointer(), pendingVsBlob_->GetBufferSize(),
        inputLayout_.GetAddressOf());
    pendingVsBlob_.Reset();  // no longer needed after layout creation
    if (FAILED(hr)) {
        logHr("create_input_layout", hr);
        return false;
    }
    return true;
}

bool PreviewDCompSpritePipeline::createBuffers(ID3D11Device* device)
{
    // Dynamic vertex buffer: capacity for kMaxVertices, mapped each frame
    // with D3D11_MAP_WRITE_DISCARD. Phase 3.1 only writes 4 vertices
    // per frame, but Phase 3.3+ will fill it with the real sprite stream.
    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = sizeof(PreviewDCompSpriteVertex) * kMaxVertices;
    vbd.Usage = D3D11_USAGE_DYNAMIC;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&vbd, nullptr, vertexBuffer_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_vertex_buffer", hr);
        return false;
    }

    // Uniform buffer: cbuffer must be at least 16-byte aligned and we
    // pack 80 bytes — D3D11 rounds up to a 16-byte boundary internally,
    // 96 bytes effective. UpdateSubresource path (vs Map) is fine for a
    // small per-frame cbuffer.
    D3D11_BUFFER_DESC ubd{};
    ubd.ByteWidth = sizeof(PreviewDCompSpriteUniformBlock);
    ubd.Usage = D3D11_USAGE_DEFAULT;
    ubd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&ubd, nullptr, uniformBuffer_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_uniform_buffer", hr);
        return false;
    }
    return true;
}

bool PreviewDCompSpritePipeline::createSamplerAndTexture(ID3D11Device* device)
{
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = device->CreateSamplerState(&sd, sampler_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_sampler", hr);
        return false;
    }

    constexpr int kSide = 64;
    auto pixels = buildCheckerboardPixels(kSide);

    D3D11_TEXTURE2D_DESC td{};
    td.Width = kSide;
    td.Height = kSide;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = pixels.data();
    srd.SysMemPitch = static_cast<UINT>(kSide * 4);

    hr = device->CreateTexture2D(&td, &srd, testTexture_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_test_texture", hr);
        return false;
    }
    hr = device->CreateShaderResourceView(testTexture_.Get(), nullptr,
                                           testTextureSrv_.GetAddressOf());
    if (FAILED(hr)) {
        logHr("create_test_texture_srv", hr);
        return false;
    }
    return true;
}

void PreviewDCompSpritePipeline::shutdown()
{
    rasterizerState_.Reset();
    blendState_.Reset();
    testTextureSrv_.Reset();
    testTexture_.Reset();
    sampler_.Reset();
    uniformBuffer_.Reset();
    vertexBuffer_.Reset();
    inputLayout_.Reset();
    ps_.Reset();
    psSolid_.Reset();
    vs_.Reset();
    ready_ = false;
}

bool PreviewDCompSpritePipeline::renderTestQuad(ID3D11DeviceContext* context,
                                                ID3D11RenderTargetView* rtv,
                                                QSize rtvLogicalSize,
                                                const PreviewDCompFrameStateSnapshot& snapshot)
{
    if (!ready_ || context == nullptr || rtv == nullptr) {
        return false;
    }
    const int width = rtvLogicalSize.width() > 0 ? rtvLogicalSize.width() : 1;
    const int height = rtvLogicalSize.height() > 0 ? rtvLogicalSize.height() : 1;

    // Phase 3.2: centre a smaller-than-RTV quad in the swap chain so
    // there's actual room for it to slide. The Phase-1 swap chain is
    // sized to the placeholder rect (200×200 for the top-left demo);
    // making the quad a fraction of that gives us a visible bounce
    // range. Horizontal position is offset by the snapshot revision —
    // any frameStateChanged advances it by 0.5 px, so 60 snapshots/sec
    // ≈ 30 px/sec, slow enough to be readable. Triangle-wave bounces
    // off both edges, so motion is sustained indefinitely.
    constexpr float kQuadFraction = 0.4f;  // quad side = 40% of RTV side
    constexpr float kPxPerRevision = 0.5f;
    const float quadSide = static_cast<float>(std::min(width, height)) * kQuadFraction;
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    const float halfSide = quadSide * 0.5f;
    const float maxOffset = (cx - halfSide) > 0.0f ? (cx - halfSide) : 0.0f;
    float rawOffset = static_cast<float>(snapshot.revision) * kPxPerRevision;
    if (maxOffset > 0.0f) {
        const float period = maxOffset * 2.0f;
        float wrapped = std::fmod(rawOffset, period);
        if (wrapped < 0.0f) wrapped += period;
        rawOffset = wrapped > maxOffset ? (period - wrapped) : wrapped;
    } else {
        rawOffset = 0.0f;
    }
    const float originX = cx + rawOffset - halfSide;
    const float left = originX;
    const float top = cy - halfSide;
    const float right = originX + quadSide;
    const float bottom = cy + halfSide;

    // Triangle strip: TL, TR, BL, BR
    const PreviewDCompSpriteVertex verts[4] = {
        { left,  top,    0.0f, 0.0f, 1.0f, 0.0f },
        { right, top,    1.0f, 0.0f, 1.0f, 0.0f },
        { left,  bottom, 0.0f, 1.0f, 1.0f, 0.0f },
        { right, bottom, 1.0f, 1.0f, 1.0f, 0.0f },
    };

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context->Map(vertexBuffer_.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        logHr("map_vertex_buffer", hr);
        return false;
    }
    std::memcpy(mapped.pData, verts, sizeof(verts));
    context->Unmap(vertexBuffer_.Get(), 0);

    PreviewDCompSpriteUniformBlock uniforms{};
    writeOrthoTopLeftPixelMatrix(uniforms.matrix, width, height);
    uniforms.opacity = 1.0f;
    uniforms.wave = 0.0f;
    uniforms.absWave = 0.0f;
    uniforms.effect = 0.0f;
    context->UpdateSubresource(uniformBuffer_.Get(), 0, nullptr, &uniforms, 0, 0);

    // Render target setup. RTV is bound + cleared to a dim grey so the
    // textured quad is visually distinct from the swap chain background
    // and missing draws are obvious.
    const float clearColor[4] = { 0.15f, 0.15f, 0.18f, 1.0f };
    ID3D11RenderTargetView* rtvs[1] = { rtv };
    context->OMSetRenderTargets(1, rtvs, nullptr);
    context->ClearRenderTargetView(rtv, clearColor);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
    context->RSSetState(rasterizerState_.Get());

    const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    context->OMSetBlendState(blendState_.Get(), blendFactor, 0xffffffff);

    context->IASetInputLayout(inputLayout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    UINT stride = sizeof(PreviewDCompSpriteVertex);
    UINT offset = 0;
    ID3D11Buffer* vbs[1] = { vertexBuffer_.Get() };
    context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);

    context->VSSetShader(vs_.Get(), nullptr, 0);
    ID3D11Buffer* cbs[1] = { uniformBuffer_.Get() };
    context->VSSetConstantBuffers(0, 1, cbs);

    context->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11SamplerState* samplers[1] = { sampler_.Get() };
    context->PSSetSamplers(0, 1, samplers);
    ID3D11ShaderResourceView* srvs[1] = { testTextureSrv_.Get() };
    context->PSSetShaderResources(0, 1, srvs);

    context->Draw(4, 0);
    return true;
}

namespace {

// Convert one PreviewSpriteDescriptor to 6 vertices forming two triangles
// (a triangle list, not strip — we need to merge sprite quads into a
// single batched draw, and a triangle list lets multiple quads share
// one Draw call without degenerate connectors). UVs derived from the
// sprite's sourceRect, position derived from center+width+height with
// rotation around the centre.
void emitSpriteVertices(const miacode::preview::scene::PreviewSpriteDescriptor& sprite,
                         std::vector<PreviewDCompSpriteVertex>& out)
{
    if (sprite.image == nullptr || sprite.image->isNull()) {
        return;
    }
    if (sprite.width <= 0.0 || sprite.height <= 0.0) {
        return;
    }
    if (sprite.opacity <= 0.0) {
        return;
    }

    const float cx = static_cast<float>(sprite.center.x());
    const float cy = static_cast<float>(sprite.center.y());
    const float halfW = static_cast<float>(sprite.width) * 0.5f;
    const float halfH = static_cast<float>(sprite.height) * 0.5f;
    const float opacity = static_cast<float>(qBound(0.0, sprite.opacity, 1.0));
    const float effect = static_cast<float>(static_cast<int>(sprite.effect));

    // UV from sourceRect. Empty source rect → use full image (0..1).
    const QImage* img = sprite.image;
    QRectF source = sprite.sourceRect;
    if (source.isEmpty()) {
        source = QRectF(0, 0, img->width(), img->height());
    }
    const float invW = img->width() > 0 ? 1.0f / static_cast<float>(img->width()) : 1.0f;
    const float invH = img->height() > 0 ? 1.0f / static_cast<float>(img->height()) : 1.0f;
    const float u0 = static_cast<float>(source.left()) * invW;
    const float v0 = static_cast<float>(source.top()) * invH;
    const float u1 = static_cast<float>(source.right()) * invW;
    const float v1 = static_cast<float>(source.bottom()) * invH;

    // Compute the four corners in world (pixel) space, applying rotation
    // around the sprite centre. Rotation degrees: positive = clockwise
    // in screen-space (Y-down convention from the QImage / QRectF world).
    const double rad = sprite.rotationDegrees * (M_PI / 180.0);
    const float cosR = static_cast<float>(std::cos(rad));
    const float sinR = static_cast<float>(std::sin(rad));
    auto rotate = [&](float lx, float ly, float& outX, float& outY) {
        outX = cx + lx * cosR - ly * sinR;
        outY = cy + lx * sinR + ly * cosR;
    };

    float tlx, tly, trx, try_, blx, bly, brx, bry;
    rotate(-halfW, -halfH, tlx, tly);
    rotate( halfW, -halfH, trx, try_);
    rotate(-halfW,  halfH, blx, bly);
    rotate( halfW,  halfH, brx, bry);

    // Two triangles: TL-TR-BL, TR-BR-BL.
    out.push_back({ tlx, tly, u0, v0, opacity, effect });
    out.push_back({ trx, try_, u1, v0, opacity, effect });
    out.push_back({ blx, bly, u0, v1, opacity, effect });
    out.push_back({ trx, try_, u1, v0, opacity, effect });
    out.push_back({ brx, bry, u1, v1, opacity, effect });
    out.push_back({ blx, bly, u0, v1, opacity, effect });
}

// Phase 3.5b — arc tessellation. Legacy QSG renders an arc as a
// clipped textured quad: a triangle-fan clip polygon over the sector
// + a full-rect QSGSimpleTextureNode behind it. We collapse that to a
// single textured triangle fan — vertices placed at the arc's segment
// edges, UVs computed from the world position relative to the arc's
// bounding rect. The output is identical (clip ∩ textured quad ==
// textured triangle fan within the arc) and uses the same sprite
// shader path with no extra state.
void emitArcVertices(const miacode::preview::scene::PreviewArcDescriptor& arc,
                      std::vector<PreviewDCompSpriteVertex>& out)
{
    constexpr int kSegments = 48;
    if (arc.image == nullptr || arc.image->isNull()) return;
    if (arc.width <= 0.0 || arc.height <= 0.0) return;
    if (arc.opacity <= 0.0) return;
    if (qFuzzyIsNull(arc.sweepDegrees)) return;

    const float cx = static_cast<float>(arc.center.x());
    const float cy = static_cast<float>(arc.center.y());
    const float rx = static_cast<float>(arc.width) * 0.5f;
    const float ry = static_cast<float>(arc.height) * 0.5f;
    const float opacity = static_cast<float>(qBound(0.0, arc.opacity, 1.0));
    const float rectLeft = cx - rx;
    const float rectTop = cy - ry;
    const float rectW = static_cast<float>(arc.width);
    const float rectH = static_cast<float>(arc.height);
    const float invW = rectW > 0.0f ? 1.0f / rectW : 1.0f;
    const float invH = rectH > 0.0f ? 1.0f / rectH : 1.0f;

    const auto pointForAngle = [&](double degrees, float& outX, float& outY) {
        const double rad = qDegreesToRadians(-degrees);
        outX = cx + rx * static_cast<float>(std::cos(rad));
        outY = cy + ry * static_cast<float>(std::sin(rad));
    };
    const float uCenter = (cx - rectLeft) * invW;  // = 0.5
    const float vCenter = (cy - rectTop) * invH;   // = 0.5

    for (int i = 0; i < kSegments; ++i) {
        const double t0 = static_cast<double>(i) / kSegments;
        const double t1 = static_cast<double>(i + 1) / kSegments;
        float x0, y0, x1, y1;
        pointForAngle(arc.startDegrees + arc.sweepDegrees * t0, x0, y0);
        pointForAngle(arc.startDegrees + arc.sweepDegrees * t1, x1, y1);
        const float u0 = (x0 - rectLeft) * invW;
        const float v0 = (y0 - rectTop) * invH;
        const float u1 = (x1 - rectLeft) * invW;
        const float v1 = (y1 - rectTop) * invH;
        out.push_back({ cx, cy, uCenter, vCenter, opacity, 0.0f });
        out.push_back({ x0, y0, u0, v0, opacity, 0.0f });
        out.push_back({ x1, y1, u1, v1, opacity, 0.0f });
    }
}

// Phase 3.5a — circle/ellipse tessellation for the solid-colour PS.
//
// Filled fan: 32 segments × 3 vertices/triangle = 96 verts.
// Stroke ring: 32 segments × 6 vertices/quad      = 192 verts.
// Per circle worst case: 288 verts. The vertex's (uv.x, uv.y, opacity,
// effect) slots are repurposed as straight (R, G, B, A) — see kSolidPS.
void emitCircleVertices(const miacode::preview::scene::PreviewCircleDescriptor& circle,
                         std::vector<PreviewDCompSpriteVertex>& out)
{
    constexpr int kSegments = 32;
    const float cx = static_cast<float>(circle.center.x());
    const float cy = static_cast<float>(circle.center.y());
    const float rx = static_cast<float>(circle.radiusX);
    const float ry = static_cast<float>(circle.radiusY);
    if (rx <= 0.0f || ry <= 0.0f) {
        return;
    }

    auto packColor = [](const QColor& c) {
        return std::array<float, 4>{
            static_cast<float>(qBound(0.0, c.redF(), 1.0)),
            static_cast<float>(qBound(0.0, c.greenF(), 1.0)),
            static_cast<float>(qBound(0.0, c.blueF(), 1.0)),
            static_cast<float>(qBound(0.0, c.alphaF(), 1.0)),
        };
    };
    auto pushVertex = [&](float x, float y, const std::array<float, 4>& rgba) {
        out.push_back({ x, y, rgba[0], rgba[1], rgba[2], rgba[3] });
    };

    // Fill — triangle fan emitted as triangle list (center + edge[i] + edge[i+1]).
    if (circle.fillColor.alpha() > 0) {
        const auto fill = packColor(circle.fillColor);
        for (int i = 0; i < kSegments; ++i) {
            const double a0 = (2.0 * M_PI) * static_cast<double>(i) / kSegments;
            const double a1 = (2.0 * M_PI) * static_cast<double>(i + 1) / kSegments;
            const float x0 = cx + rx * static_cast<float>(std::cos(a0));
            const float y0 = cy + ry * static_cast<float>(std::sin(a0));
            const float x1 = cx + rx * static_cast<float>(std::cos(a1));
            const float y1 = cy + ry * static_cast<float>(std::sin(a1));
            pushVertex(cx, cy, fill);
            pushVertex(x0, y0, fill);
            pushVertex(x1, y1, fill);
        }
    }

    // Stroke — annulus between inner (r - w/2) and outer (r + w/2). Each
    // segment is a quad split into two triangles.
    if (circle.strokeColor.alpha() > 0 && circle.strokeWidth > 0.0) {
        const auto stroke = packColor(circle.strokeColor);
        const float halfWidth = static_cast<float>(circle.strokeWidth) * 0.5f;
        const float outerRx = rx + halfWidth;
        const float outerRy = ry + halfWidth;
        const float innerRx = std::max(0.0f, rx - halfWidth);
        const float innerRy = std::max(0.0f, ry - halfWidth);
        for (int i = 0; i < kSegments; ++i) {
            const double a0 = (2.0 * M_PI) * static_cast<double>(i) / kSegments;
            const double a1 = (2.0 * M_PI) * static_cast<double>(i + 1) / kSegments;
            const float c0 = static_cast<float>(std::cos(a0));
            const float s0 = static_cast<float>(std::sin(a0));
            const float c1 = static_cast<float>(std::cos(a1));
            const float s1 = static_cast<float>(std::sin(a1));
            const float ox0 = cx + outerRx * c0;
            const float oy0 = cy + outerRy * s0;
            const float ix0 = cx + innerRx * c0;
            const float iy0 = cy + innerRy * s0;
            const float ox1 = cx + outerRx * c1;
            const float oy1 = cy + outerRy * s1;
            const float ix1 = cx + innerRx * c1;
            const float iy1 = cy + innerRy * s1;
            // Quad: outer0 - outer1 - inner0  /  outer1 - inner1 - inner0
            pushVertex(ox0, oy0, stroke);
            pushVertex(ox1, oy1, stroke);
            pushVertex(ix0, iy0, stroke);
            pushVertex(ox1, oy1, stroke);
            pushVertex(ix1, iy1, stroke);
            pushVertex(ix0, iy0, stroke);
        }
    }
}

}  // namespace

bool PreviewDCompSpritePipeline::renderSnapshot(ID3D11DeviceContext* context,
                                                ID3D11Device* device,
                                                ID3D11RenderTargetView* rtv,
                                                QSize rtvLogicalSize,
                                                const PreviewDCompFrameStateSnapshot& snapshot,
                                                PreviewDCompTextureCache& textureCache)
{
    if (!ready_ || context == nullptr || device == nullptr || rtv == nullptr) {
        return false;
    }

    // Phase 3.3 diagnostic: log first call so a silent crash on the very
    // first frame is distinguishable from a crash later. Subsequent calls
    // are silent — the renderer's `snapshot_sprite_count` log handles the
    // ongoing case.
    static thread_local bool s_loggedFirstCall = false;
    if (!s_loggedFirstCall) {
        s_loggedFirstCall = true;
        logPipeline("renderSnapshot_first_call",
                    QStringLiteral("revision=%1 sprites=%2 rtv_w=%3 rtv_h=%4")
                        .arg(snapshot.revision)
                        .arg(snapshot.sprites.size())
                        .arg(rtvLogicalSize.width())
                        .arg(rtvLogicalSize.height()));
    }

    const int rtvWidth = rtvLogicalSize.width() > 0 ? rtvLogicalSize.width() : 1;
    const int rtvHeight = rtvLogicalSize.height() > 0 ? rtvLogicalSize.height() : 1;

    // Project from the QQuickWindow logical-pixel coordinate space the
    // descriptors were built in, NOT the swap-chain pixel size. The
    // Phase-1 swap chain is fixed at 200×200 (the demo region), but the
    // sprite descriptors come from the full 1280×800 layout. Mapping
    // window-space to NDC and rasterising with a 200×200 viewport
    // squeezes the whole scene into the demo rectangle — so something
    // visible lands in the top-left, even though Phase 4 will replace
    // this with placeholder-aligned coordinates.
    const int sceneWidth = snapshot.sceneLogicalSize.width() > 0
        ? snapshot.sceneLogicalSize.width() : rtvWidth;
    const int sceneHeight = snapshot.sceneLogicalSize.height() > 0
        ? snapshot.sceneLogicalSize.height() : rtvHeight;
    PreviewDCompSpriteUniformBlock uniforms{};
    writeOrthoTopLeftPixelMatrix(uniforms.matrix, sceneWidth, sceneHeight);
    uniforms.opacity = 1.0f;
    uniforms.wave = 0.0f;
    uniforms.absWave = 0.0f;
    uniforms.effect = 0.0f;
    context->UpdateSubresource(uniformBuffer_.Get(), 0, nullptr, &uniforms, 0, 0);

    // Walk the snapshot's tagged batch list in z-order, building a
    // single vertex stream + a list of typed draw runs. Adjacent
    // sprite descriptors that share an SRV merge into one run; circle
    // descriptors merge into one run per circle batch. Runs of
    // different types never merge — they need different shaders and,
    // for sprites, an SRV bind. Phase 3.5b will add a third run kind
    // for arcs (textured + clip uniforms).
    std::vector<PreviewDCompSpriteVertex> staging;
    staging.reserve((snapshot.sprites.size() * 6) + (snapshot.circles.size() * 288));

    enum class RunKind { Sprite, Solid };
    struct DrawRun {
        RunKind kind = RunKind::Sprite;
        ID3D11ShaderResourceView* srv = nullptr;
        int firstVertex = 0;
        int vertexCount = 0;
    };
    std::vector<DrawRun> runs;
    runs.reserve(snapshot.batches.size());

    int skipNullImage = 0;
    int skipNullSrv = 0;
    int skipEmitFiltered = 0;
    int processed = 0;

    using BatchType = PreviewDCompFrameStateSnapshot::BatchType;
    for (const auto& batch : snapshot.batches) {
        if (batch.count <= 0) continue;
        switch (batch.type) {
        case BatchType::Sprites: {
            const int end = batch.firstIndex + batch.count;
            for (int i = batch.firstIndex; i < end && i < snapshot.sprites.size(); ++i) {
                const auto& sprite = snapshot.sprites.at(i);
                if (sprite.image == nullptr || sprite.image->isNull()) {
                    ++skipNullImage;
                    continue;
                }
                ID3D11ShaderResourceView* srv =
                    textureCache.lookupOrCreate(sprite.image, device, sprite.cacheable);
                if (srv == nullptr) {
                    ++skipNullSrv;
                    continue;
                }
                if (runs.empty()
                    || runs.back().kind != RunKind::Sprite
                    || runs.back().srv != srv) {
                    DrawRun r;
                    r.kind = RunKind::Sprite;
                    r.srv = srv;
                    r.firstVertex = static_cast<int>(staging.size());
                    r.vertexCount = 0;
                    runs.push_back(r);
                }
                const int before = static_cast<int>(staging.size());
                emitSpriteVertices(sprite, staging);
                const int added = static_cast<int>(staging.size()) - before;
                runs.back().vertexCount += added;
                if (added == 0) {
                    ++skipEmitFiltered;
                }
                ++processed;
            }
            break;
        }
        case BatchType::Circles: {
            const int end = batch.firstIndex + batch.count;
            DrawRun r;
            r.kind = RunKind::Solid;
            r.srv = nullptr;
            r.firstVertex = static_cast<int>(staging.size());
            r.vertexCount = 0;
            for (int i = batch.firstIndex; i < end && i < snapshot.circles.size(); ++i) {
                const int before = static_cast<int>(staging.size());
                emitCircleVertices(snapshot.circles.at(i), staging);
                r.vertexCount += static_cast<int>(staging.size()) - before;
            }
            if (r.vertexCount > 0) {
                runs.push_back(r);
            }
            processed += batch.count;
            break;
        }
        case BatchType::Arcs: {
            // Arcs use the same textured-sprite shader; each arc is a
            // textured triangle fan over its sector. Group adjacent arcs
            // sharing one SRV into a single run, like sprite batching.
            const int end = batch.firstIndex + batch.count;
            for (int i = batch.firstIndex; i < end && i < snapshot.arcs.size(); ++i) {
                const auto& arc = snapshot.arcs.at(i);
                if (arc.image == nullptr || arc.image->isNull()) {
                    ++skipNullImage;
                    continue;
                }
                ID3D11ShaderResourceView* srv =
                    textureCache.lookupOrCreate(arc.image, device, arc.cacheable);
                if (srv == nullptr) {
                    ++skipNullSrv;
                    continue;
                }
                if (runs.empty()
                    || runs.back().kind != RunKind::Sprite
                    || runs.back().srv != srv) {
                    DrawRun r;
                    r.kind = RunKind::Sprite;
                    r.srv = srv;
                    r.firstVertex = static_cast<int>(staging.size());
                    r.vertexCount = 0;
                    runs.push_back(r);
                }
                const int before = static_cast<int>(staging.size());
                emitArcVertices(arc, staging);
                const int added = static_cast<int>(staging.size()) - before;
                runs.back().vertexCount += added;
                if (added == 0) {
                    ++skipEmitFiltered;
                }
                ++processed;
            }
            break;
        }
        }
    }

    // Phase 3.3 diagnostic: log vertex count every 60 frames so we can
    // tell whether emitSpriteVertices is producing geometry — sprites=N
    // in the snapshot doesn't guarantee N quads survive the
    // width/height/opacity filter inside emitSpriteVertices.
    static thread_local qint64 s_diagFrameCount = 0;
    s_diagFrameCount += 1;
    if ((s_diagFrameCount % 60) == 1) {
        logPipeline("renderSnapshot_geom",
                    QStringLiteral("sprites=%1 vertices=%2 runs=%3 cache_hit=%4 skip_null_img=%5 skip_null_srv=%6 skip_emit_filtered=%7 processed=%8")
                        .arg(snapshot.sprites.size())
                        .arg(static_cast<int>(staging.size()))
                        .arg(static_cast<int>(runs.size()))
                        .arg(textureCache.cacheableSize() + textureCache.transientSize())
                        .arg(skipNullImage)
                        .arg(skipNullSrv)
                        .arg(skipEmitFiltered)
                        .arg(processed));
    }

    // Upload vertices. Skip the draw if no sprites contributed any
    // vertices (degenerate descriptors filtered by emitSpriteVertices,
    // or empty snapshot.sprites). Empty path uses an opaque RED clear so
    // that visually we can distinguish it from the non-empty path's GREY
    // clear — different colour makes the active code path obvious in a
    // single glance at the demo region.
    if (staging.empty()) {
        const float clearColor[4] = { 0.55f, 0.10f, 0.10f, 1.0f };
        ID3D11RenderTargetView* rtvs[1] = { rtv };
        context->OMSetRenderTargets(1, rtvs, nullptr);
        context->ClearRenderTargetView(rtv, clearColor);
        return true;
    }
    if (static_cast<int>(staging.size()) > kMaxVertices) {
        // Buffer overflow guard. Truncate `staging` AND any run whose
        // [firstVertex, firstVertex+vertexCount) extends past kMaxVertices,
        // so the per-batch Draw calls never reference uninitialised buffer
        // contents — that's UB and on real drivers can trigger device
        // removal. Phase 3.4+ will grow the buffer dynamically once we
        // know the steady-state vertex count.
        logPipeline("renderSnapshot_overflow",
                    QStringLiteral("requested=%1 max=%2 truncated=1")
                        .arg(static_cast<int>(staging.size()))
                        .arg(kMaxVertices));
        staging.resize(kMaxVertices);
        for (auto& run : runs) {
            if (run.firstVertex >= kMaxVertices) {
                run.vertexCount = 0;
            } else if (run.firstVertex + run.vertexCount > kMaxVertices) {
                run.vertexCount = kMaxVertices - run.firstVertex;
            }
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context->Map(vertexBuffer_.Get(), 0,
                               D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        logHr("renderSnapshot_map_vb", hr);
        return false;
    }
    std::memcpy(mapped.pData, staging.data(),
                staging.size() * sizeof(PreviewDCompSpriteVertex));
    context->Unmap(vertexBuffer_.Get(), 0);

    // Phase 3.3 diagnostic clear: opaque dark grey so the demo region is
    // always visible, even if every sprite lands outside the viewport
    // (the swap chain is still 200x200, but descriptors are in 1280x800
    // logical space until Phase 4 aligns the placeholder rect). Without
    // an opaque clear "DComp surface invisible because no sprites
    // rendered" is indistinguishable from "DComp surface broken." Phase
    // 4 switches this to fully-transparent so DComp can blend over QML.
    const float clearColor[4] = { 0.15f, 0.15f, 0.18f, 1.0f };
    ID3D11RenderTargetView* rtvs[1] = { rtv };
    context->OMSetRenderTargets(1, rtvs, nullptr);
    context->ClearRenderTargetView(rtv, clearColor);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(rtvWidth);
    vp.Height = static_cast<float>(rtvHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context->RSSetViewports(1, &vp);
    context->RSSetState(rasterizerState_.Get());

    const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    context->OMSetBlendState(blendState_.Get(), blendFactor, 0xffffffff);

    context->IASetInputLayout(inputLayout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(PreviewDCompSpriteVertex);
    UINT offset = 0;
    ID3D11Buffer* vbs[1] = { vertexBuffer_.Get() };
    context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);

    context->VSSetShader(vs_.Get(), nullptr, 0);
    ID3D11Buffer* cbs[1] = { uniformBuffer_.Get() };
    context->VSSetConstantBuffers(0, 1, cbs);

    ID3D11SamplerState* samplers[1] = { sampler_.Get() };
    context->PSSetSamplers(0, 1, samplers);

    // Per-run shader switch. Sprite runs bind ps_ + the run's SRV.
    // Solid runs bind psSolid_ and clear t0 — the sampler is still
    // bound but the shader doesn't read it.
    RunKind currentKind = RunKind::Sprite;
    ID3D11ShaderResourceView* currentSrv = nullptr;
    bool stateInitialised = false;
    for (const auto& run : runs) {
        if (run.vertexCount <= 0) {
            continue;
        }
        if (!stateInitialised || run.kind != currentKind) {
            if (run.kind == RunKind::Sprite) {
                context->PSSetShader(ps_.Get(), nullptr, 0);
            } else {
                context->PSSetShader(psSolid_.Get(), nullptr, 0);
                ID3D11ShaderResourceView* nullSrv[1] = { nullptr };
                context->PSSetShaderResources(0, 1, nullSrv);
                currentSrv = nullptr;
            }
            currentKind = run.kind;
            stateInitialised = true;
        }
        if (run.kind == RunKind::Sprite && run.srv != currentSrv) {
            ID3D11ShaderResourceView* srvs[1] = { run.srv };
            context->PSSetShaderResources(0, 1, srvs);
            currentSrv = run.srv;
        }
        context->Draw(run.vertexCount, run.firstVertex);
    }

    // Release any transient SRVs that were created for non-cacheable
    // sprites this frame. The runs[] vector still holds raw SRV pointers
    // but those pointers won't be dereferenced again — D3D11 only needs
    // them while the Draw command is in flight, and the command list is
    // submitted to the GPU before this returns.
    textureCache.endFrame();
    return true;
}

#endif  // Q_OS_WIN

}  // namespace miacode::preview::dcomp
