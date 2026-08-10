#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

class QObject;

// Process-resource & leak-hunt diagnostics. Split out of common/DebugLog.h so the
// logging header stays a pure channelized writer (see the note there). These sample
// OS process counters and shuttle low-frequency memory samples between the GUI and
// QSG render thread; they EMIT through debug_log::appendLine, so the dependency runs
// diagnostics -> logging (the correct direction), not the reverse.
namespace miacode::diag {

// Current-process memory counters in bytes. Every field is -1 when the platform cannot
// query the complete sample; callers can therefore keep a stable payload shape.
struct CurrentProcessMemorySample {
    qint64 residentBytes = -1;
    qint64 physFootprintBytes = -1;
    qint64 internalBytes = -1;
    qint64 compressedBytes = -1;
};

CurrentProcessMemorySample currentProcessMemorySample();

// Sample process-wide OS resource counters into a "key=val …" payload fragment for a
// LOW-FREQUENCY leak gauge (call e.g. once per playback pause — never per frame). On Windows
// reports GDI + USER handle counts and working-set / private commit (MB); other platforms
// report zeros. Append app-level counts (e.g. qobject_descendants) alongside it and log via
// appendLine(Channel::Runtime, "preview/resource_gauge", …) so a monotonic climb across
// edit→play→pause cycles localises the leak (handles vs GPU/memory vs QObject churn).
QString processResourceGaugePayload();

// Install one debug-only GUI-thread timer that records a process resource
// baseline immediately and then every 30 seconds. Idempotent per owner.
// Each tick also emits the per-adapter VRAM gauge described below.
void installPeriodicProcessResourceGauge(QObject* owner);

// ---------------------------------------------------------------------------
// Per-adapter VRAM gauge (DXGI, Windows only)
// ---------------------------------------------------------------------------
// Why per-ADAPTER and not per-device: on the reported failing machine (i5-1155G7 +
// MX450 2 GB + Iris Xe) MiaCode straddles both GPUs — the root window is bound to the
// high-performance adapter while the preview composite surface stays on the default
// adapter — and OBS's NVENC encoder lands on the same 2 GB card as the root window.
// Once this process's CurrentUsage crosses Budget, DXGI evicts resources to system
// memory and every subsequent frame re-uploads them over PCIe. That is a cliff, not a
// slope, which is the leading explanation for the ~10x density collapse. Seeing it
// requires Budget-vs-CurrentUsage for EACH adapter over time, not one combined number.
//
// QueryVideoMemoryInfo reports THIS PROCESS's usage and THIS PROCESS's budget (the
// budget the OS is currently willing to grant us, which shrinks as other processes take
// VRAM), so a shrinking budget is itself the signature of third-party contention.
struct AdapterVideoMemorySample {
    int index = -1;
    QString description;
    QString luid;                        // hex form of DXGI_ADAPTER_DESC1::AdapterLuid
    quint32 vendorId = 0;
    quint32 deviceId = 0;
    bool software = false;               // DXGI_ADAPTER_FLAG_SOFTWARE (WARP)
    bool queried = false;                // IDXGIAdapter3::QueryVideoMemoryInfo succeeded
    quint64 dedicatedVideoMemoryMb = 0;  // static capacity, from the adapter desc
    quint64 sharedSystemMemoryMb = 0;
    quint64 localBudgetMb = 0;           // DXGI_MEMORY_SEGMENT_GROUP_LOCAL (real VRAM)
    quint64 localUsageMb = 0;
    quint64 localReservedMb = 0;
    quint64 nonLocalBudgetMb = 0;        // DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL (sysmem)
    quint64 nonLocalUsageMb = 0;
    quint64 nonLocalReservedMb = 0;
};

// Enumerate every DXGI adapter and sample this process's video-memory budget/usage on
// each. Returns an empty list on non-Windows or when DXGI is unavailable. Cheap enough
// for the 30 s gauge; do NOT call per frame (it creates and releases a DXGI factory).
QList<AdapterVideoMemorySample> sampleAdapterVideoMemory();

// Render one sample as a stable `key=value` payload. Pure and platform-independent so
// the log format is covered by a spec on macOS too.
QString formatAdapterVideoMemoryPayload(const AdapterVideoMemorySample& sample);

// Combined LOCAL + NON_LOCAL CurrentUsage (KB) for one already-obtained IDXGIAdapter,
// passed as void* so this header stays free of DXGI types. Returns -1 when unavailable.
// This is the shared primitive behind BOTH the per-adapter gauge above and the timeline
// leak gauge's `gpu_kb` (timeline/quick/TimelineQuickItem.cpp), which previously carried
// its own copy of the same QueryVideoMemoryInfo call.
qint64 adapterProcessVideoMemoryUsageKb(void* dxgiAdapter);

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
