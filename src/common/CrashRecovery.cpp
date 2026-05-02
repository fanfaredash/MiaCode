#include "common/CrashRecovery.h"

#include "common/DebugLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <atomic>
#include <csignal>
#include <cstring>
#include <exception>
#include <new>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace miacode::crash_recovery {

namespace {

void logCrashRecovery(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("crash_recovery"),
        payload);
}

// Double-buffered snapshot. The GUI thread writes to the inactive buffer,
// then atomically flips the active index. Crash handlers read the active
// index and the matching buffer — never observe a half-written state.
//
// Buffer cap is generous (4 MB). Real chart text is typically a few KB to
// tens of KB; even the most extreme custom charts stay under 1 MB. Truncation
// is a documented loss case (logged once per truncation).
//
// Path is stored as a fixed-size wide-char array so the crash handler doesn't
// need to allocate to call CreateFileW. MAX_PATH * 4 covers the long-path
// case (Windows allows up to ~32 K with `\\?\` prefix; we don't go that far,
// but 1024 wide chars is plenty).
struct Snapshot {
    static constexpr size_t kBufferCap = 4 * 1024 * 1024;  // 4 MB
    static constexpr size_t kPathCap = 1024;               // wide chars
    char buf[2][kBufferCap];
    size_t bufSize[2] = {0, 0};
#ifdef Q_OS_WIN
    wchar_t pathW[2][kPathCap] = {};
#endif
    std::atomic<int> activeIdx{-1};  // -1 = no snapshot yet
};

// Heap-allocate the snapshot to avoid bloating the executable's BSS.
// Created in install(); never freed (lives until process exit).
Snapshot* g_snapshot = nullptr;

std::atomic<bool> g_installed{false};

#ifdef Q_OS_WIN
LPTOP_LEVEL_EXCEPTION_FILTER g_previousSehFilter = nullptr;
#endif
// Chained handlers — saved during install() so terminate / signal can
// fall through to whatever was already set up (e.g. Qt's default
// terminate handler), preserving the existing crash-reporting flow
// rather than swallowing it.
std::terminate_handler g_previousTerminate = nullptr;
typedef void (*SignalHandlerFn)(int);
SignalHandlerFn g_previousSigabrt = SIG_DFL;
SignalHandlerFn g_previousSigsegv = SIG_DFL;
SignalHandlerFn g_previousSigfpe = SIG_DFL;
SignalHandlerFn g_previousSigill = SIG_DFL;

// Pure Win32, no Qt, no CRT heap. Safe to call from a corrupted process.
// Reads the active snapshot index, copies the buffer to the recovery
// file via CreateFileW + WriteFile + CloseHandle. On failure, silently
// returns — there's nothing meaningful to do at crash time.
//
// Encoding: writes raw UTF-8 bytes (no BOM, no transformation). Must
// match the read side in readRecoveryFile / applyOpenedDocumentState.
//
// Critical: the parent directory MUST already exist. We do not call
// CreateDirectoryW from the crash handler — it allocates kernel
// objects, which is risky in a crashing process. The directory is
// created eagerly by prepareForChart() on chart open, well before
// any updateSnapshot can fire.
void flushSnapshotToDisk()
{
    if (g_snapshot == nullptr) {
        return;
    }
    const int idx = g_snapshot->activeIdx.load(std::memory_order_acquire);
    if (idx < 0 || idx > 1) {
        return;
    }
#ifdef Q_OS_WIN
    const wchar_t* path = g_snapshot->pathW[idx];
    if (path[0] == L'\0') {
        return;
    }
    const size_t size = g_snapshot->bufSize[idx];
    if (size == 0) {
        return;
    }
    const char* data = g_snapshot->buf[idx];

    HANDLE handle = ::CreateFileW(
        path,
        GENERIC_WRITE,
        0,                     // no sharing — exclusive write
        nullptr,
        CREATE_ALWAYS,         // overwrite if exists
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        // Most likely cause: the recovery directory was never created
        // (prepareForChart wasn't called or its mkpath failed). Nothing
        // we can safely do from a crash handler — skip.
        return;
    }
    DWORD remaining = static_cast<DWORD>(size);
    const char* cursor = data;
    while (remaining > 0) {
        DWORD written = 0;
        if (!::WriteFile(handle, cursor, remaining, &written, nullptr) || written == 0) {
            break;
        }
        remaining -= written;
        cursor += written;
    }
    ::FlushFileBuffers(handle);
    ::CloseHandle(handle);
#else
    Q_UNUSED(idx);
#endif
}

