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

// Sample process-wide OS resource counters into a "key=val …" payload fragment for a
// LOW-FREQUENCY leak gauge (call e.g. once per playback pause — never per frame). On Windows
// reports GDI + USER handle counts and working-set / private commit (MB); other platforms
// report zeros. Append app-level counts (e.g. qobject_descendants) alongside it and log via
// appendLine(Channel::Runtime, "preview/resource_gauge", …) so a monotonic climb across
// edit→play→pause cycles localises the leak (handles vs GPU/memory vs QObject churn).
QString processResourceGaugePayload();

// RAII memory-delta tracer for the leak hunt. On construction (only when runtime debug output
// is enabled) it samples process private bytes; on destruction it samples again and logs
// "stage=<stage> private_mb=<after> delta_kb=<±>" on Channel::Runtime under <scope>. Cheap
// (~one process-memory query per boundary) — for LOW-FREQUENCY user-paced pipeline stages
// (per edit / per analysis / per play), NEVER per frame. Lets a --debug log reader trace WHICH
// stage grew memory and whether a later stage released it (positive vs negative delta),
// localising the residual edit-time leak. `scope`/`stage` must be string literals.
class MemoryStageScope {
public:
    MemoryStageScope(const char* scope, const char* stage);
    ~MemoryStageScope();
    MemoryStageScope(const MemoryStageScope&) = delete;
    MemoryStageScope& operator=(const MemoryStageScope&) = delete;

private:
    const char* scope_;
    const char* stage_;
    qint64 startBytes_;
};

// Sample process-wide private bytes (Windows PrivateUsage) from ANY thread. Returns -1 if
// unavailable. One cheap GetProcessMemoryInfo syscall — for LOW-FREQUENCY use (per pause /
// per present), never per frame.
qint64 processPrivateBytes();

// beta7 render-vs-GUI leak gauge. A monotonic ~178 MB/cycle climb was reproduced (logs_33)
// with ~93% of the growth OUTSIDE the five GUI/worker MemoryStageScope brackets — i.e. on the
// render thread or in unbracketed paths. These low-frequency, user-paced hooks split the
// per-cycle growth into a playback window (GUI samples at play-start vs pause) and a
// render-present window (render thread samples at end of updatePaintNode), and expose the
// outstanding worker->GUI queued-lambda depth so async backlog can be confirmed or ruled out.
// All sampling is gated by callers on runtimeDebugOutputEnabled(); the atomics themselves are
// always cheap. See docs/PREVIEW_FRAMEDROP_DIAGNOSIS_AND_FIX_SPEC_ZH.md §8.
namespace leak_gauge {

// GUI thread: record process private bytes when playback starts. Read back at pause to compute
// the playback-window delta (d_play) — the largest previously-unmeasured slice of the cycle.
void notePlayStartPrivateBytes(qint64 bytes);
qint64 playStartPrivateBytes();  // -1 if not sampled since startup

// GUI thread (pause handler): record private bytes at pause and ARM the render thread to emit
// one timeline/leak_gauge line (node/texture counts + d_render) on its next present. txn lets
// that render line correlate with the preview/resource_gauge line from the same pause.
void armRenderSample(qint64 pausePrivateBytes, quint64 txn);
// Render thread (end of updatePaintNode): if armed, hand back the pause sample + txn and disarm
// (true at most once per arm). The caller computes d_render and appends the timeline/leak_gauge
// line with its render-thread node/texture counts.
bool takeRenderSample(qint64* outPausePrivateBytes, quint64* outTxn);

// Outstanding worker->GUI queued-lambda depth (probe to exonerate or convict async backlog).
// noteInflightDispatch(): call right before QMetaObject::invokeMethod posts the result lambda.
// noteInflightApplied(): call at the very top of that queued lambda.
void noteInflightDispatch();
void noteInflightApplied();
int inflightDepth();  // current outstanding (≈0 at a drained pause if there is no backlog)
int inflightPeak();   // high-water since startup

// Timeline render-thread present counter (probe ④). noteTimelinePresent(): call once per
// timeline updatePaintNode (render thread). markPlayStartTimelinePresents()/
// timelinePresentsSincePlayStart(): GUI thread at play-start / pause — yields how many timeline
// presents actually ran during the playback window. presents << ~60·play_seconds ⇒ the render
// thread stalled (deferred-release queue starved = the death-spiral signature).
void noteTimelinePresent();
void markPlayStartTimelinePresents();
quint64 timelinePresentsSincePlayStart();

}  // namespace leak_gauge

}  // namespace miacode::debug_log
