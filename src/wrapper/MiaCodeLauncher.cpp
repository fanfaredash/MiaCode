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
// DLL search — no /DELAYLOAD or AddDllDirectory plumbing required.

#include <windows.h>

#include <stdio.h>

#include <string>

namespace {

constexpr const wchar_t* kAppSubdir = L"app";
constexpr const wchar_t* kRealExeName = L"MiaCode.exe";

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
        const std::wstring msg = L"Real MiaCode.exe not found at:\n" + realExe;
        ::MessageBoxW(nullptr, msg.c_str(), L"MiaCode", MB_ICONERROR);
        return 1;
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
