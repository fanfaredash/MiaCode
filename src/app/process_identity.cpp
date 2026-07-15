#include "MainEntrypoints.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/GpuDevicePolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
// P2 — reference the exported high-performance GPU hint symbols (defined in
// gpu_high_performance_hint.cpp) so the linker keeps them in the export table
// (the NVIDIA Optimus / AMD PowerXpress driver shims read them there) and so
// their values can be echoed into the startup log.
extern "C" {
extern DWORD NvOptimusEnablement;
extern int AmdPowerXpressRequestHighPerformance;
}
#endif

// P0/P2/P3 — process startup diagnostics, emitted as one bundle:
//   startup/process_identity : which exe is really running (pid/ppid, packaged
//                              app-dir detection, launcher parent, cwd, argv).
//   startup/gpu_hint         : whether the high-performance GPU hint symbols are
//                              compiled in (P2).
//   startup/gpu_policy       : the resolved internal GPU device policy (P3).
//
// These are emitted very early in main() (phase=boot), which lands them in the
// app-local logs/ dir BEFORE a chart binds. When a chart opens, the runtime log
// directory rebinds to the chart's .miacode/logs/ (setSessionProjectLogDirectory
// drains + reopens the writer), so the boot copy is NOT in the log a user
// collects per-chart. They are therefore re-emitted (phase=log_dir_rebound)
// right after each rebind so the collected project log is self-contained.

namespace miacode::app::entry {

namespace {

#ifdef Q_OS_WIN
DWORD parentProcessId(DWORD pid)
{
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD ppid = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                ppid = entry.th32ParentProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }
    ::CloseHandle(snapshot);
    return ppid;
}

QString processNameForId(DWORD pid)
{
    if (pid == 0) {
        return QString();
    }
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return QString();
    }
    QString name;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                name = QString::fromWCharArray(entry.szExeFile);
                break;
            }
        } while (::Process32NextW(snapshot, &entry));
    }
    ::CloseHandle(snapshot);
    return name;
}
#endif

QString quotedArgv(const QStringList& args)
{
    QStringList tail = args.mid(1);  // arg 0 is the exe path, logged separately
    for (QString& arg : tail) {
        arg = QStringLiteral("\"%1\"").arg(arg);
    }
    return tail.join(QStringLiteral(" "));
}

QString deriveProcessRole(const QStringList& args)
{
    if (args.contains(QStringLiteral("--export-video-worker"))) {
        return QStringLiteral("export_worker");
    }
    if (args.contains(QStringLiteral("--export-video"))) {
        return QStringLiteral("cli_export");
    }
    return QStringLiteral("gui");
}

void logProcessIdentity(const QStringList& rawArgs, const QString& processRole, const QString& phase)
{
    const QString exePath = QCoreApplication::applicationFilePath();
    const QDir exeDir(QCoreApplication::applicationDirPath());
    const bool underAppSubdir =
        exeDir.dirName().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0;
    QString packageRoot = QStringLiteral("(n/a)");
    if (underAppSubdir) {
        QDir parent(exeDir);
        if (parent.cdUp()) {
            packageRoot = parent.absolutePath();
        }
    }

    QString ppidField = QStringLiteral("unknown");
    QString parentName = QStringLiteral("unknown");
    bool launchedByLauncher = false;
#ifdef Q_OS_WIN
    const DWORD ppid = parentProcessId(::GetCurrentProcessId());
    ppidField = QString::number(static_cast<qulonglong>(ppid));
    const QString parent = processNameForId(ppid);
    if (!parent.isEmpty()) {
        parentName = parent;
        launchedByLauncher =
            parent.compare(QStringLiteral("MiaCodeLauncher.exe"), Qt::CaseInsensitive) == 0;
    }
#endif

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("startup/process_identity"),
        QStringLiteral(
            "phase=%1 role=%2 pid=%3 ppid=%4 parent_process=%5 launched_by_launcher=%6 "
            "packaged_app_dir=%7 package_root=%8 exe_dir=%9 exe=%10 cwd=%11 argv=[%12]")
            .arg(phase)
            .arg(processRole)
            .arg(QCoreApplication::applicationPid())
            .arg(ppidField)
            .arg(parentName)
            .arg(launchedByLauncher ? 1 : 0)
            .arg(underAppSubdir ? 1 : 0)
            .arg(packageRoot)
            .arg(exeDir.absolutePath())
            .arg(exePath)
            .arg(QDir::currentPath())
            .arg(quotedArgv(rawArgs)));
}

void logGpuHint(const QString& phase)
{
#ifdef Q_OS_WIN
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("startup/gpu_hint"),
        QStringLiteral(
            "phase=%1 nvidia_optimus=0x%2 amd_powerxpress=%3 exported=1 "
            "note=process_level_preference_not_precise_binding")
            .arg(phase)
            .arg(NvOptimusEnablement, 0, 16)
            .arg(AmdPowerXpressRequestHighPerformance));
#else
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("startup/gpu_hint"),
        QStringLiteral("phase=%1 exported=0 platform=non_windows").arg(phase));
#endif
}

}  // namespace

void logProcessStartupDiagnostics(const QString& phase)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    const QStringList rawArgs = QCoreApplication::arguments();
    const QString processRole = deriveProcessRole(rawArgs);
    logProcessIdentity(rawArgs, processRole, phase);   // P0
    logGpuHint(phase);                                 // P2
    miacode::gpu::logResolvedGpuPolicy(                // P3 (enumeration cached)
        miacode::gpu::resolveGpuPolicyOnce(), processRole);
}

}  // namespace miacode::app::entry
