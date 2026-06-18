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

class QApplication;
class QCommandLineParser;

namespace miacode::app::entry {

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
int runCliVideoExport(QApplication& app, QString* errorMessage);          // cli_video_export.cpp
int runCliVideoExportWorker(QApplication& app, QString* errorMessage);    // cli_video_export_worker.cpp

}  // namespace miacode::app::entry
