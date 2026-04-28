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

QString formatTitleLine(const QString& title);

bool clearChannel(Channel channel);
void clearDebugSessionLogs();
void trimDebugSessionLogsForStartup();
bool resetChannel(Channel channel, const QStringList& initialLines = {}, bool force = false);
bool appendText(Channel channel, const QString& text, bool force = false);
bool appendLine(Channel channel, const QString& scope, const QString& payload, bool force = false);
bool appendTimingLine(
    Channel channel,
    const QString& scope,
    const QString& step,
    qint64 elapsedMs,
    const QString& detail = QString(),
    bool force = false);
bool initializeStartupTimingLogSession();
bool appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs);
bool appendFatalMessage(const QString& scope, const QString& payload);

}  // namespace miacode::debug_log