#ifdef Q_OS_WIN
LONG WINAPI sehTopLevelFilter(EXCEPTION_POINTERS* info)
{
    flushSnapshotToDisk();
    // Chain to the previous filter (typically Qt's WinMain default,
    // which can produce a minidump). EXCEPTION_CONTINUE_SEARCH lets
    // the OS run the WER / debugger flow as normal.
    if (g_previousSehFilter != nullptr) {
        return g_previousSehFilter(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void terminateHandler()
{
    flushSnapshotToDisk();
    // Chain to whatever handler was registered before us — typically
    // Qt's default or libc's, both of which produce useful diagnostics
    // (Qt may write a minidump). Only fall through to abort() if we
    // were the first handler in the chain. Skip self-recursion in case
    // some other code re-installed us as the previous handler.
    if (g_previousTerminate != nullptr
        && g_previousTerminate != &terminateHandler) {
        g_previousTerminate();
    }
    std::abort();
}

void signalHandler(int sig)
{
    flushSnapshotToDisk();
    // Restore the default handler then re-raise so the process dies
    // with the original signal disposition. SIG_DFL on Windows for
    // SIGABRT/SIGSEGV terminates the process and lets WER take over —
    // avoids infinite loops if the same signal fires during our
    // flushSnapshotToDisk and avoids swallowing the crash.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

}  // namespace

void install()
{
    if (g_installed.exchange(true, std::memory_order_acq_rel)) {
        return;  // already installed
    }
    if (g_snapshot == nullptr) {
        // Heap allocate so the 8 MB doesn't sit in BSS for every process.
        // std::nothrow because we're called early; a bad_alloc here would
        // abort startup, which is worse than disabling crash recovery.
        g_snapshot = new (std::nothrow) Snapshot();
        if (g_snapshot == nullptr) {
            logCrashRecovery("install_failed", QStringLiteral("reason=alloc_failed"));
            return;
        }
    }
#ifdef Q_OS_WIN
    g_previousSehFilter = ::SetUnhandledExceptionFilter(&sehTopLevelFilter);
#endif
    // Capture previous handlers so we can chain to them after our flush.
    // std::set_terminate returns the previous handler; signal() returns
    // the previous handler too (or SIG_ERR on failure).
    g_previousTerminate = std::set_terminate(&terminateHandler);
    // SIGABRT covers abort() — the most common cause of "killed without
    // notice" in MSVC builds (CRT assertions, std::terminate fallthrough
    // via std::set_terminate not being honoured by some abort paths).
    g_previousSigabrt = std::signal(SIGABRT, &signalHandler);
    // SIGSEGV / SIGFPE / SIGILL on Windows under MSVC are usually
    // delivered as SEH exceptions and routed through the SEH filter
    // above, but install signal handlers as a belt-and-braces.
    g_previousSigsegv = std::signal(SIGSEGV, &signalHandler);
    g_previousSigfpe  = std::signal(SIGFPE,  &signalHandler);
    g_previousSigill  = std::signal(SIGILL,  &signalHandler);
    logCrashRecovery("installed",
                     QStringLiteral("prev_terminate=%1 prev_sigabrt=%2")
                         .arg(g_previousTerminate != nullptr ? 1 : 0)
                         .arg(g_previousSigabrt != SIG_DFL && g_previousSigabrt != SIG_ERR ? 1 : 0));
}

bool prepareForChart(const QString& chartFilePath)
{
    if (chartFilePath.isEmpty()) {
        return false;
    }
    const QString recoveryPath = crashRecoveryFilePath(chartFilePath);
    if (recoveryPath.isEmpty()) {
        logCrashRecovery("prepare_skipped",
                         QStringLiteral("reason=path_unresolved chart=%1")
                             .arg(chartFilePath));
        return false;
    }
    const QString recoveryDir = QFileInfo(recoveryPath).absolutePath();
    if (recoveryDir.isEmpty()) {
        return false;
    }
    QDir dir;
    const bool ok = dir.mkpath(recoveryDir);
    if (!ok) {
        // Most likely cause: read-only filesystem (chart inside a .zip
        // mount, or the user opened a chart from a media-library path
        // without write perms). Crash recovery is effectively disabled
        // for this chart — log loudly so the user has visibility.
        logCrashRecovery("prepare_failed",
                         QStringLiteral("reason=mkpath_failed dir=%1")
                             .arg(recoveryDir));
        return false;
    }
    logCrashRecovery("prepare_ok",
                     QStringLiteral("dir=%1").arg(recoveryDir));
    return true;
}

void updateSnapshot(const QString& chartFilePath, const QString& utf8DocumentText)
{
    if (g_snapshot == nullptr) {
        return;
    }
    if (chartFilePath.isEmpty()) {
        clearSnapshot();
        return;
    }
    const QString recoveryPath = crashRecoveryFilePath(chartFilePath);
    if (recoveryPath.isEmpty()) {
        return;
    }
    const QByteArray utf8 = utf8DocumentText.toUtf8();
    if (utf8.isEmpty()) {
        return;
    }
    const size_t copyBytes = static_cast<size_t>(utf8.size());
    if (copyBytes > Snapshot::kBufferCap) {
        // Document is bigger than our static cap. INVALIDATE — not
        // skip — because any prior valid snapshot for this chart is
        // now stale (user has typed new content beyond the cap, but
        // we'd otherwise leave the smaller-and-older snapshot active
        // and a crash would write that as a "recovery", masquerading
        // stale state as current). Clearing activeIdx ensures the
        // crash handler writes nothing rather than something wrong.
        clearSnapshot();
        static std::atomic<bool> warnedTruncate{false};
        if (!warnedTruncate.exchange(true, std::memory_order_acq_rel)) {
            logCrashRecovery("snapshot_invalidated_too_large",
                             QStringLiteral("size=%1 cap=%2 chart=%3")
                                 .arg(copyBytes)
                                 .arg(Snapshot::kBufferCap)
                                 .arg(chartFilePath));
        }
        return;
    }

#ifdef Q_OS_WIN
    // Defensive directory ensure — covers the case where the user
    // somehow edits before prepareForChart was called (in practice
    // applyOpenedDocumentState fires prepareForChart synchronously
    // before any signal could land here). Cheap once warm.
    QDir().mkpath(QFileInfo(recoveryPath).absolutePath());

    const std::wstring wPath = recoveryPath.toStdWString();
    if (wPath.size() + 1 > Snapshot::kPathCap) {
        // Path too long for our static buffer. Skip — extremely rare.
        return;
    }
#endif

    const int curIdx = g_snapshot->activeIdx.load(std::memory_order_acquire);
    const int nextIdx = (curIdx == 0) ? 1 : 0;

    // Write to the inactive buffer.
    std::memcpy(g_snapshot->buf[nextIdx], utf8.constData(), copyBytes);
    g_snapshot->bufSize[nextIdx] = copyBytes;
#ifdef Q_OS_WIN
    std::memset(g_snapshot->pathW[nextIdx], 0,
                Snapshot::kPathCap * sizeof(wchar_t));
    std::memcpy(g_snapshot->pathW[nextIdx], wPath.c_str(),
                wPath.size() * sizeof(wchar_t));
#endif

    // Atomic flip — crash handler will see either the old (idx=curIdx)
    // or the new (idx=nextIdx) snapshot, never a half-written state.
    g_snapshot->activeIdx.store(nextIdx, std::memory_order_release);
}

void clearSnapshot()
{
    if (g_snapshot == nullptr) {
        return;
    }
    g_snapshot->activeIdx.store(-1, std::memory_order_release);
}

QString crashRecoveryFilePath(const QString& chartFilePath)
{
    if (chartFilePath.isEmpty()) {
        return QString();
    }
    const QString cleaned = QDir::cleanPath(chartFilePath);
    const QFileInfo info(cleaned);
    QString chartDir = info.absolutePath();
    if (chartDir.isEmpty()) {
        return QString();
    }
    QString fileName = info.fileName().trimmed();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("maidata.txt");
    }
    // Mirrors autosaveEntryDirectoryPathForFile +
    // <chartDir>/.miacode/autosave/<fileName>/<fileName>.crash_recovery
    return QDir(chartDir)
        .filePath(QStringLiteral(".miacode/autosave/%1/%1.crash_recovery")
                      .arg(fileName));
}

QByteArray readRecoveryFile(const QString& chartFilePath)
{
    // Returns the raw UTF-8 bytes the crash handler wrote (no BOM, no
    // transformation). The caller decodes with QString::fromUtf8 to
    // match the encoding contract documented in the header. If the
    // file isn't present or can't be read, returns an empty QByteArray.
    const QString path = crashRecoveryFilePath(chartFilePath);
    if (path.isEmpty()) {
        return QByteArray();
    }
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

bool deleteRecoveryFile(const QString& chartFilePath)
{
    const QString path = crashRecoveryFilePath(chartFilePath);
    if (path.isEmpty()) {
        return true;  // nothing to delete
    }
    QFile file(path);
    if (!file.exists()) {
        return true;
    }
    const bool removed = file.remove();
    if (removed) {
        logCrashRecovery("recovery_file_deleted",
                         QStringLiteral("path=%1").arg(path));
    } else {
        logCrashRecovery("recovery_file_delete_failed",
                         QStringLiteral("path=%1 err=%2")
                             .arg(path)
                             .arg(file.errorString()));
    }
    return removed;
}

}  // namespace miacode::crash_recovery
