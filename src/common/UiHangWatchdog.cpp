#include "common/UiHangWatchdog.h"

#include "common/UiHangWatchdogPolicy.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/ThreadStackCapture.h"

#include <QCoreApplication>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

namespace miacode::hang_watchdog {
namespace {

constexpr qint64 kHeartbeatIntervalMs = 250;
constexpr qint64 kMonitorLoopIntervalMs = 500;
constexpr qint64 kRepeatedReportMs = 5000;

// Read once at install: the monitor loop must not pay an env lookup every 500 ms, and a
// threshold that changed mid-session would make a capture impossible to interpret. The
// installed values are echoed in the `action=installed` line below, so a log always says
// which thresholds produced (or failed to produce) its reports.
qint64 activePhaseHangMs()
{
    return static_cast<qint64>(miacode::debug_options::uiHangActivePhaseMs());
}

qint64 idleHeartbeatHangMs()
{
    return static_cast<qint64>(miacode::debug_options::uiHangIdleHeartbeatMs());
}
// Stack captures are budgeted separately from the 5 s report cadence — see
// policy::shouldCaptureStack for why.
constexpr qint64 kStackCaptureIntervalMs = 30000;
constexpr int kMaxStackCapturesPerSession = 16;

// Sub-hang stall episodes — see policy::classifyHeartbeatStall for why this exists at all.
// 1 s is four missed 250 ms heartbeats and two monitor polls: comfortably outside normal
// scheduling jitter, and far enough below the 2 s active-phase threshold to cover the band
// that produced no rows at all. Deliberately a constant and not an env flag: this is the
// floor of what the log should always have said, not a knob worth a capture-time decision.
constexpr qint64 kHeartbeatStallMs = 1000;

struct PhaseState {
    bool active = false;
    QString phase;
    QString detail;
    qint64 startMs = 0;
    quint64 generation = 0;
};

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_stop{false};
std::atomic<qint64> g_lastHeartbeatMs{0};
std::atomic<quint64> g_nextGeneration{0};
std::once_flag g_threadOnce;
std::thread g_thread;
std::mutex g_phaseMutex;
PhaseState g_phase;

qint64 steadyMs()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now().time_since_epoch()).count();
}

PhaseState snapshotPhase()
{
    std::lock_guard<std::mutex> lock(g_phaseMutex);
    return g_phase;
}

QString logWriterStatsPayload()
{
    const auto stats = miacode::debug_log::logWriterStatsSnapshot();
    return QStringLiteral(" log_queue=%1 peak_queue=%2 dropped=%3 written=%4 worker=%5")
        .arg(stats.currentQueueSize)
        .arg(stats.peakQueueSize)
        .arg(stats.droppedCount)
        .arg(stats.writtenCount)
        .arg(stats.workerRunning ? 1 : 0);
}

// `stackDecision` / `capturesSoFar` are what makes a bare report readable: without them a
// stale row that carries no stack is indistinguishable from one whose capture broke. The
// decision is computed by the caller BEFORE this runs, but the stack itself is still
// emitted after — the report stays the first line of the incident.
void appendWatchdogReport(
    policy::Trigger trigger,
    const PhaseState& phase,
    qint64 nowMs,
    qint64 heartbeatAgeMs,
    policy::StackCaptureDecision stackDecision,
    int capturesSoFar,
    const std::optional<policy::SuppressedReportSummary>& suppressionSummary)
{
    miacode::oplog::flushShadowToDisk();
    const qint64 activeMs = phase.active ? qMax<qint64>(0, nowMs - phase.startMs) : 0;
    QString payload = QStringLiteral("action=gui_thread_stale trigger=%1 active=%2 active_ms=%3 heartbeat_age_ms=%4 phase=%5 generation=%6")
        .arg(QString::fromLatin1(policy::triggerName(trigger)))
        .arg(phase.active ? 1 : 0)
        .arg(activeMs)
        .arg(heartbeatAgeMs)
        .arg(phase.phase.isEmpty() ? QStringLiteral("(none)") : phase.phase)
        .arg(phase.generation);
    payload += QStringLiteral(" stack=%1 captures_so_far=%2")
                   .arg(QString::fromLatin1(policy::stackCaptureDecisionName(stackDecision)))
                   .arg(capturesSoFar);
    if (suppressionSummary.has_value()) {
        payload += QStringLiteral(" suppressed_count=%1 episode_id=%2 suppressed_trigger=%3")
                       .arg(suppressionSummary->suppressedCount)
                       .arg(suppressionSummary->episodeId)
                       .arg(QString::fromLatin1(
                           policy::triggerName(suppressionSummary->trigger)));
    }
    if (!phase.detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" detail=\"%1\"").arg(phase.detail.trimmed());
    }
    payload += logWriterStatsPayload();
    payload += QStringLiteral(" shadow_flushed=1");
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/hang_watchdog"),
        payload,
        /*force=*/true,
        miacode::debug_log::Level::Fatal);
}

