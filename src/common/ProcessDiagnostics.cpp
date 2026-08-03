#include "ProcessDiagnostics.h"

#include "DebugLog.h"
#include "DebugOptions.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QString>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "psapi.lib")
#endif
#endif

namespace miacode::diag {

namespace {

constexpr int kPeriodicResourceGaugeIntervalMs = 30000;

QString applicationStateName()
{
    const auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (app == nullptr) {
        return QStringLiteral("not_gui");
    }
    switch (app->applicationState()) {
    case Qt::ApplicationSuspended:
        return QStringLiteral("suspended");
    case Qt::ApplicationHidden:
        return QStringLiteral("hidden");
    case Qt::ApplicationInactive:
        return QStringLiteral("inactive");
    case Qt::ApplicationActive:
        return QStringLiteral("active");
    }
    return QStringLiteral("unknown");
}

}  // namespace

QString processResourceGaugePayload()
{
#ifdef Q_OS_WIN
    const HANDLE process = GetCurrentProcess();
    const DWORD gdiObjects = GetGuiResources(process, GR_GDIOBJECTS);
    const DWORD userObjects = GetGuiResources(process, GR_USEROBJECTS);
    DWORD kernelHandles = 0;
    GetProcessHandleCount(process, &kernelHandles);
    quint64 workingSetMb = 0;
    quint64 peakWorkingSetMb = 0;
    quint64 privateMb = 0;
    quint64 commitMb = 0;
    quint64 pagedPoolKb = 0;
    quint64 nonPagedPoolKb = 0;
    quint64 pageFaults = 0;
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(
            process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        workingSetMb = static_cast<quint64>(pmc.WorkingSetSize) / (1024ull * 1024ull);
        peakWorkingSetMb = static_cast<quint64>(pmc.PeakWorkingSetSize) / (1024ull * 1024ull);
        privateMb = static_cast<quint64>(pmc.PrivateUsage) / (1024ull * 1024ull);
        commitMb = static_cast<quint64>(pmc.PagefileUsage) / (1024ull * 1024ull);
        pagedPoolKb = static_cast<quint64>(pmc.QuotaPagedPoolUsage) / 1024ull;
        nonPagedPoolKb = static_cast<quint64>(pmc.QuotaNonPagedPoolUsage) / 1024ull;
        pageFaults = static_cast<quint64>(pmc.PageFaultCount);
    }
    return QStringLiteral(
               "gdi_objects=%1 user_objects=%2 kernel_handles=%3 working_set_mb=%4 "
               "peak_working_set_mb=%5 private_mb=%6 commit_mb=%7 paged_pool_kb=%8 "
               "nonpaged_pool_kb=%9 page_faults=%10")
        .arg(static_cast<quint64>(gdiObjects))
        .arg(static_cast<quint64>(userObjects))
        .arg(static_cast<quint64>(kernelHandles))
        .arg(workingSetMb)
        .arg(peakWorkingSetMb)
        .arg(privateMb)
        .arg(commitMb)
        .arg(pagedPoolKb)
        .arg(nonPagedPoolKb)
        .arg(pageFaults);
#else
    return QStringLiteral(
        "gdi_objects=0 user_objects=0 kernel_handles=0 working_set_mb=0 peak_working_set_mb=0 "
        "private_mb=0 commit_mb=0 paged_pool_kb=0 nonpaged_pool_kb=0 page_faults=0");
#endif
}

void installPeriodicProcessResourceGauge(QObject* owner)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    if (owner == nullptr) {
        owner = QCoreApplication::instance();
    }
    if (owner == nullptr
        || owner->findChild<QTimer*>(
            QStringLiteral("MiaCodePeriodicResourceGauge"),
            Qt::FindDirectChildrenOnly) != nullptr) {
        return;
    }

    auto* timer = new QTimer(owner);
    timer->setObjectName(QStringLiteral("MiaCodePeriodicResourceGauge"));
    timer->setInterval(kPeriodicResourceGaugeIntervalMs);

    const auto startedAt = std::chrono::steady_clock::now();
    const auto sampleCounter = std::make_shared<quint64>(0);
    const auto emitSample = [sampleCounter, startedAt]() {
        const qint64 uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count();
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("idle/resource_gauge"),
            QStringLiteral("action=sample sample=%1 uptime_ms=%2 app_state=%3 %4")
                .arg((*sampleCounter)++)
                .arg(qMax<qint64>(0, uptimeMs))
                .arg(applicationStateName())
                .arg(processResourceGaugePayload()));
    };

    QObject::connect(timer, &QTimer::timeout, timer, emitSample);
    emitSample();
    timer->start();
}

