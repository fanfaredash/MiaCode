#include "DebugLog.h"

#include "DebugOptions.h"
#include "OperationLog.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <mach-o/dyld.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace miacode::debug_log {
namespace {

QMutex& logMutex()
{
    static QMutex mutex;
    return mutex;
}

QMutex& projectLogDirectoryMutex()
{
    static QMutex mutex;
    return mutex;
}

QString& sessionProjectLogDirectoryStorage()
{
    static QString path;
    return path;
}

bool channelEnabled(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return miacode::debug_options::runtimeDebugOutputEnabled();
    case Channel::Audio:
        return miacode::debug_options::audioDebugOutputEnabled();
    case Channel::Export:
        return miacode::debug_options::exportDebugOutputEnabled();
    case Channel::StartupTiming:
        return miacode::debug_options::startupTimingEnabled();
    case Channel::PreviewProfile:
        return miacode::debug_options::previewProfileOutputEnabled();
    case Channel::Fatal:
        return true;
    case Channel::Operation:
        // Operation breadcrumbs only fire on failure paths and are
        // designed to be silent on the happy path. Always-on so the
        // chain is captured whenever something went wrong, regardless
        // of whether --debug is set; the sparse cadence makes this
        // safe for log volume.
        return true;
    case Channel::PvMemory:
        return miacode::debug_options::runtimeDebugOutputEnabled();
    }
    return false;
}

QString channelLabel(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return QStringLiteral("runtime");
    case Channel::Audio:
        return QStringLiteral("audio");
    case Channel::Export:
        return QStringLiteral("export");
    case Channel::StartupTiming:
        return QStringLiteral("startup");
    case Channel::Fatal:
        return QStringLiteral("fatal");
    case Channel::PreviewProfile:
        return QStringLiteral("preview_profile");
    case Channel::Operation:
        return QStringLiteral("op");
    case Channel::PvMemory:
        return QStringLiteral("pv_memory");
    }
    return QStringLiteral("unknown");
}

QString channelFileName(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return QStringLiteral("miacode_runtime_debug.log");
    case Channel::Audio:
        return QStringLiteral("miacode_audio_debug.log");
    case Channel::Export:
        return QStringLiteral("miacode_video_export.log");
    case Channel::StartupTiming:
        return QStringLiteral("miacode_startup_timing.log");
    case Channel::Fatal:
        return QStringLiteral("miacode_fatal.log");
    case Channel::PreviewProfile:
        return QStringLiteral("miacode_preview_profile_summary.txt");
    case Channel::Operation:
        return QStringLiteral("miacode_operation.log");
    case Channel::PvMemory:
        return QStringLiteral("miacode_pv_memory_debug.log");
    }
    return QStringLiteral("miacode_debug.log");
}

QString channelPathOverride(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return qEnvironmentVariable("MIACODE_RUNTIME_LOG_PATH").trimmed();
    case Channel::Audio:
        return qEnvironmentVariable("MIACODE_AUDIO_LOG_PATH").trimmed();
    case Channel::Export:
        return qEnvironmentVariable("MIACODE_EXPORT_LOG_PATH").trimmed();
    case Channel::StartupTiming:
        return qEnvironmentVariable("MIACODE_STARTUP_LOG_PATH").trimmed();
    case Channel::Fatal:
        return qEnvironmentVariable("MIACODE_FATAL_LOG_PATH").trimmed();
    case Channel::PreviewProfile:
        return qEnvironmentVariable("MIACODE_PREVIEW_PROFILE_PATH").trimmed();
    case Channel::Operation:
        return qEnvironmentVariable("MIACODE_OPERATION_LOG_PATH").trimmed();
    case Channel::PvMemory:
        return qEnvironmentVariable("MIACODE_PV_MEMORY_LOG_PATH").trimmed();
    }
    return QString();
}

QString resolvedOverridePath(Channel channel, const QString& overridePath)
{
    const QString trimmed = overridePath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const bool looksLikeDirectory = trimmed.endsWith(QLatin1Char('/')) || trimmed.endsWith(QLatin1Char('\\'));
    const QString cleanPath = QDir::cleanPath(trimmed);
    const QFileInfo info(cleanPath);
    const bool looksLikeDirectoryName = !info.exists()
        && info.suffix().isEmpty()
        && !info.fileName().contains(QLatin1Char('.'));
    if (looksLikeDirectory || (info.exists() && info.isDir()) || looksLikeDirectoryName) {
        return QDir(cleanPath).filePath(channelFileName(channel));
    }
    return cleanPath;
}

bool shouldWrite(Channel channel, bool force)
{
    return force || channelEnabled(channel);
}

// MIACODE_SKIP_ASYNCLOG_FLUSH is a process-lifetime constant (a launch-time
// diagnostic bypass for a historical Win10-22H2 singleton-construction fault).
// Read it ONCE and cache, instead of hitting the global environment lock on
// every appendText / reset / shutdown call.
bool skipAsyncLogFlush()
{
    static const bool value =
        qEnvironmentVariableIntValue("MIACODE_SKIP_ASYNCLOG_FLUSH") == 1;
    return value;
}

// Fixed-width severity token rendered into every appendLine() line.
const char* levelToken(Level level)
{
    switch (level) {
    case Level::Trace: return "TRACE";
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO ";
    case Level::Warn:  return "WARN ";
    case Level::Error: return "ERROR";
    case Level::Fatal: return "FATAL";
    }
    return "INFO ";
}

