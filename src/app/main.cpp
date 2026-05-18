#include "AppVersion.h"
#include "quick_shell/QuickShellBootstrap.h"
#include "mainwindow/MainWindow.h"
#include "preview/ipc/PreviewFrameStateProjector.h"
#include "preview/ipc/PreviewFrameStateSerial.h"
#include "preview/ipc/PreviewSnapshotRingBuffer.h"
#include "preview/ipc/PreviewWorkerProtocol.h"
#include "preview/ipc/PreviewWorkerSupervisor.h"
#include "preview/runtime/PreviewWorkerSession.h"
#include "tools/video_export/VideoExportSnapshot.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/WaveformCache.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QStringList>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QDir>
#include <QRegularExpression>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>

#include <cmath>
#include <cstdio>
#include <exception>
#include <new>

#ifdef Q_OS_WIN
#include <windows.h>
#include <timeapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#pragma comment(lib, "winmm.lib")
#endif

namespace {

#ifdef Q_OS_WIN
void setWindowsAppUserModelId()
{
    static HMODULE shell32Module = ::LoadLibraryW(L"shell32.dll");
    if (shell32Module == nullptr) {
        return;
    }
    using SetAppIdFn = HRESULT(WINAPI*)(PCWSTR);
    static auto setAppId = reinterpret_cast<SetAppIdFn>(
        ::GetProcAddress(shell32Module, "SetCurrentProcessExplicitAppUserModelID")
    );
    if (setAppId != nullptr) {
        setAppId(L"fanfaredash.MiaCode");
    }
}

// ============================================================================
// Experimental beta42 startup diagnostic. Pure Win32, heap-free, runs right
// after writeStartupBeacon — discovers which of the three regression
// hypotheses (A=VC runtime, B=GPU driver / D3D11 device, C=Win10 version)
// fired BEFORE we touch QApplication. Every line lands in the same beacon
// file (already proven to land on disk), append-mode.
//
// Each probe is wrapped in __try/__except so a crash IN the probe is also
// diagnostic: the LAST line written tells us where the probe died.
// ============================================================================

void appendBeaconLineUtf8(const char* line) noexcept
{
    miacode::oplog::appendStartupBeaconLine(line);
}

void probeOsVersion() noexcept
{
    typedef LONG (WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    LONG status = -1;
    if (ntdll != nullptr) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(
            ::GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn != nullptr) {
            status = fn(&v);
        }
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "diag/os ntstatus=0x%lx major=%lu minor=%lu build=%lu platform=%lu",
        static_cast<unsigned long>(status),
        v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, v.dwPlatformId);
    appendBeaconLineUtf8(buf);
}

void probeLoadedModule(const wchar_t* name) noexcept
{
    char nameUtf8[64];
    ::WideCharToMultiByte(CP_UTF8, 0, name, -1, nameUtf8, sizeof(nameUtf8),
                          nullptr, nullptr);

    HMODULE h = ::GetModuleHandleW(name);
    if (h != nullptr) {
        wchar_t path[MAX_PATH] = {};
        DWORD len = ::GetModuleFileNameW(h, path, MAX_PATH);
        char pathUtf8[512] = {};
        if (len > 0 && len < MAX_PATH) {
            ::WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8,
                                  sizeof(pathUtf8), nullptr, nullptr);
        }
        char buf[768];
        std::snprintf(buf, sizeof(buf),
            "diag/dll name=%s loaded=1 path=%s", nameUtf8, pathUtf8);
        appendBeaconLineUtf8(buf);
        return;
    }

    // Not loaded — try to load it explicitly to distinguish "not needed
    // yet" from "missing entirely". Use system + app dirs (no PATH search)
    // so we get a real answer about whether the DLL exists where Windows
    // would look for it at implicit-import time.
    HMODULE probed = ::LoadLibraryExW(
        name, nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    const DWORD err = ::GetLastError();
    if (probed != nullptr) {
        wchar_t path[MAX_PATH] = {};
        DWORD len = ::GetModuleFileNameW(probed, path, MAX_PATH);
        char pathUtf8[512] = {};
        if (len > 0 && len < MAX_PATH) {
            ::WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8,
                                  sizeof(pathUtf8), nullptr, nullptr);
        }
        char buf[768];
        std::snprintf(buf, sizeof(buf),
            "diag/dll name=%s loaded=0 probe_loaded=1 path=%s",
            nameUtf8, pathUtf8);
        appendBeaconLineUtf8(buf);
        ::FreeLibrary(probed);
    } else {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "diag/dll name=%s loaded=0 probe_loaded=0 err=%lu",
            nameUtf8, static_cast<unsigned long>(err));
        appendBeaconLineUtf8(buf);
    }
}

// Enumerate every module loaded into our process and write each one's name
// + address range to the beacon. Lets us identify which DLL the
// 0x00007FFE64172EB0 fault address belongs to — answer is whichever module
// has [base, base+size) covering that address. Also exposes any
// third-party injected DLL (AV / overlay / vendor UMD) that we didn't ask
// for and didn't load explicitly.
//
// Uses K32EnumProcessModules (Win10+; replacement for psapi.dll's older
// EnumProcessModules) via dynamic resolution so we don't add a new
// implicit import that might itself fail to load on stripped Win10 builds.
void probeLoadedModuleList() noexcept
{
    HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        appendBeaconLineUtf8("diag/modlist err=kernel32_handle_null");
        return;
    }
    typedef BOOL (WINAPI* K32EnumProcessModulesFn)(
        HANDLE, HMODULE*, DWORD, LPDWORD);
    typedef DWORD (WINAPI* K32GetModuleFileNameExWFn)(
        HANDLE, HMODULE, LPWSTR, DWORD);
    typedef BOOL (WINAPI* K32GetModuleInformationFn)(
        HANDLE, HMODULE, LPMODULEINFO, DWORD);
    auto enumFn = reinterpret_cast<K32EnumProcessModulesFn>(
        ::GetProcAddress(kernel32, "K32EnumProcessModules"));
    auto nameFn = reinterpret_cast<K32GetModuleFileNameExWFn>(
        ::GetProcAddress(kernel32, "K32GetModuleFileNameExW"));
    auto infoFn = reinterpret_cast<K32GetModuleInformationFn>(
        ::GetProcAddress(kernel32, "K32GetModuleInformation"));
    if (enumFn == nullptr || nameFn == nullptr || infoFn == nullptr) {
        appendBeaconLineUtf8("diag/modlist err=psapi_symbols_missing");
        return;
    }
    HMODULE modules[512];
    DWORD bytesNeeded = 0;
    HANDLE self = ::GetCurrentProcess();
    if (!enumFn(self, modules, sizeof(modules), &bytesNeeded)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "diag/modlist err=enum_failed gle=%lu",
            static_cast<unsigned long>(::GetLastError()));
        appendBeaconLineUtf8(buf);
        return;
    }
    const DWORD count = bytesNeeded / sizeof(HMODULE);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "diag/modlist count=%lu",
        static_cast<unsigned long>(count));
    appendBeaconLineUtf8(buf);
    for (DWORD i = 0; i < count && i < 512; ++i) {
        wchar_t pathW[MAX_PATH] = {};
        const DWORD len = nameFn(self, modules[i], pathW, MAX_PATH);
        MODULEINFO mi{};
        infoFn(self, modules[i], &mi, sizeof(mi));
        char nameUtf8[512] = {};
        if (len > 0) {
            ::WideCharToMultiByte(CP_UTF8, 0, pathW, -1,
                                  nameUtf8, sizeof(nameUtf8), nullptr, nullptr);
        }
        char line[768];
        const uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        const uintptr_t end = base + mi.SizeOfImage;
        std::snprintf(line, sizeof(line),
            "diag/mod base=0x%016llx end=0x%016llx size=0x%lx name=%s",
            static_cast<unsigned long long>(base),
            static_cast<unsigned long long>(end),
            static_cast<unsigned long>(mi.SizeOfImage),
            nameUtf8);
        appendBeaconLineUtf8(line);
    }
}

void probeVcRuntimeAndGfx() noexcept
{
    // Names probed in load order. vcruntime140_1 is the VS 2019 16.5+
    // addition that C++20 ABI features depend on — a fresh Win10 install
    // without latest VC redist is missing this.
    const wchar_t* names[] = {
        L"vcruntime140.dll",
        L"vcruntime140_1.dll",   // VS 2019 16.5+; required by C++20 SEH personality
        L"msvcp140.dll",
        L"msvcp140_1.dll",
        L"msvcp140_2.dll",       // VS 2019 16.0+; <cmath> C99 functions
        L"ucrtbase.dll",
        L"d3d11.dll",
        L"dxgi.dll",
        L"dcomp.dll",
        L"D3DCompiler_47.dll",
        L"avrt.dll",
    };
    for (const wchar_t* n : names) {
        probeLoadedModule(n);
    }
}

void probeD3D11Device() noexcept
{
    // Probe D3D11CreateDevice on HARDWARE → WARP → reference, recording
    // which level worked and the adapter description. If hardware works
    // we know B (GPU driver missing) is NOT the cause. If only WARP
    // works, B is likely the cause — Qt RHI / DComp on WARP is the
    // confirmed weak spot.

    typedef HRESULT (WINAPI* D3D11CreateDeviceFn)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

    HMODULE d3d11 = ::LoadLibraryExW(L"d3d11.dll", nullptr,
                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (d3d11 == nullptr) {
        appendBeaconLineUtf8("diag/d3d11 status=d3d11_dll_missing");
        return;
    }
    auto createDevice = reinterpret_cast<D3D11CreateDeviceFn>(
        ::GetProcAddress(d3d11, "D3D11CreateDevice"));
    if (createDevice == nullptr) {
        appendBeaconLineUtf8("diag/d3d11 status=createdevice_symbol_missing");
        return;
    }

    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    const D3D_DRIVER_TYPE kinds[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
    };
    const char* kindNames[] = { "hardware", "warp" };

    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL actualLevel = D3D_FEATURE_LEVEL_9_1;
        HRESULT hr = E_FAIL;

        // SEH guard: a faulty driver UMD CAN crash inside D3D11CreateDevice.
        // We want the crash itself to be diagnostic — beacon line above
        // already says we got this far; if we don't get the line below,
        // the probe itself killed the process and that IS the answer.
        __try {
            hr = createDevice(nullptr, kinds[i], nullptr, 0,
                              requested, sizeof(requested) / sizeof(requested[0]),
                              D3D11_SDK_VERSION,
                              &device, &actualLevel, &ctx);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "diag/d3d11 driver=%s seh_in_createdevice=1",
                kindNames[i]);
            appendBeaconLineUtf8(buf);
            continue;
        }

        if (SUCCEEDED(hr) && device != nullptr) {
            // Query adapter description for traceability.
            IDXGIDevice* dxgiDevice = nullptr;
            IDXGIAdapter* adapter = nullptr;
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),
                                                 reinterpret_cast<void**>(&dxgiDevice)))
                && dxgiDevice != nullptr) {
                if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr) {
                    adapter->GetDesc(&desc);
                    adapter->Release();
                }
                dxgiDevice->Release();
            }
            char descUtf8[256] = {};
            ::WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                  descUtf8, sizeof(descUtf8), nullptr, nullptr);

            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "diag/d3d11 driver=%s status=ok feature_level=0x%x "
                "vendor_id=0x%04x device_id=0x%04x adapter=\"%s\"",
                kindNames[i], static_cast<unsigned>(actualLevel),
                static_cast<unsigned>(desc.VendorId),
                static_cast<unsigned>(desc.DeviceId),
                descUtf8);
            appendBeaconLineUtf8(buf);

            if (ctx != nullptr) ctx->Release();
            device->Release();
            // First success wins — don't burn cycles trying lower-quality
            // drivers if hardware already worked.
            return;
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "diag/d3d11 driver=%s status=fail hr=0x%08lx",
            kindNames[i], static_cast<unsigned long>(hr));
        appendBeaconLineUtf8(buf);
    }

    appendBeaconLineUtf8("diag/d3d11 final=no_driver_worked");
}