void appendSuppressionEpisodeEnd(
    const policy::SuppressedReportSummary& summary,
    const char* reason)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/hang_watchdog"),
        QStringLiteral("action=report_gate_suppression_end reason=%1 suppressed_count=%2 "
                       "episode_id=%3 trigger=%4")
            .arg(QLatin1String(reason))
            .arg(summary.suppressedCount)
            .arg(summary.episodeId)
            .arg(QString::fromLatin1(policy::triggerName(summary.trigger))));
}

// One row per sub-hang stall episode edge. Info, not Fatal: a 1–5 s stall is a
// responsiveness fact, not the crash-adjacent evidence the gui_thread_stale path carries,
// and forcing every one of these through the synchronous flush would itself add GUI-thread
// latency on a machine that is already struggling. `began` still goes out immediately
// rather than being held until recovery, so a process killed mid-stall leaves the fact
// behind; `ended` is the row that carries the measured duration.
//
// The duration is the gap between two GUI-thread heartbeat timestamps, not between two
// monitor polls, so it measures how long the GUI thread actually went unserviced rather
// than the 500 ms-quantised window in which the watchdog noticed.
void appendStallReport(
    policy::StallTransition transition,
    qint64 heartbeatAgeMs,
    qint64 stallDurationMs,
    const PhaseState& phase,
    int episodeIndex,
    qint64 idleTimeoutMs,
    policy::Trigger hangTrigger)
{
    QString payload =
        QStringLiteral("action=gui_thread_stall edge=%1 episode=%2 threshold_ms=%3 "
                       "heartbeat_age_ms=%4 phase_active=%5 phase=%6 "
                       // The two fields that make a missing hang report diagnosable from
                       // the same row. Three captures have now produced stall episodes at
                       // heartbeat ages of 1000-1500 ms with the idle threshold set to
                       // 800, and not one gui_thread_stale row -- while the same code,
                       // driven against a real stalled GUI thread locally, reports at 959.
                       // `idle_timeout_ms` is what this loop actually compares against
                       // (as opposed to what `action=installed` printed from the GUI
                       // thread), and `hang_trigger` is what classify() returned on this
                       // very poll. If the trigger is idle_heartbeat and no stale row
                       // follows, the report is being generated and lost downstream; if
                       // it is none, the threshold never reached this loop. Those are
                       // different bugs and nothing in a capture could tell them apart.
                       "idle_timeout_ms=%7 hang_trigger=%8")
            .arg(QString::fromLatin1(policy::stallTransitionName(transition)))
            .arg(episodeIndex)
            .arg(kHeartbeatStallMs)
            .arg(heartbeatAgeMs)
            .arg(phase.active ? 1 : 0)
            .arg(phase.phase.isEmpty() ? QStringLiteral("(none)") : phase.phase)
            .arg(idleTimeoutMs)
            .arg(QString::fromLatin1(policy::triggerName(hangTrigger)));
    if (transition == policy::StallTransition::Ended) {
        payload += QStringLiteral(" stall_ms=%1").arg(stallDurationMs);
    }
    payload += logWriterStatsPayload();
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/hang_watchdog"),
        payload);
}

