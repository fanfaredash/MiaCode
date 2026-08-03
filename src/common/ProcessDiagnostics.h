#pragma once

#include <QString>
#include <QtGlobal>

class QObject;

// Process-resource & leak-hunt diagnostics. Split out of common/DebugLog.h so the
// logging header stays a pure channelized writer (see the note there). These sample
// OS process counters and shuttle low-frequency memory samples between the GUI and
// QSG render thread; they EMIT through debug_log::appendLine, so the dependency runs
// diagnostics -> logging (the correct direction), not the reverse.
namespace miacode::diag {

// Sample process-wide OS resource counters into a "key=val …" payload fragment for a
// LOW-FREQUENCY leak gauge (call e.g. once per playback pause — never per frame). On Windows
// reports GDI + USER handle counts and working-set / private commit (MB); other platforms
// report zeros. Append app-level counts (e.g. qobject_descendants) alongside it and log via
// appendLine(Channel::Runtime, "preview/resource_gauge", …) so a monotonic climb across
// edit→play→pause cycles localises the leak (handles vs GPU/memory vs QObject churn).
QString processResourceGaugePayload();

// Install one debug-only GUI-thread timer that records a process resource
// baseline immediately and then every 30 seconds. Idempotent per owner.
void installPeriodicProcessResourceGauge(QObject* owner);

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

}  // namespace miacode::diag
