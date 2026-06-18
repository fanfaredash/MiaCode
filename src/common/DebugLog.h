#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace miacode::debug_log {

enum class Channel {
    Runtime,
    Audio,
    Export,
    StartupTiming,
    Fatal,
    PreviewProfile,
    Operation,
};

// Orthogonal severity, independent of Channel (which is destination/category).
// A single channel can emit at any level and consumers triage by level. The
// durable synchronous-flush path is keyed off Level::Fatal (not the Fatal
// *channel*), so a fatal-grade line on ANY channel is flushed to disk before
// the next instruction. appendLine renders the level token into the line.
enum class Level {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

struct LogWriterStats {
    quint64 enqueuedCount = 0;     // total appendText calls that reached the queue
    quint64 droppedCount = 0;      // drops from queue overflow (>kMaxQueueSize)
    quint64 writtenCount = 0;      // entries actually written to disk
    quint64 totalIoTimeNs = 0;     // cumulative time spent in writeEntry on worker thread
    quint64 maxBatchTimeNs = 0;    // worst-case single-batch write time (ns)
    quint64 maxEnqueueTimeNs = 0;  // worst-case mutex-hold time on a caller thread (ns)
    int currentQueueSize = 0;      // outstanding entries waiting for the worker
    int peakQueueSize = 0;         // high-water mark
    bool workerRunning = false;
    bool asyncEnabled = true;
};

// Return a snapshot of the async log writer's counters. Cheap (atomic loads + brief mutex).
LogWriterStats logWriterStatsSnapshot();

// Block until all enqueued log entries have been written. Returns true on success, false on
// timeout. Pass -1 to wait indefinitely. Used at shutdown and before truncating operations.
bool flushAsyncLogWriter(int timeoutMs = -1);

// Stop the worker thread and synchronously flush any remaining entries. Idempotent.
void shutdownAsyncLogWriter();

QString timestampString();
QString logDirectory();
void setSessionProjectLogDirectory(const QString& directoryPath);
QString logPath(Channel channel);
QString runtimeLogPath();
QString audioLogPath();
QString exportLogPath();
QString startupTimingLogPath();
QString fatalLogPath();
QString previewProfileSummaryPath();
QString operationLogPath();

QString formatTitleLine(const QString& title);

bool clearChannel(Channel channel);
void clearDebugSessionLogs();
void trimDebugSessionLogsForStartup();
bool resetChannel(Channel channel, const QStringList& initialLines = {}, bool force = false);
bool appendText(Channel channel, const QString& text, bool force = false, Level level = Level::Info);
bool appendLine(
    Channel channel,
    const QString& scope,
    const QString& payload,
    bool force = false,
    Level level = Level::Info);
bool appendTimingLine(
    Channel channel,
    const QString& scope,
    const QString& step,
    qint64 elapsedMs,
    const QString& detail = QString(),
    bool force = false,
    Level level = Level::Info);
bool initializeStartupTimingLogSession();
bool appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs);
bool appendFatalMessage(const QString& scope, const QString& payload);

// NOTE: process-resource / leak-gauge diagnostics (processResourceGaugePayload,
// MemoryStageScope, processPrivateBytes, namespace leak_gauge) were moved OUT of
// this logging header into src/common/ProcessDiagnostics.h (namespace
// miacode::diag). They are a profiler, not a log writer; keeping this header a
// pure channelized writer restores the natural layering (diagnostics -> logging)
// and stops the ~99 TUs that only call appendLine from depending on the volatile
// leak-hunt surface. Include "common/ProcessDiagnostics.h" where you need them.

}  // namespace miacode::debug_log
