#pragma once

// Internal entry-point declarations for the MiaCode application bootstrap.
//
// main.cpp was split by subsystem into several translation units
// (startup_diagnostics_win32.cpp, graphics_backend.cpp, cli_shared.cpp,
// cli_video_export.cpp, cli_video_export_worker.cpp). Each TU defines its
// functions in `namespace miacode::app::entry`; this header declares only the
// functions that cross a TU boundary (i.e. are called from main.cpp or from a
// sibling TU). Functions used by a single TU stay file-local in that TU.

#include <QString>
#include <QStringList>

class QGuiApplication;
class QCommandLineParser;
class QQuickWindow;

namespace miacode::app::entry {

// ===== Startup diagnostics bundle (process_identity.cpp) =====
// Emits startup/process_identity (P0) + startup/gpu_hint (P2) + startup/gpu_policy
// (P3) in one call, deriving argv/role from QCoreApplication. Gated on --debug.
// `phase` tags the batch ("boot" early in main(), "log_dir_rebound" after the
// runtime log directory rebinds to a chart's .miacode/logs/) so these process-
// invariant facts are visible in whichever log file a user collects. Call after
// QGuiApplication is constructed.
void logProcessStartupDiagnostics(const QString& phase);

// ===== Actual GPU adapter / renderer probe (gpu_adapter_probe.cpp) =====
// Schedules a one-shot render-thread probe of the window's live RHI device and
// logs the bound DXGI adapter (D3D11) or GL_RENDERER string (OpenGL). Gated on
// --debug. Safe to call before the scene graph is initialized.
void logQuickWindowGpuDevice(QQuickWindow* window, const QString& surfaceLabel);

// ===== High-performance Quick graphics device provider (gpu_device_provider.cpp) =====
// P4 — before a Quick window/view initializes its scene graph, optionally bind
// it to the resolved high-performance DXGI adapter via
// QQuickGraphicsDevice::fromAdapter (Qt owns the device lifetime). Behaviour:
//  - preferVideoShareDevice=true (preview composite / video surfaces): try the
//    H2 single-device video-share device; else keep Qt's DEFAULT adapter. Never
//    fromAdapter — the D3D11VA two-device keyed-mutex bridge is same-adapter only.
//  - preferVideoShareDevice=false (root window): fromAdapter(high-perf LUID),
//    but ONLY when MIACODE_GPU_BIND_HIGH_PERFORMANCE=1 (opt-in until validated),
//    the RHI is D3D11, the policy yields a hardware LUID, and it differs from the
//    default adapter (else redundant). Any miss leaves Qt on its default adapter.
// Logs the decision (startup/gpu_provider). Returns true iff a device was bound.
// Must be called on the GUI thread BEFORE the window's scene graph initializes.
bool bindHighPerformanceQuickGraphicsDevice(
    QQuickWindow* window, const QString& surfaceLabel, bool preferVideoShareDevice);

// ===== Win32 startup diagnostics (startup_diagnostics_win32.cpp) =====
#ifdef Q_OS_WIN
// Sets the explicit AppUserModelID so taskbar grouping / pinning works.
void setWindowsAppUserModelId();
// Vectored exception handler installed via AddVectoredExceptionHandler.
// Signature matches LONG WINAPI(EXCEPTION_POINTERS*); declared with the
// underlying struct tag so this header needs no <windows.h>.
long __stdcall vectoredHandler(struct _EXCEPTION_POINTERS* info) noexcept;
// Heap-free Win32 startup probe suite (OS version, VC++ runtime DLLs, D3D11).
void runStartupDiagnostic() noexcept;
#endif

// ===== Graphics backend selector (graphics_backend.cpp) =====
struct GraphicsBackendChoice {
    QString name;            // canonical lowercase name, or empty for "auto"
    bool fromCommandLine;    // true if this came from --rhi= rather than the saved file
    bool clearedByCommand;   // true if the user explicitly asked to clear via --rhi=auto
};

QString persistedGraphicsBackendFilePath();
bool writePersistedGraphicsBackend(const QString& backend);
GraphicsBackendChoice resolveGraphicsBackendChoice(const QStringList& args);
QString applyGraphicsBackendChoice(const QString& backend);

// ===== Shared CLI helpers (cli_shared.cpp) =====
void addSharedCliDebugOption(QCommandLineParser& parser);
QString currentExceptionDetail();

// ===== CLI video export handlers =====
int runCliVideoExport(QGuiApplication& app, QString* errorMessage);          // cli_video_export.cpp
int runCliVideoExportWorker(QGuiApplication& app, QString* errorMessage);    // cli_video_export_worker.cpp

}  // namespace miacode::app::entry
