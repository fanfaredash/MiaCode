#include "MainEntrypoints.h"

#include "common/OperationLog.h"

#include <QtGlobal>

#include <cstdint>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#include <timeapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")
#endif

namespace miacode::app::entry {

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
        // Read FileVersion via GetFileVersionInfo so we can prove whether
        // the user's MSVCP140 / VCRUNTIME140 is the post-VS2019-16.5
        // build our binary needs. Mismatch here is the leading hypothesis
        // for "first std::mutex lock crashes with AV inside MSVCP140".
        char versionUtf8[64] = "?";
        if (len > 0 && len < MAX_PATH) {
            DWORD vhandle = 0;
            DWORD vsize = ::GetFileVersionInfoSizeW(path, &vhandle);
            if (vsize > 0 && vsize < 65536) {
                BYTE versionBuf[65536];
                if (::GetFileVersionInfoW(path, 0, vsize, versionBuf)) {
                    VS_FIXEDFILEINFO* fixed = nullptr;
                    UINT fixedLen = 0;
                    if (::VerQueryValueW(versionBuf, L"\\",
                                         reinterpret_cast<LPVOID*>(&fixed),
                                         &fixedLen)
                        && fixed != nullptr) {
                        std::snprintf(versionUtf8, sizeof(versionUtf8),
                            "%u.%u.%u.%u",
                            HIWORD(fixed->dwFileVersionMS),
                            LOWORD(fixed->dwFileVersionMS),
                            HIWORD(fixed->dwFileVersionLS),
                            LOWORD(fixed->dwFileVersionLS));
                    }
                }
            }
        }
        char buf[1024];
        std::snprintf(buf, sizeof(buf),
            "diag/dll name=%s loaded=1 version=%s path=%s",
            nameUtf8, versionUtf8, pathUtf8);
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
void appendExceptionBeaconLine(const char* tag, EXCEPTION_POINTERS* info) noexcept
{
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return;
    }
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const CONTEXT* context = info->ContextRecord;
    char modulePath[MAX_PATH] = {};
    void* moduleBase = nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (::VirtualQuery(record->ExceptionAddress, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        moduleBase = mbi.AllocationBase;
        if (moduleBase != nullptr) {
            ::GetModuleFileNameA(reinterpret_cast<HMODULE>(moduleBase), modulePath, MAX_PATH);
        }
    }
#if defined(_M_X64) || defined(__x86_64__)
    const unsigned long long ip = context != nullptr ? static_cast<unsigned long long>(context->Rip) : 0ull;
    const unsigned long long sp = context != nullptr ? static_cast<unsigned long long>(context->Rsp) : 0ull;
    const unsigned long long bp = context != nullptr ? static_cast<unsigned long long>(context->Rbp) : 0ull;
#elif defined(_M_IX86) || defined(__i386__)
    const unsigned long long ip = context != nullptr ? static_cast<unsigned long long>(context->Eip) : 0ull;
    const unsigned long long sp = context != nullptr ? static_cast<unsigned long long>(context->Esp) : 0ull;
    const unsigned long long bp = context != nullptr ? static_cast<unsigned long long>(context->Ebp) : 0ull;
#else
    const unsigned long long ip = 0ull;
    const unsigned long long sp = 0ull;
    const unsigned long long bp = 0ull;
#endif
    const ULONG_PTR p0 = record->NumberParameters > 0 ? record->ExceptionInformation[0] : 0;
    const ULONG_PTR p1 = record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0;
    const ULONG_PTR p2 = record->NumberParameters > 2 ? record->ExceptionInformation[2] : 0;
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "crash/%s code=0x%08lx flags=0x%08lx addr=%p tid=%lu ip=0x%llx sp=0x%llx bp=0x%llx "
        "params=%lu p0=0x%llx p1=0x%llx p2=0x%llx module_base=%p module=\"%s\"",
        tag,
        static_cast<unsigned long>(record->ExceptionCode),
        static_cast<unsigned long>(record->ExceptionFlags),
        record->ExceptionAddress,
        static_cast<unsigned long>(::GetCurrentThreadId()),
        ip,
        sp,
        bp,
        static_cast<unsigned long>(record->NumberParameters),
        static_cast<unsigned long long>(p0),
        static_cast<unsigned long long>(p1),
        static_cast<unsigned long long>(p2),
        moduleBase,
        modulePath);
    miacode::oplog::appendStartupBeaconLine(buf);
}

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
        || code == 0xC0000374 /* HEAP_CORRUPTION */
        || code == 0xC0000409 /* STACK_BUFFER_OVERRUN / __fastfail */
        || code == 0xC000041D /* UNHANDLED C++ EXCEPTION (CRT) */
        || code == 0xC0000420 /* ASSERTION_FAILURE */
        || code == 0xC0000602 /* FAIL_FAST_EXCEPTION */
        || code == 0xC0000094 /* INTEGER_DIVIDE_BY_ZERO */
        || code == 0x80000003 /* BREAKPOINT (debugger) */
        || code == 0xC00000FD /* STACK_OVERFLOW */
        || code == 0xC000001D /* ILLEGAL_INSTRUCTION */
        || code == 0x40000015 /* FATAL_APP_EXIT */;
    if (!isFatal) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    appendExceptionBeaconLine("veh_first_chance", info);
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

}  // namespace miacode::app::entry