// Process id is constant for the run; cache it once. Lets co-located processes
// (export worker + main app sharing MIACODE_LOG_DIR write the same fixed-name
// channel files) be told apart line-by-line.
qint64 cachedProcessId()
{
    static const qint64 pid = QCoreApplication::applicationPid();
    return pid;
}

// Numeric current-thread id. Matches the value the heap-free crash shadow dumps
// via GetCurrentThreadId on Windows, so channel lines and the shadow share one
// tid namespace. Lets GUI- vs render-thread lines (the leak gauge) be attributed.
quint64 currentThreadIdNumeric()
{
#ifdef Q_OS_WIN
    return static_cast<quint64>(::GetCurrentThreadId());
#else
    return static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId()));
#endif
}

// Coalesced queue-overflow gap marker. On overflow we drop the OLDEST queued
// entries (so the worker keeps the lead-up context it is about to write) and
// emit ONE marker per channel per drain recording how many lines were lost —
// so a reader sees the gap instead of a silently-continuous stream.
QByteArray buildDropMarker(Channel channel, quint64 droppedCount)
{
    const QString bracket = channelLabel(channel) + QStringLiteral("/asynclog");
    QString line =
        QStringLiteral("%1 %2 pid=%3 tid=%4 [%5] dropped=%6 reason=queue_overflow")
            .arg(timestampString(), QString::fromLatin1(levelToken(Level::Warn)))
            .arg(cachedProcessId())
            .arg(currentThreadIdNumeric())
            .arg(bracket)
            .arg(droppedCount);
    line.append(QLatin1Char('\n'));
    return line.toUtf8();
}

void ensureParentDirectory(const QString& path)
{
    const QFileInfo info(path);
    const QString dirPath = info.absolutePath();
    if (!dirPath.isEmpty()) {
        QDir().mkpath(dirPath);
    }
}

QString executableDirectoryPath()
{
    if (QCoreApplication::instance() != nullptr) {
        const QString appDir = QCoreApplication::applicationDirPath().trimmed();
        if (!appDir.isEmpty()) {
            return QDir::cleanPath(appDir);
        }
    }

#ifdef Q_OS_WIN
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD pathLength = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (pathLength > 0 && pathLength < MAX_PATH) {
        return QFileInfo(QString::fromWCharArray(modulePath, pathLength)).absolutePath();
    }
#elif defined(Q_OS_MAC)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) == -1 && size > 0) {
        QByteArray buffer(static_cast<int>(size), '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            return QFileInfo(QString::fromLocal8Bit(buffer.constData())).absolutePath();
        }
    }
#elif defined(Q_OS_UNIX)
    QByteArray buffer(4096, '\0');
    const ssize_t pathLength = ::readlink("/proc/self/exe", buffer.data(), static_cast<size_t>(buffer.size() - 1));
    if (pathLength > 0) {
        buffer[static_cast<int>(pathLength)] = '\0';
        return QFileInfo(QString::fromLocal8Bit(buffer.constData())).absolutePath();
    }
#endif

    return QString();
}

QString defaultDebugLogDirectory()
{
    const QString executableDir = executableDirectoryPath();
    if (executableDir.isEmpty()) {
        return QString();
    }
    return QDir(executableDir).filePath(QStringLiteral("logs"));
}

QString quotedLogValue(QString value)
{
    value.replace(QLatin1Char('"'), QLatin1Char('\''));
    return QStringLiteral("\"%1\"").arg(value);
}

void appendCrashBreadcrumbPathHint(const QString& normalizedProjectLogDirectory)
{
    if (normalizedProjectLogDirectory.isEmpty()) {
        return;
    }
    const bool forceForFocusedCrashDiag =
        miacode::debug_options::previewHudPaintDiagnosticsEnabled();
    if (!forceForFocusedCrashDiag
        && !miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }

    const qint64 pid = QCoreApplication::applicationPid();
    const QString explicitShadowPath =
        qEnvironmentVariable("MIACODE_OPLOG_SHADOW_PATH").trimmed();
    const QString envLogDir = qEnvironmentVariable("MIACODE_LOG_DIR").trimmed();

    QString source;
    QString breadcrumbDir;
    QString opChainPath;
    if (!explicitShadowPath.isEmpty()) {
        source = QStringLiteral("MIACODE_OPLOG_SHADOW_PATH");
        opChainPath = QDir::cleanPath(explicitShadowPath);
        breadcrumbDir = QFileInfo(opChainPath).absolutePath();
    } else if (!envLogDir.isEmpty()) {
        source = QStringLiteral("MIACODE_LOG_DIR");
        breadcrumbDir = QDir::cleanPath(envLogDir);
        opChainPath = QDir(breadcrumbDir).filePath(
            QStringLiteral("miacode_op_chain_%1.log").arg(pid));
    } else {
        source = QStringLiteral("TEMP");
        breadcrumbDir = QDir::tempPath();
        opChainPath = QDir(breadcrumbDir).filePath(
            QStringLiteral("miacode_op_chain_%1.log").arg(pid));
    }

    const QString startupBeaconPath = QDir(breadcrumbDir).filePath(
        QStringLiteral("miacode_startup_beacon_%1.txt").arg(pid));
    const QString runtimeOverride =
        resolvedOverridePath(Channel::Runtime, channelPathOverride(Channel::Runtime));
    const QString runtimePath = runtimeOverride.isEmpty()
        ? QDir(logDirectory()).filePath(channelFileName(Channel::Runtime))
        : runtimeOverride;

    const QString payload =
        QStringLiteral(
            "action=project_log_dir_bound pid=%1 project_log_dir=%2 runtime_log_path=%3 "
            "crash_path_source=%4 env_log_dir_present=%5 env_shadow_override_present=%6 "
            "startup_beacon_hint=%7 op_chain_hint=%8 "
            "note=shadow_path_is_captured_before_project_log_dir_bind "
            "future=crash_shadow_rebind_or_mirror")
            .arg(pid)
            .arg(quotedLogValue(normalizedProjectLogDirectory),
                 quotedLogValue(runtimePath),
                 source)
            .arg(envLogDir.isEmpty() ? 0 : 1)
            .arg(explicitShadowPath.isEmpty() ? 0 : 1)
            .arg(quotedLogValue(startupBeaconPath),
                 quotedLogValue(opChainPath));

    if (appendLine(
            Channel::Runtime,
            QStringLiteral("logging/crash_breadcrumb_hint"),
            payload,
            /*force=*/forceForFocusedCrashDiag,
            Level::Info)) {
        flushAsyncLogWriter(100);
    }
}

