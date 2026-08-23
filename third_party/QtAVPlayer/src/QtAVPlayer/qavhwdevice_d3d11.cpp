/***************************************************************
 * Copyright (C) 2020, 2025, Val Doroshchuk <valbok@gmail.com> *
 *                                                             *
 * This file is part of QtAVPlayer.                            *
 * Free Qt Media Player based on FFmpeg.                       *
 ***************************************************************/

#include "qavhwdevice_d3d11_p.h"
#include "qavd3d11sharedcontext_p.h"  // MiaCode H2: shared render device + preview diag
#include "qavvideobuffer_gpu_p.h"
#include <QDebug>
#include <QOpenGLFunctions>  // GLuint
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <d3d11.h>
#include <d3d11_3.h>
#include <d3dcompiler.h>
#include <system_error>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#if defined(QT_AVPLAYER_MULTIMEDIA) && QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    #include <private/qrhi_p.h>
    #include <private/qrhid3d11_p.h>
    #if QT_VERSION >= QT_VERSION_CHECK(6, 6, 2)
        #include <private/quniquehandle_p.h>
    #endif
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/d3d11va.h>
#include <libavutil/frame.h>            // AV_FRAME_FLAG_CORRUPT / decode_error_flags (§10)
#include <libavutil/hwcontext_d3d11va.h>
}

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "D3DCompiler.lib")

typedef HANDLE (*wglDXOpenDeviceNV_)(void *dxDevice);
static wglDXOpenDeviceNV_ s_wglDXOpenDeviceNV = nullptr;

typedef BOOL (*wglDXCloseDeviceNV_)(HANDLE hDevice);
static wglDXCloseDeviceNV_ s_wglDXCloseDeviceNV = nullptr;

typedef HANDLE (*wglDXRegisterObjectNV_)(HANDLE hDevice, void *dxObject, GLuint name, GLenum type, GLenum access);
static wglDXRegisterObjectNV_ s_wglDXRegisterObjectNV = nullptr;

typedef BOOL (*wglDXUnregisterObjectNV_)(HANDLE hDevice, HANDLE hObject);
static wglDXUnregisterObjectNV_ s_wglDXUnregisterObjectNV = nullptr;

typedef BOOL (WINAPI* wglDXLockObjectsNV)(HANDLE hDevice, GLint count, HANDLE *hObjects);
static wglDXLockObjectsNV s_wglDXLockObjectsNV = nullptr;

typedef BOOL (WINAPI* wglDXUnlockObjectsNV)(HANDLE hDevice, GLint count, HANDLE *hObjects);
static wglDXUnlockObjectsNV s_wglDXUnlockObjectsNV = nullptr;

typedef BOOL (WINAPI* wglDXObjectAccessNV)(HANDLE hObject, GLenum access);
static wglDXObjectAccessNV s_wglDXObjectAccessNV = nullptr;

#define WGL_ACCESS_READ_WRITE_NV          0x00000001

#define ENSURE(f, ...) CHECK(f, return __VA_ARGS__;)
#define CHECK(f, ...) \
    do { \
        HRESULT hr = f; \
        if (FAILED(hr)) { \
            qWarning() << QString::fromLatin1("Error@%1. " #f ": (0x%2)").arg(__LINE__).arg(hr, 0, 16) << qt_error_string(hr); \
            __VA_ARGS__ \
        } \
    } while (0)

// MiaCode S1: bound the render-thread keyed-mutex AcquireSync so a busy/stalled
// iGPU drops a frame instead of freezing the QSG render thread forever. AcquireSync
// returns WAIT_TIMEOUT (0x102) / WAIT_ABANDONED (0x80) on timeout/abandon — both
// POSITIVE HRESULTs that FAILED()/ENSURE do NOT catch, so callers must hand-check
// "hr != S_OK" rather than wrap AcquireSync in ENSURE.
static constexpr DWORD kPreviewAcquireSyncTimeoutMs = 180;

QT_BEGIN_NAMESPACE

// ---------------------------------------------------------------------------
// MiaCode H2: single-device D3D11 sharing.
//
// The renderer publishes its ID3D11Device here; the FFmpeg D3D11VA decoder then
// decodes onto that same device, so handle() below can skip the cross-device
// keyed-mutex bridge (and its render-thread AcquireSync(INFINITE) freeze).
// ---------------------------------------------------------------------------
static std::atomic<void *> g_sharedRenderD3D11Device{ nullptr };

void qavSetSharedRenderD3D11Device(void *device)
{
    g_sharedRenderD3D11Device.store(device, std::memory_order_release);
}

void *qavSharedRenderD3D11Device()
{
    return g_sharedRenderD3D11Device.load(std::memory_order_acquire);
}

AVBufferRef *qavCreateSharedD3D11VADeviceContext()
{
    auto *device = static_cast<ID3D11Device *>(qavSharedRenderD3D11Device());
    if (!device)
        return nullptr;

    AVBufferRef *ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!ref) {
        qWarning() << "qavCreateSharedD3D11VADeviceContext: av_hwdevice_ctx_alloc failed";
        return nullptr;
    }

    auto *hwdev = reinterpret_cast<AVHWDeviceContext *>(ref->data);
    auto *d3dctx = static_cast<AVD3D11VADeviceContext *>(hwdev->hwctx);

    // d3d11va_device_uninit() Releases ->device (and ->device_context), so hand
    // FFmpeg an owned reference. device_context / video_device / video_context
    // are left null on purpose: av_hwdevice_ctx_init() derives them from ->device
    // (GetImmediateContext + QueryInterface(ID3D11VideoDevice/Context)). That
    // QueryInterface REQUIRES the device to have D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    // if it doesn't, init fails here and the caller falls back to a private device.
    device->AddRef();
    d3dctx->device = device;

    const int ret = av_hwdevice_ctx_init(ref);
    if (ret < 0) {
        qWarning() << "qavCreateSharedD3D11VADeviceContext: av_hwdevice_ctx_init failed:" << ret
                   << "(shared device lacks video support? falling back to private device)";
        av_buffer_unref(&ref);  // free callback runs d3d11va_device_uninit -> Release(device)
        return nullptr;
    }
    return ref;
}