// Vectored exception handler: runs BEFORE any SEH filter and CANNOT be
// displaced by `SetUnhandledExceptionFilter` calls from Qt / vendor UMD.
// Flushes a beacon line on first-chance for "fatal" exception codes plus
// the op-chain shadow, then chains to the rest of SEH dispatch normally.
//
// Why first-chance not last-chance: by the time AddVectoredContinueHandler
// would fire, the process is already past the SEH search phase and the
// shadow log may not land. First-chance fires for every exception (even
// caught C++ exceptions which use SEH internally), so we filter to only
// the codes that always mean process termination.
LONG WINAPI vectoredHandler(EXCEPTION_POINTERS* info) noexcept
{
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    // Filter to codes that indicate hard process termination — skip
    // C++ exception code 0xE06D7363 (caught by handlers, not fatal) and
    // similar runtime-internal codes.
    const bool isFatal =
        code == 0xC0000005   /* ACCESS_VIOLATION */
        || code == 0xC0000409 /* STACK_BUFFER_OVERRUN / __fastfail */
        || code == 0xC000041D /* UNHANDLED C++ EXCEPTION (CRT) */
        || code == 0xC0000094 /* INTEGER_DIVIDE_BY_ZERO */
        || code == 0x80000003 /* BREAKPOINT (debugger) */
        || code == 0xC00000FD /* STACK_OVERFLOW */
        || code == 0xC000001D /* ILLEGAL_INSTRUCTION */;
    if (!isFatal) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "diag/veh first_chance code=0x%08lx addr=%p",
        static_cast<unsigned long>(code),
        info->ExceptionRecord->ExceptionAddress);
    miacode::oplog::appendStartupBeaconLine(buf);
    // Also flush the op-chain shadow so we have a chain at the moment of
    // the fault, even if the regular SEH filter never fires.
    miacode::oplog::flushShadowToDisk();
    return EXCEPTION_CONTINUE_SEARCH;
}

void runStartupDiagnostic() noexcept
{
    // Marker order matters: each line lands on disk before the next is
    // attempted, so a crash IN any probe leaves the prior lines behind.
    appendBeaconLineUtf8("phase=diag_begin");
    __try { probeOsVersion(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        appendBeaconLineUtf8("diag/os seh_in_probe=1");
    }
    appendBeaconLineUtf8("phase=diag_modules");
    __try { probeVcRuntimeAndGfx(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        appendBeaconLineUtf8("diag/modules seh_in_probe=1");
    }
    appendBeaconLineUtf8("phase=diag_modlist");
    __try { probeLoadedModuleList(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        appendBeaconLineUtf8("diag/modlist seh_in_probe=1");
    }
    // D3D11 probe creates a full hardware device, which loads AMD/NVIDIA/Intel
    // user-mode driver DLLs into the process. Once loaded these UMD DLLs hook
    // Win32 APIs and never unload — on some AMD APU + Win10 22H2 combinations
    // this hook-set interferes with subsequent std::mutex / SRWLock operations
    // and triggers a fast-fail. Allow opting out via env var so support can
    // run the rest of the diagnostic without provoking that path.
    const DWORD skipD3D11 =
        ::GetEnvironmentVariableW(L"MIACODE_SKIP_DIAG_D3D11", nullptr, 0);
    if (skipD3D11 > 0) {
        appendBeaconLineUtf8("phase=diag_d3d11_skipped_via_env");
    } else {
        appendBeaconLineUtf8("phase=diag_d3d11");
        __try { probeD3D11Device(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            appendBeaconLineUtf8("diag/d3d11 seh_in_probe=1");
        }
    }
    appendBeaconLineUtf8("phase=diag_end");
}
#endif

bool wantsCliVideoExport(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--export-video"));
}

bool wantsCliVideoExportWorker(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--export-video-worker"));
}

// Out-of-process preview worker entry. Four flavours:
//   --preview-worker                       : production mode (Phase 1+, snapshot stream)
//   --preview-worker-static-test           : Phase 0 PoC (static red rectangle)
//   --preview-worker-ring-self-test        : Phase 1 self-test (in-process IPC round-trip)
//   --preview-worker-crash-recovery-test   : Phase 5 stress harness (injects worker crashes
//                                            via __fastfail and measures supervisor
//                                            respawn-and-attach time across N cycles)
// All four go through the same runCliPreviewWorker() with a sub-mode selector so
// the dispatch path in main() is identical to the export-worker pattern.
// See docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md.
bool wantsCliPreviewWorker(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--preview-worker"))
        || arguments.contains(QStringLiteral("--preview-worker-static-test"))
        || arguments.contains(QStringLiteral("--preview-worker-ring-self-test"))
        || arguments.contains(QStringLiteral("--preview-worker-crash-recovery-test"));
}

bool wantsCliPreviewWorkerStaticTest(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--preview-worker-static-test"));
}

bool wantsCliPreviewWorkerRingSelfTest(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--preview-worker-ring-self-test"));
}

bool wantsCliPreviewWorkerCrashRecoveryTest(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--preview-worker-crash-recovery-test"));
}

bool wantsQuickShellBeta(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--quick-shell-beta"));
}

QString startupOpenTargetFromArguments(const QStringList& arguments)
{
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index).trimmed();
        if (argument.isEmpty()) {
            continue;
        }
        if (argument == QStringLiteral("--")) {
            if (index + 1 < arguments.size()) {
                return arguments.at(index + 1);
            }
            return QString();
        }
        if (argument == QStringLiteral("--rhi")) {
            ++index;
            continue;
        }
        if (argument.startsWith('-')) {
            continue;
        }
        return arguments.at(index);
    }
    return QString();
}

// ===== Graphics backend selector =====
//
// User-driven backend selection without driver probing (which would touch GPU vendor DLLs
// and trip Windows Defender heuristics). Strategy:
//   1. CLI flag `--rhi=<name>` overrides everything for this run AND persists the choice
//      so the same backend is used on the next launch.
//   2. With no flag, the persisted choice from a small JSON file beside the executable is
//      applied. If neither exists, we leave Qt on its platform default (D3D11 on Windows).
//   3. The persistence file is plain JSON, written only on explicit user choice — the
//      app never tries backends behind the user's back. If a chosen backend fails to
//      initialise the user can recover by relaunching with `--rhi=auto` (clears the file)
//      or `--rhi=d3d11` (forces the safe Windows default).
//
// Recognised values: "auto" / "default" (no override), "d3d11", "d3d12", "opengl",
// "vulkan", "metal", "software". Anything else is rejected and we fall through to auto.

struct GraphicsBackendChoice {
    QString name;            // canonical lowercase name, or empty for "auto"
    bool fromCommandLine;    // true if this came from --rhi= rather than the saved file
    bool clearedByCommand;   // true if the user explicitly asked to clear via --rhi=auto
};

QString persistedGraphicsBackendFilePath()
{
    // Beside the executable so the file is portable with the install. Hidden (leading dot)
    // to keep the directory uncluttered.
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(QStringLiteral(".miacode_graphics.json"));
}

QString readPersistedGraphicsBackend()
{
    const QString path = persistedGraphicsBackendFilePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }
    return doc.object().value(QStringLiteral("backend")).toString().trimmed().toLower();
}

bool writePersistedGraphicsBackend(const QString& backend)
{
    const QString path = persistedGraphicsBackendFilePath();
    if (backend.isEmpty()) {
        // "auto" / clear: just remove the file.
        QFile::remove(path);
        return true;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("backend"), backend);
    obj.insert(QStringLiteral("schema"), QStringLiteral("miacode_graphics_v1"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    const qint64 written = file.write(bytes);
    file.close();
    return written == bytes.size();
}

QString canonicalRhiName(const QString& raw)
{
    const QString name = raw.trimmed().toLower();
    if (name == QStringLiteral("auto") || name == QStringLiteral("default")
        || name == QStringLiteral("platform") || name == QStringLiteral("none")) {
        return QString();
    }
    if (name == QStringLiteral("d3d11") || name == QStringLiteral("direct3d11")
        || name == QStringLiteral("dx11")) {
        return QStringLiteral("d3d11");
    }
    if (name == QStringLiteral("d3d12") || name == QStringLiteral("direct3d12")
        || name == QStringLiteral("dx12")) {
        return QStringLiteral("d3d12");
    }
    if (name == QStringLiteral("opengl") || name == QStringLiteral("gl")) {
        return QStringLiteral("opengl");
    }
    if (name == QStringLiteral("vulkan") || name == QStringLiteral("vk")) {
        return QStringLiteral("vulkan");
    }
    if (name == QStringLiteral("metal")) {
        return QStringLiteral("metal");
    }
    if (name == QStringLiteral("software") || name == QStringLiteral("sw")
        || name == QStringLiteral("null")) {
        return QStringLiteral("software");
    }
    return QString();  // unknown — caller treats as "no override".
}

// Pulls "--rhi=<value>" or "--rhi <value>" out of the raw argv. We do NOT use
// QCommandLineParser here because it requires a constructed QApplication, and we want to
// pick the backend before that.
QString parseRhiCommandLineArg(const QStringList& args)
{
    static const QString kFlag = QStringLiteral("--rhi");
    static const QString kFlagEq = QStringLiteral("--rhi=");
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a.startsWith(kFlagEq)) {
            return a.mid(kFlagEq.size());
        }
        if (a == kFlag && (i + 1) < args.size()) {
            return args.at(i + 1);
        }
    }
    return QString();
}

GraphicsBackendChoice resolveGraphicsBackendChoice(const QStringList& args)
{
    GraphicsBackendChoice choice{};
    const QString cliRaw = parseRhiCommandLineArg(args);
    if (!cliRaw.isEmpty()) {
        choice.fromCommandLine = true;
        const QString canonical = canonicalRhiName(cliRaw);
        choice.name = canonical;
        choice.clearedByCommand = canonical.isEmpty()
            && (cliRaw.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
                || cliRaw.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0
                || cliRaw.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0);
        return choice;
    }
    choice.name = readPersistedGraphicsBackend();
    return choice;
}

// Apply the chosen backend to QQuickWindow. Must be called AFTER QApplication construction
// (because setGraphicsApi consults Qt's per-process state) but BEFORE any QQuickWindow
// is realised. Returns the canonical name actually applied (empty for "auto/default").
QString applyGraphicsBackendChoice(const QString& backend)
{
    if (backend == QStringLiteral("d3d11")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    } else if (backend == QStringLiteral("d3d12")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
    } else if (backend == QStringLiteral("opengl")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    } else if (backend == QStringLiteral("vulkan")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    } else if (backend == QStringLiteral("metal")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    } else if (backend == QStringLiteral("software")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    } else {
        return QString();
    }
    return backend;
}

void addSharedCliDebugOption(QCommandLineParser& parser)
{
    // main() already enables debug mode before CLI dispatch. We still declare
    // this option here so subcommand parsers accept forwarded "--debug".
    parser.addOption(QCommandLineOption(
        QStringLiteral("debug"),
        QStringLiteral("Enable debug mode and debug-only log output.")
    ));
}

QString currentExceptionDetail()
{
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return QStringLiteral("std::bad_alloc");
    } catch (const std::exception& ex) {
        const QString what = QString::fromUtf8(ex.what()).trimmed();
        return what.isEmpty() ? QStringLiteral("std::exception") : what;
    } catch (...) {
        return QStringLiteral("unknown non-std exception");
    }
}