// Suspend the GUI thread, walk it, resume it, then log the frames. Runs on the watchdog
// thread only, immediately after a hang report, and only within the budget granted by
// policy::shouldCaptureStack.
//
// This is the log's substitute for a crash dump: affected users cannot practically
// transfer a full `.dmp`, so the stack has to arrive as text. Emitted at Level::Fatal so
// every line takes the durable synchronous-flush path — a stack that is still sitting in
// the async writer's queue when the process is killed is worth nothing.
policy::StackCaptureOutcome appendGuiThreadStackReport(
    policy::Trigger trigger, qint64 heartbeatAgeMs, int captureIndex)
{
    const miacode::diag::StackCaptureResult stack =
        miacode::diag::captureRegisteredThreadStack();
    // Queried after the capture, not cached from startup: captureRegisteredThreadStack's
    // lazy fallback can be the call that performs the attempt, so a startup snapshot could
    // under-report. Carried on the header so a reader holding one stack can tell
    // symbol=(nosym) "no PDB on this machine" (sym_ready=1, the documented normal case)
    // from "the symbol handler never came up" (sym_ready=0) without scrolling back to the
    // startup line.
    const miacode::diag::SymbolHandlerStatus symbols = miacode::diag::stackWalkSymbolStatus();
    const policy::StackCaptureOutcome outcome =
        stack.timedOut ? policy::StackCaptureOutcome::TimedOut
        : stack.captured ? policy::StackCaptureOutcome::Captured
                         : policy::StackCaptureOutcome::Failed;
    QString header =
        QStringLiteral("action=gui_thread_stack trigger=%1 heartbeat_age_ms=%2 capture_index=%3 "
                       "capture=%4 supported=%5 captured=%6 frame_count=%7 suspended_us=%8 "
                       "sym_ready=%9 sym_invaded=%10")
            .arg(QString::fromLatin1(policy::triggerName(trigger)))
            .arg(heartbeatAgeMs)
            .arg(captureIndex)
            .arg(QString::fromLatin1(policy::stackCaptureOutcomeName(outcome)))
            .arg(stack.supported ? 1 : 0)
            .arg(stack.captured ? 1 : 0)
            .arg(stack.frameCount)
            .arg(stack.suspendedUs)
            .arg(symbols.ready ? 1 : 0)
            .arg(symbols.invadedProcess ? 1 : 0);
    if (!stack.skipReason.isEmpty()) {
        header += QStringLiteral(" reason=%1").arg(stack.skipReason);
    }
    if (stack.lastErrorCode != 0) {
        header += QStringLiteral(" errno=%1").arg(stack.lastErrorCode);
    }
    if (outcome == policy::StackCaptureOutcome::TimedOut) {
        // Say plainly what happened and what it costs, because this is the one outcome
        // where the diagnostic itself had to intervene in the process it was observing.
        header += QStringLiteral(" forced_resume=1 session_disabled=1");
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/hang_watchdog"),
        header,
        /*force=*/true,
        miacode::debug_log::Level::Fatal);
    for (const QString& frame : stack.frames) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("ui/hang_watchdog_stack"),
            QStringLiteral("capture_index=%1 %2").arg(captureIndex).arg(frame),
            /*force=*/true,
            miacode::debug_log::Level::Fatal);
    }
    return outcome;
}

