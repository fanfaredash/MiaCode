#pragma once

#include <QtQuick/QQuickGraphicsDevice>

namespace miacode::preview {

// H2 single-device preview decode
// (docs/PREVIEW_VIDEO_IGPU_STUTTER_INVESTIGATION_AND_FIX_ZH.md §4 Tier 3).
//
// Lazily creates ONE video-capable, multithread-protected ID3D11Device, publishes
// it to the FFmpeg D3D11VA decoder (via QtAVPlayer's qavSetSharedRenderD3D11Device)
// AND returns it wrapped as a QQuickGraphicsDevice so the preview QQuickView's QRhi
// renders on the SAME device. With both sides on one device, qavhwdevice_d3d11.cpp
// takes its single-device fast path and the per-frame cross-device keyed-mutex
// bridge (and its render-thread AcquireSync(INFINITE) freeze) disappears.
//
// Returns a NULL QQuickGraphicsDevice when the feature is disabled
// (MIACODE_PREVIEW_SINGLE_D3D11_DEVICE unset), when not built with the QtAVPlayer
// backend, or when device creation fails — the caller then simply does NOT call
// QQuickWindow::setGraphicsDevice(), letting Qt create its own device (the legacy
// two-device bridge). Idempotent: the device is created at most once and reused.
//
// Must be called on the GUI thread before the preview QQuickView initialises its
// scene graph (i.e. before it is shown/exposed).
QQuickGraphicsDevice sharedPreviewQuickGraphicsDevice();

// True once sharedPreviewQuickGraphicsDevice() has created and published the shared
// device (for diagnostics/logging). False when disabled or creation failed.
bool sharedPreviewD3D11DeviceActive();

// Wire the preview HW-decode diagnostics into the QtAVPlayer decode path
// (docs/PREVIEW_HWDECODE_GREEN_GARBLE_SEEK_DEBUG_PLAN_ZH.md §9). Installs the logging
// sink (so the render/decode-thread copy path can emit structured lines into the async
// runtime log) and publishes the resolved MIACODE_PREVIEW_DUMP_HWFRAMES budget + dump
// directory into the decoder. Idempotent and cheap; call once on the GUI thread before
// any media loads (works on BOTH the H2 single-device and legacy two-device paths). On
// non-Windows / non-QtAVPlayer builds it is a no-op.
void installPreviewDecodeDiagnostics();

// Drain any pending D3D11 debug-layer (ID3D11InfoQueue) messages from the H2 shared
// device into the runtime log (scope preview/hwframe_d3d11dbg). Only does work when
// the shared device was created with the debug layer
// (MIACODE_PREVIEW_D3D11_DEBUG_LAYER + --debug); otherwise a no-op. Call on a
// low-frequency cadence (e.g. the GUI-thread pause/seek summary), never per frame.
void drainSharedPreviewD3D11DebugMessages();

}  // namespace miacode::preview