qint64 startupTrimMaxBytes()
{
    // Beta35 — bumped from 2 MB to 4 MB so a single project session
    // can retain a longer trailing window of diagnostic context
    // before the rolling trim kicks in. The trim runs every
    // kTrimEveryWritesPerChannel (200) writes per channel; at the
    // 2 MB cap, busy sessions on muri/render perf channels were
    // dropping evidence faster than user-reported reproductions
    // could be triaged. 4 MB doubles the retained window without
    // affecting the trim cadence or worker-thread I/O cost.
    return 4 * 1024 * 1024;
}

// Number of archived segments kept per channel (miacode_x.1.log .. .N.log).
// Total on-disk per channel is bounded by (N + 1) × startupTrimMaxBytes().
constexpr int kMaxLogSegments = 3;

// Build the rotated-segment path: ".../miacode_runtime_debug.log" + index 1 ->
// ".../miacode_runtime_debug.1.log" (index inserted before the suffix).
QString rotatedSegmentPath(const QString& path, int index)
{
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    QString name = suffix.isEmpty()
        ? QStringLiteral("%1.%2").arg(base).arg(index)
        : QStringLiteral("%1.%2.%3").arg(base).arg(index).arg(suffix);
    return dir.isEmpty() ? name : QDir(dir).filePath(name);
}