QString videoExportWorkerProjectLogDirectory(const VideoExportSnapshot& snapshot)
{
    const QString chartPath = snapshot.originalChartPath.trimmed();
    if (!chartPath.isEmpty()) {
        const QString projectDataDirectoryPath = miacode::waveform::projectDataDirectoryPathForFile(chartPath);
        if (!projectDataDirectoryPath.isEmpty()) {
            return QDir(projectDataDirectoryPath).filePath(QStringLiteral("logs"));
        }
    }

    const QString projectDir = snapshot.projectDir.trimmed();
    if (!projectDir.isEmpty()) {
        return QDir(QDir::cleanPath(projectDir)).filePath(QStringLiteral(".miacode/logs"));
    }

    return QString();
}

void writeWorkerJsonLine(const QJsonObject& object)
{
    QTextStream out(stdout);
    out << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
    out.flush();
}

bool parseCliResolutionToken(const QString& token, int* outputWidth, int* outputHeight)
{
    if (outputWidth == nullptr || outputHeight == nullptr) {
        return false;
    }
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    static const QRegularExpression re(
        QStringLiteral("^\\s*(\\d+)\\s*(?:[xX]\\s*(\\d+)\\s*)?$")
    );
    const QRegularExpressionMatch match = re.match(trimmed);
    if (!match.hasMatch()) {
        return false;
    }
    bool widthOk = false;
    const int parsedWidth = match.captured(1).toInt(&widthOk);
    if (!widthOk || parsedWidth <= 0) {
        return false;
    }
    const QString heightText = match.captured(2).trimmed();
    if (heightText.isEmpty()) {
        *outputWidth = parsedWidth;
        *outputHeight = parsedWidth;
        return true;
    }
    bool heightOk = false;
    const int parsedHeight = heightText.toInt(&heightOk);
    if (!heightOk || parsedHeight <= 0) {
        return false;
    }
    *outputWidth = parsedWidth;
    *outputHeight = parsedHeight;
    return true;
}

void appendAppShutdownRuntimeLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown"),
        text
    );
}

QString summarizeTopLevelWidgets()
{
    const auto widgets = QApplication::topLevelWidgets();
    QStringList items;
    items.reserve(widgets.size());
    for (QWidget* widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        items.append(
            QStringLiteral("%1(vis=%2 hidden=%3 active=%4 title=%5)")
                .arg(widget->metaObject() != nullptr ? widget->metaObject()->className() : QStringLiteral("unknown"))
                .arg(widget->isVisible() ? 1 : 0)
                .arg(widget->isHidden() ? 1 : 0)
                .arg(widget->isActiveWindow() ? 1 : 0)
                .arg(widget->windowTitle().trimmed().isEmpty() ? QStringLiteral("(empty)") : widget->windowTitle().trimmed())
        );
    }
    return items.join(QStringLiteral("; "));
}

QString summarizeTopLevelWindows()
{
    const auto windows = QGuiApplication::topLevelWindows();
    QStringList items;
    items.reserve(windows.size());
    for (QWindow* window : windows) {
        if (window == nullptr) {
            continue;
        }
        items.append(
            QStringLiteral("%1(vis=%2 title=%3)")
                .arg(window->metaObject() != nullptr ? window->metaObject()->className() : QStringLiteral("unknown"))
                .arg(window->isVisible() ? 1 : 0)
                .arg(window->title().trimmed().isEmpty() ? QStringLiteral("(empty)") : window->title().trimmed())
        );
    }
    return items.join(QStringLiteral("; "));
}

int runCliVideoExport(QApplication& app, QString* errorMessage)
{
    MC_OP("runCliVideoExport");
    try {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MiaCode CLI video export"));
    parser.addHelpOption();
    parser.addVersionOption();
    addSharedCliDebugOption(parser);
    parser.addOption(QCommandLineOption(
        QStringLiteral("export-video"),
        QStringLiteral("Run single-pass video export and exit.")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("chart"), QStringLiteral("chart-path")},
        QStringLiteral("Chart file path or chart directory path."),
        QStringLiteral("path")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("d"), QStringLiteral("difficulty")},
        QStringLiteral("Difficulty short name or id (ESY/BAS/ADV/EXP/MAS/REM/UTG or 1..7)."),
        QStringLiteral("difficulty"),
        QStringLiteral("MAS")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("r"), QStringLiteral("resolution")},
        QStringLiteral("Output resolution. Accepts N (square) or WxH (e.g. 1280x720)."),
        QStringLiteral("size"),
        QStringLiteral("1024")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("f"), QStringLiteral("fps")},
        QStringLiteral("Output frame rate."),
        QStringLiteral("fps"),
        QStringLiteral("60")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Output .mp4 file path or output directory."),
        QStringLiteral("path")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("start"),
        QStringLiteral("Export start second."),
        QStringLiteral("seconds"),
        QStringLiteral("0")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("duration"),
        QStringLiteral("Export content duration in seconds. Omit to export until timeline end."),
        QStringLiteral("seconds")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("hide-timestamp"),
        QStringLiteral("Hide timestamp overlay in output video.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("show-object-stats"),
        QStringLiteral("Show object stats HUD in output video.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("smooth-brightness"),
        QStringLiteral("Enable smooth brightness in output video.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("brightness-outer"),
        QStringLiteral("Outer background brightness (0.0-1.0)."),
        QStringLiteral("value"),
        QString::number(miacode::preview_video::kBackgroundBrightnessDefault, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("brightness-inner"),
        QStringLiteral("Inner background brightness (0.0-1.0)."),
        QStringLiteral("value"),
        QString::number(miacode::preview_video::kBackgroundBrightnessInnerDefault, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("layout-square-scale"),
        QStringLiteral("Judge line size scale."),
        QStringLiteral("value"),
        QString::number(miacode::preview_video::kLayoutSquareScaleDefault, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("background-scale"),
        QStringLiteral("Background scale mode: fill or fit."),
        QStringLiteral("mode"),
        QStringLiteral("fill")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("flow-speed"),
        QStringLiteral("Note flow speed."),
        QStringLiteral("value"),
        QString::number(miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("skin-wait-ms"),
        QStringLiteral("Max wait milliseconds for async skin loading before export."),
        QStringLiteral("milliseconds"),
        QStringLiteral("2000")
    ));

    if (!parser.parse(app.arguments())) {
        if (errorMessage != nullptr) {
            *errorMessage = parser.errorText();
        }
        return 2;
    }
    if (!parser.isSet(QStringLiteral("export-video"))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("internal CLI dispatch error: --export-video not set");
        }
        return 2;
    }

    const QString chartInput = parser.value(QStringLiteral("chart")).trimmed();
    if (chartInput.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--chart is required in --export-video mode");
        }
        return 2;
    }

    int outputWidth = 0;
    int outputHeight = 0;
    const bool resolutionOk = parseCliResolutionToken(
        parser.value(QStringLiteral("resolution")),
        &outputWidth,
        &outputHeight
    );
    bool fpsOk = false;
    const int fps = parser.value(QStringLiteral("fps")).toInt(&fpsOk);
    bool startOk = false;
    const double startSeconds = parser.value(QStringLiteral("start")).toDouble(&startOk);
    bool outerBrightnessOk = false;
    const double outerBrightness = parser.value(QStringLiteral("brightness-outer")).toDouble(&outerBrightnessOk);
    bool innerBrightnessOk = false;
    const double innerBrightness = parser.value(QStringLiteral("brightness-inner")).toDouble(&innerBrightnessOk);
    bool layoutScaleOk = false;
    const double layoutSquareScale = parser.value(QStringLiteral("layout-square-scale")).toDouble(&layoutScaleOk);
    const QString backgroundScaleToken = parser.value(QStringLiteral("background-scale")).trimmed().toLower();
    bool flowSpeedOk = false;
    const double flowSpeed = parser.value(QStringLiteral("flow-speed")).toDouble(&flowSpeedOk);
    bool skinWaitOk = false;
    const int skinWaitMs = parser.value(QStringLiteral("skin-wait-ms")).toInt(&skinWaitOk);

    if (!resolutionOk || outputWidth <= 0 || outputHeight <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--resolution must be N or WxH (positive integers)");
        }
        return 2;
    }
    if (outputWidth < outputHeight) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--resolution currently requires width >= height");
        }
        return 2;
    }
    if (!fpsOk || fps <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--fps must be a positive integer");
        }
        return 2;
    }
    if (!startOk || !std::isfinite(startSeconds) || startSeconds < 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--start must be a non-negative number");
        }
        return 2;
    }
    if (!skinWaitOk || skinWaitMs < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--skin-wait-ms must be a non-negative integer");
        }
        return 2;
    }
    if (!outerBrightnessOk || !std::isfinite(outerBrightness) || outerBrightness < 0.0 || outerBrightness > 1.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--brightness-outer must be between 0.0 and 1.0");
        }
        return 2;
    }
    if (!innerBrightnessOk || !std::isfinite(innerBrightness) || innerBrightness < 0.0 || innerBrightness > 1.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--brightness-inner must be between 0.0 and 1.0");
        }
        return 2;
    }
    if (!layoutScaleOk || !std::isfinite(layoutSquareScale) || layoutSquareScale <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--layout-square-scale must be a positive number");
        }
        return 2;
    }
    if (backgroundScaleToken != QStringLiteral("fill") && backgroundScaleToken != QStringLiteral("fit")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--background-scale must be either fill or fit");
        }
        return 2;
    }
    if (!flowSpeedOk || !std::isfinite(flowSpeed) || flowSpeed <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--flow-speed must be a positive number");
        }
        return 2;
    }

    double durationSeconds = -1.0;
    if (parser.isSet(QStringLiteral("duration"))) {
        bool durationOk = false;
        durationSeconds = parser.value(QStringLiteral("duration")).toDouble(&durationOk);
        if (!durationOk || !std::isfinite(durationSeconds) || durationSeconds <= 0.0) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("--duration must be a positive number");
            }
            return 2;
        }
    }

    MainWindow::CliVideoExportRequest request;
    request.chartPathOrDirectory = chartInput;
    request.difficulty = parser.value(QStringLiteral("difficulty")).trimmed();
    request.outputPath = parser.value(QStringLiteral("output")).trimmed();
    request.outputWidth = outputWidth;
    request.outputHeight = outputHeight;
    request.fps = fps;
    request.exportStartSeconds = startSeconds;
    request.contentDurationSeconds = durationSeconds;
    request.showTimestamp = !parser.isSet(QStringLiteral("hide-timestamp"));
    request.showObjectStatsHud = parser.isSet(QStringLiteral("show-object-stats"));
    request.smoothBrightness = parser.isSet(QStringLiteral("smooth-brightness"));
    request.backgroundBrightnessOuter = outerBrightness;
    request.backgroundBrightnessInner = innerBrightness;
    request.layoutSquareScale = layoutSquareScale;
    request.backgroundScaleMode = backgroundScaleToken == QStringLiteral("fit")
        ? PreviewBackgroundScaleMode::FitContain
        : PreviewBackgroundScaleMode::FillCrop;
    request.noteFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    request.touchFlowSpeed = request.noteFlowSpeed;
    request.skinLoadWaitMs = skinWaitMs;

    MainWindow window;
    QString resolvedOutputPath;
    QString exportError;
    QString exportDetails;
    if (!window.exportPreviewVideoFromCli(request, &resolvedOutputPath, &exportError, &exportDetails)) {
        QTextStream(stderr) << "Video export failed: " << exportError << "\n";
        if (!exportDetails.trimmed().isEmpty()) {
            QTextStream(stderr) << exportDetails << "\n";
        }
        return 1;
    }

    QTextStream(stdout) << "Video export success: " << QDir::toNativeSeparators(resolvedOutputPath) << "\n";
    if (!exportDetails.trimmed().isEmpty()) {
        QTextStream(stdout) << exportDetails << "\n";
    }
    return 0;
    } catch (...) {
        const QString detail = currentExceptionDetail();
        const QString message = QStringLiteral("Unhandled CLI export exception: %1").arg(detail);
        miacode::debug_log::appendFatalMessage(QStringLiteral("export/cli_exception"), message);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return 1;
    }
}

