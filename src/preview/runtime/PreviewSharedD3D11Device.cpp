#include "preview/runtime/PreviewSharedD3D11Device.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QtGlobal>

#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>  // ID3D11InfoQueue / D3D11_MESSAGE (debug layer drain)
#include <wrl/client.h>

#include <QtAVPlayer/qavd3d11sharedcontext_p.h>

#pragma comment(lib, "d3d11.lib")
#endif

namespace miacode::preview {

#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)

namespace {

using Microsoft::WRL::ComPtr;

// Canonical process-lifetime owners of the shared device. These ComPtrs hold the
// reference for the whole process and Release exactly once at static destruction
// (process exit) — never during normal media load/unload. By then the decoder's
// AddRef'd reference (held via the AVBufferRef, owned by the QAVPlayer) has already
// been dropped during teardown, and Qt's imported-device RHI does NOT Release an
// imported device, so there is no double-free and nothing decodes after this point.
// The borrowed pointer published via qavSetSharedRenderD3D11Device is intentionally
// never reset on unload because g_device outlives all decoders.
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<ID3D11InfoQueue> g_infoQueue;  // non-null only when the debug layer is active
bool g_attempted = false;
bool g_active = false;

void logShared(const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/shared_d3d11_device"),
        payload);
}

// Logging sink published into the QtAVPlayer decode path so its render/decode-thread
// copy code (qavhwdevice_d3d11.cpp) can emit structured lines into our async runtime
// log. appendLine is thread-safe (mutexed async queue, drop-on-overflow). The payload
// is a "key=val …" fragment built on the render thread; scope + timestamp are added here.
void previewHwDiagLogSink(const char* payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/hwframe"),
        QString::fromUtf8(payload));
}

bool createSharedDevice()
{
    // Mirror Qt RHI's flags (BGRA) and add VIDEO_SUPPORT, which FFmpeg's
    // d3d11va_device_init() requires to QueryInterface(ID3D11VideoDevice).
    const UINT baseFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    // Diagnostics: the D3D11 debug layer (typed-SRV / NV12 PlaneSlice warnings = the
    // decisive H-FMT signal; resource-not-ready / device-removed = H-DEC) must be set
    // at CreateDevice time — this device is IMPORTED into Qt's QRhi, so Qt's own
    // QSG_RHI_DEBUG_LAYER cannot reach it. The debug layer needs the D3D11 SDK layers
    // installed; if absent, D3D11CreateDevice fails with DXGI_ERROR_SDK_COMPONENT_MISSING
    // and we transparently retry without it (so a normal machine is unaffected).
    bool debugLayerActive = miacode::debug_options::previewSharedD3D11DebugLayerEnabled();

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL obtained{};

    // Default hardware adapter (D3D_DRIVER_TYPE_HARDWARE + null adapter) — the same
    // default Qt's RHI and FFmpeg's own device creation pick. On the affected
    // single-GPU iGPU machines there is only one adapter anyway.
    const auto tryCreate = [&](UINT flags, bool useLevels) -> HRESULT {
        return D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            useLevels ? levels : nullptr, useLevels ? ARRAYSIZE(levels) : 0u,
            D3D11_SDK_VERSION,
            device.ReleaseAndGetAddressOf(), &obtained, context.ReleaseAndGetAddressOf());
    };

    const UINT debugBit = static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
    HRESULT hr = tryCreate(baseFlags | (debugLayerActive ? debugBit : 0u), true);
    if (FAILED(hr) && debugLayerActive) {
        // Debug layer requested but unavailable (SDK layers not installed) — retry
        // without it so the feature still works as a plain shared device.
        logShared(QStringLiteral("event=debug_layer_unavailable hr=0x%1 retry=without_debug")
                      .arg(static_cast<quint32>(hr), 0, 16));
        debugLayerActive = false;
        hr = tryCreate(baseFlags, true);
    }
    if (FAILED(hr)) {
        // Retry letting the runtime pick the feature level (older drivers can
        // reject an explicit 11_1-first list).
        hr = tryCreate(baseFlags | (debugLayerActive ? debugBit : 0u), false);
    }
    if (FAILED(hr)) {
        logShared(QStringLiteral("event=create_failed stage=D3D11CreateDevice hr=0x%1")
                      .arg(static_cast<quint32>(hr), 0, 16));
        return false;
    }

    // The immediate context is shared between the QSG render thread (RHI draws)
    // and the FFmpeg decode thread. Without multithread protection D3D11 does not
    // serialize cross-thread context use → corruption/crash. Bail (legacy path)
    // if the runtime can't give us a protected device.
    ComPtr<ID3D10Multithread> multithread;
    if (FAILED(device.As(&multithread)) || !multithread) {
        logShared(QStringLiteral("event=create_failed stage=ID3D10Multithread"));
        return false;
    }
    multithread->SetMultithreadProtected(TRUE);

    g_device = device;
    g_context = context;
    if (debugLayerActive) {
        // Capture the InfoQueue so drainSharedPreviewD3D11DebugMessages() can pull
        // warnings/errors into the runtime log on a low-frequency cadence.
        if (FAILED(device.As(&g_infoQueue))) {
            g_infoQueue.Reset();
        }
    }
    logShared(QStringLiteral("event=created feature_level=0x%1 video_support=1 mt_protected=1 debug_layer=%2")
                  .arg(static_cast<quint32>(obtained), 0, 16)
                  .arg(debugLayerActive ? 1 : 0));
    return true;
}

}  // namespace