// Rename-based rotation. When `path` exceeds maxBytes, shift the archive chain up
// (.N-1 -> .N, …, .1 -> .2) and rename the live file to .1, leaving the caller to
// reopen a fresh empty base. Replaces the old in-place head-truncation, which
// permanently discarded the session start (startup-timing / qt_config /
// media_backend — the context a late-session repro needs). O(1) renames, bounded
// disk, and the head is preserved in an archived segment. Defensive: any failed
// rename (e.g. a still-open handle on Windows) just leaves the file to retry next
// round — never throws, never truncates.
bool rotateFileLocked(const QString& path, qint64 maxBytes, int maxSegments = kMaxLogSegments)
{
    if (maxBytes <= 0 || maxSegments < 1) {
        return true;
    }
    const QFileInfo info(path);
    if (!info.exists() || info.size() <= maxBytes) {
        return true;
    }
    // Drop the oldest archive, then shift the rest up by one.
    QFile::remove(rotatedSegmentPath(path, maxSegments));
    for (int i = maxSegments - 1; i >= 1; --i) {
        const QString from = rotatedSegmentPath(path, i);
        if (QFile::exists(from)) {
            const QString to = rotatedSegmentPath(path, i + 1);
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    const QString firstArchive = rotatedSegmentPath(path, 1);
    QFile::remove(firstArchive);
    return QFile::rename(path, firstArchive);
}

void trimDebugLogsInCurrentDirectoryLocked()
{
    const qint64 maxBytes = startupTrimMaxBytes();
    for (const Channel channel : {
             Channel::Runtime,
             Channel::Audio,
             Channel::Export,
             Channel::StartupTiming,
             Channel::Fatal,
             Channel::Operation,
             Channel::PvMemory}) {
        (void)rotateFileLocked(logPath(channel), maxBytes);
    }
}

// =====================================================================================
// AsyncLogWriter
//
// Routes log writes onto a dedicated worker thread so that caller threads (GUI / render)
// pay only the cost of a brief mutex + std::deque emplace_back. The worker thread keeps
// per-channel file handles open (no open/close per write), drains in batches, and
// performs periodic size-based trimming. Stats counters let us prove the writer is not
// a bottleneck.
// =====================================================================================
class AsyncLogWriter
{
public:
    static AsyncLogWriter& instance()
    {
        static AsyncLogWriter writer;
        return writer;
    }

    // Push one log line onto the queue and signal the worker. Returns false only if the
    // queue is at capacity and dropped the entry (still increments droppedCount_).
    bool enqueue(Channel channel, QByteArray bytes)
    {
        ensureWorker();

        const auto enqueueStart = std::chrono::steady_clock::now();
        bool dropped = false;
        int newSize = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (permanentShutdown_.load(std::memory_order_relaxed)) {
                // Worker has been torn down (app exit). Late-arriving logs go straight to
                // disk synchronously so we don't lose them.
                writeEntrySync(channel, bytes);
                writtenCount_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if (queue_.size() >= kMaxQueueSize) {
                // Drop the OLDEST entry, but record it per-channel so the worker
                // can emit a visible "dropped=N" gap marker into that channel's
                // file instead of leaving a silently-continuous stream.
                pendingDrops_[static_cast<size_t>(queue_.front().channel)] += 1;
                queue_.pop_front();
                dropped = true;
            }
            queue_.push_back(Entry{channel, std::move(bytes)});
            newSize = static_cast<int>(queue_.size());
        }
        cv_.notify_one();

        const auto enqueueEnd = std::chrono::steady_clock::now();
        const quint64 elapsedNs = static_cast<quint64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(enqueueEnd - enqueueStart).count());
        updateMaxAtomic(maxEnqueueTimeNs_, elapsedNs);

        enqueuedCount_.fetch_add(1, std::memory_order_relaxed);
        if (dropped) {
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
        }
        currentQueueSize_.store(newSize, std::memory_order_relaxed);
        int prevPeak = peakQueueSize_.load(std::memory_order_relaxed);
        while (newSize > prevPeak
               && !peakQueueSize_.compare_exchange_weak(prevPeak, newSize, std::memory_order_relaxed)) {
        }
        return !dropped;
    }

    // Wait until the queue has been fully drained. Returns false on timeout.
    bool flush(int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!workerStarted_) {
            return true;
        }
        auto pred = [this]() { return queue_.empty() && !writing_; };
        if (timeoutMs < 0) {
            drained_.wait(lock, pred);
            return true;
        }
        return drained_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred);
    }

    // Stop the worker thread, drain any remaining entries to disk, and close cached file
    // handles. After this returns, the worker is gone — but a subsequent enqueue() call
    // will lazily restart it. Use this around destructive file operations (clear/reset/
    // remove on Windows where we can't remove an open file).
    void stop()
    {
        joinAndDrain(/*permanent=*/false);
    }

    // Permanent shutdown for application exit. Same as stop() but flips a flag so further
    // enqueue() calls go straight to disk synchronously instead of starting a new worker.
    void shutdown()
    {
        joinAndDrain(/*permanent=*/true);
    }

    LogWriterStats snapshot() const
    {
        LogWriterStats s;
        s.enqueuedCount = enqueuedCount_.load(std::memory_order_relaxed);
        s.droppedCount = droppedCount_.load(std::memory_order_relaxed);
        s.writtenCount = writtenCount_.load(std::memory_order_relaxed);
        s.totalIoTimeNs = totalIoTimeNs_.load(std::memory_order_relaxed);
        s.maxBatchTimeNs = maxBatchTimeNs_.load(std::memory_order_relaxed);
        s.maxEnqueueTimeNs = maxEnqueueTimeNs_.load(std::memory_order_relaxed);
        s.currentQueueSize = currentQueueSize_.load(std::memory_order_relaxed);
        s.peakQueueSize = peakQueueSize_.load(std::memory_order_relaxed);
        s.workerRunning = workerStartedAtomic_.load(std::memory_order_acquire)
                          && !permanentShutdown_.load(std::memory_order_relaxed);
        s.asyncEnabled = true;
        return s;
    }

