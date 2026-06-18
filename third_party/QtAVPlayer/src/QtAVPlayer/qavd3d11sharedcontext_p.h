/***************************************************************
 * MiaCode addition (not upstream QtAVPlayer).
 *
 * H2 single-device D3D11 preview decode. See
 * docs/PREVIEW_VIDEO_IGPU_STUTTER_INVESTIGATION_AND_FIX_ZH.md.
 ***************************************************************/

#ifndef QAVD3D11SHAREDCONTEXT_P_H
#define QAVD3D11SHAREDCONTEXT_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qtavplayerglobal.h"

QT_BEGIN_NAMESPACE

// MiaCode single-device (H2) support.
//
// The renderer publishes the ID3D11Device that QRhi draws on; the FFmpeg
// D3D11VA decoder then decodes onto that SAME device, so qavhwdevice_d3d11.cpp
// can skip the per-frame cross-device keyed-mutex bridge (and its render-thread
// AcquireSync(INFINITE) freeze) and do a single same-device copy instead.
//
// `device` is a borrowed ID3D11Device* (passed as void* to keep this header free
// of <d3d11.h>): the renderer owns it and MUST keep it alive for as long as any
// QAVPlayer may decode. Pass nullptr to clear it and revert the decoder to the
// legacy two-device bridge. Both functions are thread-safe.
//
// The device must have been created with D3D11_CREATE_DEVICE_VIDEO_SUPPORT and
// must have multithread protection enabled (ID3D10Multithread); otherwise the
// shared decode-context init fails and the decoder transparently falls back to
// its own device.
Q_AVPLAYER_EXPORT void qavSetSharedRenderD3D11Device(void *device);
Q_AVPLAYER_EXPORT void *qavSharedRenderD3D11Device();

// ---------------------------------------------------------------------------
// MiaCode preview HW-decode diagnostics (green / garble / seek debug plan,
// docs/PREVIEW_HWDECODE_GREEN_GARBLE_SEEK_DEBUG_PLAN_ZH.md).
//
// The decode + texture-copy path lives in the QtAVPlayer translation units
// (qavhwdevice_d3d11.cpp on the render thread, qavplayer.cpp skipFrame on the
// decode thread). MiaCode publishes a logging callback so those sparse,
// bounded events land in MiaCode's async runtime log; the per-frame counters
// are cheap relaxed atomics drained by the GUI thread on a low-frequency
// cadence (never per frame on a hot thread). All MIACODE_* env reads stay in
// src/common/DebugOptions.h; only resolved ints/pointers cross over here.
// ---------------------------------------------------------------------------

// Logging sink: payload is a "key=val …" fragment; the sink prepends scope +
// timestamp and enqueues onto the async writer. nullptr disables logging.
// Set once on the GUI thread before any media loads.
using QAVPreviewDiagLogFn = void (*)(const char *payload);
Q_AVPLAYER_EXPORT void qavSetPreviewDiagLogSink(QAVPreviewDiagLogFn fn);

// Bounded NV12 readback config. budgetPerArm = number of decoder surfaces to read
// back + stat-classify per arm (0 = OFF). Emits one preview/hwframe stat line per
// readback; no image files are written.
Q_AVPLAYER_EXPORT void qavSetPreviewHwFrameDumpConfig(int budgetPerArm);

// Re-arm the per-arm readback budget (call on each seek so seek-time corruption
// is captured too). Also resets the "ms since last seek" epoch used by the dump
// line's since_seek_ms field. Cheap; safe from the GUI thread.
Q_AVPLAYER_EXPORT void qavArmPreviewHwFrameDump();

// Publish the resolved HW-decode FIX config (green / garble debug plan §10).
//   completionWait != 0 => force decode GPU completion (ID3D11Query EVENT + Flush +
//                          bounded spin) before copying the DPB slot. PRIMARY FIX:
//                          the decoder's DecoderEndFrame is not implicitly ordered
//                          against our copy on some Intel/Arc drivers, so a post-seek
//                          copy reads a half-decoded (zero-chroma => green) slot.
//   dropCorrupt   != 0 => skip frames FFmpeg flagged corrupt / decode-error instead
//                          of sampling them (safety net; RHI keeps the last good frame).
// Literals stay in src/common/DebugOptions.h; only resolved ints cross over here. Set
// once on the GUI thread before any media loads (mirrors qavSetPreviewHwFrameDumpConfig).
Q_AVPLAYER_EXPORT void qavSetPreviewHwDecodeFixConfig(int completionWait, int dropCorrupt);

// Process-wide cumulative counters for the GUI-thread summary line (form A).
struct QAVPreviewDiagCounters
{
    unsigned long long copiedFramesSingle;   // copyTextureSameDevice successes (H2 path)
    unsigned long long copiedFramesTwoDevice;// copyTexture successes (legacy bridge)
    unsigned long long texturesCreated;      // CreateTexture2D calls in the copy paths
    unsigned long long acquireSyncTimeouts;  // S1 keyed-mutex AcquireSync !=S_OK (two-device)
    unsigned long long copyFailures;         // copy/CreateTexture2D failures that dropped a frame
    unsigned long long resolutionChanges;    // coded WxH changed between frames
    unsigned long long hwFramesDumped;        // successful readback dumps so far
    unsigned long long completionWaits;       // §10 decode-completion waits performed
    unsigned long long completionWaitTimeouts;// waits that hit the bounded cap (stuck GPU)
    unsigned long long framesDecodeError;     // frames FFmpeg flagged corrupt / decode_error
    unsigned long long corruptFramesDropped;  // frames dropped by the drop-corrupt safety net
    int lastCodedWidth;
    int lastCodedHeight;
    int lastDisplayWidth;
    int lastDisplayHeight;
    unsigned int lastDxgiFormat;             // DXGI_FORMAT of the decoder surface
    int lastPath;                            // 1=single-device, 0=two-device, -1=none yet
    const char* codecName;                   // avcodec_get_name(codec_id); null pre-decode
};
Q_AVPLAYER_EXPORT void qavGetPreviewDiagCounters(QAVPreviewDiagCounters *out);

// Catch-up (decode-but-don't-display) skip counter for the S-SEEK GOP-burst
// signature. Incremented on the decode thread in skipFrame; read-and-reset on
// the GUI thread at the first displayed frame after a seek.
Q_AVPLAYER_EXPORT void qavNotePreviewCatchupSkip();
Q_AVPLAYER_EXPORT unsigned long long qavTakePreviewCatchupSkipCount();

QT_END_NAMESPACE

#endif
