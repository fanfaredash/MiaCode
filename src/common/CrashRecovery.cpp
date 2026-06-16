#include "common/CrashRecovery.h"

#include "common/DebugLog.h"
#include "common/OperationLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
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

    // Phase 4 — also flush the heap-free op-chain shadow so the call
    // chain at the moment of the crash is recoverable. Heap-safe and
    // no-op if installShadow() was never called.
    miacode::oplog::flushShadowToDisk();
}

#ifdef Q_OS_WIN
void appendSehBeacon(const char* tag, EXCEPTION_POINTERS* info) noexcept
{
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        miacode::oplog::appendStartupBeaconLine(tag);
        return;
    }
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "crash/%s code=0x%08lx flags=0x%08lx addr=%p tid=%lu params=%lu",
        tag,
        static_cast<unsigned long>(record->ExceptionCode),
        static_cast<unsigned long>(record->ExceptionFlags),
        record->ExceptionAddress,
        static_cast<unsigned long>(::GetCurrentThreadId()),
        static_cast<unsigned long>(record->NumberParameters));
    miacode::oplog::appendStartupBeaconLine(buf);
}

LONG WINAPI sehTopLevelFilter(EXCEPTION_POINTERS* info)
{
    appendSehBeacon("seh_top_level", info);
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
    miacode::oplog::appendStartupBeaconLine("crash/terminate_handler");
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
    char buf[80];
    std::snprintf(buf, sizeof(buf), "crash/signal_handler sig=%d", sig);
    miacode::oplog::appendStartupBeaconLine(buf);
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
    // Phase 4 — pre-resolve the op-chain shadow log path so the SEH
    // filter (which calls flushShadowToDisk) doesn't need any heap.
    miacode::oplog::installShadow();
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
    MC_OP("crash_recovery::prepareForChart");
    _mc_op_.note(QStringLiteral("chart=%1").arg(chartFilePath));
    if (chartFilePath.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty chartFilePath"));
        return false;
    }
    const QString recoveryPath = crashRecoveryFilePath(chartFilePath);
    if (recoveryPath.isEmpty()) {
        logCrashRecovery("prepare_skipped",
                         QStringLiteral("reason=path_unresolved chart=%1")
                             .arg(chartFilePath));
        _mc_op_.fail(QStringLiteral("path_unresolved"));
        return false;
    }
    const QString recoveryDir = QFileInfo(recoveryPath).absolutePath();
    if (recoveryDir.isEmpty()) {
        _mc_op_.fail(QStringLiteral("recoveryDir empty"));
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
        _mc_op_.fail(QStringLiteral("mkpath_failed dir=%1").arg(recoveryDir));
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
    // <chartDir>/.miacode/.autosave/<fileName>/<fileName>.crash_recovery
    return QDir(chartDir)
        .filePath(QStringLiteral(".miacode/.autosave/%1/%1.crash_recovery")
                      .arg(fileName));
}

QByteArray readRecoveryFile(const QString& chartFilePath)
{
    MC_OP("crash_recovery::readRecoveryFile");
    _mc_op_.note(QStringLiteral("chart=%1").arg(chartFilePath));
    // Returns the raw UTF-8 bytes the crash handler wrote (no BOM, no
    // transformation). The caller decodes with QString::fromUtf8 to
    // match the encoding contract documented in the header. If the
    // file isn't present or can't be read, returns an empty QByteArray.
    const QString path = crashRecoveryFilePath(chartFilePath);
    if (path.isEmpty()) {
        // Not a real failure — chart path was empty, common at startup
        // before any chart is open. Stay silent.
        return QByteArray();
    }
    QFile file(path);
    if (!file.exists()) {
        // Not a failure — no recovery state to read. Stay silent.
        return QByteArray();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        _mc_op_.fail(QStringLiteral("open_failed err=%1").arg(file.errorString()));
        return QByteArray();
    }
    return file.readAll();
}

namespace {

std::atomic<bool> g_sessionMarkerEnabled{false};

// Directory holding per-instance session markers. Each concurrently
// running GUI process owns exactly one file in here, named by its PID, so
// two instances never share or clobber each other's marker. The old design
// used a single `session.marker`, which made a second instance read the
// first (still-running) instance's marker and wrongly report it as an
// abnormal exit — and then delete it, corrupting the first instance's state.
QString sessionMarkerDir()
{
    const QString configRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configRoot.isEmpty()) {
        return QString();
    }
    return QDir(configRoot).filePath(QStringLiteral("sessions"));
}

// This process's own marker file: <sessionsDir>/session-<pid>.marker.
QString ownSessionMarkerFilePath()
{
    const QString dir = sessionMarkerDir();
    if (dir.isEmpty()) {
        return QString();
    }
    return QDir(dir).filePath(
        QStringLiteral("session-%1.marker")
            .arg(QCoreApplication::applicationPid()));
}

#ifdef Q_OS_WIN
// Creation time of THIS process. Recorded in the marker so liveness checks
// can reject PID reuse (a recycled PID now owned by an unrelated process).
// 0 if it can't be queried.
quint64 ownProcessCreationTime()
{
    FILETIME creation, exitT, kernel, user;
    if (::GetProcessTimes(::GetCurrentProcess(), &creation, &exitT, &kernel, &user)) {
        return (static_cast<quint64>(creation.dwHighDateTime) << 32)
             | static_cast<quint64>(creation.dwLowDateTime);
    }
    return 0;
}

// True iff a process with `pid` is currently running AND (when a non-zero
// creation time was recorded) its creation time matches `storedCreation`.
// The creation-time match defends against PID reuse across reboots: a dead
// instance's PID may have been recycled by an unrelated live process, which
// must NOT count as "the marker's owner is still alive".
bool processIsLive(qint64 pid, quint64 storedCreation)
{
    if (pid <= 0) {
        return false;
    }
    HANDLE handle = ::OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        static_cast<DWORD>(pid));
    if (handle == nullptr) {
        // No such process (or unopenable) — for a same-user GUI marker this
        // means the owner is gone.
        return false;
    }
    bool live = false;
    // WAIT_TIMEOUT => the process object is non-signaled => still running.
    if (::WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) {
        if (storedCreation == 0) {
            live = true;  // nothing to compare against; assume live.
        } else {
            FILETIME creation, exitT, kernel, user;
            if (::GetProcessTimes(handle, &creation, &exitT, &kernel, &user)) {
                const quint64 ct =
                    (static_cast<quint64>(creation.dwHighDateTime) << 32)
                  | static_cast<quint64>(creation.dwLowDateTime);
                live = (ct == storedCreation);
            } else {
                live = true;
            }
        }
    }
    ::CloseHandle(handle);
    return live;
}
#else
quint64 ownProcessCreationTime()
{
    return 0;
}