int runCliVideoExportWorker(QApplication& app, QString* errorMessage)
{
    MC_OP("runCliVideoExportWorker");
    QString workerJobId;
    try {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MiaCode export worker"));
    parser.addHelpOption();
    parser.addVersionOption();
    addSharedCliDebugOption(parser);
    parser.addOption(QCommandLineOption(
        QStringLiteral("export-video-worker"),
        QStringLiteral("Run background export worker and exit.")
    ));

    if (!parser.parse(app.arguments())) {
        if (errorMessage != nullptr) {
            *errorMessage = parser.errorText();
        }
        return 2;
    }
    if (!parser.isSet(QStringLiteral("export-video-worker"))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("internal CLI dispatch error: --export-video-worker not set");
        }
        return 2;
    }

    writeWorkerJsonLine(QJsonObject{
        {QStringLiteral("event"), QStringLiteral("worker_ready")},
        {QStringLiteral("protocol"), 1},
    });

    QFile stdinFile;
    if (!stdinFile.open(stdin, QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to open stdin for export worker");
        }
        return 1;
    }

    const QList<QByteArray> inputLines = stdinFile.readAll().split('\n');
    QByteArray rawCommand;
    for (const QByteArray& line : inputLines) {
        if (!line.trimmed().isEmpty()) {
            rawCommand = line.trimmed();
            break;
        }
    }
    if (rawCommand.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker received empty command payload");
        }
        return 1;
    }

    QJsonParseError parseError;
    const QJsonDocument commandDocument = QJsonDocument::fromJson(rawCommand, &parseError);
    if (parseError.error != QJsonParseError::NoError || !commandDocument.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker failed to parse command JSON");
        }
        return 1;
    }

    const QJsonObject commandObject = commandDocument.object();
    if (commandObject.value(QStringLiteral("cmd")).toString() != QLatin1String("start_export")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unsupported export worker command");
        }
        return 1;
    }

    VideoExportSnapshot snapshot;
    QString snapshotError;
    if (!VideoExportSnapshot::fromJson(commandObject.value(QStringLiteral("snapshot")).toObject(), &snapshot, &snapshotError)) {
        if (errorMessage != nullptr) {
            *errorMessage = snapshotError;
        }
        return 1;
    }
    workerJobId = snapshot.jobId;
    miacode::debug_log::setSessionProjectLogDirectory(videoExportWorkerProjectLogDirectory(snapshot));

    writeWorkerJsonLine(QJsonObject{
        {QStringLiteral("event"), QStringLiteral("accepted")},
        {QStringLiteral("job_id"), snapshot.jobId},
    });

    VideoExportTask task;
    QString taskError;
    if (!buildVideoExportTaskFromSnapshot(snapshot, &task, &taskError)) {
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("finished")},
            {QStringLiteral("job_id"), snapshot.jobId},
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("Failed to prepare export task.")},
            {QStringLiteral("details"), taskError},
        });
        return 1;
    }

    const auto progressCallback = [&snapshot](int percent, const QString& text) {
        if (percent < 0 && text.isEmpty()) {
            return false;
        }
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("progress")},
            {QStringLiteral("job_id"), snapshot.jobId},
            {QStringLiteral("stage"), QStringLiteral("render")},
            {QStringLiteral("percent"), percent},
            {QStringLiteral("message"), text},
        });
        QCoreApplication::processEvents();
        return false;
    };

    const VideoExportResult result = VideoExportController::exportPreparedTask(task, progressCallback);

    QJsonObject finishedObject{
        {QStringLiteral("event"), QStringLiteral("finished")},
        {QStringLiteral("job_id"), snapshot.jobId},
        {QStringLiteral("success"), result.success},
    };
    if (result.success) {
        finishedObject.insert(QStringLiteral("output_path"), task.outputPath);
    } else {
        finishedObject.insert(QStringLiteral("error"), result.message);
        finishedObject.insert(QStringLiteral("details"), result.details);
    }
    writeWorkerJsonLine(finishedObject);
    return result.success ? 0 : 1;
    } catch (...) {
        const QString detail = currentExceptionDetail();
        const QString error = QStringLiteral("Unhandled export worker exception.");
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/worker_exception"),
            QStringLiteral("%1 details=%2").arg(error, detail)
        );
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("finished")},
            {QStringLiteral("job_id"), workerJobId},
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), error},
            {QStringLiteral("details"), detail},
        });
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 %2").arg(error, detail);
        }
        return 1;
    }
}

