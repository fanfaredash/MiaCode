#include "common/OperationLog.h"

#include "common/DebugLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <atomic>
#include <cstring>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace miacode::oplog {

namespace {

// Per-thread chain top. POD pointer initialised to nullptr — no
// constructor, no static-init-order trap. Safe to read from a Scope
// constructor that runs during library init.
thread_local Scope* g_chainTop = nullptr;

// =============================================================
// Phase 4 — heap-free shadow buffer (see header for design notes).
// =============================================================
constexpr int kMaxShadowThreads = 32;
constexpr int kMaxShadowFrames = 16;
constexpr int kShadowOpCap = 96;
constexpr size_t kShadowPathCap = 1024;

struct ShadowThreadSlot {
    std::atomic<bool> active{false};
    quint64 threadId{0};
    std::atomic<int> depth{0};
    char frames[kMaxShadowFrames][kShadowOpCap]{};
};

// Static pool — zero-initialised at process start, no constructors,
// safe from any thread. Slots are claimed lazily on first MC_OP push
// from each thread (lock-free fetch_add).
ShadowThreadSlot g_shadowPool[kMaxShadowThreads];
std::atomic<int> g_shadowPoolNext{0};
thread_local ShadowThreadSlot* tls_shadowSlot = nullptr;

#ifdef Q_OS_WIN
// Path to the shadow log file. Captured at installShadow() time as a
// wide-char array so the SEH/signal/terminate handlers can write to
// disk without calling any heap-allocating Qt API.
wchar_t g_shadowPathW[kShadowPathCap] = {};
std::atomic<bool> g_shadowInstalled{false};
#endif

quint64 currentThreadId() noexcept
{
#ifdef Q_OS_WIN
    return static_cast<quint64>(::GetCurrentThreadId());
#else
    return 0;
#endif
}

// Lazy per-thread slot allocation. First call from each thread claims
// a slot via atomic fetch_add. Returns nullptr if the pool is
// exhausted (more than kMaxShadowThreads have ever pushed) — pool
// exhaustion is silent, that thread's chain is just absent from the
// dump.
ShadowThreadSlot* acquireShadowSlot() noexcept
{
    if (tls_shadowSlot != nullptr) {
        return tls_shadowSlot;
    }
    const int idx = g_shadowPoolNext.fetch_add(1, std::memory_order_relaxed);
    if (idx >= kMaxShadowThreads) {
        return nullptr;
    }
    ShadowThreadSlot* slot = &g_shadowPool[idx];
    slot->threadId = currentThreadId();
    // Release-store last so a concurrent flush reader either sees
    // active=false (skip slot) or active=true with valid threadId.
    slot->active.store(true, std::memory_order_release);
    tls_shadowSlot = slot;
    return slot;
}

void shadowPush(const char* op) noexcept
{
    ShadowThreadSlot* slot = acquireShadowSlot();
    if (slot == nullptr) {
        return;
    }
    const int d = slot->depth.load(std::memory_order_relaxed);
    if (d >= kMaxShadowFrames) {
        // Stack too deep — increment so balanced pop still works.
        // The frame is dropped from the dump.
        slot->depth.store(d + 1, std::memory_order_release);
        return;
    }
    char* dst = slot->frames[d];
    const char* src = op != nullptr ? op : "";
    int i = 0;
    while (i < kShadowOpCap - 1 && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
    // Release-store ensures the frame copy above is visible before
    // the depth bump becomes visible to other threads (the SEH reader).
    slot->depth.store(d + 1, std::memory_order_release);
}

void shadowPop() noexcept
{
    ShadowThreadSlot* slot = tls_shadowSlot;
    if (slot == nullptr) {
        return;
    }
    const int d = slot->depth.load(std::memory_order_relaxed);
    if (d > 0) {
        slot->depth.store(d - 1, std::memory_order_release);
    }
}

#ifdef Q_OS_WIN

// Inline pure-Win32 helpers used only from flushShadowToDisk(). All
// stack-only, no CRT calls beyond strlen/memcpy.

void writeBytes(HANDLE handle, const char* data, DWORD len) noexcept
{
    DWORD written = 0;
    ::WriteFile(handle, data, len, &written, nullptr);
}

void writeStr(HANDLE handle, const char* s) noexcept
{
    if (s == nullptr) {
        return;
    }
    DWORD len = 0;
    while (s[len] != '\0' && len < 256) {
        ++len;
    }
    writeBytes(handle, s, len);
}

void writeUint(HANDLE handle, quint64 v) noexcept
{
    char rev[24];
    int rn = 0;
    if (v == 0) {
        rev[rn++] = '0';
    } else {
        while (v > 0 && rn < 24) {
            rev[rn++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        }
    }
    char fwd[24];
    int n = 0;
    while (rn > 0 && n < 24) {
        fwd[n++] = rev[--rn];
    }
    writeBytes(handle, fwd, static_cast<DWORD>(n));
}

#endif

QString shortFile(const std::source_location& loc)
{
    const char* filePath = loc.file_name();
    if (filePath == nullptr) {
        return QString();
    }
    const QString full = QString::fromUtf8(filePath);
    return QFileInfo(full).fileName();
}

QString locationSuffix(const std::source_location& loc)
{
    const QString file = shortFile(loc);
    if (file.isEmpty()) {
        return QString();
    }
    return QStringLiteral(" at=%1:%2").arg(file).arg(static_cast<unsigned>(loc.line()));
}

QString notesSuffix(const QStringList& notes)
{
    if (notes.isEmpty()) {
        return QString();
    }
    return QStringLiteral(" notes=[%1]").arg(notes.join(QStringLiteral("; ")));
}

QString currentExceptionWhat()
{
    // Called from ~Scope() during stack unwind. On MSVC,
    // std::current_exception() returns a null pointer when invoked
    // before the matching catch handler is entered (per MSVC docs:
    // "outside of exception handling, returns an empty exception_ptr").
    // Itanium ABI implementations are more permissive but we target
    // MSVC-first, so callers must treat empty as "what unavailable".
    //
    // Reliable what() capture happens in the catch handler via
    // appendFatalMessage, which runs after the exception is "currently
    // handled" — see DebugLog.cpp::appendFatalMessage and the
    // currentExceptionDetail() helper in app/main.cpp.
    std::exception_ptr ep = std::current_exception();
    if (!ep) {
        return QString();
    }
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception& ex) {
        const QString what = QString::fromUtf8(ex.what()).trimmed();
        return what.isEmpty() ? QStringLiteral("std::exception") : what;
    } catch (...) {
        return QStringLiteral("unknown non-std exception");
    }
}

}  // namespace

QString currentChain()
{
    QStringList parts;
    for (const Scope* s = g_chainTop; s != nullptr; s = s->parent_) {
        parts << QString::fromUtf8(s->op_);
    }
    return parts.join(QStringLiteral(" \xe2\x86\x90 "));  // ' ← ' (UTF-8)
}

Scope::Scope(const char* op, std::source_location loc) noexcept
    : op_(op != nullptr ? op : "")
    , entryUncaught_(std::uncaught_exceptions())
    , parent_(g_chainTop)
    , location_(loc)
{
    g_chainTop = this;
    shadowPush(op_);
}

Scope::Scope(const char* op, QString detail, std::source_location loc) noexcept
    : op_(op != nullptr ? op : "")
    , entryUncaught_(std::uncaught_exceptions())
    , parent_(g_chainTop)
    , location_(loc)
{
    g_chainTop = this;
    shadowPush(op_);
    if (!detail.isEmpty()) {
        notes_.append(std::move(detail));
    }
}

Scope::~Scope() noexcept
{
    // Unlink before any logging so that re-entrance into log code
    // sees a chain that excludes this (about-to-die) scope.
    if (g_chainTop == this) {
        g_chainTop = parent_;
    }
    shadowPop();

    if (emitted_) {
        return;
    }

    const int now = std::uncaught_exceptions();
    if (now <= entryUncaught_) {
        // Normal exit — happy path stays silent.
        return;
    }

    // Exception unwind. g_chainTop now points at our parent; we prepend
    // ourselves to the chain string so the failing op is at the head.
    QString parentChain = currentChain();
    QString chain = parentChain.isEmpty()
                        ? QString::fromUtf8(op_)
                        : QStringLiteral("%1 \xe2\x86\x90 %2")
                              .arg(QString::fromUtf8(op_), parentChain);
    QString what;
    try {
        what = currentExceptionWhat();
    } catch (...) {
        // Defensive — currentExceptionWhat itself shouldn't throw,
        // but ~Scope is noexcept so we belt-and-brace.
    }
    // Omit the what= field when MSVC's std::current_exception() returns
    // null during pre-catch unwind. The catch-handler path
    // (appendFatalMessage) captures what() reliably; the destructor
    // provides the chain context.
    QString whatField = what.isEmpty()
                            ? QStringLiteral("(unwind)")
                            : what;
    QString payload = QStringLiteral("op=%1 chain=%2 reason=exception what=%3%4%5")
                          .arg(QString::fromUtf8(op_),
                               chain,
                               whatField,
                               locationSuffix(location_),
                               notesSuffix(notes_));
    try {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Operation,
            QStringLiteral("failed"),
            payload,
            /*force=*/true);
    } catch (...) {
        // Swallow — destructor is noexcept; letting an exception out
        // during unwind would call std::terminate.
    }
}