private:
    static constexpr int kMaxQueueSize = 4096;
    static constexpr int kTrimEveryWritesPerChannel = 200;
    // Must match the number of Channel enum values (index = static_cast<size_t>).
    // Channel::PvMemory is the last value; a channel added after it must bump this
    // (and the four channel switch statements). The static_assert catches a stale
    // count so the per-channel arrays below can never be indexed out of bounds.
    static constexpr size_t kChannelCount = 8;
    static_assert(static_cast<size_t>(Channel::PvMemory) + 1 == kChannelCount,
                  "kChannelCount out of sync with the Channel enum");

    struct Entry {
        Channel channel;
        QByteArray bytes;
    };

    AsyncLogWriter() = default;

    ~AsyncLogWriter()
    {
        stop();
    }

    AsyncLogWriter(const AsyncLogWriter&) = delete;
    AsyncLogWriter& operator=(const AsyncLogWriter&) = delete;

    void ensureWorker()
    {
        if (workerStartedAtomic_.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (workerStarted_ || permanentShutdown_.load(std::memory_order_relaxed)) {
            return;
        }
        stopRequested_ = false;
        workerStarted_ = true;
        worker_ = std::thread([this]() { workerLoop(); });
        workerStartedAtomic_.store(true, std::memory_order_release);
    }

    void joinAndDrain(bool permanent)
    {
        std::thread localWorker;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!workerStarted_) {
                if (permanent) {
                    permanentShutdown_.store(true, std::memory_order_relaxed);
                }
                return;
            }
            stopRequested_ = true;
            localWorker = std::move(worker_);
            workerStarted_ = false;
            workerStartedAtomic_.store(false, std::memory_order_release);
        }
        cv_.notify_all();
        if (localWorker.joinable()) {
            localWorker.join();
        }

        std::deque<Entry> remaining;
        std::array<quint64, kChannelCount> drops{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            remaining.swap(queue_);
            drops = pendingDrops_;
            pendingDrops_.fill(0);
            currentQueueSize_.store(0, std::memory_order_relaxed);
            if (permanent) {
                permanentShutdown_.store(true, std::memory_order_relaxed);
            }
        }
        // Flush any overflow gap markers accumulated since the last drain before
        // the surviving tail, so a shutdown-window drop is still recorded.
        for (size_t i = 0; i < kChannelCount; ++i) {
            if (drops[i] > 0) {
                const Channel ch = static_cast<Channel>(i);
                writeEntrySync(ch, buildDropMarker(ch, drops[i]));
                writtenCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        for (const Entry& entry : remaining) {
            writeEntrySync(entry.channel, entry.bytes);
            writtenCount_.fetch_add(1, std::memory_order_relaxed);
        }
        closeAllCachedHandles();
        drained_.notify_all();
    }

    void workerLoop()
    {
        std::deque<Entry> batch;
        for (;;) {
            std::array<quint64, kChannelCount> drops{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return !queue_.empty() || stopRequested_; });
                if (queue_.empty() && stopRequested_) {
                    return;
                }
                batch.swap(queue_);
                drops = pendingDrops_;
                pendingDrops_.fill(0);
                currentQueueSize_.store(0, std::memory_order_relaxed);
                writing_ = true;
            }

            // Emit one coalesced gap marker per channel that overflowed since the
            // last drain. The dropped entries were the oldest, so the marker
            // correctly precedes this batch's surviving (newer) lines.
            for (size_t i = 0; i < kChannelCount; ++i) {
                if (drops[i] > 0) {
                    const Channel ch = static_cast<Channel>(i);
                    writeEntryWorker(Entry{ch, buildDropMarker(ch, drops[i])});
                }
            }

            const auto batchStart = std::chrono::steady_clock::now();
            for (const Entry& entry : batch) {
                writeEntryWorker(entry);
            }
            const auto batchEnd = std::chrono::steady_clock::now();
            const quint64 batchNs = static_cast<quint64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(batchEnd - batchStart).count());
            totalIoTimeNs_.fetch_add(batchNs, std::memory_order_relaxed);
            updateMaxAtomic(maxBatchTimeNs_, batchNs);
            writtenCount_.fetch_add(static_cast<quint64>(batch.size()), std::memory_order_relaxed);
            batch.clear();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                writing_ = false;
            }
            drained_.notify_all();
        }
    }

    // Worker-thread-only: write one entry using the cached file handle for that channel.
    void writeEntryWorker(const Entry& entry)
    {
        const size_t idx = static_cast<size_t>(entry.channel);
        // Resolve the channel's path ONCE per worker lifetime. It only changes when
        // the project log dir changes, which stops the worker and clears this cache,
        // so the per-entry drain path is free of the env reads + project-dir mutex +
        // QString churn that logPath() would otherwise incur on every single line.
        if (channelPaths_[idx].isEmpty()) {
            channelPaths_[idx] = logPath(entry.channel);
        }
        const QString& path = channelPaths_[idx];
        QFile* file = openFiles_[idx];
        if (file == nullptr || !file->isOpen()) {
            ensureParentDirectory(path);
            file = new QFile(path);
            if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                delete file;
                openFiles_[idx] = nullptr;
                return;
            }
            openFiles_[idx] = file;
            writeCountSinceTrim_[idx] = 0;
        }
        file->write(entry.bytes);
        // No flush here — Qt's QFile writes go through the OS file cache, and we let the
        // OS flush on its own schedule. Trade-off: a hard crash may lose the last few log
        // lines, but the alternative (flush per write) re-introduces the I/O stall we're
        // trying to eliminate.
        const qint64 newCount = writeCountSinceTrim_[idx] + 1;
        if (newCount >= kTrimEveryWritesPerChannel
            && miacode::debug_options::debugModeEnabled()) {
            // Close the handle, rotate if oversized (rename-based, preserves the
            // session start in an archived segment), then reopen a fresh base for
            // further appends. rotateFileLocked short-circuits when under the cap.
            file->close();
            rotateFileLocked(path, startupTrimMaxBytes());
            if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                delete file;
                openFiles_[idx] = nullptr;
                writeCountSinceTrim_[idx] = 0;
                return;
            }
            writeCountSinceTrim_[idx] = 0;
        } else {
            writeCountSinceTrim_[idx] = newCount;
        }
    }

    // Caller-thread fallback for late-arriving logs (post-shutdown). Opens, writes, closes.
    static void writeEntrySync(Channel channel, const QByteArray& bytes)
    {
        const QString path = logPath(channel);
        ensureParentDirectory(path);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            return;
        }
        file.write(bytes);
        file.close();
    }

    void closeAllCachedHandles()
    {
        for (QFile*& f : openFiles_) {
            if (f != nullptr) {
                if (f->isOpen()) {
                    f->close();
                }
                delete f;
                f = nullptr;
            }
        }
        writeCountSinceTrim_.fill(0);
        // Drop cached paths too, so a project-dir change (which stops the worker)
        // forces re-resolution against the new directory on the next write.
        channelPaths_.fill(QString());
    }

    static void updateMaxAtomic(std::atomic<quint64>& target, quint64 candidate)
    {
        quint64 prev = target.load(std::memory_order_relaxed);
        while (candidate > prev
               && !target.compare_exchange_weak(prev, candidate, std::memory_order_relaxed)) {
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable drained_;
    std::deque<Entry> queue_;
    // Per-channel count of entries dropped on overflow since the last drain
    // (guarded by mutex_). The worker turns these into visible gap markers.
    std::array<quint64, kChannelCount> pendingDrops_{};
    std::thread worker_;
    bool workerStarted_ = false;
    bool stopRequested_ = false;
    std::atomic<bool> permanentShutdown_{false};
    bool writing_ = false;
    std::atomic<bool> workerStartedAtomic_{false};

    // Worker-thread-only state (no locking needed), indexed by Channel so the drain
    // path needs no QString key derivation. channelPaths_ caches each channel's
    // resolved log path (filled once per worker lifetime; cleared on stop, so a
    // project-dir change re-resolves it).
    std::array<QFile*, kChannelCount> openFiles_{};
    std::array<qint64, kChannelCount> writeCountSinceTrim_{};
    std::array<QString, kChannelCount> channelPaths_;

    // Stats — atomics so snapshot() does not contend with the hot path.
    std::atomic<quint64> enqueuedCount_{0};
    std::atomic<quint64> droppedCount_{0};
    std::atomic<quint64> writtenCount_{0};
    std::atomic<quint64> totalIoTimeNs_{0};
    std::atomic<quint64> maxBatchTimeNs_{0};
    std::atomic<quint64> maxEnqueueTimeNs_{0};
    std::atomic<int> currentQueueSize_{0};
    std::atomic<int> peakQueueSize_{0};
};

QByteArray prepareLogPayload(const QString& text)
{
    if (text.endsWith(QLatin1Char('\n'))) {
        return text.toUtf8();
    }
    QByteArray bytes = text.toUtf8();
    bytes.append('\n');
    return bytes;
}

}  // namespace