// ===========================================================================
// MiaCode preview HW-decode diagnostics (declarations in qavd3d11sharedcontext_p.h;
// design in docs/PREVIEW_HWDECODE_GREEN_GARBLE_SEEK_DEBUG_PLAN_ZH.md §9).
//
// Storm-safety contract: the per-frame copy path (copyTexture*, on the QSG render
// thread) only ever does relaxed-atomic increments + ONE relaxed-atomic load+compare
// for the dump gate. NO env reads (the resolved budget is published from src once),
// NO QString formatting, NO logging, NO file I/O when the dump budget is 0 (default).
// The heavy work (STAGING readback Map + stat walk + raw dump + the log line) runs at
// most `budget` frames per arm, then permanently self-stops. The cumulative counters
// are drained + logged by the GUI thread on a low-frequency cadence (pause/seek/eom),
// never per frame on a hot thread.
// ===========================================================================
namespace {

std::atomic<QAVPreviewDiagLogFn> g_diagLogFn{ nullptr };

// Dump config (published once from src before any media loads) + per-arm budget.
std::atomic<int> g_dumpBudgetPerArm{ 0 };
std::atomic<int> g_dumpRemaining{ 0 };

// Cumulative counters (relaxed atomics: render/decode-thread increment, GUI drains).
std::atomic<unsigned long long> g_copiedSingle{ 0 };
std::atomic<unsigned long long> g_copiedTwoDevice{ 0 };
std::atomic<unsigned long long> g_texturesCreated{ 0 };
std::atomic<unsigned long long> g_acquireTimeouts{ 0 };
std::atomic<unsigned long long> g_srcAcquireWaitSamples{ 0 };
std::atomic<unsigned long long> g_srcAcquireWaitTotalUs{ 0 };
std::atomic<unsigned long long> g_srcAcquireWaitMaxUs{ 0 };
std::atomic<unsigned long long> g_destAcquireWaitSamples{ 0 };
std::atomic<unsigned long long> g_destAcquireWaitTotalUs{ 0 };
std::atomic<unsigned long long> g_destAcquireWaitMaxUs{ 0 };
std::atomic<unsigned long long> g_twoDeviceBridgeSamples{ 0 };
std::atomic<unsigned long long> g_twoDeviceBridgeTotalUs{ 0 };
std::atomic<unsigned long long> g_twoDeviceBridgeMaxUs{ 0 };
std::atomic<unsigned long long> g_twoDeviceSourceSetupSamples{ 0 };
std::atomic<unsigned long long> g_twoDeviceSourceSetupTotalUs{ 0 };
std::atomic<unsigned long long> g_twoDeviceSourceSetupMaxUs{ 0 };
std::atomic<unsigned long long> g_twoDeviceDestinationSetupSamples{ 0 };
std::atomic<unsigned long long> g_twoDeviceDestinationSetupTotalUs{ 0 };
std::atomic<unsigned long long> g_twoDeviceDestinationSetupMaxUs{ 0 };
std::atomic<unsigned long long> g_sourceTextureCreateSamples{ 0 };
std::atomic<unsigned long long> g_sourceTextureCreateTotalUs{ 0 };
std::atomic<unsigned long long> g_sourceTextureCreateMaxUs{ 0 };
std::atomic<unsigned long long> g_destinationTextureCreateSamples{ 0 };
std::atomic<unsigned long long> g_destinationTextureCreateTotalUs{ 0 };
std::atomic<unsigned long long> g_destinationTextureCreateMaxUs{ 0 };
std::atomic<unsigned long long> g_sharedResourceOpenSamples{ 0 };
std::atomic<unsigned long long> g_sharedResourceOpenTotalUs{ 0 };
std::atomic<unsigned long long> g_sharedResourceOpenMaxUs{ 0 };
std::atomic<unsigned long long> g_copyFailures{ 0 };
std::atomic<unsigned long long> g_resolutionChanges{ 0 };
std::atomic<unsigned long long> g_hwFramesDumped{ 0 };
std::atomic<unsigned long long> g_catchupSkips{ 0 };

// §10 HW-decode FIX config (published once from src; default ON for the completion
// wait so the green/garble fix is active in normal runs even before the publish lands)
// + the FIX's cumulative counters.
std::atomic<int> g_completionWait{ 1 };           // force decode GPU completion before copy
std::atomic<int> g_dropCorrupt{ 0 };              // drop FFmpeg-flagged corrupt/error frames
std::atomic<unsigned long long> g_completionWaits{ 0 };
std::atomic<unsigned long long> g_completionWaitTimeouts{ 0 };
std::atomic<unsigned long long> g_framesDecodeError{ 0 };
std::atomic<unsigned long long> g_corruptDropped{ 0 };
// avcodec_get_name() returns a process-stable static string, so storing the bare
// pointer is safe to read from the GUI thread (set once per decode init).
std::atomic<const char *> g_codecName{ nullptr };
// steady_clock ns at the last arm (= playback start / each seek); the dump line's
// since_seek_ms quantifies "green only within N ms after a seek" (§10.5).
std::atomic<long long> g_seekEpochNs{ 0 };
// This is published once from the GUI thread with the --debug runtime-log gate.
// Keeping it false in ordinary runs avoids calling the clock in the render hot path.
std::atomic<int> g_diagTimingEnabled{ 0 };

inline long long steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void noteTimedPhase(std::atomic<unsigned long long>& samples,
                    std::atomic<unsigned long long>& totalUs,
                    std::atomic<unsigned long long>& maxUs,
                    long long startedNs)
{
    const auto elapsedUs = static_cast<unsigned long long>(
        qMax<qint64>(0, (steadyNowNs() - startedNs) / 1000));
    samples.fetch_add(1, std::memory_order_relaxed);
    totalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
    auto observed = maxUs.load(std::memory_order_relaxed);
    while (observed < elapsedUs
           && !maxUs.compare_exchange_weak(
               observed, elapsedUs, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

std::atomic<int> g_lastCodedW{ 0 };
std::atomic<int> g_lastCodedH{ 0 };
std::atomic<int> g_lastDispW{ 0 };
std::atomic<int> g_lastDispH{ 0 };
std::atomic<unsigned int> g_lastFormat{ 0 };
std::atomic<int> g_lastPath{ -1 };

void diagLog(const QString &payload)
{
    QAVPreviewDiagLogFn fn = g_diagLogFn.load(std::memory_order_acquire);
    if (fn) {
        fn(payload.toUtf8().constData());
    }
}

// Per-frame geometry tracking: cheap atomic stores + a coded-size change compare.
// Runs every frame on BOTH paths so coded-vs-display (the decisive H-CROP signal)
// and the resolved decode path are always available in the GUI summary line even
// when the readback dump is OFF (budget 0).
void noteFrameGeometry(int codedW, int codedH, int dispW, int dispH,
                       unsigned int dxgiFmt, int path)
{
    const int prevW = g_lastCodedW.exchange(codedW, std::memory_order_relaxed);
    const int prevH = g_lastCodedH.exchange(codedH, std::memory_order_relaxed);
    if ((prevW != 0 || prevH != 0) && (prevW != codedW || prevH != codedH)) {
        g_resolutionChanges.fetch_add(1, std::memory_order_relaxed);
    }
    g_lastDispW.store(dispW, std::memory_order_relaxed);
    g_lastDispH.store(dispH, std::memory_order_relaxed);
    g_lastFormat.store(dxgiFmt, std::memory_order_relaxed);
    g_lastPath.store(path, std::memory_order_relaxed);
}

struct Nv12Stats {
    double yMean = -1.0;
    double yRowDelta = -1.0;       // mean |row - row+step| of Y: garbage/noise proxy (high => corrupt)
    double uvNeutralPct = -1.0;    // fraction of UV samples within 128±12 (flat/neutral chroma)
    double uvZeroPct = -1.0;       // fraction with U,V <=12 (zeroed chroma => "green" in limited-range)
    double interiorZeroPct = -1.0; // zeroed-chroma fraction inside the display rect
    double edgeZeroPct = -1.0;     // zeroed-chroma fraction in the coded>display padding band
    bool edgeExists = false;       // codedW>dispW || codedH>dispH
    bool valid = false;
};

// Walk a sparse grid of the mapped NV12 staging surface. NV12 is semi-planar: a Y
// plane of `codedH` rows at `rowPitch` stride, then a UV plane (interleaved U,V byte
// pairs) of `codedH/2` rows at the SAME `rowPitch`, starting at offset
// rowPitch*codedH. Getting the pitch/offset wrong would forge a false "garbage"
// verdict, so we derive both from the driver-reported RowPitch + coded height.
Nv12Stats computeNv12Stats(const uint8_t *base, UINT rowPitch,
                           int codedW, int codedH, int dispW, int dispH)
{
    Nv12Stats s;
    if (!base || codedW <= 0 || codedH <= 1 || rowPitch < static_cast<UINT>(codedW)) {
        return s;
    }
    const int stepX = qMax(1, codedW / 256);
    const int stepY = qMax(1, codedH / 256);
    long long ySum = 0, yCount = 0;
    double rowDeltaSum = 0.0;
    long long rowDeltaCount = 0;
    for (int y = 0; y + stepY < codedH; y += stepY) {
        const uint8_t *row = base + static_cast<size_t>(y) * rowPitch;
        const uint8_t *rowN = base + static_cast<size_t>(y + stepY) * rowPitch;
        for (int x = 0; x < codedW; x += stepX) {
            ySum += row[x];
            ++yCount;
            rowDeltaSum += std::abs(static_cast<int>(row[x]) - static_cast<int>(rowN[x]));
            ++rowDeltaCount;
        }
    }
    if (yCount > 0) s.yMean = static_cast<double>(ySum) / yCount;
    if (rowDeltaCount > 0) s.yRowDelta = rowDeltaSum / rowDeltaCount;

    const uint8_t *uvBase = base + static_cast<size_t>(rowPitch) * codedH;
    const int chromaH = codedH / 2;
    const int chromaPairs = codedW / 2;
    if (chromaH > 0 && chromaPairs > 0) {
        const int cStepX = qMax(1, chromaPairs / 256);
        const int cStepY = qMax(1, chromaH / 256);
        long long uvCount = 0, neutral = 0, zero = 0;
        long long inCount = 0, inZero = 0, edCount = 0, edZero = 0;
        for (int cy = 0; cy < chromaH; cy += cStepY) {
            const uint8_t *row = uvBase + static_cast<size_t>(cy) * rowPitch;
            for (int cx = 0; cx < chromaPairs; cx += cStepX) {
                const int u = row[cx * 2];
                const int v = row[cx * 2 + 1];
                ++uvCount;
                if (std::abs(u - 128) <= 12 && std::abs(v - 128) <= 12) ++neutral;
                const bool isZero = u <= 12 && v <= 12;
                if (isZero) ++zero;
                const int lx = cx * 2;
                const int ly = cy * 2;
                if (lx < dispW && ly < dispH) { ++inCount; if (isZero) ++inZero; }
                else { ++edCount; if (isZero) ++edZero; }
            }
        }
        if (uvCount > 0) {
            s.uvNeutralPct = static_cast<double>(neutral) / uvCount;
            s.uvZeroPct = static_cast<double>(zero) / uvCount;
        }
        if (inCount > 0) s.interiorZeroPct = static_cast<double>(inZero) / inCount;
        if (edCount > 0) { s.edgeZeroPct = static_cast<double>(edZero) / edCount; s.edgeExists = true; }
    }
    s.valid = true;
    return s;
}

// NV12-domain verdict hint (matrix-independent: "green" == zeroed chroma in
// limited-range YUV). The raw numbers are logged alongside so the reader can
// override the heuristic.
const char *nv12VerdictHint(const Nv12Stats &s)
{
    if (!s.valid) return "n/a";
    if (s.yRowDelta > 45.0) return "garbage_interior(H-DEC?)";            // noisy/corrupt luma
    if (s.interiorZeroPct > 0.40) return "green_fill_interior(H-DEC/decode-incomplete?)";
    if (s.edgeExists && s.edgeZeroPct > 0.40 && s.interiorZeroPct < 0.10)
        return "green_edge(H-CROP: coded>display padding)";
    return "interior_clean(decode_ok->suspect_sampling/H-FMT)";
}

// §10 PRIMARY FIX: drain the decode GPU work for the DPB slot before a copy reads it.
// The decoder's DecoderEndFrame was submitted earlier on this same immediate context
// (under m_hwctx->lock); on some Intel/Arc drivers it is NOT implicitly ordered against
// our CopySubresourceRegion, so a post-seek copy can read a half-decoded slot whose
// chroma is still zeroed (=> samples as pure green). End(EVENT) + Flush + a bounded spin
// establishes "decode complete -> copy". Returns true when the GPU drained; on the
// bounded timeout it returns false and the caller copies anyway (the drop-corrupt safety
// net / next frame handle it) rather than freeze the render thread under the device lock.
// Normally returns almost immediately because frames are decoded ahead of the render
// thread, so the slot is already complete by the time we get here.
constexpr DWORD kPreviewCompletionWaitTimeoutMs = 100;

bool waitForDecodeCompletion(ID3D11Device *dev, ID3D11DeviceContext *ctx)
{
    if (!dev || !ctx) return false;
    D3D11_QUERY_DESC qd{};
    qd.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    if (FAILED(dev->CreateQuery(&qd, query.GetAddressOf())) || !query) {
        return false;
    }
    ctx->End(query.Get());
    ctx->Flush();  // submit the pending decode commands + this event to the GPU
    g_completionWaits.fetch_add(1, std::memory_order_relaxed);
    const DWORD start = GetTickCount();
    for (;;) {
        const HRESULT hr = ctx->GetData(query.Get(), nullptr, 0, 0);
        if (hr == S_OK) return true;       // all prior work (incl. the decode) completed
        if (hr != S_FALSE) return false;   // genuine error
        if (GetTickCount() - start > kPreviewCompletionWaitTimeoutMs) {
            g_completionWaitTimeouts.fetch_add(1, std::memory_order_relaxed);
            return false;                  // stuck GPU: proceed, don't freeze
        }
        SwitchToThread();                  // yield without burning a full core
    }
}

// Read back one decoder NV12 slice into a STAGING surface, compute classifying
// stats, and emit ONE event line. Consumes one dump budget unit on success. Returns
// true if a readback happened. No image files are written — the NV12-domain stats +
// verdict hint in the log line are the decisive output.
//
// Caller MUST hold m_hwctx->lock when src is the decoder DPB array (so the slot is
// not recycled mid-copy), and must pass a ctx bound to the device that owns `src`.
bool dumpNv12Slice(ID3D11Device *dev, ID3D11DeviceContext *ctx,
                   ID3D11Texture2D *src, UINT srcSlice,
                   int dispW, int dispH, const char *point, int path, double framePts,
                   int decodeErr, bool corrupt)
{
    if (!dev || !ctx || !src) return false;
    D3D11_TEXTURE2D_DESC d = {};
    src->GetDesc(&d);
    const int codedW = static_cast<int>(d.Width);
    const int codedH = static_cast<int>(d.Height);

    D3D11_TEXTURE2D_DESC sd = {};
    sd.Width = d.Width;
    sd.Height = d.Height;
    sd.Format = d.Format;
    sd.ArraySize = 1;          // single slice (DPB is a Texture2DArray; STAGING must be 1)
    sd.MipLevels = 1;
    sd.SampleDesc = { 1, 0 };
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;          // STAGING forbids bind flags
    sd.MiscFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
        return false;  // dump failure is non-fatal: skip this sample, do not drop the frame
    }
    ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, src, srcSlice, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    const uint8_t *base = static_cast<const uint8_t *>(mapped.pData);

    Nv12Stats st;
    if (d.Format == DXGI_FORMAT_NV12) {
        st = computeNv12Stats(base, mapped.RowPitch, codedW, codedH, dispW, dispH);
    }
    ctx->Unmap(staging.Get(), 0);

    // §10.5 decisive context for the verdict: how long after the last seek this frame
    // landed (green clusters just after a seek), whether FFmpeg already flagged the
    // frame bad (free safety-net signal), whether the completion-wait fix was active,
    // and the codec — so a single line says "src green, 380ms after seek, FFmpeg
    // decode_err=2 (missing ref), completion_wait was off" without external tooling.
    const long long epochNs = g_seekEpochNs.load(std::memory_order_relaxed);
    const long long sinceSeekMs =
        epochNs > 0 ? (steadyNowNs() - epochNs) / 1000000LL : -1;
    const char *codec = g_codecName.load(std::memory_order_relaxed);

    diagLog(QStringLiteral(
                "event=hwframe_dump point=%1 path=%2 frame_pts=%3 fmt=0x%4 coded=%5x%6 disp=%7x%8 "
                "y_mean=%9 y_rowdelta=%10 uv_neutral=%11 uv_zero=%12 interior_zero=%13 edge_zero=%14 "
                "hint=%15 since_seek_ms=%16 decode_err=%17 corrupt=%18 completion_wait=%19 codec=%20")
                .arg(QString::fromLatin1(point))
                .arg(path == 1 ? QStringLiteral("single") : path == 0 ? QStringLiteral("two-device") : QStringLiteral("?"))
                .arg(framePts, 0, 'f', 0)
                .arg(static_cast<unsigned int>(d.Format), 0, 16)
                .arg(codedW).arg(codedH)
                .arg(dispW).arg(dispH)
                .arg(st.yMean, 0, 'f', 1)
                .arg(st.yRowDelta, 0, 'f', 1)
                .arg(st.uvNeutralPct, 0, 'f', 3)
                .arg(st.uvZeroPct, 0, 'f', 3)
                .arg(st.interiorZeroPct, 0, 'f', 3)
                .arg(st.edgeZeroPct, 0, 'f', 3)
                .arg(QString::fromLatin1(nv12VerdictHint(st)))
                .arg(sinceSeekMs)
                .arg(decodeErr)
                .arg(corrupt ? 1 : 0)
                .arg(g_completionWait.load(std::memory_order_relaxed) ? QStringLiteral("on") : QStringLiteral("off"))
                .arg(codec ? QString::fromLatin1(codec) : QStringLiteral("?")));

    g_hwFramesDumped.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Try to consume one dump-budget unit. Returns true (and decrements) only while the
// current arm still has budget. Single-threaded on the render thread except the GUI
// re-arm store (benign).
bool tryConsumeDumpBudget()
{
    if (g_dumpRemaining.load(std::memory_order_relaxed) <= 0) return false;
    return g_dumpRemaining.fetch_sub(1, std::memory_order_relaxed) > 0;
}

}  // namespace

void qavSetPreviewDiagLogSink(QAVPreviewDiagLogFn fn)
{
    g_diagLogFn.store(fn, std::memory_order_release);
}

void qavSetPreviewHwFrameDumpConfig(int budgetPerArm)
{
    const int budget = budgetPerArm > 0 ? budgetPerArm : 0;
    g_dumpBudgetPerArm.store(budget, std::memory_order_release);
    g_dumpRemaining.store(budget, std::memory_order_release);  // arm the initial window
    g_seekEpochNs.store(steadyNowNs(), std::memory_order_release);  // start the since-seek clock
}

void qavArmPreviewHwFrameDump()
{
    g_dumpRemaining.store(g_dumpBudgetPerArm.load(std::memory_order_relaxed),
                          std::memory_order_release);
    g_seekEpochNs.store(steadyNowNs(), std::memory_order_release);  // reset since-seek clock
}

void qavSetPreviewHwDecodeFixConfig(int completionWait, int dropCorrupt)
{
    g_completionWait.store(completionWait != 0 ? 1 : 0, std::memory_order_release);
    g_dropCorrupt.store(dropCorrupt != 0 ? 1 : 0, std::memory_order_release);
}

void qavSetPreviewDiagTimingEnabled(int enabled)
{
    g_diagTimingEnabled.store(enabled != 0 ? 1 : 0, std::memory_order_release);
}

void qavGetPreviewDiagCounters(QAVPreviewDiagCounters *out)
{
    if (!out) return;
    out->copiedFramesSingle = g_copiedSingle.load(std::memory_order_relaxed);
    out->copiedFramesTwoDevice = g_copiedTwoDevice.load(std::memory_order_relaxed);
    out->texturesCreated = g_texturesCreated.load(std::memory_order_relaxed);
    out->acquireSyncTimeouts = g_acquireTimeouts.load(std::memory_order_relaxed);
    out->srcAcquireWaitSamples = g_srcAcquireWaitSamples.load(std::memory_order_relaxed);
    out->srcAcquireWaitTotalUs = g_srcAcquireWaitTotalUs.load(std::memory_order_relaxed);
    out->srcAcquireWaitMaxUs = g_srcAcquireWaitMaxUs.load(std::memory_order_relaxed);
    out->destAcquireWaitSamples = g_destAcquireWaitSamples.load(std::memory_order_relaxed);
    out->destAcquireWaitTotalUs = g_destAcquireWaitTotalUs.load(std::memory_order_relaxed);
    out->destAcquireWaitMaxUs = g_destAcquireWaitMaxUs.load(std::memory_order_relaxed);
    out->twoDeviceBridgeSamples = g_twoDeviceBridgeSamples.load(std::memory_order_relaxed);
    out->twoDeviceBridgeTotalUs = g_twoDeviceBridgeTotalUs.load(std::memory_order_relaxed);
    out->twoDeviceBridgeMaxUs = g_twoDeviceBridgeMaxUs.load(std::memory_order_relaxed);
    out->twoDeviceSourceSetupSamples = g_twoDeviceSourceSetupSamples.load(std::memory_order_relaxed);
    out->twoDeviceSourceSetupTotalUs = g_twoDeviceSourceSetupTotalUs.load(std::memory_order_relaxed);
    out->twoDeviceSourceSetupMaxUs = g_twoDeviceSourceSetupMaxUs.load(std::memory_order_relaxed);
    out->twoDeviceDestinationSetupSamples = g_twoDeviceDestinationSetupSamples.load(std::memory_order_relaxed);
    out->twoDeviceDestinationSetupTotalUs = g_twoDeviceDestinationSetupTotalUs.load(std::memory_order_relaxed);
    out->twoDeviceDestinationSetupMaxUs = g_twoDeviceDestinationSetupMaxUs.load(std::memory_order_relaxed);
    out->sourceTextureCreateSamples = g_sourceTextureCreateSamples.load(std::memory_order_relaxed);
    out->sourceTextureCreateTotalUs = g_sourceTextureCreateTotalUs.load(std::memory_order_relaxed);
    out->sourceTextureCreateMaxUs = g_sourceTextureCreateMaxUs.load(std::memory_order_relaxed);
    out->destinationTextureCreateSamples = g_destinationTextureCreateSamples.load(std::memory_order_relaxed);
    out->destinationTextureCreateTotalUs = g_destinationTextureCreateTotalUs.load(std::memory_order_relaxed);
    out->destinationTextureCreateMaxUs = g_destinationTextureCreateMaxUs.load(std::memory_order_relaxed);
    out->sharedResourceOpenSamples = g_sharedResourceOpenSamples.load(std::memory_order_relaxed);
    out->sharedResourceOpenTotalUs = g_sharedResourceOpenTotalUs.load(std::memory_order_relaxed);
    out->sharedResourceOpenMaxUs = g_sharedResourceOpenMaxUs.load(std::memory_order_relaxed);
    out->copyFailures = g_copyFailures.load(std::memory_order_relaxed);
    out->resolutionChanges = g_resolutionChanges.load(std::memory_order_relaxed);
    out->hwFramesDumped = g_hwFramesDumped.load(std::memory_order_relaxed);
    out->completionWaits = g_completionWaits.load(std::memory_order_relaxed);
    out->completionWaitTimeouts = g_completionWaitTimeouts.load(std::memory_order_relaxed);
    out->framesDecodeError = g_framesDecodeError.load(std::memory_order_relaxed);
    out->corruptFramesDropped = g_corruptDropped.load(std::memory_order_relaxed);
    out->codecName = g_codecName.load(std::memory_order_relaxed);
    out->lastCodedWidth = g_lastCodedW.load(std::memory_order_relaxed);
    out->lastCodedHeight = g_lastCodedH.load(std::memory_order_relaxed);
    out->lastDisplayWidth = g_lastDispW.load(std::memory_order_relaxed);
    out->lastDisplayHeight = g_lastDispH.load(std::memory_order_relaxed);
    out->lastDxgiFormat = g_lastFormat.load(std::memory_order_relaxed);
    out->lastPath = g_lastPath.load(std::memory_order_relaxed);
}

void qavNotePreviewCatchupSkip()
{
    g_catchupSkips.fetch_add(1, std::memory_order_relaxed);
}

unsigned long long qavTakePreviewCatchupSkipCount()
{
    return g_catchupSkips.exchange(0, std::memory_order_relaxed);
}

void QAVHWDevice_D3D11::init(AVCodecContext *avctx)
{
    // §10.5: stash the codec name for the diag lines so the user need not ffprobe.
    // avcodec_get_name returns a process-stable static string (safe to store bare).
    if (avctx) {
        g_codecName.store(avcodec_get_name(avctx->codec_id), std::memory_order_relaxed);
    }

    int ret = avcodec_get_hw_frames_parameters(avctx,
                                               avctx->hw_device_ctx,
                                               AV_PIX_FMT_D3D11,
                                               &avctx->hw_frames_ctx);

    if (ret < 0) {
        qWarning() << "Failed to allocate HW frames context:" << ret;
        return;
    }

    auto frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    auto hwctx = (AVD3D11VAFramesContext *)frames_ctx->hwctx;
    hwctx->MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hwctx->BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
    // MiaCode S2: give the D3D11VA decode pool headroom. QtAVPlayer builds the
    // hw_frames_ctx manually via avcodec_get_hw_frames_parameters (above), which
    // bypasses FFmpeg's internal +3 working-surface margin, so the default pool is
    // tight; a slow render-thread consumer (the cross-device keyed-mutex bridge) can
    // pin the last surface and stall the decode thread on av_hwframe_get_buffer. Add
    // a small margin (DPB peak + in-flight bridge/QSG frames). Guarded on >0 so the
    // software path (initial_pool_size==0) and any backend that didn't size the pool
    // here are untouched. ~25MB extra @1080p NV12 — negligible, dGPU included.
    if (frames_ctx->initial_pool_size > 0)
        frames_ctx->initial_pool_size += 8;
    ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);
    if (ret < 0) {
        qWarning() << "Failed to initialize HW frames context:" << ret;
        av_buffer_unref(&avctx->hw_frames_ctx);
    }
}

AVPixelFormat QAVHWDevice_D3D11::format() const
{
    return AV_PIX_FMT_D3D11;
}

AVHWDeviceType QAVHWDevice_D3D11::type() const
{
    return AV_HWDEVICE_TYPE_D3D11VA;
}

static ComPtr<ID3D11Texture2D> shareTexture(ID3D11Device *dev, ID3D11Texture2D *tex)
{
    ComPtr<IDXGIResource> dxgiResource;
    ENSURE(tex->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(dxgiResource.GetAddressOf())), {});
    HANDLE shared = nullptr;
    ENSURE(dxgiResource->GetSharedHandle(&shared), {});
    ComPtr<ID3D11Texture2D> sharedTex;
    ENSURE(dev->OpenSharedResource(shared, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(sharedTex.GetAddressOf())), {});
    return sharedTex;
}

static ComPtr<ID3D11Texture2D> copyTexture(ID3D11Device *dev, ID3D11Texture2D *from, int index)
{
    D3D11_TEXTURE2D_DESC fromDesc = {};
    from->GetDesc(&fromDesc);

    D3D11_TEXTURE2D_DESC toDesc = {};
    toDesc.Width = fromDesc.Width;
    toDesc.Height = fromDesc.Height;
    toDesc.Format = fromDesc.Format;
    toDesc.ArraySize = 1;
    toDesc.MipLevels = 1;
    toDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    toDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    toDesc.Usage = D3D11_USAGE_DEFAULT;
    toDesc.SampleDesc = { 1, 0 };

    ComPtr<ID3D11Texture2D> copy;
    ENSURE(dev->CreateTexture2D(&toDesc, nullptr, &copy), {});

    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(ctx.GetAddressOf());
    ctx->CopySubresourceRegion(copy.Get(), 0, 0, 0, 0, from, index, nullptr);
    return copy;
}

#if defined(QT_AVPLAYER_MULTIMEDIA) && QT_VERSION >= QT_VERSION_CHECK(6, 6, 2)

static ComPtr<ID3D11Device1> D3D11Device(QRhi *rhi)
{
    auto native = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
    if (!native) {
        qWarning() << "Failed to get nativeHandles";
        return {};
    }

    ComPtr<ID3D11Device> rhiDevice = static_cast<ID3D11Device *>(native->dev);
    ComPtr<ID3D11Device1> dev;
    ENSURE(rhiDevice.As(&dev), {});
    return dev;
}

class QAVVideoFrame_D3D11
{
public:
    QAVVideoFrame_D3D11(const AVFrame *frame)
        : m_texture((ID3D11Texture2D *)(uintptr_t)frame->data[0])
        , m_textureIndex((intptr_t)frame->data[1])
        , m_displayW(frame->width)    // MiaCode diag: display size (vs coded/aligned texture size)
        , m_displayH(frame->height)
        , m_framePts(frame->pts != AV_NOPTS_VALUE ? double(frame->pts) : -1.0)  // raw stream-timebase pts
        , m_decodeErrorFlags(frame->decode_error_flags)                          // §10 free corrupt signal
        , m_corrupt((frame->flags & AV_FRAME_FLAG_CORRUPT) != 0)
    {
        auto ctx = reinterpret_cast<AVHWFramesContext *>(frame->hw_frames_ctx->data);
        m_hwctx = static_cast<AVD3D11VADeviceContext *>(ctx->device_ctx->hwctx);
    }

