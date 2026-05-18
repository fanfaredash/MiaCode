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

bool trimFileToMaxBytesLocked(const QString& path, qint64 maxBytes)
{
    if (maxBytes <= 0) {
        return true;
    }

    // Short-circuit on file size (cheap stat call) before doing readAll. The previous
    // implementation read the entire file into memory on every call, which was the per-write
    // hot path under --debug mode — for a 100KB log file at ~5 logs/sec that was ~500KB/sec
    // of file I/O on the GUI thread with the global log mutex held, causing 12-69ms hot-path
    // stalls on every per-tick diagnostic write.
    const QFileInfo info(path);
    if (!info.exists()) {
        return true;
    }
    const qint64 currentSize = info.size();
    if (currentSize <= maxBytes) {
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.size() <= maxBytes) {
        return true;
    }

    // Trim down to 75% of maxBytes (low watermark) instead of right at maxBytes. Without
    // this, every single write while the file is at the cap triggers a full readAll + rewrite
    // because each write pushes the size 1 byte over and we trim back to exactly the cap.
    // Trimming to a lower watermark gives us ~25% × maxBytes of headroom — at ~150B/log line
    // and 100KB cap that's ~165 writes between trim operations.
    const qint64 lowWatermark = (maxBytes * 3) / 4;
    const qint64 retain = lowWatermark > 0 ? lowWatermark : maxBytes;
    int start = qMax(0, data.size() - static_cast<int>(retain));
    while (start < data.size() && start > 0 && data.at(start - 1) != '\n') {
        ++start;
    }
    if (start >= data.size()) {
        start = qMax(0, data.size() - static_cast<int>(retain));
    }
    QByteArray trimmed = data.mid(start);
    if (trimmed.size() > retain) {
        trimmed = trimmed.right(static_cast<int>(retain));
    }

    ensureParentDirectory(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 written = file.write(trimmed);
    file.close();
    return written == trimmed.size();
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
             Channel::Operation}) {
        (void)trimFileToMaxBytesLocked(logPath(channel), maxBytes);
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
            if (permanentShutdown_) {
                // Worker has been torn down (app exit). Late-arriving logs go straight to
                // disk synchronously so we don't lose them.
                writeEntrySync(channel, bytes);
                writtenCount_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if (queue_.size() >= kMaxQueueSize) {
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
                          && !permanentShutdown_;
        s.asyncEnabled = true;
        return s;
    }

private:
    static constexpr int kMaxQueueSize = 4096;
    static constexpr int kTrimEveryWritesPerChannel = 200;

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
        if (workerStarted_ || permanentShutdown_) {
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
                    permanentShutdown_ = true;
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            remaining.swap(queue_);
            currentQueueSize_.store(0, std::memory_order_relaxed);
            if (permanent) {
                permanentShutdown_ = true;
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
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return !queue_.empty() || stopRequested_; });
                if (queue_.empty() && stopRequested_) {
                    return;
                }
                batch.swap(queue_);
                currentQueueSize_.store(0, std::memory_order_relaxed);
                writing_ = true;
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
        const QString path = logPath(entry.channel);
        QFile* file = openFiles_.value(path, nullptr);
        if (file == nullptr || !file->isOpen()) {
            ensureParentDirectory(path);
            file = new QFile(path);
            if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                delete file;
                openFiles_.remove(path);
                return;
            }
            openFiles_.insert(path, file);
            writeCountSinceTrim_.insert(path, 0);
        }
        file->write(entry.bytes);
        // No flush here — Qt's QFile writes go through the OS file cache, and we let the
        // OS flush on its own schedule. Trade-off: a hard crash may lose the last few log
        // lines, but the alternative (flush per write) re-introduces the I/O stall we're
        // trying to eliminate.
        const qint64 newCount = writeCountSinceTrim_.value(path, 0) + 1;
        if (newCount >= kTrimEveryWritesPerChannel
            && miacode::debug_options::debugModeEnabled()) {
            // Close the handle, run trim, then reopen for further appends.
            file->close();
            trimFileToMaxBytesLocked(path, startupTrimMaxBytes());
            if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                delete file;
                openFiles_.remove(path);
                writeCountSinceTrim_.remove(path);
                return;
            }
            writeCountSinceTrim_.insert(path, 0);
        } else {
            writeCountSinceTrim_.insert(path, newCount);
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
        for (auto it = openFiles_.begin(); it != openFiles_.end(); ++it) {
            QFile* f = it.value();
            if (f != nullptr) {
                if (f->isOpen()) {
                    f->close();
                }
                delete f;
            }
        }
        openFiles_.clear();
        writeCountSinceTrim_.clear();
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
    std::thread worker_;
    bool workerStarted_ = false;
    bool stopRequested_ = false;
    bool permanentShutdown_ = false;
    bool writing_ = false;
    std::atomic<bool> workerStartedAtomic_{false};

    // Worker-thread-only state (no locking needed).
    QHash<QString, QFile*> openFiles_;
    QHash<QString, qint64> writeCountSinceTrim_;

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
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
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
    if (previousDirectory != normalizedNewDirectory) {
        AsyncLogWriter::instance().flush(2000);
        AsyncLogWriter::instance().stop();
    }

    {
        QMutexLocker locker(&projectLogDirectoryMutex());
        sessionProjectLogDirectoryStorage() = normalizedNewDirectory;
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
    miacode::oplog::appendStartupBeaconLine("reset/entry");
    if (!shouldWrite(channel, force)) {
        miacode::oplog::appendStartupBeaconLine("reset/should_not_write");
        return false;
    }
    // Diagnostic bypass: setting MIACODE_SKIP_ASYNCLOG_FLUSH=1 short-circuits
    // the flush+stop calls below. On the user's broken Win10 22H2 the first
    // touch of AsyncLogWriter::instance() (Meyers singleton construction)
    // appears to fault — if this env var lets the app start, we've localised
    // the regression to that singleton's construction.
    const bool skipAsyncLog =
        qEnvironmentVariableIntValue("MIACODE_SKIP_ASYNCLOG_FLUSH") == 1;
    if (skipAsyncLog) {
        miacode::oplog::appendStartupBeaconLine("reset/asynclog_skipped_via_env");
    } else {
        miacode::oplog::appendStartupBeaconLine("reset/before_asynclog_instance_call");
        // Truncate is destructive — drain and stop the worker so it isn't writing to the
        // file we're about to wipe (and so its cached handle is closed before we open in
        // Truncate mode, which is otherwise a Windows sharing-violation hazard).
        AsyncLogWriter& writer = AsyncLogWriter::instance();
        miacode::oplog::appendStartupBeaconLine("reset/after_asynclog_instance_call");
        miacode::oplog::appendStartupBeaconLine("reset/before_asynclog_flush");
        writer.flush(2000);
        miacode::oplog::appendStartupBeaconLine("reset/after_asynclog_flush");
        miacode::oplog::appendStartupBeaconLine("reset/before_asynclog_stop");
        writer.stop();
        miacode::oplog::appendStartupBeaconLine("reset/after_asynclog_stop");
    }
    miacode::oplog::appendStartupBeaconLine("reset/before_log_mutex_lock");
    QMutexLocker locker(&logMutex());
    miacode::oplog::appendStartupBeaconLine("reset/before_log_path");
    const QString path = logPath(channel);
    {
        const QByteArray pathUtf8 = path.toUtf8();
        char buf[1024];
        std::snprintf(buf, sizeof(buf), "reset/log_path=%.900s", pathUtf8.constData());
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    miacode::oplog::appendStartupBeaconLine("reset/before_ensure_parent_dir");
    ensureParentDirectory(path);
    miacode::oplog::appendStartupBeaconLine("reset/before_qfile_ctor");
    QFile file(path);
    miacode::oplog::appendStartupBeaconLine("reset/before_qfile_open");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        const QFile::FileError err = file.error();
        const QByteArray errStr = file.errorString().toUtf8();
        char buf[768];
        std::snprintf(buf, sizeof(buf),
            "reset/qfile_open_failed err=%d msg=%.500s",
            static_cast<int>(err), errStr.constData());
        miacode::oplog::appendStartupBeaconLine(buf);
        return false;
    }
    miacode::oplog::appendStartupBeaconLine("reset/qfile_open_ok");
    QTextStream stream(&file);
    for (const QString& line : initialLines) {
        stream << line;
        if (!line.endsWith(QLatin1Char('\n'))) {
            stream << '\n';
        }
    }
    miacode::oplog::appendStartupBeaconLine("reset/return_ok");
    return true;
}

bool appendText(Channel channel, const QString& text, bool force)
{
    if (!shouldWrite(channel, force)) {
        return false;
    }
    QByteArray bytes = prepareLogPayload(text);
    // Diagnostic bypass: when MIACODE_SKIP_ASYNCLOG_FLUSH=1 is set, write
    // synchronously and never touch AsyncLogWriter::instance(). Used to
    // localise startup crashes that happen in the singleton's first-time
    // construction on certain Win10 22H2 builds.
    const bool skipAsyncLog =
        qEnvironmentVariableIntValue("MIACODE_SKIP_ASYNCLOG_FLUSH") == 1;
    if (skipAsyncLog || channel == Channel::Fatal) {
        if (!skipAsyncLog) {
            AsyncLogWriter::instance().flush(1000);
        }
        QMutexLocker locker(&logMutex());
        const QString path = logPath(channel);
        ensureParentDirectory(path);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            return false;
        }
        file.write(bytes);
        file.flush();
        file.close();
        return true;
    }
    AsyncLogWriter::instance().enqueue(channel, std::move(bytes));
    return true;
}