// Out-of-process preview worker — sibling of runCliVideoExportWorker. The
// worker process exists so that DXGI_ERROR_DEVICE_REMOVED on the DComp
// preview pipeline can be confined to a respawnable child instead of
// destabilising the editor (audio engine, document state, timeline UI).
//
// Phase 0 implements the static-popup PoC: spawn the worker, hand it the
// editor's HWND, watch a top-level transparent owned popup appear and
// render a static red rectangle. Phase 1+ adds the snapshot ring buffer
// and the real PreviewDCompSurface render path.
//
// See docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md.
int runCliPreviewWorkerRingSelfTest(QString* errorMessage)
{
    MC_OP("runCliPreviewWorkerRingSelfTest");
    // Single-process Phase 1 round-trip: create a publisher, attach a
    // consumer to the same key, push N synthetic snapshots, read them
    // back, report avg/min/max latency in microseconds. Exercises the
    // exact same QSharedMemory + sequence-parity path as the cross-
    // process flow but without QProcess overhead, so it runs in CI and
    // gives a clean go/no-go on the IPC primitive.
    namespace ipc = miacode::preview::ipc;

    ipc::PreviewSnapshotRingBuffer publisher;
    QString errOut;
    if (!publisher.createAsPublisher(QStringLiteral("miacode_preview_selftest"), 4, &errOut)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("publisher create failed: %1").arg(errOut);
        }
        _mc_op_.fail(QStringLiteral("publisher create failed: %1").arg(errOut));
        return 1;
    }

    ipc::PreviewSnapshotRingBuffer consumer;
    if (!consumer.attachAsConsumer(publisher.key(), &errOut)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("consumer attach failed: %1").arg(errOut);
        }
        return 1;
    }

    constexpr int kSampleCount = 32;
    qint64 latencySumNs = 0;
    qint64 latencyMaxNs = 0;
    qint64 latencyMinNs = INT64_MAX;
    int validSamples = 0;

    auto snapshotHeap = std::make_unique<ipc::PreviewFrameStateSerial>();
    auto readBackHeap = std::make_unique<ipc::PreviewFrameStateSerial>();
    for (int i = 0; i < kSampleCount; ++i) {
        // Heap-allocate the per-iteration scratch — the trivially-copyable
        // PreviewFrameStateSerial is ~150 KB and would crash a default
        // 1 MB Windows stack within a few nested frames.
        *snapshotHeap = ipc::PreviewFrameStateSerial{};
        ipc::PreviewFrameStateSerial& snapshot = *snapshotHeap;
        snapshot.layoutVersion = ipc::kSerialLayoutVersion;
        snapshot.playheadSeconds = static_cast<double>(i) / 60.0;
        if (!publisher.publish(snapshot)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("publish failed at sample %1").arg(i);
            }
            return 1;
        }

        *readBackHeap = ipc::PreviewFrameStateSerial{};
        ipc::PreviewFrameStateSerial& readBack = *readBackHeap;
        if (!consumer.readLatest(&readBack)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("readLatest failed at sample %1").arg(i);
            }
            return 1;
        }

        const qint64 nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
        const qint64 latency = nowNs - readBack.publishMonotonicNs;
        if (readBack.sequence != static_cast<quint64>(i + 1)) {
            QTextStream(stdout) << "FAIL sequence mismatch sample=" << i
                                << " expected=" << (i + 1)
                                << " got=" << readBack.sequence << "\n";
            return 1;
        }
        if (qAbs(readBack.playheadSeconds - snapshot.playheadSeconds) > 1e-9) {
            QTextStream(stdout) << "FAIL playhead mismatch sample=" << i << "\n";
            return 1;
        }
        if (latency < 0) {
            continue;
        }
        latencySumNs += latency;
        if (latency > latencyMaxNs) latencyMaxNs = latency;
        if (latency < latencyMinNs) latencyMinNs = latency;
        ++validSamples;
    }

    if (validSamples == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no valid latency samples");
        }
        return 1;
    }

    QTextStream(stdout)
        << "PASS preview_worker_ring_self_test"
        << " samples=" << validSamples
        << " avg_us=" << (latencySumNs / validSamples / 1000)
        << " min_us=" << (latencyMinNs / 1000)
        << " max_us=" << (latencyMaxNs / 1000)
        << " key=" << publisher.key() << "\n";

    // ---- Sprite projection round-trip ----
    // Build a synthetic PreviewFrameState with a deliberate mix of marker
    // types, flags, and times spanning past + future, then project + read
    // back through the ring buffer. Verifies the visible-window filter,
    // the type-kind enum mapping, the flag bitmap pack/unpack, and the
    // string blob layout all survive the QSharedMemory boundary.
    miacode::preview::scene::PreviewFrameState syntheticState;
    syntheticState.playheadSeconds = 1.0;
    auto pushMarker = [&syntheticState](double second, double endSecond, const QString& type,
                                         int lane, bool isBreak, bool slideHead, const QString& trackKey) {
        TimelineNoteMarker m;
        m.second = second;
        m.endSecond = endSecond;
        m.type = type;
        m.lane = lane;
        m.isBreak = isBreak;
        m.slideHead = slideHead;
        m.slideTrackKey = trackKey;
        syntheticState.noteMarkers.push_back(m);
    };
    pushMarker(0.5, -1.0, QStringLiteral("tap"), 1, false, false, QString());
    pushMarker(0.9, 1.6, QStringLiteral("hold"), 2, true, false, QString());
    pushMarker(1.2, 2.5, QStringLiteral("slide"), 3, false, true, QStringLiteral("1-3"));
    pushMarker(2.0, -1.0, QStringLiteral("touch"), 4, false, false, QString());
    pushMarker(7.0, -1.0, QStringLiteral("tap"), 5, false, false, QString());  // outside lookahead
    pushMarker(-2.0, -1.0, QStringLiteral("tap"), 6, false, false, QString()); // outside lookback

    // Decorate the slide marker with synthetic geometry so we can test
    // the projector's slide-geometry packer + the inflater's unpacker
    // for byte-level round trip.
    {
        TimelineNoteMarker& slide = syntheticState.noteMarkers[2];
        slide.slideSegmentPoints.push_back({ QPointF(100.0, 200.0),
                                              QPointF(150.0, 250.0),
                                              QPointF(200.0, 300.0) });
        slide.slideSegmentPoints.push_back({ QPointF(200.0, 300.0),
                                              QPointF(250.0, 350.0) });
        slide.slideSegmentAngles.push_back({ 0.0, 45.0, 90.0 });
        slide.slideSegmentAngles.push_back({ 90.0, 135.0 });
        slide.slideSegmentDurations = { 0.4, 0.3 };
        slide.slideSegmentShootSeconds = { 1.2, 1.6 };
        slide.slideSegmentCriticalProportions = { 0.5, 0.5 };
        slide.slideTrackAreaPoints.push_back({
            QVector<QPointF>{ QPointF(100, 200), QPointF(110, 210), QPointF(150, 250) },
            QVector<QPointF>{ QPointF(150, 250), QPointF(160, 260) },
        });
        slide.slideNativeTrackLength = 320.5;
        slide.slideRuntimeTrackLength = 384.0;
        slide.touchPoint = QPointF(640.5, 480.25);  // synthetic touch coord
        slide.parseOrder = 7;
        slide.eachGroupId = 3;
    }

    auto spriteSnapshotHeap = std::make_unique<miacode::preview::ipc::PreviewFrameStateSerial>();
    miacode::preview::ipc::PreviewFrameStateSerial& spriteSnapshot = *spriteSnapshotHeap;
    spriteSnapshot.layoutVersion = miacode::preview::ipc::kSerialLayoutVersion;
    miacode::preview::ipc::projectScalarsToSerial(syntheticState, spriteSnapshot);
    const int packed = miacode::preview::ipc::projectActiveSpritesToSerial(syntheticState, spriteSnapshot);
    if (packed != 4) {
        QTextStream(stdout) << "FAIL sprite projection visibility filter expected=4 got=" << packed << "\n";
        return 1;
    }
    // Append a synthetic skin directory path AFTER sprite projection (the
    // contract requires this order — both share the string blob).
    const QString syntheticSkinDir = QStringLiteral("D:/skins/default");
    miacode::preview::ipc::projectAssetPathsToSerial(syntheticSkinDir, spriteSnapshot);

    if (!publisher.publish(spriteSnapshot)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("publish (sprite snapshot) failed");
        }
        return 1;
    }

    auto readBackHeap2 = std::make_unique<miacode::preview::ipc::PreviewFrameStateSerial>();
    miacode::preview::ipc::PreviewFrameStateSerial& readBack = *readBackHeap2;
    if (!consumer.readLatest(&readBack)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("readLatest (sprite snapshot) failed");
        }
        return 1;
    }
    if (readBack.spriteCount != 4) {
        QTextStream(stdout) << "FAIL sprite roundtrip count expected=4 got=" << readBack.spriteCount << "\n";
        return 1;
    }

    // Verify type-kind round-trip on the first sprite (tap @ 0.5s).
    if (readBack.sprites[0].typeKind != miacode::preview::ipc::SerialSpriteTypeKind::Tap) {
        QTextStream(stdout) << "FAIL sprite[0] expected typeKind=Tap got="
                            << static_cast<unsigned>(readBack.sprites[0].typeKind) << "\n";
        return 1;
    }
    // The hold sprite should round-trip its kIsBreak flag.
    if ((readBack.sprites[1].flagsBitmap & miacode::preview::ipc::SerialSpriteFlags::kIsBreak) == 0) {
        QTextStream(stdout) << "FAIL sprite[1] expected kIsBreak set\n";
        return 1;
    }
    // The slide sprite should round-trip its kSlideHead flag and trackKey.
    if ((readBack.sprites[2].flagsBitmap & miacode::preview::ipc::SerialSpriteFlags::kSlideHead) == 0) {
        QTextStream(stdout) << "FAIL sprite[2] expected kSlideHead set\n";
        return 1;
    }
    const auto& trackRef = readBack.sprites[2].slideTrackKey;
    if (trackRef.length == 0) {
        QTextStream(stdout) << "FAIL sprite[2] slideTrackKey not stored\n";
        return 1;
    }
    const QString recoveredTrackKey = QString::fromUtf8(
        readBack.stringBlob.data() + trackRef.offset, static_cast<int>(trackRef.length));
    if (recoveredTrackKey != QStringLiteral("1-3")) {
        QTextStream(stdout) << "FAIL sprite[2] trackKey expected=1-3 got=" << recoveredTrackKey << "\n";
        return 1;
    }

    // Asset path round-trip — verifies the skinDirectory blob entry
    // survives publish + readLatest with bytewise integrity.
    const auto& skinRef = readBack.skinDirectory;
    if (skinRef.length == 0) {
        QTextStream(stdout) << "FAIL skinDirectory not stored\n";
        return 1;
    }
    const QString recoveredSkin = QString::fromUtf8(
        readBack.stringBlob.data() + skinRef.offset, static_cast<int>(skinRef.length));
    if (recoveredSkin != syntheticSkinDir) {
        QTextStream(stdout) << "FAIL skinDirectory expected=" << syntheticSkinDir
                            << " got=" << recoveredSkin << "\n";
        return 1;
    }

    // Slide geometry round-trip — sprite[2] (the slide marker) carries
    // segment + track-area data through markerGeometryBlob. Inflating
    // it back to a TimelineNoteMarker should restore the segment count,
    // first-segment point count + first point, and durations.
    if (readBack.sprites[2].slideGeometry.length == 0) {
        QTextStream(stdout) << "FAIL slide geometry blob ref empty\n";
        return 1;
    }
    const TimelineNoteMarker recovered =
        miacode::preview::ipc::inflateSerialSpriteToMarker(readBack.sprites[2], readBack);
    if (recovered.slideSegmentPoints.size() != 2) {
        QTextStream(stdout) << "FAIL slide segments expected=2 got="
                            << recovered.slideSegmentPoints.size() << "\n";
        return 1;
    }
    if (recovered.slideSegmentPoints[0].size() != 3
        || recovered.slideSegmentPoints[1].size() != 2) {
        QTextStream(stdout) << "FAIL slide point counts inner mismatch\n";
        return 1;
    }
    const QPointF firstPt = recovered.slideSegmentPoints[0][0];
    if (qAbs(firstPt.x() - 100.0) > 0.01 || qAbs(firstPt.y() - 200.0) > 0.01) {
        QTextStream(stdout) << "FAIL slide first point expected=(100,200) got=("
                            << firstPt.x() << "," << firstPt.y() << ")\n";
        return 1;
    }
    if (recovered.slideSegmentDurations.size() != 2
        || qAbs(recovered.slideSegmentDurations[0] - 0.4) > 0.01) {
        QTextStream(stdout) << "FAIL slide durations mismatch\n";
        return 1;
    }
    if (recovered.slideTrackAreaPoints.size() != 1
        || recovered.slideTrackAreaPoints[0].size() != 2) {
        QTextStream(stdout) << "FAIL slide track-area shape mismatch\n";
        return 1;
    }
    if (qAbs(recovered.touchPoint.x() - 640.5) > 0.01
        || qAbs(recovered.touchPoint.y() - 480.25) > 0.01) {
        QTextStream(stdout) << "FAIL touchPoint roundtrip mismatch got=("
                            << recovered.touchPoint.x() << "," << recovered.touchPoint.y() << ")\n";
        return 1;
    }
    if (recovered.parseOrder != 7 || recovered.eachGroupId != 3) {
        QTextStream(stdout) << "FAIL parseOrder/eachGroupId roundtrip mismatch\n";
        return 1;
    }

    QTextStream(stdout) << "PASS sprite_projection round_trip"
                        << " sprites=" << readBack.spriteCount
                        << " blob_used=" << readBack.stringBlobUsedBytes
                        << " skin_dir_bytes=" << skinRef.length
                        << " geom_blob_used=" << readBack.markerGeometryBlobUsedBytes << "\n";
    return 0;
}