bool processIsLive(qint64 pid, quint64 /*storedCreation*/)
{
    if (pid <= 0) {
        return false;
    }
    // Best-effort on POSIX: signal 0 probes existence without delivering a
    // signal. EPERM means the process exists but is owned by someone else
    // (treat as alive). PID-reuse is not defended here (creation time is
    // Windows-only), which is acceptable: this binary's marker bug is
    // Windows-specific and the worst POSIX outcome is one missed recovery.
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}
#endif

// Serialize this process's marker (pid + creation time + chart path).
QByteArray sessionMarkerPayload(const QString& chartFilePath)
{
    QString text;
    text += QStringLiteral("pid=%1\n").arg(QCoreApplication::applicationPid());
    text += QStringLiteral("created=%1\n").arg(ownProcessCreationTime());
    text += QStringLiteral("chart=%1\n").arg(chartFilePath);
    return text.toUtf8();
}

// Capture-once snapshot of the chart paths recorded by markers whose owning
// process was already DEAD when this process started — i.e. instances that
// terminated without a clean close. Markers belonging to still-running peers
// (or to this process) are left untouched. Dead markers are deleted as they
// are captured so they're never claimed twice. The static is initialized
// thread-safely; mutation afterwards (consume) is GUI-thread only.
QSet<QString>& abandonedSessionCharts()
{
    static QSet<QString> captured = []() -> QSet<QString> {
        QSet<QString> result;
        const QString dirPath = sessionMarkerDir();
        if (dirPath.isEmpty()) {
            return result;
        }
        QDir dir(dirPath);
        const QFileInfoList entries = dir.entryInfoList(
            QStringList() << QStringLiteral("session-*.marker"),
            QDir::Files);
        for (const QFileInfo& entry : entries) {
            QFile file(entry.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString content = QString::fromUtf8(file.readAll());
            file.close();

            qint64 pid = 0;
            quint64 created = 0;
            QString chart;
            const QStringList lines = content.split(QLatin1Char('\n'));
            for (const QString& rawLine : lines) {
                const QString line = rawLine.trimmed();
                if (line.startsWith(QStringLiteral("pid="))) {
                    pid = line.mid(4).toLongLong();
                } else if (line.startsWith(QStringLiteral("created="))) {
                    created = line.mid(8).toULongLong();
                } else if (line.startsWith(QStringLiteral("chart="))) {
                    chart = line.mid(6);
                }
            }

            if (processIsLive(pid, created)) {
                // A concurrent, still-running instance — not abandoned.
                continue;
            }
            // Owner is gone: this marker is a genuine abnormal-exit remnant.
            if (!chart.isEmpty()) {
                result.insert(QDir::cleanPath(chart));
            }
            file.remove();
            logCrashRecovery("abandoned_session_marker_found",
                             QStringLiteral("chart=%1 pid=%2")
                                 .arg(chart)
                                 .arg(pid));
        }
        return result;
    }();
    return captured;
}

}  // namespace