    ComPtr<ID3D11Texture2D> copyTexture(const ComPtr<ID3D11Device1> &dev,
                                        const ComPtr<ID3D11DeviceContext> &ctx);

    // MiaCode H2: same-device fast path. When the decoder shares the renderer's
    // ID3D11Device, copy straight out of the decoder DPB slot into a sampleable
    // NV12 texture — no shared handle, no keyed mutex, no AcquireSync(INFINITE).
    ComPtr<ID3D11Texture2D> copyTextureSameDevice(const ComPtr<ID3D11Device1> &dev,
                                                  const ComPtr<ID3D11DeviceContext> &ctx);

    // The ID3D11Device the frame was decoded on (for single-device detection).
    ID3D11Device *decodeDevice() const { return m_hwctx ? m_hwctx->device : nullptr; }

    // §10 free corrupt signals copied off the AVFrame at construction.
    int decodeErrorFlags() const { return m_decodeErrorFlags; }
    bool isCorrupt() const { return m_corrupt; }

private:
    bool copyToShared(bool collectTiming);

    const ComPtr<ID3D11Texture2D> m_texture;
    const UINT m_textureIndex = 0;
    const int m_displayW = 0;   // MiaCode diag: display (cropped) size for coded-vs-display H-CROP signal
    const int m_displayH = 0;
    const double m_framePts = -1.0;
    const int m_decodeErrorFlags = 0;   // §10: AVFrame::decode_error_flags (FF_DECODE_ERROR_*)
    const bool m_corrupt = false;       // §10: AVFrame flag AV_FRAME_FLAG_CORRUPT
    AVD3D11VADeviceContext *m_hwctx = nullptr;