// Phase 5 stress test harness. Spawns a worker that crash-injects via
// __fastfail every N frames; the supervisor respawns it; we collect
// exit-to-attach timings for kCycles cycles and report p50/p95/max.
//
// The worker needs a real editor HWND, so this harness creates a hidden
// top-level window first. Without that, popup creation fails immediately
// and the supervisor sees no `attached` events.
//
// Default crash interval = 30 frames (~1 s at the worker's 30 Hz timer);
// default cycle target = 5 cycles. Override with
//   MIACODE_PREVIEW_CRASH_TEST_CYCLES=N
//   MIACODE_PREVIEW_WORKER_INJECT_CRASH=N      (forwarded to the worker)
int runCliPreviewWorkerCrashRecoveryTest(QApplication& app, QString* errorMessage)
{
    MC_OP("runCliPreviewWorkerCrashRecoveryTest");
#ifdef Q_OS_WIN
    namespace ipc = miacode::preview::ipc;

    bool ok = false;
    const int targetCycles = qEnvironmentVariable("MIACODE_PREVIEW_CRASH_TEST_CYCLES").toInt(&ok);
    const int kCycles = (ok && targetCycles > 0 && targetCycles <= 50) ? targetCycles : 5;
    const int kCrashAtFrame = qEnvironmentVariableIsSet("MIACODE_PREVIEW_WORKER_INJECT_CRASH")
                                  ? qEnvironmentVariable("MIACODE_PREVIEW_WORKER_INJECT_CRASH").toInt()
                                  : 30;

    // Force-set so spawned children inherit our values even if the user
    // didn't pass them in. Children read these once at attach time.
    qputenv("MIACODE_PREVIEW_WORKER_INJECT_CRASH", QByteArray::number(kCrashAtFrame));
    qputenv("MIACODE_PREVIEW_WORKER_DISABLE_CRASH_LIMIT", "1");

    // Create a hidden top-level HWND to act as the editor for the worker's
    // SetWindowLongPtr(GWLP_HWNDPARENT) call. WS_EX_TOOLWINDOW keeps it
    // out of the taskbar; SW_HIDE keeps it invisible. Since we're a
    // QApplication we already have an HINSTANCE and message pump.
    const HWND hiddenEditor = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"MiaCode Crash Recovery Test",
        WS_POPUP, 0, 0, 320, 240,
        nullptr, nullptr,
        ::GetModuleHandleW(nullptr), nullptr);
    if (hiddenEditor == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("CreateWindowExW failed: err=%1").arg(::GetLastError());
        }
        return 1;
    }

    QVector<qint64> totalRespawnSamples;
    QVector<qint64> spawnToAttachSamples;
    int crashesObserved = 0;
    int givenUp = 0;

    QEventLoop loop;
    QTimer overallTimeout;
    overallTimeout.setSingleShot(true);
    overallTimeout.setInterval(60'000);  // 60 s ceiling

    auto supervisor = std::make_unique<ipc::PreviewWorkerSupervisor>();

    QObject::connect(supervisor.get(), &ipc::PreviewWorkerSupervisor::workerExited,
                     &loop, [&crashesObserved](int /*exitCode*/, QProcess::ExitStatus status) {
                         if (status == QProcess::CrashExit) {
                             ++crashesObserved;
                         }
                     });

    QObject::connect(supervisor.get(), &ipc::PreviewWorkerSupervisor::workerRespawnTimeRecorded,
                     &loop, [&totalRespawnSamples, &spawnToAttachSamples, &loop, kCycles](
                                qint64 totalMs, qint64 spawnMs) {
                         totalRespawnSamples.append(totalMs);
                         spawnToAttachSamples.append(spawnMs);
                         QTextStream(stdout)
                             << "cycle=" << totalRespawnSamples.size()
                             << " total_ms=" << totalMs
                             << " spawn_to_attach_ms=" << spawnMs << "\n";
                         if (totalRespawnSamples.size() >= kCycles) {
                             loop.quit();
                         }
                     });

    QObject::connect(supervisor.get(), &ipc::PreviewWorkerSupervisor::workerCrashLoopGivenUp,
                     &loop, [&givenUp, &loop]() {
                         ++givenUp;
                         loop.quit();
                     });

    QObject::connect(&overallTimeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QString spawnError;
    if (!supervisor->spawnStaticTest(reinterpret_cast<quint64>(hiddenEditor), &spawnError)) {
        ::DestroyWindow(hiddenEditor);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("supervisor spawn failed: %1").arg(spawnError);
        }
        return 1;
    }

    overallTimeout.start();
    loop.exec();
    overallTimeout.stop();

    supervisor->shutdown();
    supervisor.reset();
    ::DestroyWindow(hiddenEditor);

    Q_UNUSED(app);

    QTextStream out(stdout);
    out << "=== preview worker crash-recovery test summary ===\n"
        << "target_cycles=" << kCycles
        << " observed_cycles=" << totalRespawnSamples.size()
        << " crashes_seen=" << crashesObserved
        << " gave_up=" << givenUp
        << " crash_at_frame=" << kCrashAtFrame << "\n";

    if (totalRespawnSamples.isEmpty()) {
        out << "FAIL no respawn samples (timed out before any successful respawn)\n";
        return 1;
    }

    auto computeStats = [](QVector<qint64> samples) {
        std::sort(samples.begin(), samples.end());
        auto pct = [&samples](double p) {
            const int idx = qBound(0, static_cast<int>(p * (samples.size() - 1) + 0.5),
                                   samples.size() - 1);
            return samples[idx];
        };
        qint64 sum = 0;
        for (qint64 v : samples) sum += v;
        return std::make_tuple(samples.first(), pct(0.50), pct(0.95), samples.last(),
                               sum / samples.size());
    };

    auto [tMin, tP50, tP95, tMax, tAvg] = computeStats(totalRespawnSamples);
    out << "total_respawn_ms (includes backoff delay):"
        << " min=" << tMin
        << " p50=" << tP50
        << " p95=" << tP95
        << " max=" << tMax
        << " avg=" << tAvg << "\n";

    auto [sMin, sP50, sP95, sMax, sAvg] = computeStats(spawnToAttachSamples);
    out << "spawn_to_attach_ms (excludes backoff delay):"
        << " min=" << sMin
        << " p50=" << sP50
        << " p95=" << sP95
        << " max=" << sMax
        << " avg=" << sAvg << "\n";

    // §6.3 success criterion is < 800 ms p95 for "respawn-and-attach". The
    // exponential-backoff delay is a deliberate production safeguard, not
    // a startup cost — gate the pass/fail on spawn_to_attach_ms which is
    // what's actually under test (QProcess::start → first popup paint).
    const bool pass = sP95 < 800 && totalRespawnSamples.size() >= kCycles && givenUp == 0;
    out << (pass ? "PASS" : "FAIL")
        << " budget_p95_ms=800"
        << " observed_spawn_to_attach_p95_ms=" << sP95
        << " observed_total_respawn_p95_ms=" << tP95 << "\n";
    return pass ? 0 : 1;
#else
    Q_UNUSED(app);
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("crash recovery test is Windows-only");
    }
    return 1;
#endif
}

int runCliPreviewWorker(QApplication& app, bool staticTestMode, QString* errorMessage)
{
    MC_OP("runCliPreviewWorker");
    _mc_op_.note(QStringLiteral("staticTestMode=%1").arg(staticTestMode ? 1 : 0));
    try {
        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral("MiaCode preview worker"));
        parser.addHelpOption();
        parser.addVersionOption();
        addSharedCliDebugOption(parser);
        parser.addOption(QCommandLineOption(
            QStringLiteral("preview-worker"),
            QStringLiteral("Run out-of-process preview worker and exit.")
        ));
        parser.addOption(QCommandLineOption(
            QStringLiteral("preview-worker-static-test"),
            QStringLiteral("Run static-popup preview worker proof of concept.")
        ));

        if (!parser.parse(app.arguments())) {
            if (errorMessage != nullptr) {
                *errorMessage = parser.errorText();
            }
            return 2;
        }

#ifdef Q_OS_WIN
        // Raise Windows scheduler quantum from the default 15.6 ms to 1 ms
        // for the worker process's lifetime. Without this, the snapshot
        // poll timer (5 ms cadence) can't fire faster than ~15 ms — which
        // shows up in the trace log as `latency_summary_1s missed_jumps>0
        // max_us≈18000-24000` and visible chart stutter (the worker
        // misses ~1 vsync of editor publishes per second). Released on
        // process exit. Same approach the editor uses behind
        // `MIACODE_PREVIEW_FIXED_TIMER_HIGH_RES` (Tier 1 §2.1) — for the
        // worker we make it unconditional since the entire purpose of
        // this process is timing-sensitive preview rendering.
        const MMRESULT timerBeginResult = ::timeBeginPeriod(1);
        if (timerBeginResult != TIMERR_NOERROR) {
            QTextStream(stderr) << "[preview-worker] timeBeginPeriod(1) failed: "
                                << timerBeginResult << '\n';
        }
        // Best-effort restore — RAII on a struct so an exception still
        // calls timeEndPeriod. The OS reference-counts these calls.
        struct TimePeriodGuard {
            bool active;
            ~TimePeriodGuard() {
                if (active) {
                    ::timeEndPeriod(1);
                }
            }
        } timerGuard{ timerBeginResult == TIMERR_NOERROR };
#endif

        miacode::preview::worker::PreviewWorkerSession session;
        const int exitCode = staticTestMode ? session.runStaticTest() : session.run();
        return exitCode;
    } catch (...) {
        const QString detail = currentExceptionDetail();
        const QString error = QStringLiteral("Unhandled preview worker exception.");
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("preview/worker_exception"),
            QStringLiteral("%1 details=%2").arg(error, detail)
        );
        miacode::preview::ipc::emitFatalEvent(
            QStringLiteral("preview/worker_exception"),
            QStringLiteral("%1 %2").arg(error, detail)
        );
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 %2").arg(error, detail);
        }
        return 1;
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    // Phase 6 — startup beacon. Heap-free, pure-Win32. Lands a tiny
    // miacode_startup_beacon_<pid>.txt next to the eventual op-chain
    // shadow log (MIACODE_LOG_DIR or %TEMP%). Its presence proves the
    // process made it to line 1 of main() — its absence means the
    // process died in static init / DLL load (outside our reach), which
    // is a fundamentally different debug path than a crash inside our
    // code. Written before MC_OP, before crash_recovery::install, and
    // before QApplication construction so a crash in any of those still
    // leaves the breadcrumb behind.
    miacode::oplog::writeStartupBeacon(MIACODE_DISPLAY_VERSION_STRING);

#ifdef Q_OS_WIN
    // Vectored exception handler — installed as early as possible because
    // it cannot be displaced by later SetUnhandledExceptionFilter calls
    // from Qt / AMD UMD / Defender. Catches the fast-fail / __fastfail
    // codes (0xC0000409 etc.) that bypass the regular SEH filter chain
    // and would otherwise leave NO breadcrumb on hard process death.
    ::AddVectoredExceptionHandler(/*first=*/1, &vectoredHandler);
    miacode::oplog::appendStartupBeaconLine("phase=veh_installed");

    // Experimental beta42 startup diagnostic. Probes OS version, VC++
    // runtime DLL availability, and D3D11 device creation BEFORE any
    // Qt code touches the process — narrows "silent crash on Win10
    // after the beacon" to one of three hypotheses (A=VC runtime,
    // B=GPU driver, C=Win10 build too old) on a single run, no --debug
    // required. All output goes to the same beacon file via append.
    runStartupDiagnostic();
    miacode::oplog::appendStartupBeaconLine("phase=pre_mc_op");