bool appendLine(Channel channel, const QString& scope, const QString& payload, bool force)
{
    QString bracket = channelLabel(channel);
    if (!scope.trimmed().isEmpty()) {
        bracket += QLatin1Char('/') + scope.trimmed();
    }
    QString text = QStringLiteral("%1 [%2]").arg(timestampString(), bracket);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload;
    }
    return appendText(channel, text, force);
}

bool appendTimingLine(
    Channel channel,
    const QString& scope,
    const QString& step,
    qint64 elapsedMs,
    const QString& detail,
    bool force)
{
    QString payload = QStringLiteral("step=%1 elapsed_ms=%2")
                          .arg(step.trimmed().isEmpty() ? QStringLiteral("unknown") : step.trimmed())
                          .arg(elapsedMs);
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" ") + detail.trimmed();
    }
    return appendLine(channel, scope, payload, force);
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
    // See docs/OPERATION_BREADCRUMB_LOGGING_PLAN.md §3.5.
    const QString chain = miacode::oplog::currentChain();
    const QString fullPayload = chain.isEmpty()
        ? payload
        : QStringLiteral("%1 | chain=%2").arg(payload, chain);
    return appendLine(Channel::Fatal, scope, fullPayload, true);
}

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
    // Diagnostic bypass: never touch the singleton if the user has opted out
    // for the duration of this run. Reading the env var here covers the case
    // where the singleton was never constructed during startup.
    if (qEnvironmentVariableIntValue("MIACODE_SKIP_ASYNCLOG_FLUSH") == 1) {
        return;
    }
    AsyncLogWriter::instance().shutdown();
}

}  // namespace miacode::debug_log