    struct SharedTextureHandleTraits
    {
        using Type = HANDLE;
        static Type invalidValue() { return nullptr; }
        static bool close(Type handle) { return CloseHandle(handle) != 0; }
    };
    QUniqueHandle<SharedTextureHandleTraits> m_sharedHandle;

    const UINT m_srcKey = 0;
    ComPtr<ID3D11Texture2D> m_srcTex;
    ComPtr<IDXGIKeyedMutex> m_srcMutex;

    const UINT m_destKey = 1;
    ComPtr<ID3D11Texture2D> m_destTex;
    ComPtr<IDXGIKeyedMutex> m_destMutex;
};

bool QAVVideoFrame_D3D11::copyToShared(bool collectTiming)
{
    const bool timeBridge = collectTiming;
    const long long sourceSetupStartedNs = timeBridge ? steadyNowNs() : 0;
    const auto sourceSetupTimer = qScopeGuard([&] {
        if (timeBridge) {
            noteTimedPhase(
                g_twoDeviceSourceSetupSamples,
                g_twoDeviceSourceSetupTotalUs,
                g_twoDeviceSourceSetupMaxUs,
                sourceSetupStartedNs);
        }
    });
    m_hwctx->lock(m_hwctx->lock_ctx);
    QScopeGuard autoUnlock([&] { m_hwctx->unlock(m_hwctx->lock_ctx); });

    CD3D11_TEXTURE2D_DESC desc{};
    m_texture->GetDesc(&desc);

    CD3D11_TEXTURE2D_DESC texDesc{ desc.Format, desc.Width, desc.Height };
    texDesc.MipLevels = 1;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const long long sourceCreateStartedNs = timeBridge ? steadyNowNs() : 0;
    const HRESULT sourceCreateHr =
        m_hwctx->device->CreateTexture2D(&texDesc, nullptr, m_srcTex.ReleaseAndGetAddressOf());
    if (timeBridge) {
        noteTimedPhase(
            g_sourceTextureCreateSamples,
            g_sourceTextureCreateTotalUs,
            g_sourceTextureCreateMaxUs,
            sourceCreateStartedNs);
    }
    ENSURE(sourceCreateHr, false);
    g_texturesCreated.fetch_add(1, std::memory_order_relaxed);

    // §10 PRIMARY FIX: force the decode GPU work for this DPB slot to COMPLETE before we
    // read it (the src dump below and the CopySubresourceRegion further down). The decode
    // ran on this same decode-device immediate context; without this drain a post-seek
    // copy can read a half-decoded (zero-chroma => green) slot. Done first so the readback
    // dump reflects the post-wait state (so the A/B shows up in the dump's verdict hint).
    if (g_completionWait.load(std::memory_order_relaxed)) {
        waitForDecodeCompletion(m_hwctx->device, m_hwctx->device_context);
    }

    // MiaCode diag: read back the decoder DPB slot (the SOURCE) on the decode device
    // while we hold m_hwctx->lock (so the slot isn't recycled). On the two-device path
    // this is the only chance to see the decoder output before the cross-device bridge.
    if (tryConsumeDumpBudget()) {
        dumpNv12Slice(m_hwctx->device, m_hwctx->device_context, m_texture.Get(),
                      m_textureIndex, m_displayW, m_displayH, "src", /*path=*/0, m_framePts,
                      m_decodeErrorFlags, m_corrupt);
    }

    ComPtr<IDXGIResource1> res;
    ENSURE(m_srcTex.As(&res), false);
    ENSURE(res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &m_sharedHandle), false);
    ENSURE(m_srcTex.As(&m_srcMutex), false);
    m_hwctx->device_context->Flush();
    // MiaCode S1: bounded acquire (was INFINITE). AcquireSync returns WAIT_TIMEOUT
    // (0x102, POSITIVE) on timeout, which FAILED()/ENSURE do NOT catch — so hand-check
    // hr != S_OK. On timeout we did NOT acquire the mutex: skip the copy AND the
    // ReleaseSync, drop the frame (copyTexture/handle return {} -> RHI reuses the
    // previous frame). m_srcTex + its keyed mutex are rebuilt next frame, so there is
    // no lingering key state to rebalance.
    const bool timeAcquire = timeBridge;
    const long long srcAcquireStartedNs = timeAcquire ? steadyNowNs() : 0;
    const HRESULT srcAcq = m_srcMutex->AcquireSync(m_srcKey, kPreviewAcquireSyncTimeoutMs);
    if (timeAcquire) {
        noteTimedPhase(
            g_srcAcquireWaitSamples, g_srcAcquireWaitTotalUs, g_srcAcquireWaitMaxUs, srcAcquireStartedNs);
    }
    if (srcAcq != S_OK) {
        g_acquireTimeouts.fetch_add(1, std::memory_order_relaxed);
        qWarning() << QString::fromLatin1("Error@%1. AcquireSync(src) timeout/fail: (0x%2)")
                          .arg(__LINE__).arg(static_cast<quint32>(srcAcq), 0, 16);
        return false;
    }
    m_hwctx->device_context->CopySubresourceRegion(m_srcTex.Get(), 0, 0, 0, 0, m_texture.Get(), m_textureIndex, nullptr);
    m_srcMutex->ReleaseSync(m_destKey);
    return true;
}