QString timestampString()
{
    // UTC with a trailing 'Z'. The crash shadow + startup beacon already stamp
    // GetSystemTime (UTC); using local wall-clock here made the channel logs and
    // those crash files un-correlatable across the local offset (and the local
    // stamp carried no offset to recover it). One clock convention everywhere.
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString logDirectory()
{
    const QString envDir = qEnvironmentVariable("MIACODE_LOG_DIR").trimmed();
    if (!envDir.isEmpty()) {
        return QDir::cleanPath(envDir);
    }

    {
        QMutexLocker locker(&projectLogDirectoryMutex());
        const QString projectDir = sessionProjectLogDirectoryStorage().trimmed();
        if (!projectDir.isEmpty()) {
            return QDir::cleanPath(projectDir);
        }
    }

    if (miacode::debug_options::debugModeEnabled()) {
        const QString appDebugDir = defaultDebugLogDirectory();
        if (!appDebugDir.isEmpty()) {
            return QDir::cleanPath(appDebugDir);
        }
    }

    return QDir::tempPath();
}

void setSessionProjectLogDirectory(const QString& directoryPath)
{
    const QString normalizedNewDirectory = directoryPath.trimmed().isEmpty()
        ? QString()
        : QDir::cleanPath(directoryPath);

    QString previousDirectory;
    {
        QMutexLocker locker(&projectLogDirectoryMutex());
        previousDirectory = sessionProjectLogDirectoryStorage();
    }

    // If the project folder actually changed, drain the async log
    // writer and tear down its cached file handles. The writer keeps
    // one QFile* per channel open for the lifetime of the worker
    // thread; on Windows those handles take an exclusive write lock
    // that blocks the user from zipping / compressing the old
    // project folder while MiaCode is still running. Stopping the
    // writer here closes every cached handle synchronously, and the
    // next enqueue() lazily restarts the worker — which then opens
    // fresh handles in the new project directory.
    const bool directoryChanged = previousDirectory != normalizedNewDirectory;
    if (directoryChanged) {
        AsyncLogWriter::instance().flush(2000);
        AsyncLogWriter::instance().stop();
    }

    {
        QMutexLocker locker(&projectLogDirectoryMutex());
        sessionProjectLogDirectoryStorage() = normalizedNewDirectory;
    }
    if (directoryChanged) {
        appendCrashBreadcrumbPathHint(normalizedNewDirectory);
    }
    if (miacode::debug_options::debugModeEnabled()) {
        trimDebugLogsInCurrentDirectoryLocked();
    }
}

QString logPath(Channel channel)
{
    const QString overridePath = resolvedOverridePath(channel, channelPathOverride(channel));
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    return QDir(logDirectory()).filePath(channelFileName(channel));
}

QString runtimeLogPath()
{
    return logPath(Channel::Runtime);
}

namespace {

// Last resort for a fatal-grade line whose durable write could not get logMutex() in time.
// Deliberately its OWN file and NOT the contended one: serialising appends is the whole
// job of that mutex, so a writer that just failed to acquire it must not append to the
// file it guards. Per-process so two processes sharing a log directory cannot interleave
// either.
//
// The file existing at all is the signal. It means some thread held the log mutex for
// longer than the timeout -- in practice a stalled disk or a disconnected network share --
// and that the corresponding lines are missing from the normal log. Read it alongside the
// runtime log, not instead of it.
bool writeDurableFallbackLine(const QByteArray& bytes)
{
    // Its own mutex, never the contended one, so it cannot be held by the writer whose
    // stall sent us here. Still bounded: if the underlying storage is what stalled then
    // this write can stall too, and the point of this path is that no caller waits
    // indefinitely. Losing a line beats wedging the watchdog that was reporting the freeze.
    static QMutex fallbackMutex;
    if (!fallbackMutex.tryLock(500)) {
        return false;
    }
    bool written = false;
    {
        const QString path = QDir(logDirectory())
                                 .filePath(QStringLiteral("miacode_durable_fallback_%1.log")
                                               .arg(cachedProcessId()));
        ensureParentDirectory(path);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            file.write(bytes);
            file.flush();
            file.close();
            written = true;
        }
    }
    fallbackMutex.unlock();
    return written;
}

}  // namespace