#endif

    MC_OP("main");
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_crash_recovery_install");
#endif
    // Install crash-time autosave handlers BEFORE anything else can fail.
    // This way an early-startup segfault (e.g. graphics driver bug during
    // QApplication construction) still gets a chance to flush the
    // last-known document text — though in practice early crashes happen
    // before any document is loaded so the snapshot is empty / safe.
    miacode::crash_recovery::install();
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_crash_recovery_install");
#endif

#ifdef Q_OS_WIN
    // Force PER_MONITOR_AWARE_V2 DPI awareness for BOTH the editor and
    // the worker process so MoveWindow's pixel interpretation is
    // identical across the IPC boundary. Qt6's QGuiApplication
    // normally sets this in its constructor, but doing it ourselves
    // here — before any Qt code runs — guarantees the awareness is
    // locked in before any other library code (Defender, third-party
    // DLLs in startup-load) might inadvertently set a different
    // value, which can no longer be overridden once locked.
    const BOOL dpiAwareOk =
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "phase=dpi_aware_set ok=%d err=%lu",
            dpiAwareOk ? 1 : 0,
            dpiAwareOk ? 0UL : static_cast<unsigned long>(::GetLastError()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif

    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        rawArgs.append(QString::fromLocal8Bit(argv[index]));
    }
    const bool cliVideoExportRequested = wantsCliVideoExport(rawArgs);
    const bool cliVideoExportWorkerRequested = wantsCliVideoExportWorker(rawArgs);
    const bool cliPreviewWorkerRequested = wantsCliPreviewWorker(rawArgs);
    const bool cliPreviewWorkerStaticTestMode = wantsCliPreviewWorkerStaticTest(rawArgs);
    const bool cliPreviewWorkerRingSelfTestMode = wantsCliPreviewWorkerRingSelfTest(rawArgs);
    const bool cliPreviewWorkerCrashRecoveryTestMode =
        wantsCliPreviewWorkerCrashRecoveryTest(rawArgs);
    const bool forceOpenGlGraphicsApi = cliVideoExportRequested || cliVideoExportWorkerRequested;
    const QString startupOpenTarget =
        !cliVideoExportRequested && !cliVideoExportWorkerRequested && !cliPreviewWorkerRequested
            ? startupOpenTargetFromArguments(rawArgs)
            : QString();

    // Preview worker self-redirect for the runtime log channel. The worker
    // is the same MiaCode.exe relaunched with --preview-worker, but it
    // shares a log directory with its parent editor process. On Windows
    // QFile::open(append) fails when another process holds the file
    // exclusively, so two MiaCode instances writing to the same
    // `miacode_runtime_debug.log` would silently drop the second's lines.
    // Set MIACODE_RUNTIME_LOG_PATH unconditionally for worker processes
    // so child writes go to a sibling file. Done before
    // setDebugModeEnabled (which causes the first log writes) and
    // before trimDebugSessionLogsForStartup (which reads logPath).
    if (cliPreviewWorkerRequested
        && qEnvironmentVariableIsEmpty("MIACODE_RUNTIME_LOG_PATH")) {
        // Default fallback path — supervisor sets it explicitly with the
        // chart project's log dir; this self-redirect kicks in if the
        // worker is launched standalone (CLI testing, debugging) so the
        // process still produces an inspectable log.
        const QString fallbackPath = QDir(QDir::tempPath()).filePath(
            QStringLiteral("miacode_runtime_debug_worker.log"));
        qputenv("MIACODE_RUNTIME_LOG_PATH", fallbackPath.toUtf8());
    }

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_args_parsed");
#endif
    miacode::debug_options::setDebugModeEnabled(miacode::debug_options::hasDebugArg(rawArgs));
#ifdef Q_OS_WIN
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "phase=debug_mode_set debug=%d",
            miacode::debug_options::debugModeEnabled() ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif
    if (miacode::debug_options::debugModeEnabled()) {
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_trim_debug_logs");
#endif
        miacode::debug_log::trimDebugSessionLogsForStartup();
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=after_trim_debug_logs");
#endif
    }
    if (miacode::debug_options::startupTimingEnabled()) {
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_init_startup_timing");
#endif
        miacode::debug_log::initializeStartupTimingLogSession();
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=after_init_startup_timing");
#endif
    }

    // (libmpv probe removed in beta20 — the "Phase 4 video source built on
    // top of this" never landed; chart-preview video backgrounds use Qt's
    // QMediaPlayer + QVideoSink stack via PreviewStageMediaHost. Shipping
    // libmpv-2.dll cost ~113 MB to log a single startup version line.)

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_env_var_parse");
#endif
    const bool qsgFullDisable = miacode::debug_options::previewQsgFullDisableEnabled();
    const bool forceBasicRenderLoop = qsgFullDisable
        || miacode::debug_options::envFlagEnabled(
            "MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP"
        );
    const bool disableDontCreateNativeWidgetSiblings = miacode::debug_options::envFlagEnabled(
        "MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS"
    );
    const bool dontCreateNativeWidgetSiblingsEnabled = !disableDontCreateNativeWidgetSiblings;
    const QString requestedRenderLoop = qEnvironmentVariable("QSG_RENDER_LOOP").trimmed();
    if (forceBasicRenderLoop && requestedRenderLoop.isEmpty()) {
        qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("basic"));
    }
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_set_qt_attributes");
#endif
    if (dontCreateNativeWidgetSiblingsEnabled) {
        QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    }
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_set_qt_attributes");
#endif

    // Diagnostic: capture Qt's scene-graph render timings into our runtime log
    // when the user opts in. Has to happen before QApplication construction
    // because Qt reads QSG_RENDER_TIMING / QT_LOGGING_RULES once at startup
    // and the categories are gated on those env vars. We append our category
    // override to whatever the user already has rather than overwriting, so
    // existing rule overrides keep working.
    if (miacode::debug_options::previewQsgRenderTimingEnabled()) {
        qputenv("QSG_RENDER_TIMING", QByteArrayLiteral("1"));
        const QByteArray existingRules = qgetenv("QT_LOGGING_RULES");
        QByteArray nextRules = existingRules;
        if (!nextRules.isEmpty() && !nextRules.endsWith(';')) {
            nextRules.append(';');
        }
        nextRules.append("qt.scenegraph.time.*=true");
        qputenv("QT_LOGGING_RULES", nextRules);

        // Route the resulting qt.scenegraph.time.* messages to our log so they
        // sit alongside renderer_perf / frame_pacing in the same file with
        // matching timestamps. Pass everything else through to the prior
        // handler (whatever Qt had installed before us) so we don't silence
        // ordinary Qt warnings/criticals.
        static QtMessageHandler s_priorMessageHandler = nullptr;
        s_priorMessageHandler = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
                const char* category = ctx.category != nullptr ? ctx.category : "";
                if (qstrncmp(category, "qt.scenegraph.time.", 19) == 0) {
                    miacode::debug_log::appendLine(
                        miacode::debug_log::Channel::Runtime,
                        QStringLiteral("preview/qsg_timing"),
                        QString("category=%1 msg=%2")
                            .arg(QString::fromLatin1(category))
                            .arg(msg));
                    return;
                }
                if (s_priorMessageHandler != nullptr) {
                    s_priorMessageHandler(type, ctx, msg);
                }
            });
    }
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_first_runtime_log_append");
#endif
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/qt_config"),
            QString(
                "qsg_full_disable=%1 force_basic_render_loop=%2 qsg_render_loop=%3 "
                "dont_create_native_widget_siblings_default=1 "
                "disable_dont_create_native_widget_siblings=%4 "
                "effective_dont_create_native_widget_siblings=%5"
            )
                .arg(qsgFullDisable ? 1 : 0)
                .arg(forceBasicRenderLoop ? 1 : 0)
                .arg(qEnvironmentVariable("QSG_RENDER_LOOP").trimmed().isEmpty()
                         ? QStringLiteral("(default)")
                         : qEnvironmentVariable("QSG_RENDER_LOOP").trimmed())
                .arg(disableDontCreateNativeWidgetSiblings ? 1 : 0)
                .arg(dontCreateNativeWidgetSiblingsEnabled ? 1 : 0)
        );
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=after_first_runtime_log_append");
#endif
    }

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_startup_timer_init");
#endif
    QElapsedTimer startupTimer;
    startupTimer.start();
    qint64 lastStageMs = 0;
    const auto logStartupStage = [&](const QString& stage) {
        const qint64 nowMs = startupTimer.elapsed();
        const qint64 deltaMs = nowMs - lastStageMs;
        lastStageMs = nowMs;
        miacode::debug_log::appendStartupTimingStage(stage, nowMs, deltaMs);
    };
    logStartupStage("process_entry");

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_arm64_probe");
#endif

    // Detect Apple Silicon Windows VM (Windows-on-ARM running x86/x64
    // emulation). When detected, previewUseDCompEnabled() and
    // previewOutOfProcessEnabled() auto-fall-back to false — the legacy
    // QSG-only render path (beta19-equivalent), which doesn't create
    // any popup HWNDs that would otherwise crash under that emulation.
    // Logged once at startup so support can confirm the fallback fired.
    if (miacode::debug_options::runningOnArm64WindowsEmulation()) {
        miacode::debug_log::appendStartupTimingStage(
            QStringLiteral("arm64_emulation_detected"),
            startupTimer.elapsed(), 0);
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/preview_path"),
            QStringLiteral("reason=arm64_windows_emulation "
                          "dcomp=false out_of_process=false "
                          "fallback=qsg_only_legacy"));
    }
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_arm64_probe");
#endif

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    // Triple-buffer + vsync. With DoubleBuffer the swap chain caps the GPU at 1 frame in
    // flight, so a fast Render submit (~5 ms) immediately stalls on the next Present until
    // the previous frame swaps — surfacing as a bimodal render_submit_ms (median ~5 ms,
    // p90 ~24 ms ≈ one vsync of forced wait). TripleBuffer keeps two frames in flight,
    // letting the CPU stay one frame ahead of the GPU and removing the back-pressure stall.
    // Cost: one extra frame (~16.7 ms) of latency from input to display, which is
    // imperceptible for a chart preview where the playhead is audio-authoritative anyway.
    format.setSwapBehavior(QSurfaceFormat::TripleBuffer);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    logStartupStage("surface_format_ready");

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_qapplication_construct");
#endif
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_qapplication_construct");
#endif

    // Backend selection. CLI export always forces OpenGL (legacy export pipeline relies on
    // it). Otherwise: honour user's --rhi=<name> if present (and persist for next launch),
    // else fall back to the persisted choice from the prior run, else Qt's platform default.
    QString appliedGraphicsBackend;
    QString graphicsBackendSource;
    if (forceOpenGlGraphicsApi) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        appliedGraphicsBackend = QStringLiteral("opengl");
        graphicsBackendSource = QStringLiteral("cli_video_export_force");
    } else if (qsgFullDisable) {
        // Diagnostic mode: completely exclude Qt Quick's native render
        // path. Force software backend regardless of CLI / persisted
        // setting; do NOT persist, so a normal next launch reverts.
        appliedGraphicsBackend =
            applyGraphicsBackendChoice(QStringLiteral("software"));
        graphicsBackendSource = QStringLiteral("qsg_full_disable_forced");
    } else {
        const GraphicsBackendChoice choice = resolveGraphicsBackendChoice(rawArgs);
        if (choice.fromCommandLine) {
            // Persist (or clear) so the next launch defaults to the same backend without
            // requiring the flag again.
            writePersistedGraphicsBackend(choice.name);
        }
        appliedGraphicsBackend = applyGraphicsBackendChoice(choice.name);
        graphicsBackendSource = choice.fromCommandLine
            ? (choice.clearedByCommand
                   ? QStringLiteral("cli_cleared")
                   : QStringLiteral("cli_override"))
            : (choice.name.isEmpty() ? QStringLiteral("platform_default")
                                     : QStringLiteral("persisted"));
    }
    logStartupStage("qapplication_constructed");