void Scope::note(QString kv) noexcept
{
    if (kv.isEmpty()) {
        return;
    }
    try {
        notes_.append(std::move(kv));
    } catch (...) {
        // QStringList allocation failed — drop the note silently.
    }
}

void Scope::fail(QString reason) noexcept
{
    if (emitted_) {
        return;
    }
    emitted_ = true;
    try {
        QString chain = currentChain();
        QString payload = QStringLiteral("op=%1 chain=%2 reason=%3%4%5")
                              .arg(QString::fromUtf8(op_),
                                   chain,
                                   reason.isEmpty() ? QStringLiteral("(unspecified)")
                                                    : reason,
                                   locationSuffix(location_),
                                   notesSuffix(notes_));
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Operation,
            QStringLiteral("failed"),
            payload,
            /*force=*/true);
    } catch (...) {
        // fail() is noexcept; drop on failure to log.
    }
}

void installShadow()
{
#ifdef Q_OS_WIN
    if (g_shadowInstalled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    // Resolve the shadow log path eagerly. We can use Qt freely here
    // (called at startup, heap is intact) — once cached as wchar_t,
    // the SEH path doesn't touch Qt or the C heap.
    //
    // Path resolution order (most-specific first):
    //   1. MIACODE_OPLOG_SHADOW_PATH — explicit per-process override.
    //   2. MIACODE_LOG_DIR/miacode_op_chain_<pid>.log — co-locates every
    //      process's shadow next to the parent's other log files. The
    //      parent supervisor sets MIACODE_LOG_DIR for spawned children
    //      (worker, export worker), so all shadow files for a session
    //      land in the same directory and the parent can auto-collate
    //      crashed children's chains via PID glob.
    //   3. <%TEMP%>/miacode_op_chain_<pid>.log — last-resort fallback
    //      when no log dir is configured (e.g. very early startup).
    QString path = qEnvironmentVariable("MIACODE_OPLOG_SHADOW_PATH").trimmed();
    if (path.isEmpty()) {
        const QString logDir = qEnvironmentVariable("MIACODE_LOG_DIR").trimmed();
        if (!logDir.isEmpty()) {
            const qint64 pid = QCoreApplication::applicationPid();
            path = QDir(QDir::cleanPath(logDir)).filePath(
                QStringLiteral("miacode_op_chain_%1.log").arg(pid));
        }
    }
    if (path.isEmpty()) {
        const qint64 pid = QCoreApplication::applicationPid();
        path = QDir(QDir::tempPath()).filePath(
            QStringLiteral("miacode_op_chain_%1.log").arg(pid));
    }

    const std::wstring wpath = path.toStdWString();
    if (wpath.empty() || wpath.size() + 1 > kShadowPathCap) {
        // Path too long — disable, leave g_shadowPathW empty.
        g_shadowInstalled.store(false, std::memory_order_release);
        return;
    }
    std::memcpy(g_shadowPathW, wpath.c_str(),
                (wpath.size() + 1) * sizeof(wchar_t));

    // Pre-create parent directory so the SEH writer doesn't have to.
    QDir().mkpath(QFileInfo(path).absolutePath());
#endif
}

void flushShadowToDisk()
{
#ifdef Q_OS_WIN
    if (!g_shadowInstalled.load(std::memory_order_acquire)) {
        return;
    }
    if (g_shadowPathW[0] == L'\0') {
        return;
    }

    HANDLE handle = ::CreateFileW(
        g_shadowPathW,
        GENERIC_WRITE,
        FILE_SHARE_READ,  // allow concurrent reads (e.g. tail -f)
        nullptr,
        CREATE_ALWAYS,    // overwrite previous shadow on each crash
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    // Header line — always present, helps confirm the file is fresh.
    writeStr(handle, "miacode operation breadcrumb shadow\n");
    writeStr(handle, "pid=");
    writeUint(handle, static_cast<quint64>(::GetCurrentProcessId()));
    writeStr(handle, " utc=");
    SYSTEMTIME st;
    ::GetSystemTime(&st);
    writeUint(handle, st.wYear);    writeStr(handle, "-");
    writeUint(handle, st.wMonth);   writeStr(handle, "-");
    writeUint(handle, st.wDay);     writeStr(handle, "T");
    writeUint(handle, st.wHour);    writeStr(handle, ":");
    writeUint(handle, st.wMinute);  writeStr(handle, ":");
    writeUint(handle, st.wSecond);  writeStr(handle, ".");
    writeUint(handle, st.wMilliseconds);
    writeStr(handle, "Z\n");

    int activeCount = 0;
    for (int i = 0; i < kMaxShadowThreads; ++i) {
        ShadowThreadSlot& slot = g_shadowPool[i];
        if (!slot.active.load(std::memory_order_acquire)) {
            continue;
        }
        const int d = slot.depth.load(std::memory_order_acquire);
        if (d <= 0) {
            continue;
        }
        ++activeCount;
        writeStr(handle, "\nthread tid=");
        writeUint(handle, slot.threadId);
        writeStr(handle, " depth=");
        const int writeD = d > kMaxShadowFrames ? kMaxShadowFrames : d;
        writeUint(handle, static_cast<quint64>(writeD));
        if (d > kMaxShadowFrames) {
            writeStr(handle, " (truncated_from=");
            writeUint(handle, static_cast<quint64>(d));
            writeStr(handle, ")");
        }
        writeStr(handle, "\n");
        // Walk leaf-first: the topmost (most recently pushed) frame
        // is the function that was running when we crashed.
        for (int f = writeD - 1; f >= 0; --f) {
            writeStr(handle, "  [");
            writeUint(handle, static_cast<quint64>(f));
            writeStr(handle, "] ");
            const char* op = slot.frames[f];
            DWORD oplen = 0;
            while (oplen < kShadowOpCap && op[oplen] != '\0') {
                ++oplen;
            }
            writeBytes(handle, op, oplen);
            writeStr(handle, "\n");
        }
    }

    if (activeCount == 0) {
        writeStr(handle, "\n(no active operation chains)\n");
    }

    ::FlushFileBuffers(handle);
    ::CloseHandle(handle);
#endif
}

void writeStartupBeacon(const char* versionUtf8)
{
#ifdef Q_OS_WIN
    // Resolve path with the SAME priority as installShadow so the
    // beacon lands next to the eventual shadow log. Done independently
    // here because crash_recovery::install (which calls installShadow)
    // hasn't run yet — that's the whole point of the beacon.
    QString path = qEnvironmentVariable("MIACODE_OPLOG_SHADOW_PATH").trimmed();
    QString resolvedDir;
    if (!path.isEmpty()) {
        // Caller passed an explicit shadow path; put the beacon in the
        // same directory with a fixed-prefix name.
        resolvedDir = QFileInfo(path).absolutePath();
    }
    if (resolvedDir.isEmpty()) {
        const QString logDir = qEnvironmentVariable("MIACODE_LOG_DIR").trimmed();
        if (!logDir.isEmpty()) {
            resolvedDir = QDir::cleanPath(logDir);
        }
    }
    if (resolvedDir.isEmpty()) {
        resolvedDir = QDir::tempPath();
    }
    QDir().mkpath(resolvedDir);

    const qint64 pid = static_cast<qint64>(::GetCurrentProcessId());
    const QString beaconPath = QDir(resolvedDir).filePath(
        QStringLiteral("miacode_startup_beacon_%1.txt").arg(pid));
    const std::wstring wpath = beaconPath.toStdWString();
    if (wpath.empty()) {
        return;
    }

    HANDLE handle = ::CreateFileW(
        wpath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    writeStr(handle, "pid=");
    writeUint(handle, static_cast<quint64>(pid));
    writeStr(handle, " utc=");
    SYSTEMTIME st;
    ::GetSystemTime(&st);
    writeUint(handle, st.wYear);    writeStr(handle, "-");
    writeUint(handle, st.wMonth);   writeStr(handle, "-");
    writeUint(handle, st.wDay);     writeStr(handle, "T");
    writeUint(handle, st.wHour);    writeStr(handle, ":");
    writeUint(handle, st.wMinute);  writeStr(handle, ":");
    writeUint(handle, st.wSecond);  writeStr(handle, "Z");
    if (versionUtf8 != nullptr && versionUtf8[0] != '\0') {
        writeStr(handle, " version=");
        DWORD vlen = 0;
        while (vlen < 64 && versionUtf8[vlen] != '\0') {
            ++vlen;
        }
        writeBytes(handle, versionUtf8, vlen);
    }
    writeStr(handle, "\nlogdir=");
    const QByteArray dirUtf8 = resolvedDir.toUtf8();
    writeBytes(handle, dirUtf8.constData(),
               static_cast<DWORD>(dirUtf8.size()));
    writeStr(handle, "\n");

    ::FlushFileBuffers(handle);
    ::CloseHandle(handle);
#else
    Q_UNUSED(versionUtf8);
#endif
}

}  // namespace miacode::oplog