ComPtr<ID3D11Texture2D> QAVVideoFrame_D3D11::copyTexture(const ComPtr<ID3D11Device1> &dev,
                                                         const ComPtr<ID3D11DeviceContext> &ctx)
{
    const bool timeBridge = g_diagTimingEnabled.load(std::memory_order_relaxed) != 0;
    const long long bridgeStartedNs = timeBridge ? steadyNowNs() : 0;
    const auto bridgeTimer = qScopeGuard([&] {
        if (timeBridge) {
            noteTimedPhase(
                g_twoDeviceBridgeSamples,
                g_twoDeviceBridgeTotalUs,
                g_twoDeviceBridgeMaxUs,
                bridgeStartedNs);
        }
    });
    if (!copyToShared(timeBridge))
        return {};

    const long long destinationSetupStartedNs = timeBridge ? steadyNowNs() : 0;
    const auto destinationSetupTimer = qScopeGuard([&] {
        if (timeBridge) {
            noteTimedPhase(
                g_twoDeviceDestinationSetupSamples,
                g_twoDeviceDestinationSetupTotalUs,
                g_twoDeviceDestinationSetupMaxUs,
                destinationSetupStartedNs);
        }
    });
    const long long openSharedStartedNs = timeBridge ? steadyNowNs() : 0;
    const HRESULT openSharedHr =
        dev->OpenSharedResource1(m_sharedHandle.get(), IID_PPV_ARGS(&m_destTex));
    if (timeBridge) {
        noteTimedPhase(
            g_sharedResourceOpenSamples,
            g_sharedResourceOpenTotalUs,
            g_sharedResourceOpenMaxUs,
            openSharedStartedNs);
    }
    ENSURE(openSharedHr, {});
    CD3D11_TEXTURE2D_DESC desc{};
    m_destTex->GetDesc(&desc);

    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    desc.Usage = D3D11_USAGE_DEFAULT;
    // The bridge output is sampled by Qt Quick only.  In particular P010 is a
    // valid shader-resource video texture but not a general render target;
    // requesting D3D11_BIND_RENDER_TARGET here can make an otherwise valid
    // Main10 decode surface fail or trip a driver error.
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> outputTex;
    const long long destinationCreateStartedNs = timeBridge ? steadyNowNs() : 0;
    const HRESULT destinationCreateHr =
        dev->CreateTexture2D(&desc, nullptr, outputTex.ReleaseAndGetAddressOf());
    if (timeBridge) {
        noteTimedPhase(
            g_destinationTextureCreateSamples,
            g_destinationTextureCreateTotalUs,
            g_destinationTextureCreateMaxUs,
            destinationCreateStartedNs);
    }
    ENSURE(destinationCreateHr, {});
    g_texturesCreated.fetch_add(1, std::memory_order_relaxed);
    ENSURE(m_destTex.As(&m_destMutex), {});
    // MiaCode S1: bounded acquire (was INFINITE). See copyToShared. A dest-side
    // timeout just drops the frame (return {}); no rebalance — this whole per-frame
    // QAVVideoFrame_D3D11 (srcTex/sharedHandle/destTex + keyed mutex) is rebuilt fresh
    // next frame, so the 0->1->0 handoff self-resets.
    const bool timeAcquire = timeBridge;
    const long long destAcquireStartedNs = timeAcquire ? steadyNowNs() : 0;
    const HRESULT destAcq = m_destMutex->AcquireSync(m_destKey, kPreviewAcquireSyncTimeoutMs);
    if (timeAcquire) {
        noteTimedPhase(
            g_destAcquireWaitSamples, g_destAcquireWaitTotalUs, g_destAcquireWaitMaxUs, destAcquireStartedNs);
    }
    if (destAcq != S_OK) {
        g_acquireTimeouts.fetch_add(1, std::memory_order_relaxed);
        qWarning() << QString::fromLatin1("Error@%1. AcquireSync(dest) timeout/fail: (0x%2)")
                          .arg(__LINE__).arg(static_cast<quint32>(destAcq), 0, 16);
        return {};
    }

    ctx->CopySubresourceRegion(outputTex.Get(), 0, 0, 0, 0, m_destTex.Get(), 0, nullptr);
    m_destMutex->ReleaseSync(m_srcKey);

    // MiaCode diag: geometry tracking (coded-vs-display, always) + bounded OUTPUT
    // readback. On the two-device path the src vs output comparison localizes whether
    // the cross-device keyed-mutex bridge corrupted the active region.
    noteFrameGeometry(static_cast<int>(desc.Width), static_cast<int>(desc.Height),
                      m_displayW, m_displayH, static_cast<unsigned int>(desc.Format), /*path=*/0);
    g_copiedTwoDevice.fetch_add(1, std::memory_order_relaxed);
    if (tryConsumeDumpBudget()) {
        dumpNv12Slice(dev.Get(), ctx.Get(), outputTex.Get(), /*slice=*/0,
                      m_displayW, m_displayH, "out", /*path=*/0, m_framePts,
                      m_decodeErrorFlags, m_corrupt);
    }
    return outputTex;
}

