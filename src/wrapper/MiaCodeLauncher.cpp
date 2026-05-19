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

void showMissingDllMessage(const std::wstring& missingDll, DWORD lastError)
{
    // Bilingual layout: simplified Chinese block on top, English block on
    // bottom, separator line in between. Each block is self-contained so
    // a reader can ignore the other language entirely.
    std::wstring zh;
    std::wstring en;
    switch (classifyDll(missingDll)) {
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

    if (::GetFileAttributesW(realExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
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
        wchar_t buf[512];
        ::swprintf_s(buf, L"Failed to launch:\n%ls\n\nWindows error %lu", realExe.c_str(), err);
        ::MessageBoxW(nullptr, buf, L"MiaCode", MB_ICONERROR);
        return static_cast<int>(err);
    }

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
}