QString audioLogPath()
{
    return logPath(Channel::Audio);
}

QString exportLogPath()
{
    return logPath(Channel::Export);
}

QString startupTimingLogPath()
{
    return logPath(Channel::StartupTiming);
}

QString fatalLogPath()
{
    return logPath(Channel::Fatal);
}

QString previewProfileSummaryPath()
{
    return logPath(Channel::PreviewProfile);
}

QString operationLogPath()
{
    return logPath(Channel::Operation);
}

QString pvMemoryLogPath()
{
    return logPath(Channel::PvMemory);
}

QString formatTitleLine(const QString& title)
{
    return QStringLiteral("[%1] %2").arg(timestampString(), title);
}

bool clearChannel(Channel channel)
{
    // Drain pending async writes and tear down the worker so we don't race with the file
    // delete (Windows holds an exclusive lock on files opened by the worker). The worker
    // restarts lazily on the next enqueue.
    AsyncLogWriter::instance().flush(2000);
    AsyncLogWriter::instance().stop();
    QMutexLocker locker(&logMutex());
    QFile::remove(logPath(channel));
    return true;
}

void clearDebugSessionLogs()
{
    clearChannel(Channel::Runtime);
    clearChannel(Channel::Audio);
    clearChannel(Channel::Export);
    clearChannel(Channel::PreviewProfile);
    clearChannel(Channel::PvMemory);
}

void trimDebugSessionLogsForStartup()
{
    if (!miacode::debug_options::debugModeEnabled()) {
        return;
    }
    QMutexLocker locker(&logMutex());
    trimDebugLogsInCurrentDirectoryLocked();
}

bool resetChannel(Channel channel, const QStringList& initialLines, bool force)
{
    if (!shouldWrite(channel, force)) {
        return false;
    }
    // MIACODE_SKIP_ASYNCLOG_FLUSH (cached) short-circuits the flush+stop below —
    // the diagnostic escape hatch for the historical Win10-22H2 singleton-
    // construction fault that avoids ever touching AsyncLogWriter::instance().
    if (!skipAsyncLogFlush()) {
        // Truncate is destructive — drain and stop the worker so it isn't writing to the
        // file we're about to wipe (and so its cached handle is closed before we open in
        // Truncate mode, which is otherwise a Windows sharing-violation hazard).
        AsyncLogWriter& writer = AsyncLogWriter::instance();
        writer.flush(2000);
        writer.stop();
    }
    QMutexLocker locker(&logMutex());
    const QString path = logPath(channel);
    ensureParentDirectory(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        // Keep one breadcrumb on the genuine failure branch — a reset failure is
        // exactly the kind of silent-startup-crash signal the beacon exists for.
        const QByteArray errStr = file.errorString().toUtf8();
        char buf[768];
        std::snprintf(buf, sizeof(buf), "reset/qfile_open_failed err=%d msg=%.500s",
            static_cast<int>(file.error()), errStr.constData());
        miacode::oplog::appendStartupBeaconLine(buf);
        return false;
    }
    QTextStream stream(&file);
    for (const QString& line : initialLines) {
        stream << line;
        if (!line.endsWith(QLatin1Char('\n'))) {
            stream << '\n';
        }
    }
    return true;
}

bool appendText(Channel channel, const QString& text, bool force, Level level)
{
    if (!shouldWrite(channel, force)) {
        return false;
    }
    QByteArray bytes = prepareLogPayload(text);
    // Durable synchronous write for fatal-grade lines (keyed off the LEVEL, so a
    // Level::Fatal line on any channel is flushed to disk before the next
    // instruction — not just the dedicated Fatal channel) and for the cached
    // MIACODE_SKIP_ASYNCLOG_FLUSH diagnostic bypass.
    const bool skipAsyncLog = skipAsyncLogFlush();
    if (skipAsyncLog || level == Level::Fatal || channel == Channel::Fatal) {
        if (!skipAsyncLog) {
            AsyncLogWriter::instance().flush(1000);
        }
        // Bounded, not QMutexLocker. This lock is held across an open/write/flush/close,
        // so a thread stuck in a slow write -- a stalled disk, a disconnected network share
        // -- holds it for as long as that write takes. An unbounded wait here means the one
        // writer that most needs to get its bytes out, the hang watchdog reporting a frozen
        // GUI thread, blocks on its FIRST line and the freeze report never lands. That is
        // the diagnostic being defeated by the exact condition it exists to record.
        //
        // On timeout, write the line anyway to a per-process fallback file. Two writers
        // appending to one file without the mutex is what the lock prevents, so the
        // fallback deliberately does not touch the contended path; it is a separate file,
        // and the line carries `durable_lock=timeout` so a reader can tell a fallback row
        // from a normal one and knows the main log is missing it.
        constexpr int kDurableLockTimeoutMs = 2000;
        QMutex& mutex = logMutex();
        if (!mutex.tryLock(kDurableLockTimeoutMs)) {
            return writeDurableFallbackLine(bytes);
        }
        bool written = false;
        int openError = 0;
        QString openErrorText;
        QString attemptedPath;
        {
            const QString path = logPath(channel);
            attemptedPath = path;
            ensureParentDirectory(path);
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                file.write(bytes);
                file.flush();
                file.close();
                written = true;
            } else {
                openError = static_cast<int>(file.error());
                openErrorText = file.errorString();
            }
        }
        mutex.unlock();
        if (written) {
            return true;
        }
        // The durable path used to end here, returning false into a caller that does not
        // check it -- so a fatal-grade line whose open() failed vanished without a trace.
        // That is not theoretical: across five captures the hang watchdog generated its
        // report (proven by action=report_gate will_report=1, and by reportedAtMs advancing
        // past appendWatchdogReport) and not one gui_thread_stale row ever reached disk,
        // with the lock never timing out, no rotation, and the same path working for other
        // writers. A silently failed open is the only branch left that behaves like that.
        //
        // Two changes, both about never losing these bytes again: the line goes to the
        // per-process fallback file, and the reason goes to the startup beacon -- which is
        // the established escape hatch in this file for a failure the log system itself
        // cannot report (see resetLogFiles), and which cannot recurse back into here.
        {
            const QByteArray reason = openErrorText.toUtf8();
            const QByteArray pathUtf8 = attemptedPath.toUtf8();
            char buf[1024];
            std::snprintf(buf, sizeof(buf),
                          "durable/open_failed err=%d path=%.320s msg=%.320s",
                          openError, pathUtf8.constData(), reason.constData());
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        return writeDurableFallbackLine(bytes);
    }
    AsyncLogWriter::instance().enqueue(channel, std::move(bytes));
    return true;
}