ComPtr<ID3D11Texture2D> QAVVideoFrame_D3D11::copyTextureSameDevice(const ComPtr<ID3D11Device1> &dev,
                                                                   const ComPtr<ID3D11DeviceContext> &ctx)
{
    // Decoder and renderer share one ID3D11Device, so the decoded NV12 surface
    // already lives where the RHI samples. We still copy it out of the decoder's
    // DPB array slot (which FFmpeg will recycle for the next frame) into a fresh
    // single-slice texture — but with NO shared handle, NO keyed mutex and NO
    // AcquireSync(INFINITE). This is the whole point of H2: factors A's render-
    // thread accomplice (cross-device copy) and B (the INFINITE-wait freeze) are
    // gone; one same-device CopySubresourceRegion remains (cf. mpv's copy_tex).
    CD3D11_TEXTURE2D_DESC srcDesc{};
    m_texture->GetDesc(&srcDesc);

    CD3D11_TEXTURE2D_DESC outDesc{ srcDesc.Format, srcDesc.Width, srcDesc.Height };
    outDesc.MipLevels = 1;
    outDesc.ArraySize = 1;
    outDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    outDesc.Usage = D3D11_USAGE_DEFAULT;
    outDesc.SampleDesc = { 1, 0 };

    ComPtr<ID3D11Texture2D> outputTex;
    ENSURE(dev->CreateTexture2D(&outDesc, nullptr, outputTex.ReleaseAndGetAddressOf()), {});
    g_texturesCreated.fetch_add(1, std::memory_order_relaxed);

    // Hold the device lock around the copy: the D3D11VA decoder takes the same
    // lock (ff_dxva2_lock) around DecoderBeginFrame..EndFrame, so this copy
    // serializes against an in-flight decode and the DPB slot isn't recycled
    // mid-copy.
    //
    // CAVEAT (single-device only): this lock covers the decode submission and
    // THIS copy, but NOT Qt's ordinary QSG scene-graph draws, which run on the
    // render thread and drive the *same* shared immediate context without taking
    // lock_ctx. SetMultithreadProtected(TRUE) (set by the renderer when it
    // creates the device) serializes each individual context call, which is the
    // same guarantee mpv/Chromium rely on when sharing one device — but it does
    // NOT make the decoder's multi-call BeginFrame..EndFrame atomic against
    // unrelated render-thread draws. This is the key risk to validate on the
    // target Arc iGPU under the D3D11 debug layer before H2 becomes the default;
    // see docs/PREVIEW_VIDEO_IGPU_STUTTER_INVESTIGATION_AND_FIX_ZH.md §4 H2.
    m_hwctx->lock(m_hwctx->lock_ctx);
    QScopeGuard autoUnlock([&] { m_hwctx->unlock(m_hwctx->lock_ctx); });

    // §10 PRIMARY FIX: drain the decode GPU work for this DPB slot before the copy reads
    // it. `ctx` is the shared device's immediate context (== the context the decoder
    // submitted DecoderEndFrame on), so the EVENT query here orders decode -> copy and
    // kills the post-seek half-decoded green slot. Bounded; on timeout we copy anyway.
    if (g_completionWait.load(std::memory_order_relaxed)) {
        waitForDecodeCompletion(dev.Get(), ctx.Get());
    }

    ctx->CopySubresourceRegion(outputTex.Get(), 0, 0, 0, 0, m_texture.Get(), m_textureIndex, nullptr);

    // MiaCode diag: geometry tracking (coded-vs-display, every frame, cheap atomics) +
    // bounded SOURCE readback. On the single-device path outputTex is a bit-exact whole-
    // slice copy of the DPB at coded size, so dumping the SOURCE slot here is the decisive
    // experiment: source bad => H-DEC; source clean (interior) but screen bad => sampling/
    // H-FMT or coded>display H-CROP (read coded vs disp + uv_zero edge/interior in the line).
    // Done under m_hwctx->lock so the DPB slot can't be recycled mid-readback.
    noteFrameGeometry(static_cast<int>(srcDesc.Width), static_cast<int>(srcDesc.Height),
                      m_displayW, m_displayH, static_cast<unsigned int>(srcDesc.Format), /*path=*/1);
    g_copiedSingle.fetch_add(1, std::memory_order_relaxed);
    if (tryConsumeDumpBudget()) {
        dumpNv12Slice(dev.Get(), ctx.Get(), m_texture.Get(), m_textureIndex,
                      m_displayW, m_displayH, "src", /*path=*/1, m_framePts,
                      m_decodeErrorFlags, m_corrupt);
    }
    return outputTex;
}

class VideoBuffer_D3D11: public QAVVideoBuffer_GPU
{
public:
    VideoBuffer_D3D11(const QAVVideoFrame &frame)
        : QAVVideoBuffer_GPU(frame)
    {
    }

    QAVVideoFrame::HandleType handleType() const override
    {
        return QAVVideoFrame::D3D11Texture2DHandle;
    }

