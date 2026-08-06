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

void appendWatchdogReport(
    policy::Trigger trigger,
    const PhaseState& phase,
    qint64 nowMs,
    qint64 heartbeatAgeMs)
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
    const policy::StackCaptureOutcome outcome =
        stack.timedOut ? policy::StackCaptureOutcome::TimedOut
        : stack.captured ? policy::StackCaptureOutcome::Captured
                         : policy::StackCaptureOutcome::Failed;
    QString header =
        QStringLiteral("action=gui_thread_stack trigger=%1 heartbeat_age_ms=%2 capture_index=%3 "
                       "capture=%4 supported=%5 captured=%6 frame_count=%7 suspended_us=%8")
            .arg(QString::fromLatin1(policy::triggerName(trigger)))
            .arg(heartbeatAgeMs)
            .arg(captureIndex)
            .arg(QString::fromLatin1(policy::stackCaptureOutcomeName(outcome)))
            .arg(stack.supported ? 1 : 0)
            .arg(stack.captured ? 1 : 0)
            .arg(stack.frameCount)
            .arg(stack.suspendedUs);
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
    // Warm dbghelp here, on this thread, at process start — never during a hang. See
    // prepareStackWalkSymbols() for why deferring it to first use is a deadlock risk.
    miacode::diag::prepareStackWalkSymbols();

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
    qint64 previousMonitorWakeMs = steadyMs();
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
            if (g_lastHeartbeatMs.load(std::memory_order_acquire) > 0) {
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
            continue;
        }
        const qint64 lastHeartbeat = g_lastHeartbeatMs.load(std::memory_order_acquire);
        const bool heartbeatArmed = lastHeartbeat > 0;
        const qint64 heartbeatAge = heartbeatArmed
            ? qMax<qint64>(0, now - lastHeartbeat)
            : 0;
        const PhaseState phase = snapshotPhase();
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
        if (!policy::shouldReport(
                trigger,
                phase.generation,
                now,
                reportedTrigger,
                reportedGeneration,
                reportedAtMs,
                kRepeatedReportMs)) {
            continue;
        }
        appendWatchdogReport(trigger, phase, now, heartbeatAge);
        reportedTrigger = trigger;
        reportedGeneration = phase.generation;
        reportedAtMs = now;
        if (policy::shouldCaptureStack(
                stackCaptureSessionEnabled,
                trigger,
                now,
                lastStackCaptureAtMs,
                stackCaptureCount,
                kStackCaptureIntervalMs,
                kMaxStackCapturesPerSession)) {
            const policy::StackCaptureOutcome outcome =
                appendGuiThreadStackReport(trigger, heartbeatAge, stackCaptureCount);
            lastStackCaptureAtMs = now;
            ++stackCaptureCount;
            stackCaptureSessionEnabled =
                policy::stackCaptureSessionEnabledAfter(stackCaptureSessionEnabled, outcome);
        }
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