void watchdogLoop()
{
    // dbghelp is warmed on this thread — never on the GUI thread, and never lazily during a
    // hang — but NOT at the first instruction. SymInitialize enumerates the loaded modules
    // and takes the loader lock, and this thread starts from installGuiHeartbeat(), which
    // runs just after the QApplication constructor while the GUI thread is still loading Qt
    // platform plugins and graphics backends. Both want the loader lock, and the watchdog
    // loses: it would sit blocked through exactly the startup window it is meant to watch.
    //
    // So wait for the first GUI heartbeat. That is the cheapest available proof that the
    // event loop is running and plugin loading is done — the quiet moment the eager call
    // was reaching for and, at startup, could not actually have. The idle trigger is
    // disarmed until that same heartbeat anyway, so this costs no coverage it had before.
    // A marked-phase hang before the first heartbeat still gets a stack: the lazy fallback
    // inside captureRegisteredThreadStack performs the attempt itself.
    //
    // Its own line rather than a field on `action=installed`: that line is written on the
    // GUI thread and installGuiHeartbeat() returns before this thread has necessarily run,
    // so folding the result in would need a handshake the diagnostic has no business
    // introducing. Not force/Fatal either — this is a startup fact recorded long before
    // any hang, not hang evidence that has to survive a kill.
    // Prepared inside the monitor loop below, on the first poll that sees a heartbeat --
    // NOT by blocking here until one arrives. Marked-phase detection is live before the
    // first heartbeat (policy::classify only gates the *idle* trigger on it), so stopping
    // the loop to wait would trade one startup blind spot for a worse one.
    const qint64 symbolWaitStartMs = steadyMs();
    bool symbolsPrepared = false;

    // Sampled once, not per loop: an env lookup every 500 ms is waste, and a threshold
    // that changed mid-session would make a capture impossible to interpret.
    const qint64 activePhaseTimeoutMs = activePhaseHangMs();
    const qint64 idleHeartbeatTimeoutMs = idleHeartbeatHangMs();

    policy::Trigger reportedTrigger = policy::Trigger::None;
    quint64 reportedGeneration = 0;
    qint64 reportedAtMs = 0;
    qint64 lastStackCaptureAtMs = 0;
    int stackCaptureCount = 0;
    bool stackCaptureSessionEnabled = true;
    policy::SuppressionEpisode suppressionEpisode;
    qint64 previousMonitorWakeMs = steadyMs();
    bool stallOpen = false;
    qint64 stallBaselineHeartbeatMs = 0;
    int stallEpisodeCount = 0;
    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kMonitorLoopIntervalMs));
        if (!g_enabled.load(std::memory_order_acquire)) {
            continue;
        }
        const qint64 now = steadyMs();
        const qint64 monitorLoopGap = qMax<qint64>(0, now - previousMonitorWakeMs);
        previousMonitorWakeMs = now;
        if (policy::monitorPauseRequiresRearm(monitorLoopGap, kMonitorLoopIntervalMs)) {
            // The monitor thread itself did not run, as happens across system
            // suspend/hibernate. Neither heartbeat nor phase elapsed time can
            // be attributed to a GUI stall, so restart both baselines.
            //
            // THIS BRANCH DISCARDS A HANG, so it has to say so. It was silent, and that
            // silence is load-bearing: system suspend is not the only thing that stops this
            // thread running. A process-wide freeze starves the watchdog thread along with
            // everything else, and then this heuristic reads that freeze as a suspend,
            // rebaselines the heartbeat, and drops the report -- which would explain why no
            // capture has ever contained a `gui_thread_stale` row, including one taken with
            // the idle threshold lowered to 800 ms specifically to force one.
            //
            // Whether that is what happens is exactly what no capture could answer, because
            // nothing here left a trace. `discarded_heartbeat_age_ms` is the number that
            // settles it: a large value means a real GUI stall was thrown away here.
            const qint64 discardedHeartbeat = g_lastHeartbeatMs.load(std::memory_order_acquire);
            const qint64 discardedHeartbeatAgeMs =
                discardedHeartbeat > 0 ? qMax<qint64>(0, now - discardedHeartbeat) : -1;
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("ui/hang_watchdog"),
                QStringLiteral("action=monitor_rearm monitor_gap_ms=%1 expected_loop_ms=%2 "
                               "discarded_heartbeat_age_ms=%3 discarded_stall_open=%4 "
                               "discarded_trigger=%5 phase_active=%6")
                    .arg(monitorLoopGap)
                    .arg(kMonitorLoopIntervalMs)
                    .arg(discardedHeartbeatAgeMs)
                    .arg(stallOpen ? 1 : 0)
                    .arg(QString::fromLatin1(policy::triggerName(reportedTrigger)))
                    .arg(snapshotPhase().active ? 1 : 0));
            if (discardedHeartbeat > 0) {
                g_lastHeartbeatMs.store(now, std::memory_order_release);
            }
            {
                std::lock_guard<std::mutex> lock(g_phaseMutex);
                if (g_phase.active) {
                    g_phase.startMs = now;
                }
            }
            reportedTrigger = policy::Trigger::None;
            reportedGeneration = 0;
            reportedAtMs = 0;
            if (const auto summary = suppressionEpisode.endEpisode(); summary.has_value()) {
                appendSuppressionEpisodeEnd(*summary, "monitor_rearm");
            }
            // Drop any open stall episode WITHOUT reporting an end: its baseline was taken
            // against a heartbeat timeline that the rearm above has just discarded, so the
            // only duration we could print would be the machine's sleep, not a GUI stall.
            stallOpen = false;
            stallBaselineHeartbeatMs = 0;
            continue;
        }
        const qint64 lastHeartbeat = g_lastHeartbeatMs.load(std::memory_order_acquire);
        const bool heartbeatArmed = lastHeartbeat > 0;
        const qint64 heartbeatAge = heartbeatArmed
            ? qMax<qint64>(0, now - lastHeartbeat)
            : 0;
        if (!symbolsPrepared && heartbeatArmed) {
            symbolsPrepared = true;
            const miacode::diag::SymbolHandlerStatus symbols =
                miacode::diag::prepareStackWalkSymbols();
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("ui/hang_watchdog"),
                QStringLiteral("action=stack_symbols sym_attempted=%1 sym_ready=%2 sym_err=%3 "
                               "sym_invaded=%4 sym_invade_err=%5 waited_for_heartbeat_ms=%6")
                    .arg(symbols.attempted ? 1 : 0)
                    .arg(symbols.ready ? 1 : 0)
                    .arg(symbols.lastErrorCode)
                    .arg(symbols.invadedProcess ? 1 : 0)
                    .arg(symbols.invadeErrorCode)
                    .arg(qMax<qint64>(0, now - symbolWaitStartMs)));
        }
        const PhaseState phase = snapshotPhase();
        // Classified before the stall block so the stall row can carry the hang verdict for
        // the same poll. Pure function, no side effects — evaluating it early changes
        // nothing except what the log can say.
        const qint64 activeMs = phase.active
            ? qMax<qint64>(0, now - phase.startMs)
            : 0;
        const policy::Trigger trigger = policy::classify(
            phase.active,
            activeMs,
            heartbeatArmed,
            heartbeatAge,
            activePhaseTimeoutMs,
            idleHeartbeatTimeoutMs);
        // Runs before the hang classification and independently of it: a stall episode is
        // an observation about the heartbeat alone, and the two must not be able to
        // suppress each other. A stall that goes on to become a hang produces both.
        switch (policy::classifyHeartbeatStall(
            heartbeatArmed, heartbeatAge, kHeartbeatStallMs, stallOpen)) {
        case policy::StallTransition::Began:
            stallOpen = true;
            stallBaselineHeartbeatMs = lastHeartbeat;
            appendStallReport(
                policy::StallTransition::Began, heartbeatAge, 0, phase, stallEpisodeCount,
                idleHeartbeatTimeoutMs, trigger);
            break;
        case policy::StallTransition::Ended: {
            // Both timestamps are written by the GUI thread itself, so their difference is
            // the time it spent not running its own timer — the number the reader wants.
            const qint64 stallMs = stallBaselineHeartbeatMs > 0 && lastHeartbeat > 0
                ? qMax<qint64>(0, lastHeartbeat - stallBaselineHeartbeatMs)
                : 0;
            appendStallReport(
                policy::StallTransition::Ended, heartbeatAge, stallMs, phase, stallEpisodeCount,
                idleHeartbeatTimeoutMs, trigger);
            ++stallEpisodeCount;
            stallOpen = false;
            stallBaselineHeartbeatMs = 0;
            break;
        }
        case policy::StallTransition::None:
            break;
        }
        const bool willReport = policy::shouldReport(
            trigger,
            phase.generation,
            now,
            reportedTrigger,
            reportedGeneration,
            reportedAtMs,
            kRepeatedReportMs);
        if (trigger == policy::Trigger::None) {
            if (const auto summary = suppressionEpisode.endEpisode(); summary.has_value()) {
                appendSuppressionEpisodeEnd(*summary, "trigger_cleared");
            }
            continue;
        }
        if (!willReport) {
            suppressionEpisode.observe(false, trigger);
            continue;
        }
        const auto suppressionSummary = suppressionEpisode.observe(true, trigger);
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("ui/hang_watchdog"),
            QStringLiteral("action=report_gate trigger=%1 will_report=1 "
                           "last_trigger=%2 last_reported_at_ms=%3 age_since_report_ms=%4 "
                           "phase_generation=%5 last_phase_generation=%6 repeat_ms=%7")
                .arg(QString::fromLatin1(policy::triggerName(trigger)))
                .arg(QString::fromLatin1(policy::triggerName(reportedTrigger)))
                .arg(reportedAtMs)
                .arg(reportedAtMs > 0 ? now - reportedAtMs : -1)
                .arg(phase.generation)
                .arg(reportedGeneration)
                .arg(kRepeatedReportMs));
        // Decided before the report is written so the report can state it, but acted on
        // only after — the stale line must stay the first line of the incident.
        const policy::StackCaptureDecision stackDecision = policy::classifyStackCapture(
            stackCaptureSessionEnabled,
            trigger,
            now,
            lastStackCaptureAtMs,
            stackCaptureCount,
            kStackCaptureIntervalMs,
            kMaxStackCapturesPerSession);
        appendWatchdogReport(
            trigger, phase, now, heartbeatAge, stackDecision, stackCaptureCount, suppressionSummary);
        reportedTrigger = trigger;
        reportedGeneration = phase.generation;
        reportedAtMs = now;
        if (stackDecision == policy::StackCaptureDecision::Capture) {
            const policy::StackCaptureOutcome outcome =
                appendGuiThreadStackReport(trigger, heartbeatAge, stackCaptureCount);
            lastStackCaptureAtMs = now;
            ++stackCaptureCount;
            stackCaptureSessionEnabled =
                policy::stackCaptureSessionEnabledAfter(stackCaptureSessionEnabled, outcome);
        }
    }
    if (const auto summary = suppressionEpisode.endEpisode(); summary.has_value()) {
        appendSuppressionEpisodeEnd(*summary, "shutdown");
    }
}

}  // namespace