    QVariant handle(QRhi *rhi) const override
    {
        if (!rhi || rhi->backend() != QRhi::D3D11)
            return {};

        if (!m_texture) {
            if (!frame())
                return {};

            if (frame().format() != AV_PIX_FMT_D3D11) {
                qWarning() << "Only AV_PIX_FMT_D3D11 is supported, but got" << frame().formatName();
                return {};
            }
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
            auto av_frame = frame().frame();
            auto texture = (ID3D11Texture2D *)(uintptr_t)av_frame->data[0];
            auto texture_index = (intptr_t)av_frame->data[1];
            if (!texture) {
                qWarning() << "No texture in the frame" << frame().pts();
                return {};
            }
            auto nh = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
            if (!nh) {
                qWarning() << "No QRhiD3D11NativeHandles";
                return {};
            }

            auto dev = reinterpret_cast<ID3D11Device *>(nh->dev);
            if (!dev) {
                qWarning() << "No ID3D11Device device";
                return {};
            }

            auto shared = shareTexture(dev, texture);
            if (shared)
                const_cast<VideoBuffer_D3D11*>(this)->m_texture = copyTexture(dev, shared.Get(), texture_index);
#else
            auto devRHI = D3D11Device(rhi);
            if (!devRHI) {
                qWarning() << "No device for RHI";
                return {};
            }
            ComPtr<ID3D11DeviceContext> ctxRHI;
            devRHI->GetImmediateContext(ctxRHI.GetAddressOf());

            // MiaCode H2: identity-compare the *raw* render device (the exact
            // ID3D11Device* the renderer handed to QRhi, which is also what it
            // published to the decoder) against the decode device. ID3D11Device1
            // QI'd from it can have a different pointer, so don't compare devRHI.
            auto nh = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
            ID3D11Device *renderDevice = nh ? static_cast<ID3D11Device *>(nh->dev) : nullptr;

            QAVVideoFrame_D3D11 videoFrame(frame().frame());

            // §10 SAFETY NET: count frames FFmpeg flagged corrupt / decode-error, and —
            // when the drop-corrupt fix is enabled — skip them entirely (return {} => RHI
            // keeps the last good frame; a brief freeze beats a green flash). Free: reads
            // the AVFrame flags copied at construction, no GPU work. The count is always
            // tracked so the summary reveals whether FFmpeg even knows the green frames
            // are bad (it may not — then completion-wait, not this, is the real fix).
            if (videoFrame.isCorrupt() || videoFrame.decodeErrorFlags() != 0) {
                g_framesDecodeError.fetch_add(1, std::memory_order_relaxed);
                if (g_dropCorrupt.load(std::memory_order_relaxed)) {
                    g_corruptDropped.fetch_add(1, std::memory_order_relaxed);
                    return {};
                }
            }

            const bool singleDevice =
                renderDevice != nullptr && renderDevice == videoFrame.decodeDevice();
            // Log on every transition of the resolved path (not once per process):
            // the decision is per-(QAVPlayer, consumer RHI), so a single global
            // latch could report a stale path for later media. -1 = nothing logged.
            static std::atomic<int> lastLoggedPath{ -1 };
            const int currentPath = singleDevice ? 1 : 0;
            if (lastLoggedPath.exchange(currentPath) != currentPath) {
                qDebug() << "QtAVPlayer D3D11 preview frame path:"
                         << (singleDevice ? "single-device (H2, no cross-device bridge)"
                                          : "two-device bridge (keyed-mutex)");
                // MiaCode diag: sparse path-transition line into the structured runtime
                // log (qDebug above only reaches the console). One line per path flip.
                diagLog(QStringLiteral("event=hwframe_path path=%1 render_device=0x%2 decode_device=0x%3")
                            .arg(singleDevice ? QStringLiteral("single") : QStringLiteral("two-device"))
                            .arg(reinterpret_cast<quintptr>(renderDevice), 0, 16)
                            .arg(reinterpret_cast<quintptr>(videoFrame.decodeDevice()), 0, 16));
            }
            auto outputTex = singleDevice ? videoFrame.copyTextureSameDevice(devRHI, ctxRHI)
                                          : videoFrame.copyTexture(devRHI, ctxRHI);
            if (!outputTex) {
                g_copyFailures.fetch_add(1, std::memory_order_relaxed);
                qWarning() << "Could not copy d3d11 texture";
                return {};
            }

            // Keep alive texture while the frame is alive
            const_cast<VideoBuffer_D3D11*>(this)->m_texture = outputTex;
#endif // #if QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
        }

        // Return both planes of the same multi-plane D3D11 texture. Qt selects
        // the matching plane views from QVideoFrameFormat (NV12, P010, or P016).
        QList<quint64> textures = {quint64(m_texture.Get()), quint64(m_texture.Get())};
        return QVariant::fromValue(textures);
    }

    ComPtr<ID3D11Texture2D> m_texture;
};

QAVVideoBuffer *QAVHWDevice_D3D11::videoBuffer(const QAVVideoFrame &frame) const
{
    return new VideoBuffer_D3D11(frame);
}

#else // #if QT_VERSION >= QT_VERSION_CHECK(6, 6, 2)

QAVVideoBuffer *QAVHWDevice_D3D11::videoBuffer(const QAVVideoFrame &frame) const
{
    return new QAVVideoBuffer_GPU(frame);
}

#endif // #if QT_VERSION >= QT_VERSION_CHECK(6, 6, 2)

static constexpr const char* hlsl = R"(
    Texture2D<float> texY : register(t0);         // Y plane
    Texture2D<float2> texUV : register(t1);       // UV plane

    RWTexture2D<float> outY : register(u0);       // Output Y
    RWTexture2D<float4> outUV : register(u1);     // Output UV

    [numthreads(16, 16, 1)]
    void CSMain(uint3 id : SV_DispatchThreadID)
    {
        uint2 coord = id.xy;

        // Write Y (full size)
        outY[coord] = texY.Load(int3(coord, 0));

        // UV is subsampled vertically (not horizontally!)
        if (coord.y % 2 == 0)
        {
            uint2 uvCoord = coord / uint2(1, 2);
            float2 uv = texUV.Load(int3(uvCoord, 0));
            outUV[uvCoord] = float4(uv.x, 0, 0, uv.y);
        }
    }
)";

class VideoBuffer_D3D11_GL : public QAVVideoBuffer_GPU
{
public:
    static void init()
    {
        if (!s_wglDXOpenDeviceNV) {
#ifndef _MSC_VER
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
            s_wglDXOpenDeviceNV = (wglDXOpenDeviceNV_)wglGetProcAddress("wglDXOpenDeviceNV");
            s_wglDXCloseDeviceNV = (wglDXCloseDeviceNV_)wglGetProcAddress("wglDXCloseDeviceNV");
            s_wglDXRegisterObjectNV = (wglDXRegisterObjectNV_)wglGetProcAddress("wglDXRegisterObjectNV");
            s_wglDXUnregisterObjectNV = (wglDXUnregisterObjectNV_)wglGetProcAddress("wglDXUnregisterObjectNV");
            s_wglDXLockObjectsNV = (wglDXLockObjectsNV)wglGetProcAddress("wglDXLockObjectsNV");
            s_wglDXUnlockObjectsNV = (wglDXUnlockObjectsNV)wglGetProcAddress("wglDXUnlockObjectsNV");
            s_wglDXObjectAccessNV = (wglDXObjectAccessNV)wglGetProcAddress("wglDXObjectAccessNV");
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
            createDevice();
            initDebug();
            compileShader();
        }
    }

    static void createDevice()
    {
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef DEBUG
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_1
        };
        CHECK(D3D11CreateDevice(nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            s_d3ddev.ReleaseAndGetAddressOf(),
            nullptr,
            s_d3dctx.ReleaseAndGetAddressOf()));
        s_dxdev = s_wglDXOpenDeviceNV(s_d3ddev.Get());
    }

    static void compileShader()
    {
        CHECK(D3DCompile(
            hlsl,                      // Pointer to the source code
            strlen(hlsl),              // Length of the code
            nullptr,                   // Optional source file name (for error messages)
            nullptr, nullptr,          // Optional macros and include handler
            "CSMain",                  // Entry point
            "cs_5_0",                  // Target profile
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &s_compiledBlob,
            &s_errorBlob
        ));

        CHECK(s_d3ddev->CreateComputeShader(s_compiledBlob->GetBufferPointer(),
            s_compiledBlob->GetBufferSize(),
            nullptr,
            s_computeShader.GetAddressOf()));
        s_d3dctx->CSSetShader(s_computeShader.Get(), nullptr, 0);
    }

    static void initDebug()
    {
#ifdef DEBUG
        ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(s_d3ddev.As(&debug)))
            CHECK(debug->QueryInterface(IID_PPV_ARGS(&s_infoQueue)));
#endif
    }

    static void printDebug()
    {
#ifdef DEBUG
        Q_ASSERT(s_infoQueue);
        UINT64 messageCount = s_infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < messageCount; i++) {
            SIZE_T messageLength = 0;
            s_infoQueue->GetMessage(i, nullptr, &messageLength);

            D3D11_MESSAGE* message = (D3D11_MESSAGE*)malloc(messageLength);
            if (message) {
                s_infoQueue->GetMessage(i, message, &messageLength);
                printf("D3D11 Debug: %s\n", message->pDescription);
                OutputDebugStringA(message->pDescription);
                free(message);
            }
        }