QQuickGraphicsDevice sharedPreviewQuickGraphicsDevice()
{
    if (!miacode::debug_options::previewSingleD3D11DeviceEnabled()) {
        return {};
    }

    if (!g_attempted) {
        g_attempted = true;
        if (createSharedDevice()) {
            // Publish to the FFmpeg D3D11VA decoder (QtAVPlayer) BEFORE any media
            // loads so setup_video_codec picks it up. The pointer is borrowed; our
            // g_device ComPtr keeps it alive for the process lifetime.
            qavSetSharedRenderD3D11Device(g_device.Get());
            g_active = true;
            logShared(QStringLiteral("event=published device=0x%1 context=0x%2")
                          .arg(reinterpret_cast<quintptr>(g_device.Get()), 0, 16)
                          .arg(reinterpret_cast<quintptr>(g_context.Get()), 0, 16));
        }
    }

    if (!g_active) {
        return {};
    }
    return QQuickGraphicsDevice::fromDeviceAndContext(g_device.Get(), g_context.Get());
}

bool sharedPreviewD3D11DeviceActive()
{
    return g_active;
}

void installPreviewDecodeDiagnostics()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;

    qavSetPreviewDiagLogSink(&previewHwDiagLogSink);

    const int dumpBudget = miacode::debug_options::previewDumpHwFrameBudget();
    qavSetPreviewHwFrameDumpConfig(dumpBudget);

    // §10 HW-decode green/garble fix: publish the resolved config (completion-wait is
    // the primary fix, default ON; drop-corrupt is the safety net, default OFF). These
    // are NOT --debug-gated — they are real fixes that must apply in normal runs.
    const int completionWait =
        miacode::debug_options::previewHwDecodeCompletionWaitEnabled() ? 1 : 0;
    const int dropCorrupt =
        miacode::debug_options::previewHwDecodeDropCorruptFramesEnabled() ? 1 : 0;
    qavSetPreviewHwDecodeFixConfig(completionWait, dropCorrupt);

    logShared(QStringLiteral(
                  "event=diag_installed dump_budget=%1 completion_wait=%2 drop_corrupt=%3 "
                  "(stats-only, no frame files)")
                  .arg(dumpBudget)
                  .arg(completionWait)
                  .arg(dropCorrupt));
}

void drainSharedPreviewD3D11DebugMessages()
{
    if (!g_infoQueue) {
        return;
    }
    const UINT64 total = g_infoQueue->GetNumStoredMessages();
    if (total == 0) {
        return;
    }
    // Bounded drain so a flood can't stall the GUI thread; the rest is cleared.
    const UINT64 cap = total < 64 ? total : 64;
    for (UINT64 i = 0; i < cap; ++i) {
        SIZE_T len = 0;
        if (FAILED(g_infoQueue->GetMessage(i, nullptr, &len)) || len == 0) {
            continue;
        }
        QByteArray storage(static_cast<int>(len), '\0');
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (FAILED(g_infoQueue->GetMessage(i, message, &len))) {
            continue;
        }
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview/hwframe_d3d11dbg"),
            QStringLiteral("severity=%1 category=%2 id=%3 msg=%4")
                .arg(static_cast<int>(message->Severity))
                .arg(static_cast<int>(message->Category))
                .arg(static_cast<int>(message->ID))
                .arg(QString::fromLatin1(message->pDescription)));
    }
    g_infoQueue->ClearStoredMessages();
}

#else  // non-Windows or QtAVPlayer backend not built

QQuickGraphicsDevice sharedPreviewQuickGraphicsDevice()
{
    return {};
}

bool sharedPreviewD3D11DeviceActive()
{
    return false;
}

void installPreviewDecodeDiagnostics()
{
}

void drainSharedPreviewD3D11DebugMessages()
{
}

#endif

}  // namespace miacode::preview