void installGuiHeartbeat(QObject* owner)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    if (owner == nullptr) {
        owner = QCoreApplication::instance();
    }
    g_enabled.store(true, std::memory_order_release);
    // Do not arm the idle trigger until the event loop proves it can deliver a
    // heartbeat. Startup may legitimately spend more than five seconds before
    // app.exec(), while marked phase monitoring remains available immediately.
    g_lastHeartbeatMs.store(0, std::memory_order_release);

    // installGuiHeartbeat runs ON the GUI thread, which is the only place a real handle
    // to it can be duplicated (GetCurrentThread() is a pseudo-handle). Do it here so the
    // watchdog thread can suspend-and-walk the GUI thread when the heartbeat dies.
    QString stackTargetReason;
    const bool stackTargetRegistered =
        miacode::diag::registerStackWalkTargetThread(&stackTargetReason);

    auto* timer = new QTimer(owner);
    timer->setObjectName(QStringLiteral("MiaCodeGuiHangHeartbeat"));
    timer->setInterval(static_cast<int>(kHeartbeatIntervalMs));
    QObject::connect(timer, &QTimer::timeout, []() {
        g_lastHeartbeatMs.store(steadyMs(), std::memory_order_release);
    });
    timer->start();

    std::call_once(g_threadOnce, []() {
        g_stop.store(false, std::memory_order_release);
        g_thread = std::thread(&watchdogLoop);
    });

    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        QObject::connect(app, &QCoreApplication::aboutToQuit, []() {
            g_enabled.store(false, std::memory_order_release);
            g_stop.store(true, std::memory_order_release);
            if (g_thread.joinable()) {
                g_thread.join();
            }
            // Only after the watchdog thread is joined — releasing the handle while it
            // could still be mid-capture would close a handle in use.
            miacode::diag::releaseStackWalkTargetThread();
        });
    }

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/hang_watchdog"),
        QStringLiteral("action=installed heartbeat_ms=%1 active_phase_timeout_ms=%2 "
                       "idle_heartbeat_timeout_ms=%3 stack_capture=%4 stack_capture_reason=%5 "
                       "stack_capture_interval_ms=%6 stack_capture_max=%7 stack_frame_cap=%8")
            .arg(kHeartbeatIntervalMs)
            .arg(activePhaseHangMs())
            .arg(idleHeartbeatHangMs())
            .arg(stackTargetRegistered ? 1 : 0)
            .arg(stackTargetReason.isEmpty() ? QStringLiteral("(none)") : stackTargetReason)
            .arg(kStackCaptureIntervalMs)
            .arg(kMaxStackCapturesPerSession)
            .arg(miacode::diag::kMaxCapturedStackFrames));
}

