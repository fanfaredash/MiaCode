// Tiny launcher exe that ships at the dist-package root and forwards
// execution to the real MiaCode.exe under app/. Lets the package keep
// Qt / BASS / Qt6Multimedia-ffmpeg / D3D shader DLLs inside app/ rather
// than scattering ~40 DLLs across the dist root the user sees in File
// Explorer.
//
// The wrapper waits on the child and propagates its exit code so that
// callers using `start "" /wait %~dp0MiaCode.exe` (the .bat launchers)
// still observe the real application's exit status. The child process
// has its own application directory = app/, so its statically-imported
// Qt6Core.dll, bass.dll, etc. resolve from app/ via Windows' standard
// DLL search.
//
// Preflight probe (Layer 2 of the DLL-deployment defence). Before
// spawning the child, walk the PE import tables of app/MiaCode.exe and
// every transitively-imported app-local DLL (BFS), then LoadLibraryExW
// each unique dependency name with the exact search policy the child
// will see (AddDllDirectory(appDir) + USER_DIRS + SYSTEM32). First
// failure raises a bilingual MessageBox that names the missing DLL,
// classifies the cause (VC++ runtime / Qt / Qt platform plugin /
// DirectX / BASS / Generic), and prints actionable instructions plus
// the LoadLibrary error code. Catches:
//   * MSVCP140-class ABI / version mismatches (the beta20→48 silent
//     crash regression resolved in beta49)
//   * Partial zip extraction missing app/Qt6*.dll
//   * Antivirus false-positive removal
//   * OneDrive cloud-only sentinel files
//   * "No Qt platform plugin could be initialized" race
// The probe runs in-process and uses LoadLibraryExW with full import
// resolution, so its verdict is logically equivalent to what the child
// loader would do — no false positives from forwarder DLLs, no false
// negatives from missing transitive deps.

#include <windows.h>

#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr const wchar_t* kAppSubdir = L"app";
constexpr const wchar_t* kRealExeName = L"MiaCode.exe";

// =============================================================
// String helpers — Win32-only, no STL <algorithm> dependency.
// =============================================================

std::wstring toLowerW(std::wstring s)
{
    for (auto& c : s) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return s;
}

std::string toLowerA(std::string s)
{
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

std::wstring asciiToWide(const std::string& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

bool startsWithA(const std::string& s, const char* prefix)
{
    const size_t n = std::char_traits<char>::length(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] != prefix[i]) return false;
    }
    return true;
}

bool startsWithW(const std::wstring& s, const wchar_t* prefix)
{
    const size_t n = std::char_traits<wchar_t>::length(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] != prefix[i]) return false;
    }
    return true;
}

// =============================================================
// PE import table parser. Memory-maps the file read-only, walks
// the IMAGE_IMPORT_DESCRIPTOR array, returns the lowercased ASCII
// basenames of every implicitly-imported DLL.
// =============================================================