bool appendLine(Channel channel, const QString& scope, const QString& payload, bool force, Level level)
{
    QString bracket = channelLabel(channel);
    if (!scope.trimmed().isEmpty()) {
        bracket += QLatin1Char('/') + scope.trimmed();
    }
    // The dedicated Fatal channel always renders (and flushes) at Fatal level,
    // regardless of the level the caller passed, so lines in miacode_fatal.log
    // are never mislabelled INFO.
    const Level effective = (channel == Channel::Fatal) ? Level::Fatal : level;
    // Canonical record grammar: <ts> <LEVEL> pid=<n> tid=<n> [<channel>/<scope>] <payload>
    QString text = QStringLiteral("%1 %2 pid=%3 tid=%4 [%5]")
                       .arg(timestampString(), QString::fromLatin1(levelToken(effective)))
                       .arg(cachedProcessId())
                       .arg(currentThreadIdNumeric())
                       .arg(bracket);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload;
    }
    return appendText(channel, text, force, effective);
}

bool appendTimingLine(
    Channel channel,
    const QString& scope,
    const QString& step,
    qint64 elapsedMs,
    const QString& detail,
    bool force,
    Level level)
{
    QString payload = QStringLiteral("step=%1 elapsed_ms=%2")
                          .arg(step.trimmed().isEmpty() ? QStringLiteral("unknown") : step.trimmed())
                          .arg(elapsedMs);
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" ") + detail.trimmed();
    }
    return appendLine(channel, scope, payload, force, level);
}

bool initializeStartupTimingLogSession()
{
    if (!miacode::debug_options::startupTimingEnabled()) {
        return false;
    }
    return resetChannel(
        Channel::StartupTiming,
        {
            QStringLiteral("%1 [startup/session] pid=%2 log_path=%3")
                .arg(timestampString())
                .arg(QCoreApplication::applicationPid())
                .arg(startupTimingLogPath())
        }
    );
}

bool initializePvMemoryLogSession()
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return false;
    }
    if (!resetChannel(Channel::PvMemory)) {
        return false;
    }
    return appendLine(
        Channel::PvMemory,
        QStringLiteral("pv_memory"),
        QStringLiteral("action=session_start pid=%1 log_path=%2")
            .arg(QCoreApplication::applicationPid())
            .arg(pvMemoryLogPath())
    );
}

bool appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs)
{
    if (!miacode::debug_options::startupTimingEnabled()) {
        return false;
    }
    return appendLine(
        Channel::StartupTiming,
        QStringLiteral("stage"),
        QStringLiteral("stage=%1 elapsed_ms=%2 delta_ms=%3").arg(stage).arg(elapsedMs).arg(deltaMs)
    );
}

bool appendFatalMessage(const QString& scope, const QString& payload)
{
    // Inline the current thread's operation chain so existing top-level
    // catch (...) sites pick up logical-call-chain context for free.
    // See docs/ops/OPERATION_LOG_PATTERNS_SPEC.md.
    const QString chain = miacode::oplog::currentChain();
    const QString fullPayload = chain.isEmpty()
        ? payload
        : QStringLiteral("%1 | chain=%2").arg(payload, chain);
    return appendLine(Channel::Fatal, scope, fullPayload, /*force=*/true, Level::Fatal);
}

// NOTE: processResourceGaugePayload / stageScopePrivateBytes / processPrivateBytes /
// the leak_gauge cross-thread state + namespace / MemoryStageScope were all moved to
// src/common/ProcessDiagnostics.cpp (namespace miacode::diag). DebugLog is now a pure
// channelized writer; the profiler depends on it, not the reverse.

LogWriterStats logWriterStatsSnapshot()
{
    return AsyncLogWriter::instance().snapshot();
}

bool flushAsyncLogWriter(int timeoutMs)
{
    return AsyncLogWriter::instance().flush(timeoutMs);
}

void shutdownAsyncLogWriter()
{
    // Diagnostic bypass (cached): never touch the singleton if the user has opted
    // out for the duration of this run. Covers the case where the singleton was
    // never constructed during startup.
    if (skipAsyncLogFlush()) {
        return;
    }
    AsyncLogWriter::instance().shutdown();
}

}  // namespace miacode::debug_log
