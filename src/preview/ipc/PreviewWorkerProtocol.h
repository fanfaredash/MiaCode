#pragma once

// Cross-process protocol shared between the editor (parent) process and the
// out-of-process preview worker. See
// docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md
// for the full architecture rationale.
//
// The worker is just MiaCode.exe relaunched with a CLI flag, so this header
// lives at the seam — editor includes it from MainWindow.PreviewWorkerSupervisor,
// worker includes it from main.cpp's runCliPreviewWorker entry. Keeping the
// schema in one place prevents the two sides from drifting silently.
//
// Two transport channels:
//
//   1. Control / lifecycle: line-delimited JSON over stdin (editor → worker)
//      and stdout (worker → editor). Mirrors the export-worker protocol so
//      the editor side can reuse QProcess::SeparateChannels + readyReadyStandardOutput
//      + the existing line-buffering. Low rate, never on a hot path.
//
//   2. Per-frame snapshot: shared-memory ring buffer keyed on a session id
//      passed in the "attach" command. Hot path; never goes through stdin.
//      Defined in PreviewSnapshotRingBuffer.{h,cpp} (Phase 1).
//
// Phase 0 only uses channel (1) — the static-popup PoC has no per-frame data.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace miacode::preview::ipc {

// CLI flag the worker process is launched with. The flag name is also the
// "process role" indicator — main.cpp dispatches to runCliPreviewWorker when
// it sees this on argv.
inline constexpr const char* kCliPreviewWorkerFlag = "preview-worker";

// Optional Phase 0 sub-mode. When set on the worker's argv after
// --preview-worker, the worker enters static-popup mode: creates the popup
// HWND, renders a static red rectangle, and idles waiting for shutdown. No
// snapshot ring buffer is mapped; no per-frame stream is consumed. Used by
// the Phase 0 verification test in docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md
// section 6, Phase 0.
inline constexpr const char* kCliPreviewWorkerStaticTestFlag = "preview-worker-static-test";

// Protocol version. Bumped whenever the JSON command schema or shared-memory
// layout changes incompatibly. Editor sends its expected version in "attach";
// worker rejects if it doesn't match (forces a redeploy in mismatched
// install + running-app scenarios).
inline constexpr int kPreviewWorkerProtocolVersion = 1;

// JSON command names (editor → worker, "cmd" field).
inline constexpr const char* kCmdAttach = "attach";
inline constexpr const char* kCmdResize = "resize";
inline constexpr const char* kCmdSetVisualTransform = "set_visual_transform";
inline constexpr const char* kCmdShutdown = "shutdown";

// JSON event names (worker → editor, "event" field).
inline constexpr const char* kEvtWorkerReady = "worker_ready";
inline constexpr const char* kEvtAck = "ack";
inline constexpr const char* kEvtAttached = "attached";
inline constexpr const char* kEvtPresented = "presented";
inline constexpr const char* kEvtDeviceRemoved = "device_removed";
inline constexpr const char* kEvtFatal = "fatal";

// Build the "attach" command payload. The editor sends this exactly once as
// the first JSON line on the worker's stdin after the worker emits
// "worker_ready". The worker creates its DComp surface owned by editor_hwnd
// and replies with "attached".
QByteArray buildAttachCommand(quint64 editorHwnd, quint32 parentPid,
                              const QString& sessionId, const QString& logDirectory,
                              const QString& snapshotRingShmKey,
                              int snapshotSlotByteSize, int snapshotSlotCount);

// Build "shutdown" command payload — graceful exit request.
QByteArray buildShutdownCommand();

// Build "set_visual_transform" — popup origin and display size in pixels of
// the editor HWND's client area. Worker forwards directly to
// PreviewDCompCore::setVisualTransform after MoveWindow on the popup.
QByteArray buildSetVisualTransformCommand(int xPx, int yPx, int displayWPx, int displayHPx);

// Build "resize" — swap chain pixel size. Forwarded to PreviewDCompCore::resize.
QByteArray buildResizeCommand(int wPx, int hPx);

// Helpers used on the worker side to emit JSON events back to the editor.
// Writes a single compact JSON line followed by '\n', flushed to stdout.
void writeWorkerEvent(const QJsonObject& event);

// Convenience wrappers — the worker emits these at well-known points.
void emitWorkerReadyEvent();
void emitAttachedEvent(const QString& sessionId, quint64 popupHwnd);
void emitDeviceRemovedEvent(const QString& reason, quint64 framesTotal);
void emitFatalEvent(const QString& tag, const QString& message);

}  // namespace miacode::preview::ipc