bool getImportedDllNames(const std::wstring& path, std::vector<std::string>& out)
{
    HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    HANDLE hMap = ::CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (hMap == nullptr) {
        ::CloseHandle(hFile);
        return false;
    }
    LARGE_INTEGER fileSize{};
    if (!::GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) {
        ::CloseHandle(hMap);
        ::CloseHandle(hFile);
        return false;
    }
    const size_t fileSz = static_cast<size_t>(fileSize.QuadPart);
    LPVOID base = ::MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (base == nullptr) {
        ::CloseHandle(hMap);
        ::CloseHandle(hFile);
        return false;
    }

    bool ok = false;
    do {
        if (fileSz < sizeof(IMAGE_DOS_HEADER)) break;
        auto dos = static_cast<PIMAGE_DOS_HEADER>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) break;
        if (dos->e_lfanew < 0
            || static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > fileSz) {
            break;
        }
        auto nt = reinterpret_cast<PIMAGE_NT_HEADERS64>(
            static_cast<BYTE*>(base) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) break;
        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) break;
        if (IMAGE_DIRECTORY_ENTRY_IMPORT >= nt->OptionalHeader.NumberOfRvaAndSizes) {
            ok = true;  // valid PE with no imports
            break;
        }

        const DWORD importDirRva =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (importDirRva == 0) {
            ok = true;
            break;
        }

        auto sections = IMAGE_FIRST_SECTION(nt);
        const WORD sectionCount = nt->FileHeader.NumberOfSections;
        auto rvaToOffset = [&](DWORD rva) -> DWORD {
            for (WORD i = 0; i < sectionCount; ++i) {
                const DWORD sectStart = sections[i].VirtualAddress;
                DWORD sectSize = sections[i].Misc.VirtualSize;
                if (sectSize == 0) sectSize = sections[i].SizeOfRawData;
                const DWORD sectEnd = sectStart + sectSize;
                if (rva >= sectStart && rva < sectEnd) {
                    return rva - sectStart + sections[i].PointerToRawData;
                }
            }
            return 0;
        };

        DWORD importTableOffset = rvaToOffset(importDirRva);
        if (importTableOffset == 0
            || importTableOffset + sizeof(IMAGE_IMPORT_DESCRIPTOR) > fileSz) {
            break;
        }

        auto descriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
            static_cast<BYTE*>(base) + importTableOffset);

        // Bounded iteration — terminator is an all-zero descriptor, but
        // we also cap by file size in case the PE is truncated/corrupt.
        for (size_t guard = 0; guard < 4096; ++guard) {
            if (reinterpret_cast<BYTE*>(descriptor) + sizeof(IMAGE_IMPORT_DESCRIPTOR) >
                static_cast<BYTE*>(base) + fileSz) {
                break;
            }
            if (descriptor->Name == 0) break;
            DWORD nameOffset = rvaToOffset(descriptor->Name);
            if (nameOffset != 0 && nameOffset < fileSz) {
                const char* nameStart =
                    reinterpret_cast<const char*>(static_cast<BYTE*>(base) + nameOffset);
                const size_t maxLen = fileSz - nameOffset;
                size_t len = 0;
                while (len < maxLen && len < 255 && nameStart[len] != '\0') ++len;
                if (len > 0) {
                    out.push_back(toLowerA(std::string(nameStart, len)));
                }
            }
            ++descriptor;
        }
        ok = true;
    } while (false);

    ::UnmapViewOfFile(base);
    ::CloseHandle(hMap);
    ::CloseHandle(hFile);
    return ok;
}

// =============================================================
// Launcher-side failure log.
//
// The wrapper deliberately does NOT link src/common (no Qt, no
// debug_log) — it has to start even when app/ is broken, which is the
// whole point of it existing. But a failure here is exactly the case
// where the real app never runs and therefore writes nothing at all:
// no runtime log, no startup timing, no crash beacon (those are all
// written by the child process). Remote support was left with a
// screenshot of a MessageBox and nothing else.
//
// So: an append-only line into <root>\logs\launcher.log using raw Win32
// calls only. Written on every failure path that ends in a MessageBox.
// Timestamps are UTC to match the debug_log / beacon convention.
// =============================================================

wchar_t g_launcherLogPath[MAX_PATH] = {};

void initLauncherLog(const std::wstring& rootDir)
{
    const std::wstring logDir = rootDir + L"\\logs";
    // Package already ships a logs\ directory; create it anyway so a
    // hand-assembled tree still logs. Failure is fine — the append
    // below just no-ops if the path is unusable.
    ::CreateDirectoryW(logDir.c_str(), nullptr);

    const std::wstring path = logDir + L"\\launcher.log";
    if (path.size() >= MAX_PATH) {
        // Leave g_launcherLogPath empty → logging silently disabled.
        // A too-long install path must never block the launch itself.
        return;
    }
    ::swprintf_s(g_launcherLogPath, L"%ls", path.c_str());
}