#endif
    }

    VideoBuffer_D3D11_GL(const QAVVideoFrame &frame)
        : QAVVideoBuffer_GPU(frame)
    {
        init();
    }

    ~VideoBuffer_D3D11_GL()
    {
        glBindTexture(GL_TEXTURE_2D, 0);
        for (int i = 0; i < 2 && m_texHD[i]; ++i) {
            s_wglDXUnlockObjectsNV(s_dxdev, 1, &m_texHD[i]);
            s_wglDXUnregisterObjectNV(s_dxdev, m_texHD[i]);
        }
        glDeleteTextures(2, m_texGL);
    }

    QAVVideoFrame::HandleType handleType() const override
    {
        return QAVVideoFrame::GLTextureHandle;
    }

    QList<quint64> textures()
    {
        if (!s_wglDXOpenDeviceNV) {
            qWarning() << "Could not get proc address: s_wglDXOpenDeviceNV";
            return {};
        }

        if (!frame())
            return {};

        if (m_texGL[0])
            return {m_texGL[0], m_texGL[1]};

        // First copy ffmpeg texture to shared one
        const auto av_frame = frame().frame();
        const auto width = av_frame->width;
        const auto height = av_frame->height;

        auto texture = (ID3D11Texture2D *)(uintptr_t)av_frame->data[0];
        auto texture_index = (intptr_t)av_frame->data[1];
        if (!texture) {
            qWarning() << "No texture in the frame" << frame().pts();
            return {};
        }
        auto shared = shareTexture(s_d3ddev.Get(), texture);
        auto outputTex = copyTexture(s_d3ddev.Get(), shared.Get(), texture_index);
        if (!outputTex) {
            qWarning() << "Could not copy d3d11 texture";
            return {};
        }

        // OpenGL does not support NV12 textures natively, so
        // we should return 2 textures: for Y and UV planes.
        // Need to copy planes to specific texture:
        //  1. DXGI_FORMAT_NV12 cannot be used in wglDXRegisterObjectNV
        //  2. Creating few ID3D11Texture2D and copying the data to it
        //     via CopySubresourceRegion(nv12Texture) is not allowed.
        // Solution:
        //  1. Create 2 shared ID3D11Texture2D textures: R8 and BGRA (not R8G8)
        //  2. Create Shader Resource Views (SRV): R8 and R8G8
        //  3. Create Unordered Access Views (UAV) for ID3D11Texture2D textures: plane 0 and plane 1
        //  4. Extract planes in HLSL and copy to ID3D11Texture2D: R8 and BGRA
        //     NOTE: BGRA (and not R8G8) is needed by OpenGL shader used to render in QAVWidget_OpenGL
        //  5. Generate OpenGL textures and bind ID3D11Texture2D R8 and BGRA to OpenGL via wglDXRegisterObjectNV

        // For Y plane (Plane 0)
        D3D11_TEXTURE2D_DESC descY = {};
        descY.Width = width;
        descY.Height = height;
        descY.Format = DXGI_FORMAT_R8_UNORM;
        descY.Usage = D3D11_USAGE_DEFAULT;
        descY.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        descY.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        descY.ArraySize = 1;
        descY.MipLevels = 1;
        descY.SampleDesc.Count = 1;

        ComPtr<ID3D11Texture2D> texY;
        ENSURE(s_d3ddev->CreateTexture2D(&descY, nullptr, &texY), {});

        // For UV plane (Plane 1)
        D3D11_TEXTURE2D_DESC descUV = {};
        descUV.Width = width / 2;
        descUV.Height = height / 2;
        descUV.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        descUV.Usage = D3D11_USAGE_DEFAULT;
        descUV.BindFlags = D3D11_BIND_SHADER_RESOURCE| D3D11_BIND_UNORDERED_ACCESS;
        descUV.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        descUV.ArraySize = 1;
        descUV.MipLevels = 1;
        descUV.SampleDesc.Count = 1;

        ComPtr<ID3D11Texture2D> texUV;
        ENSURE(s_d3ddev->CreateTexture2D(&descUV, nullptr, &texUV), {});

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDescY = {};
        srvDescY.Format = DXGI_FORMAT_R8_UNORM;
        srvDescY.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDescY.Texture2D.MipLevels = 1;
        srvDescY.Texture2D.MostDetailedMip = 0;

        ComPtr<ID3D11ShaderResourceView> srvY;
        ENSURE(s_d3ddev->CreateShaderResourceView(outputTex.Get(), &srvDescY, &srvY), {});

        D3D11_SHADER_RESOURCE_VIEW_DESC1 srvDescUV = {};
        srvDescUV.Format = DXGI_FORMAT_R8G8_UNORM;
        srvDescUV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDescUV.Texture2D.MipLevels = 1;
        srvDescUV.Texture2D.MostDetailedMip = 0;
        srvDescUV.Texture2D.PlaneSlice = 1;

        ComPtr<ID3D11ShaderResourceView1> srvUV;
        ComPtr<ID3D11Device3> dev3;
        ENSURE(s_d3ddev->QueryInterface(IID_PPV_ARGS(&dev3)), {});

        ENSURE(dev3->CreateShaderResourceView1(outputTex.Get(), &srvDescUV, &srvUV), {});

        ComPtr<ID3D11UnorderedAccessView> yUAV, uvUAV;
        ENSURE(s_d3ddev->CreateUnorderedAccessView(texY.Get(), nullptr, &yUAV), {});
        ENSURE(s_d3ddev->CreateUnorderedAccessView(texUV.Get(), nullptr, &uvUAV), {});

        ID3D11ShaderResourceView* srvs[] = { srvY.Get(), srvUV.Get() };
        s_d3dctx->CSSetShaderResources(0, 2, srvs);
        ID3D11UnorderedAccessView* uavs[2] = { yUAV.Get(), uvUAV.Get() };
        s_d3dctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

        s_d3dctx->Dispatch((width + 15) / 16, (height + 15) / 16, 1);

        glGenTextures(2, m_texGL);
        m_texHD[0] = s_wglDXRegisterObjectNV(s_dxdev, texY.Get(), m_texGL[0], GL_TEXTURE_2D, WGL_ACCESS_READ_WRITE_NV);
        s_wglDXLockObjectsNV(s_dxdev, 1, &m_texHD[0]);
        s_wglDXObjectAccessNV(m_texHD[0], WGL_ACCESS_READ_WRITE_NV);

        m_texHD[1] = s_wglDXRegisterObjectNV(s_dxdev, texUV.Get(), m_texGL[1], GL_TEXTURE_2D, WGL_ACCESS_READ_WRITE_NV);
        s_wglDXLockObjectsNV(s_dxdev, 1, &m_texHD[1]);
        s_wglDXObjectAccessNV(m_texHD[1], WGL_ACCESS_READ_WRITE_NV);

        // Optionally unbind (to avoid D3D warnings)
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        s_d3dctx->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
        s_d3dctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

        return {m_texGL[0], m_texGL[1]};
    }

    QVariant handle(QRhi *) const override
    {
        QList<quint64> v = const_cast<VideoBuffer_D3D11_GL *>(this)->textures();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QList<QVariant> list;
        for (auto &a : v)
            list.append(QVariant(a));
        return list;
#else
        return QVariant::fromValue(v);
#endif
    }

    GLuint m_texGL[2] = {0, 0};
    HANDLE m_texHD[2] = {nullptr, nullptr};

    static ComPtr<ID3D11Device> s_d3ddev;
    static ComPtr<ID3D11DeviceContext> s_d3dctx;
    static ComPtr<ID3DBlob> s_compiledBlob;
    static ComPtr<ID3DBlob> s_errorBlob;
    static ComPtr<ID3D11ComputeShader> s_computeShader;
    static HANDLE s_dxdev;
#ifdef DEBUG
    static ComPtr<ID3D11InfoQueue> s_infoQueue;
#endif
};

ComPtr<ID3D11Device> VideoBuffer_D3D11_GL::s_d3ddev;
ComPtr<ID3D11DeviceContext> VideoBuffer_D3D11_GL::s_d3dctx;
ComPtr<ID3DBlob> VideoBuffer_D3D11_GL::s_compiledBlob;
ComPtr<ID3DBlob> VideoBuffer_D3D11_GL::s_errorBlob;
ComPtr<ID3D11ComputeShader> VideoBuffer_D3D11_GL::s_computeShader;
HANDLE VideoBuffer_D3D11_GL::s_dxdev = 0;
#ifdef DEBUG
ComPtr<ID3D11InfoQueue> VideoBuffer_D3D11_GL::s_infoQueue;
#endif

AVPixelFormat QAVHWDevice_D3D11_GL::format() const
{
    return AV_PIX_FMT_D3D11;
}

AVHWDeviceType QAVHWDevice_D3D11_GL::type() const
{
    return AV_HWDEVICE_TYPE_D3D11VA;
}

QAVVideoBuffer *QAVHWDevice_D3D11_GL::videoBuffer(const QAVVideoFrame &frame) const
{
    return new VideoBuffer_D3D11_GL(frame);
}

QT_END_NAMESPACE