#ifdef Q_OS_WIN
    {
        const QByteArray appliedUtf8 = appliedGraphicsBackend.isEmpty()
            ? QByteArrayLiteral("(qt_default)")
            : appliedGraphicsBackend.toUtf8();
        const QByteArray sourceUtf8 = graphicsBackendSource.toUtf8();
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "phase=graphics_api_applied backend=%.64s source=%.64s",
            appliedUtf8.constData(), sourceUtf8.constData());
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/graphics_backend"),
            QString("applied=%1 source=%2 persisted_path=%3")
                .arg(appliedGraphicsBackend.isEmpty() ? QStringLiteral("(qt_default)")
                                                      : appliedGraphicsBackend)
                .arg(graphicsBackendSource)
                .arg(persistedGraphicsBackendFilePath())
        );
    }
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/qt_config"),
            QString("graphics_api=%1 dont_create_native_widget_siblings=%2 cli_export=%3 cli_export_worker=%4 cli_preview_worker=%5 cli_preview_worker_static_test=%6")
                .arg(forceOpenGlGraphicsApi ? QStringLiteral("OpenGL") : QStringLiteral("PlatformDefault"))
                .arg(QApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings) ? 1 : 0)
                .arg(cliVideoExportRequested ? 1 : 0)
                .arg(cliVideoExportWorkerRequested ? 1 : 0)
                .arg(cliPreviewWorkerRequested ? 1 : 0)
                .arg(cliPreviewWorkerStaticTestMode ? 1 : 0)
        );
    }
#ifdef Q_OS_WIN
    setWindowsAppUserModelId();
#endif
    app.setApplicationName("MiaCode");
    app.setApplicationVersion(MIACODE_DISPLAY_VERSION_STRING);
    const QIcon appIcon(QStringLiteral(":/icons/app.png"));
    app.setWindowIcon(appIcon);
    app.setStyle(QStyleFactory::create("Fusion"));
    UiTheme::applyApplicationTheme(app);
    logStartupStage("app_style_ready");

    if (UiText::isChineseUi()) {
        QFont zhUiFont;
        for (const QString& family : QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"}) {
            zhUiFont.setFamily(family);
            if (zhUiFont.family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
        zhUiFont.setStyleStrategy(QFont::PreferAntialias);
        zhUiFont.setHintingPreference(QFont::PreferNoHinting);
        app.setFont(zhUiFont);
    }
    logStartupStage("ui_font_ready");

    if (cliVideoExportWorkerRequested) {
        QString cliError;
        const int exitCode = runCliVideoExportWorker(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "Worker error: " << cliError << "\n";
        }
        return exitCode;
    }

    if (cliPreviewWorkerRingSelfTestMode) {
        QString cliError;
        const int exitCode = runCliPreviewWorkerRingSelfTest(&cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "Preview worker ring self-test error: " << cliError << "\n";
        }
        return exitCode;
    }

    if (cliPreviewWorkerCrashRecoveryTestMode) {
        QString cliError;
        const int exitCode = runCliPreviewWorkerCrashRecoveryTest(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "Preview worker crash-recovery test error: " << cliError << "\n";
        }
        return exitCode;
    }

    if (cliPreviewWorkerRequested) {
        QString cliError;
        const int exitCode = runCliPreviewWorker(app, cliPreviewWorkerStaticTestMode, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "Preview worker error: " << cliError << "\n";
        }
        return exitCode;
    }

    if (cliVideoExportRequested) {
        QString cliError;
        const int exitCode = runCliVideoExport(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "CLI argument error: " << cliError << "\n";
        }
        return exitCode;
    }

    // Phase 3a of the v2-refactor — `--quick-shell-beta` becomes the
    // canonical opt-in for the new DComp pipeline. Setting the env vars
    // here (before any code that reads them via envFlagEnabled) makes
    // the flag self-contained: users running with --quick-shell-beta no
    // longer need to also set MIACODE_PREVIEW_USE_DCOMP=1 in their
    // shell, and the same release build covers both legacy QSG and
    // DComp paths via this single argv check.
    //
    // We use qputenv(..., "1") only when the env var is currently
    // *unset*, so an explicit MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND=0 in
    // the launching shell still wins (lets the user A/B without
    // rebuilding). previewUseDCompEnabled defers to envFlagEnabled
    // which checks the live env on every call, so setting it here is
    // sufficient.
    const bool quickShellBetaRequested = wantsQuickShellBeta(app.arguments());
    if (quickShellBetaRequested) {
        if (qEnvironmentVariableIsEmpty("MIACODE_PREVIEW_USE_DCOMP")) {
            qputenv("MIACODE_PREVIEW_USE_DCOMP", QByteArrayLiteral("1"));
        }
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QElapsedTimer appExecElapsed;
    QElapsedTimer postLastWindowClosedElapsed;
    bool lastWindowClosedSeen = false;
    int shutdownHeartbeatCount = 0;
    QTimer shutdownHeartbeatTimer;
    shutdownHeartbeatTimer.setInterval(250);
    shutdownHeartbeatTimer.setSingleShot(false);

    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, [&]() {
        lastWindowClosedSeen = true;
        shutdownHeartbeatCount = 0;
        postLastWindowClosedElapsed.restart();
        appendAppShutdownRuntimeLog(
            QStringLiteral("last_window_closed"),
            QStringLiteral("quit_on_last_window_closed=%1 top_level_widgets=%2 widgets={%3} top_level_windows=%4 windows={%5}")
                .arg(app.quitOnLastWindowClosed() ? 1 : 0)
                .arg(QApplication::topLevelWidgets().size())
                .arg(summarizeTopLevelWidgets())
                .arg(QGuiApplication::topLevelWindows().size())
                .arg(summarizeTopLevelWindows())
        );
        shutdownHeartbeatTimer.start();
    });
    QObject::connect(&shutdownHeartbeatTimer, &QTimer::timeout, &app, [&]() {
        if (!lastWindowClosedSeen) {
            return;
        }
        ++shutdownHeartbeatCount;
        const qint64 elapsedMs = postLastWindowClosedElapsed.isValid() ? postLastWindowClosedElapsed.elapsed() : -1;
        appendAppShutdownRuntimeLog(
            QStringLiteral("last_window_closed_heartbeat"),
            QStringLiteral("count=%1 elapsed_ms=%2 closing_down=%3 top_level_widgets=%4 widgets={%5} top_level_windows=%6 windows={%7}")
                .arg(shutdownHeartbeatCount)
                .arg(elapsedMs)
                .arg(QCoreApplication::closingDown() ? 1 : 0)
                .arg(QApplication::topLevelWidgets().size())
                .arg(summarizeTopLevelWidgets())
                .arg(QGuiApplication::topLevelWindows().size())
                .arg(summarizeTopLevelWindows())
        );
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&]() {
        shutdownHeartbeatTimer.stop();
        appendAppShutdownRuntimeLog(
            QStringLiteral("about_to_quit"),
            QStringLiteral("after_last_window_closed_ms=%1 top_level_widgets=%2 widgets={%3} top_level_windows=%4 windows={%5}")
                .arg(postLastWindowClosedElapsed.isValid() ? postLastWindowClosedElapsed.elapsed() : -1)
                .arg(QApplication::topLevelWidgets().size())
                .arg(summarizeTopLevelWidgets())
                .arg(QGuiApplication::topLevelWindows().size())
                .arg(summarizeTopLevelWindows())
        );
    });

    int exitCode = 1;
    QElapsedTimer postExecObjectTeardownElapsed;
    {
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_quick_shell_bootstrap_start");
#endif
        QuickShellBootstrap quickShellBootstrap(appIcon);
        if (!quickShellBootstrap.start(startupOpenTarget)) {
#ifdef Q_OS_WIN
            miacode::oplog::appendStartupBeaconLine("phase=quick_shell_bootstrap_failed");
#endif
            QTextStream(stderr) << "Failed to start Quick Shell Beta.\n";
            return 1;
        }
        logStartupStage("quick_shell_bootstrap_started");
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=quick_shell_bootstrap_started");
#endif
        QTimer::singleShot(0, &app, [&logStartupStage]() {
            logStartupStage("event_loop_first_tick");
#ifdef Q_OS_WIN
            miacode::oplog::appendStartupBeaconLine("phase=event_loop_first_tick");
#endif
        });
        appExecElapsed.start();
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=entering_event_loop");
#endif
        exitCode = app.exec();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("app_shutdown"),
            QStringLiteral("event_loop_exit"),
            appExecElapsed.elapsed(),
            QStringLiteral("exit_code=%1 after_last_window_closed_ms=%2")
                .arg(exitCode)
                .arg(postLastWindowClosedElapsed.isValid() ? postLastWindowClosedElapsed.elapsed() : -1)
        );
        postExecObjectTeardownElapsed.start();
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown"),
        QStringLiteral("post_exec_object_teardown"),
        postExecObjectTeardownElapsed.isValid() ? postExecObjectTeardownElapsed.elapsed() : -1,
        QStringLiteral("top_level_widgets=%1 top_level_windows=%2")
            .arg(QApplication::topLevelWidgets().size())
            .arg(QGuiApplication::topLevelWindows().size())
    );
    // Permanently shut down the async log writer last so any teardown logs above are
    // drained to disk and the worker thread is joined before the process exits.
    miacode::debug_log::shutdownAsyncLogWriter();
    return exitCode;
}