void appendLauncherLog(const wchar_t* scope, const std::wstring& payload)
{
    if (g_launcherLogPath[0] == L'\0') {
        return;
    }

    SYSTEMTIME st{};
    ::GetSystemTime(&st);

    wchar_t line[1024];
    ::swprintf_s(
        line,
        L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ ERROR pid=%lu [launcher/%ls] %ls\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        static_cast<unsigned long>(::GetCurrentProcessId()),
        scope,
        payload.c_str());

    // UTF-8 on disk so Chinese payloads survive; drop the terminating
    // NUL from the byte count so the file stays plain text.
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return;
    }
    std::vector<char> utf8(static_cast<size_t>(bytes));
    if (::WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytes, nullptr, nullptr) <= 0) {
        return;
    }

    HANDLE handle = ::CreateFileW(
        g_launcherLogPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    ::WriteFile(handle, utf8.data(), static_cast<DWORD>(bytes - 1), &written, nullptr);
    ::CloseHandle(handle);
}

// =============================================================
// Failure classifier + bilingual MessageBox.
// =============================================================

enum class DllCategory {
    VCRuntime,
    QtPlatform,
    Qt,
    DirectX,
    BASS,
    Generic,
};

DllCategory classifyDll(const std::wstring& name)
{
    const std::wstring n = toLowerW(name);
    if (startsWithW(n, L"vcruntime") || startsWithW(n, L"msvcp") || startsWithW(n, L"concrt")) {
        return DllCategory::VCRuntime;
    }
    if (n == L"qwindows.dll" || startsWithW(n, L"qmodernwindows") || startsWithW(n, L"qwindowsvistastyle")) {
        return DllCategory::QtPlatform;
    }
    if (startsWithW(n, L"qt6")) {
        return DllCategory::Qt;
    }
    if (startsWithW(n, L"d3dcompiler") || startsWithW(n, L"dxcompiler") || n == L"dxil.dll") {
        return DllCategory::DirectX;
    }
    if (startsWithW(n, L"bass")) {
        return DllCategory::BASS;
    }
    return DllCategory::Generic;
}