void setPhase(const char* phase, const QString& detail)
{
    if (!g_enabled.load(std::memory_order_acquire) || phase == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_phaseMutex);
    g_phase.active = true;
    g_phase.phase = QString::fromUtf8(phase);
    g_phase.detail = detail;
    g_phase.startMs = steadyMs();
    g_phase.generation = g_nextGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void clearPhase(const char* phase)
{
    if (!g_enabled.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_phaseMutex);
    if (phase != nullptr && g_phase.phase != QString::fromUtf8(phase)) {
        return;
    }
    g_phase.active = false;
    g_phase.startMs = 0;
}

PhaseScope::PhaseScope(const char* phase, QString detail) noexcept
    : phase_(phase)
{
    if (!g_enabled.load(std::memory_order_acquire) || phase == nullptr) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(g_phaseMutex);
        previousActive_ = g_phase.active;
        previousPhase_ = g_phase.phase;
        previousDetail_ = g_phase.detail;
        previousStartMs_ = g_phase.startMs;
        previousGeneration_ = g_phase.generation;
        generation_ = g_nextGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
        g_phase.active = true;
        g_phase.phase = QString::fromUtf8(phase);
        g_phase.detail = std::move(detail);
        g_phase.startMs = steadyMs();
        g_phase.generation = generation_;
        armed_ = true;
    } catch (...) {
        armed_ = false;
    }
}

PhaseScope::~PhaseScope() noexcept
{
    if (!armed_ || !g_enabled.load(std::memory_order_acquire)) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(g_phaseMutex);
        if (g_phase.generation != generation_) {
            return;
        }
        g_phase.active = previousActive_;
        g_phase.phase = previousPhase_;
        g_phase.detail = previousDetail_;
        g_phase.startMs = previousStartMs_;
        g_phase.generation = previousGeneration_;
    } catch (...) {
    }
}

}  // namespace miacode::hang_watchdog