void setSessionMarkerEnabled(bool enabled)
{
    g_sessionMarkerEnabled.store(enabled, std::memory_order_release);
}

void updateSessionMarker(const QString& chartFilePath)
{
    if (!g_sessionMarkerEnabled.load(std::memory_order_acquire)) {
        return;
    }
    // Snapshot any abandoned (dead-owner) markers before we create or mutate
    // this process's own marker, so a stale same-PID remnant from a crashed
    // previous run is recovered rather than silently overwritten.
    abandonedSessionCharts();
    if (chartFilePath.isEmpty()) {
        clearSessionMarker();
        return;
    }
    const QString markerPath = ownSessionMarkerFilePath();
    if (markerPath.isEmpty()) {
        return;
    }
    QDir().mkpath(QFileInfo(markerPath).absolutePath());
    QSaveFile file(markerPath);
    if (!file.open(QIODevice::WriteOnly)) {
        logCrashRecovery("session_marker_write_failed",
                         QStringLiteral("path=%1 err=%2")
                             .arg(markerPath)
                             .arg(file.errorString()));
        return;
    }
    const QByteArray payload = sessionMarkerPayload(chartFilePath);
    if (file.write(payload) != payload.size() || !file.commit()) {
        logCrashRecovery("session_marker_write_failed",
                         QStringLiteral("path=%1 err=%2")
                             .arg(markerPath)
                             .arg(file.errorString()));
    }
}

void clearSessionMarker()
{
    if (!g_sessionMarkerEnabled.load(std::memory_order_acquire)) {
        return;
    }
    // Capture abandoned markers before mutating the sessions dir.
    abandonedSessionCharts();
    const QString markerPath = ownSessionMarkerFilePath();
    if (markerPath.isEmpty()) {
        return;
    }
    QFile file(markerPath);
    if (file.exists() && !file.remove()) {
        logCrashRecovery("session_marker_delete_failed",
                         QStringLiteral("path=%1 err=%2")
                             .arg(markerPath)
                             .arg(file.errorString()));
    }
}

bool consumeAbandonedSessionChartMatch(const QString& chartFilePath)
{
    if (!g_sessionMarkerEnabled.load(std::memory_order_acquire)
        || chartFilePath.isEmpty()) {
        return false;
    }
    // GUI-thread only — no synchronization needed on the captured set.
    QSet<QString>& abandoned = abandonedSessionCharts();
    if (abandoned.isEmpty()) {
        return false;
    }
    const QString cleaned = QDir::cleanPath(chartFilePath);
    for (auto it = abandoned.begin(); it != abandoned.end(); ++it) {
        if (it->compare(cleaned, Qt::CaseInsensitive) == 0) {
            // Consume so each abandoned chart is offered recovery exactly
            // once, even if multiple dead instances are detected at startup.
            abandoned.erase(it);
            logCrashRecovery("abandoned_session_marker_consumed",
                             QStringLiteral("chart=%1").arg(chartFilePath));
            return true;
        }
    }
    return false;
}

bool deleteRecoveryFile(const QString& chartFilePath)
{
    MC_OP("crash_recovery::deleteRecoveryFile");
    _mc_op_.note(QStringLiteral("chart=%1").arg(chartFilePath));
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
        _mc_op_.fail(QStringLiteral("remove_failed err=%1").arg(file.errorString()));
    }
    return removed;
}

}  // namespace miacode::crash_recovery