// Bilingual layout: simplified Chinese block on top, English block on
// bottom, separator line in between. Each block is self-contained so
// a reader can ignore the other language entirely. Both failure paths
// (missing DLL, CreateProcess refusal) render through here so the two
// dialogs stay visually identical.
void showBilingualError(const std::wstring& zh, const std::wstring& en, DWORD lastError)
{
    wchar_t errZh[96];
    ::swprintf_s(errZh, L"\n\n错误代码: 0x%08lX (%lu)", lastError, lastError);
    wchar_t errEn[96];
    ::swprintf_s(errEn, L"\n\nError code: 0x%08lX (%lu)", lastError, lastError);

    const std::wstring body =
        zh + errZh +
        L"\n\n────────────────────────────────────\n\n" +
        en + errEn;
    ::MessageBoxW(nullptr, body.c_str(), L"MiaCode",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

// Stable ASCII tags for the log line — greppable, and independent of
// the user-facing wording (which is translated and may be reworded).
const wchar_t* dllCategoryTag(DllCategory category)
{
    switch (category) {
    case DllCategory::VCRuntime:  return L"vc_runtime";
    case DllCategory::QtPlatform: return L"qt_platform_plugin";
    case DllCategory::Qt:         return L"qt_runtime";
    case DllCategory::DirectX:    return L"directx";
    case DllCategory::BASS:       return L"bass";
    case DllCategory::Generic:
    default:                      return L"generic";
    }
}

void showMissingDllMessage(const std::wstring& missingDll, DWORD lastError)
{
    const DllCategory category = classifyDll(missingDll);

    wchar_t logLine[512];
    ::swprintf_s(logLine, L"dll=%ls category=%ls code=%lu",
                 missingDll.c_str(), dllCategoryTag(category), lastError);
    appendLauncherLog(L"missing_dll", logLine);

    std::wstring zh;
    std::wstring en;
    switch (category) {
    case DllCategory::VCRuntime:
        zh = L"缺少 Visual C++ 2015-2022 运行库: " + missingDll + L"\n\n"
             L"解决方案:\n"
             L"下载并安装:\n"
             L"https://aka.ms/vs/17/release/vc_redist.x64.exe";
        en = L"Missing Visual C++ 2015-2022 Runtime: " + missingDll + L"\n\n"
             L"Solution:\n"
             L"Download and install:\n"
             L"https://aka.ms/vs/17/release/vc_redist.x64.exe";
        break;
    case DllCategory::QtPlatform:
        zh = L"缺少 Qt 平台插件: " + missingDll + L"\n\n"
             L"可能原因:\n"
             L"  1. app\\platforms\\ 子目录缺失\n"
             L"  2. 杀毒软件误判删除\n"
             L"  3. 从 OneDrive 等云端目录运行,文件尚未同步\n\n"
             L"解决方案:\n"
             L"重新解压 MiaCode-*.zip 到本地非云端目录";
        en = L"Missing Qt platform plugin: " + missingDll + L"\n\n"
             L"Possible causes:\n"
             L"  1. app\\platforms\\ subfolder is missing\n"
             L"  2. Antivirus false-positive removal\n"
             L"  3. Running from a OneDrive cloud folder with unsynced files\n\n"
             L"Solution:\n"
             L"Re-extract MiaCode-*.zip to a local non-cloud folder";
        break;
    case DllCategory::Qt:
        zh = L"缺少 Qt 运行库: " + missingDll + L"\n\n"
             L"解决方案:\n"
             L"  1. 确认 app\\ 子目录已完整解压\n"
             L"  2. 若 zip 解压中断, 请重新下载并解压\n"
             L"  3. 杀毒软件可能误删文件, 请将 MiaCode 加入排除列表";
        en = L"Missing Qt runtime: " + missingDll + L"\n\n"
             L"Solution:\n"
             L"  1. Verify the app\\ subfolder is fully extracted\n"
             L"  2. If extraction was interrupted, re-download and re-extract\n"
             L"  3. Antivirus may have removed the file — add MiaCode to exclusions";
        break;
    case DllCategory::DirectX:
        zh = L"缺少 DirectX 运行库: " + missingDll + L"\n\n"
             L"解决方案:\n"
             L"  1. 确认 app\\ 子目录已完整解压\n"
             L"  2. 安装 DirectX End-User Runtime:\n"
             L"     https://www.microsoft.com/download/details.aspx?id=35";
        en = L"Missing DirectX runtime: " + missingDll + L"\n\n"
             L"Solution:\n"
             L"  1. Verify the app\\ subfolder is fully extracted\n"
             L"  2. Install DirectX End-User Runtime:\n"
             L"     https://www.microsoft.com/download/details.aspx?id=35";
        break;
    case DllCategory::BASS:
        zh = L"缺少 BASS 音频运行库: " + missingDll + L"\n\n"
             L"解决方案:\n"
             L"重新解压 MiaCode-*.zip, 保留完整的 app\\ 子目录";
        en = L"Missing BASS audio runtime: " + missingDll + L"\n\n"
             L"Solution:\n"
             L"Re-extract MiaCode-*.zip preserving the full app\\ subfolder";
        break;
    case DllCategory::Generic:
    default:
        zh = L"缺少必要 DLL: " + missingDll + L"\n\n"
             L"解决方案:\n"
             L"  1. 重新解压 MiaCode-*.zip 到本地目录\n"
             L"  2. 确认杀毒软件未误删文件";
        en = L"Missing required DLL: " + missingDll + L"\n\n"
             L"Solution:\n"
             L"  1. Re-extract MiaCode-*.zip to a local folder\n"
             L"  2. Confirm antivirus did not remove the file";
        break;
    }

    showBilingualError(zh, en, lastError);
}

// =============================================================
// CreateProcessW failure classifier.
//
// The preflight probe above only covers *missing dependencies*. Once it
// passes we know app\MiaCode.exe exists and every imported DLL loads —
// so a CreateProcessW failure here means the OS actively REFUSED to
// start the process. Those refusals are almost always policy / security
// blocks, and a bare "Windows error 4551" gives the user nothing to act
// on. Classify the common ones into actionable bilingual text.
//
// NB: the 4550-4553 codes are the ERROR_SYSTEM_INTEGRITY_* range (code
// integrity / Smart App Control / WDAC policy). They are spelled as
// numeric literals rather than SDK macros so this stays buildable
// against older Windows SDKs that predate those definitions.
// =============================================================

enum class LaunchFailure {
    CodeIntegrityPolicy,
    AntivirusBlock,
    RestrictedByPolicy,
    AccessDenied,
    ElevationRequired,
    ArchMismatch,
    Resources,
    Generic,
};

LaunchFailure classifyLaunchFailure(DWORD err)
{
    switch (err) {
    case 4550:  // ERROR_SYSTEM_INTEGRITY_ROLLBACK_DETECTED
    case 4551:  // ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION
    case 4552:  // ERROR_SYSTEM_INTEGRITY_INVALID_POLICY
    case 4553:  // ERROR_SYSTEM_INTEGRITY_POLICY_NOT_SIGNED
        return LaunchFailure::CodeIntegrityPolicy;
    case ERROR_VIRUS_INFECTED:
    case ERROR_VIRUS_DELETED:
        return LaunchFailure::AntivirusBlock;
    case ERROR_ACCESS_DISABLED_BY_POLICY:
        return LaunchFailure::RestrictedByPolicy;
    case ERROR_ACCESS_DENIED:
        return LaunchFailure::AccessDenied;
    case ERROR_ELEVATION_REQUIRED:
        return LaunchFailure::ElevationRequired;
    case ERROR_BAD_EXE_FORMAT:
    case ERROR_EXE_MACHINE_TYPE_MISMATCH:
        return LaunchFailure::ArchMismatch;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NO_SYSTEM_RESOURCES:
        return LaunchFailure::Resources;
    default:
        return LaunchFailure::Generic;
    }
}

const wchar_t* launchFailureTag(LaunchFailure failure)
{
    switch (failure) {
    case LaunchFailure::CodeIntegrityPolicy: return L"code_integrity_policy";
    case LaunchFailure::AntivirusBlock:      return L"antivirus_block";
    case LaunchFailure::RestrictedByPolicy:  return L"restricted_by_policy";
    case LaunchFailure::AccessDenied:        return L"access_denied";
    case LaunchFailure::ElevationRequired:   return L"elevation_required";
    case LaunchFailure::ArchMismatch:        return L"arch_mismatch";
    case LaunchFailure::Resources:           return L"resources";
    case LaunchFailure::Generic:
    default:                                 return L"generic";
    }
}

void showLaunchFailureMessage(const std::wstring& realExe, DWORD lastError)
{
    const LaunchFailure failure = classifyLaunchFailure(lastError);

    wchar_t logLine[512];
    ::swprintf_s(logLine, L"code=%lu category=%ls exe=\"%ls\"",
                 lastError, launchFailureTag(failure), realExe.c_str());
    appendLauncherLog(L"create_process_failed", logLine);

    std::wstring zh;
    std::wstring en;
    switch (failure) {
    case LaunchFailure::CodeIntegrityPolicy:
        zh = L"系统安全策略阻止了 MiaCode 启动\n" + realExe + L"\n\n"
             L"程序文件本身完好, 依赖检查也已通过, 是 Windows 拒绝创建进程.\n"
             L"通常由「智能应用控制」或企业代码完整性(WDAC)策略拦截未签名程序引起.\n\n"
             L"排查方案:\n"
             L"  1. Windows 安全中心 → 应用和浏览器控制 → 智能应用控制, 查看是否已开启\n"
             L"     (注意: 该开关一旦关闭将无法再次开启, 只能重装系统恢复)\n"
             L"  2. 事件查看器 → 应用程序和服务日志 → Microsoft → Windows →\n"
             L"     CodeIntegrity → Operational, 查看具体拦截记录\n"
             L"  3. 若为公司/学校设备, 请联系 IT 管理员将 MiaCode 加入白名单";
        en = L"A system security policy blocked MiaCode from starting\n" + realExe + L"\n\n"
             L"The program files are intact and the dependency check passed —\n"
             L"Windows itself refused to create the process. This is usually\n"
             L"Smart App Control or a WDAC code-integrity policy blocking an\n"
             L"unsigned application.\n\n"
             L"What to check:\n"
             L"  1. Windows Security → App & browser control → Smart App Control\n"
             L"     (note: turning it off is permanent — only a Windows reinstall\n"
             L"      can turn it back on)\n"
             L"  2. Event Viewer → Applications and Services Logs → Microsoft →\n"
             L"     Windows → CodeIntegrity → Operational, for the block record\n"
             L"  3. On a managed device, ask IT to allow-list MiaCode";
        break;
    case LaunchFailure::AntivirusBlock:
        zh = L"杀毒软件阻止了 MiaCode 启动\n" + realExe + L"\n\n"
             L"文件被判定为威胁并拦截 —— 这是对未签名程序的常见误报.\n\n"
             L"解决方案:\n"
             L"  1. 打开杀毒软件的「保护历史 / 隔离区」, 恢复被拦截的文件\n"
             L"  2. 将 MiaCode 整个文件夹加入排除列表\n"
             L"  3. 重新解压 MiaCode-*.zip (文件可能已被删改)";
        en = L"Antivirus blocked MiaCode from starting\n" + realExe + L"\n\n"
             L"The file was flagged as a threat — a common false positive for\n"
             L"unsigned applications.\n\n"
             L"Solution:\n"
             L"  1. Open your antivirus quarantine / protection history and restore it\n"
             L"  2. Add the whole MiaCode folder to the exclusion list\n"
             L"  3. Re-extract MiaCode-*.zip (the file may have been altered)";
        break;
    case LaunchFailure::RestrictedByPolicy:
        zh = L"组策略禁止运行 MiaCode\n" + realExe + L"\n\n"
             L"设备上的软件限制策略 / AppLocker 规则不允许从该位置运行程序.\n\n"
             L"解决方案:\n"
             L"  1. 将 MiaCode 移动到策略允许的目录 (如 C:\\Program Files\\)\n"
             L"  2. 若为公司/学校设备, 请联系 IT 管理员";
        en = L"Group Policy is blocking MiaCode\n" + realExe + L"\n\n"
             L"A software restriction policy / AppLocker rule forbids running\n"
             L"programs from this location.\n\n"
             L"Solution:\n"
             L"  1. Move MiaCode to an allowed folder (e.g. C:\\Program Files\\)\n"
             L"  2. On a managed device, contact your IT administrator";
        break;
    case LaunchFailure::AccessDenied:
        zh = L"没有权限启动 MiaCode\n" + realExe + L"\n\n"
             L"解决方案:\n"
             L"  1. 将 MiaCode 移出受保护目录 (如 Program Files, 系统盘根目录)\n"
             L"  2. 检查该文件夹的读取/执行权限\n"
             L"  3. 杀毒软件或安全软件可能正在锁定文件";
        en = L"Access denied while starting MiaCode\n" + realExe + L"\n\n"
             L"Solution:\n"
             L"  1. Move MiaCode out of a protected folder (Program Files, drive root)\n"
             L"  2. Check read/execute permissions on the folder\n"
             L"  3. Antivirus or security software may be locking the file";
        break;
    case LaunchFailure::ElevationRequired:
        zh = L"MiaCode 需要管理员权限才能启动\n" + realExe + L"\n\n"
             L"解决方案:\n"
             L"右键点击 MiaCode.exe → 以管理员身份运行";
        en = L"MiaCode requires administrator privileges to start\n" + realExe + L"\n\n"
             L"Solution:\n"
             L"Right-click MiaCode.exe → Run as administrator";
        break;
    case LaunchFailure::ArchMismatch:
        zh = L"程序文件损坏或架构不匹配\n" + realExe + L"\n\n"
             L"解决方案:\n"
             L"  1. 确认使用的是 64 位 Windows\n"
             L"  2. 重新下载并解压 MiaCode-*.zip (文件可能下载不完整)";
        en = L"Corrupt executable or architecture mismatch\n" + realExe + L"\n\n"
             L"Solution:\n"
             L"  1. Confirm you are on 64-bit Windows\n"
             L"  2. Re-download and re-extract MiaCode-*.zip (it may be truncated)";
        break;
    case LaunchFailure::Resources:
        zh = L"系统资源不足, 无法启动 MiaCode\n" + realExe + L"\n\n"
             L"解决方案:\n"
             L"关闭部分正在运行的程序后重试";
        en = L"Not enough system resources to start MiaCode\n" + realExe + L"\n\n"
             L"Solution:\n"
             L"Close some running programs and try again";
        break;
    case LaunchFailure::Generic:
    default:
        zh = L"无法启动 MiaCode\n" + realExe + L"\n\n"
             L"程序文件存在且依赖检查已通过, 但 Windows 拒绝创建进程.\n\n"
             L"排查方案:\n"
             L"  1. 在命令提示符执行 net helpmsg <下方错误代码> 查看系统说明\n"
             L"  2. 暂时关闭杀毒软件后重试\n"
             L"  3. 重新解压 MiaCode-*.zip 到本地非云端目录";
        en = L"Failed to start MiaCode\n" + realExe + L"\n\n"
             L"The executable exists and its dependencies loaded, but Windows\n"
             L"refused to create the process.\n\n"
             L"What to try:\n"
             L"  1. Run: net helpmsg <the error code below>, for the system description\n"
             L"  2. Temporarily disable antivirus and retry\n"
             L"  3. Re-extract MiaCode-*.zip to a local, non-cloud folder";
        break;
    }

    showBilingualError(zh, en, lastError);
}

// =============================================================
// BFS preflight probe.
// =============================================================

bool isApiSetForwarder(const std::string& name)
{
    // api-ms-win-*.dll and ext-ms-*.dll are inbox API set forwarders —
    // always provided by Windows itself, no need to probe.
    return startsWithA(name, "api-ms-win-") || startsWithA(name, "ext-ms-");
}

bool runPreflightProbe(const std::wstring& appDir)
{
    // Replicate the DLL search policy the child MiaCode.exe will see:
    // app dir (= appDir from the launcher's POV — we have to add it
    // explicitly because the launcher's app dir is the dist root) plus
    // System32. Use SetDefaultDllDirectories so any LoadLibrary[Ex]
    // we issue follows the same policy whether or not we pass flags.
    typedef DLL_DIRECTORY_COOKIE (WINAPI* AddDllDirectoryFn)(PCWSTR);
    typedef BOOL (WINAPI* SetDefaultDllDirectoriesFn)(DWORD);

    HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
    if (k32 != nullptr) {
        auto addDir = reinterpret_cast<AddDllDirectoryFn>(
            ::GetProcAddress(k32, "AddDllDirectory"));
        auto setDef = reinterpret_cast<SetDefaultDllDirectoriesFn>(
            ::GetProcAddress(k32, "SetDefaultDllDirectories"));
        if (addDir != nullptr && setDef != nullptr) {
            addDir(appDir.c_str());
            setDef(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
    }

    std::queue<std::wstring> q;
    std::unordered_set<std::string> visited;  // lowercased basenames

    auto enqueueIfExists = [&](const std::wstring& path) {
        if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            q.push(path);
        }
    };

    // Seed: the real exe plus the plugin DLLs Qt loads dynamically
    // (via QLibrary / QPluginLoader, not implicit imports — so they
    // don't show up in MiaCode.exe's PE import table).
    enqueueIfExists(appDir + L"\\MiaCode.exe");
    enqueueIfExists(appDir + L"\\platforms\\qwindows.dll");
    enqueueIfExists(appDir + L"\\multimedia\\ffmpegmediaplugin.dll");
    enqueueIfExists(appDir + L"\\multimedia\\windowsmediaplugin.dll");

    const std::wstring appDirLower = toLowerW(appDir);

    while (!q.empty()) {
        const std::wstring current = std::move(q.front());
        q.pop();

        // Basename for the visited set.
        const wchar_t* sep = ::wcsrchr(current.c_str(), L'\\');
        const std::wstring basenameW = sep ? std::wstring(sep + 1) : current;
        std::string basenameAscii;
        basenameAscii.reserve(basenameW.size());
        for (wchar_t c : basenameW) {
            basenameAscii.push_back(static_cast<char>(c & 0xFF));
        }
        const std::string basenameLower = toLowerA(basenameAscii);
        if (visited.count(basenameLower)) continue;
        visited.insert(basenameLower);

        std::vector<std::string> imports;
        if (!getImportedDllNames(current, imports)) {
            // PE we couldn't parse — skip rather than fail the probe.
            // Real loader will fail loudly if this matters at runtime.
            continue;
        }

        for (const std::string& impName : imports) {
            if (visited.count(impName)) continue;
            if (isApiSetForwarder(impName)) {
                visited.insert(impName);
                continue;
            }

            const std::wstring impNameW = asciiToWide(impName);
            HMODULE h = ::LoadLibraryExW(
                impNameW.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (h == nullptr) {
                const DWORD err = ::GetLastError();
                showMissingDllMessage(impNameW, err);
                return false;
            }

            // Record the actual resolved path. If it's inside app/, queue
            // it for transitive walk; if it's in System32 / elsewhere,
            // we've already proven it loads — no need to recurse.
            wchar_t modPath[MAX_PATH] = {};
            const DWORD pathLen = ::GetModuleFileNameW(h, modPath, MAX_PATH);
            ::FreeLibrary(h);
            if (pathLen > 0 && pathLen < MAX_PATH) {
                const std::wstring modPathStr(modPath, pathLen);
                const std::wstring modPathLower = toLowerW(modPathStr);
                if (modPathLower.size() >= appDirLower.size()
                    && modPathLower.compare(0, appDirLower.size(), appDirLower) == 0) {
                    q.push(modPathStr);
                }
            }
        }
    }
    return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    wchar_t modulePath[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        ::MessageBoxW(nullptr, L"Failed to resolve launcher path.", L"MiaCode", MB_ICONERROR);
        return 1;
    }
    wchar_t* lastSep = ::wcsrchr(modulePath, L'\\');
    if (lastSep == nullptr) {
        ::MessageBoxW(nullptr, L"Launcher path has no directory component.", L"MiaCode", MB_ICONERROR);
        return 1;
    }
    *lastSep = L'\0';

    const std::wstring rootDir(modulePath);
    const std::wstring appDir = rootDir + L"\\" + kAppSubdir;
    const std::wstring realExe = appDir + L"\\" + kRealExeName;

    // Resolve the log path as soon as rootDir is known, so every failure
    // path below leaves a durable record next to the app.
    initLauncherLog(rootDir);

    if (::GetFileAttributesW(realExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        appendLauncherLog(L"real_exe_missing", L"exe=\"" + realExe + L"\"");
        const std::wstring msg =
            L"找不到 app\\MiaCode.exe\n" + realExe + L"\n\n"
            L"解决方案:\n"
            L"重新解压 MiaCode-*.zip, 保留完整的 app\\ 子目录"
            L"\n\n────────────────────────────────────\n\n"
            L"Real MiaCode.exe not found at:\n" + realExe + L"\n\n"
            L"Solution:\n"
            L"Re-extract MiaCode-*.zip preserving the full app\\ subfolder";
        ::MessageBoxW(nullptr, msg.c_str(), L"MiaCode",
                      MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        return 1;
    }

    // Layer 2 preflight: PE-import-driven transitive DLL probe. Any
    // missing dependency raises a friendly bilingual MessageBox naming
    // the missing DLL and returns before we even try CreateProcessW.
    // Suppress this with MIACODE_SKIP_PREFLIGHT=1 (developer escape
    // hatch — never set this in a release build's launcher script).
    DWORD skipBufLen = ::GetEnvironmentVariableW(L"MIACODE_SKIP_PREFLIGHT", nullptr, 0);
    if (skipBufLen == 0) {
        if (!runPreflightProbe(appDir)) {
            return 1;
        }
    }

    // CreateProcessW requires a writable command-line buffer.
    std::wstring cmdLineCopy = ::GetCommandLineW();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(
            realExe.c_str(),
            cmdLineCopy.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi)) {
        const DWORD err = ::GetLastError();
        showLaunchFailureMessage(realExe, err);
        return static_cast<int>(err);
    }

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
}