static qint64 stageScopePrivateBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        return static_cast<qint64>(pmc.PrivateUsage);
    }
#endif
    return -1;
}

qint64 processPrivateBytes()
{
    return stageScopePrivateBytes();
}

namespace {
// beta7 render-vs-GUI leak gauge cross-thread state. Cheap atomics; written/read from the GUI
// thread (play-start, pause) and the render thread (present). See ProcessDiagnostics.h leak_gauge.
std::atomic<qint64> g_leakPlayStartPriv{-1};
std::atomic<qint64> g_leakPausePriv{-1};
std::atomic<quint64> g_leakPauseTxn{0};
std::atomic<bool> g_leakArmed{false};
std::atomic<int> g_leakInflight{0};
std::atomic<int> g_leakInflightPeak{0};
std::atomic<quint64> g_timelinePresents{0};
std::atomic<quint64> g_playStartPresents{0};
}  // namespace

namespace leak_gauge {

void notePlayStartPrivateBytes(qint64 bytes)
{
    g_leakPlayStartPriv.store(bytes, std::memory_order_relaxed);
}

qint64 playStartPrivateBytes()
{
    return g_leakPlayStartPriv.load(std::memory_order_relaxed);
}

void armRenderSample(qint64 pausePrivateBytes, quint64 txn)
{
    g_leakPausePriv.store(pausePrivateBytes, std::memory_order_relaxed);
    g_leakPauseTxn.store(txn, std::memory_order_relaxed);
    g_leakArmed.store(true, std::memory_order_release);
}

bool takeRenderSample(qint64* outPausePrivateBytes, quint64* outTxn)
{
    if (!g_leakArmed.exchange(false, std::memory_order_acquire)) {
        return false;
    }
    if (outPausePrivateBytes != nullptr) {
        *outPausePrivateBytes = g_leakPausePriv.load(std::memory_order_relaxed);
    }
    if (outTxn != nullptr) {
        *outTxn = g_leakPauseTxn.load(std::memory_order_relaxed);
    }
    return true;
}

void noteInflightDispatch()
{
    const int depth = g_leakInflight.fetch_add(1, std::memory_order_relaxed) + 1;
    int prevPeak = g_leakInflightPeak.load(std::memory_order_relaxed);
    while (depth > prevPeak
           && !g_leakInflightPeak.compare_exchange_weak(prevPeak, depth, std::memory_order_relaxed)) {
    }
}

void noteInflightApplied()
{
    g_leakInflight.fetch_sub(1, std::memory_order_relaxed);
}

int inflightDepth()
{
    return g_leakInflight.load(std::memory_order_relaxed);
}

int inflightPeak()
{
    return g_leakInflightPeak.load(std::memory_order_relaxed);
}

void noteTimelinePresent()
{
    g_timelinePresents.fetch_add(1, std::memory_order_relaxed);
}

void markPlayStartTimelinePresents()
{
    g_playStartPresents.store(
        g_timelinePresents.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

quint64 timelinePresentsSincePlayStart()
{
    const quint64 now = g_timelinePresents.load(std::memory_order_relaxed);
    const quint64 start = g_playStartPresents.load(std::memory_order_relaxed);
    return now >= start ? now - start : 0;
}

}  // namespace leak_gauge

MemoryStageScope::MemoryStageScope(const char* scope, const char* stage)
    : scope_(scope), stage_(stage), startBytes_(-1)
{
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        startBytes_ = stageScopePrivateBytes();
    }
}

MemoryStageScope::~MemoryStageScope()
{
    if (startBytes_ < 0) {
        return;
    }
    const qint64 endBytes = stageScopePrivateBytes();
    if (endBytes < 0) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QString::fromLatin1(scope_),
        QStringLiteral("stage=%1 private_mb=%2 delta_kb=%3")
            .arg(QString::fromLatin1(stage_))
            .arg(endBytes / (1024 * 1024))
            .arg((endBytes - startBytes_) / 1024));
}

}  // namespace miacode::diag
