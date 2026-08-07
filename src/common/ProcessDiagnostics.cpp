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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#if defined(_MSC_VER)
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dxgi.lib")
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

#ifdef Q_OS_WIN
constexpr quint64 kBytesPerMb = 1024ull * 1024ull;

quint64 bytesToMb(quint64 bytes)
{
    return bytes / kBytesPerMb;
}
#endif

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

qint64 adapterProcessVideoMemoryUsageKb(void* dxgiAdapter)
{
#ifdef Q_OS_WIN
    if (dxgiAdapter == nullptr) {
        return -1;
    }
    auto* adapter = static_cast<IDXGIAdapter*>(dxgiAdapter);
    IDXGIAdapter3* adapter3 = nullptr;
    if (FAILED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3)))
        || adapter3 == nullptr) {
        return -1;
    }
    quint64 totalBytes = 0;
    bool any = false;
    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    ZeroMemory(&info, sizeof(info));
    if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        totalBytes += info.CurrentUsage;
        any = true;
    }
    ZeroMemory(&info, sizeof(info));
    if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info))) {
        totalBytes += info.CurrentUsage;
        any = true;
    }
    adapter3->Release();
    return any ? static_cast<qint64>(totalBytes / 1024ull) : -1;
#else
    Q_UNUSED(dxgiAdapter);
    return -1;
#endif
}

QList<AdapterVideoMemorySample> sampleAdapterVideoMemory()
{
    QList<AdapterVideoMemorySample> samples;
#ifdef Q_OS_WIN
    // Cached rather than created and destroyed on every 30 s sample. CreateDXGIFactory1
    // touches the graphics driver DLLs, and doing that on the GUI thread on a timer means
    // periodically contending with the two QSG render threads for the same DXGI objects --
    // a self-inflicted hitch in a probe whose whole purpose is to measure hitches.
    //
    // IsCurrent() is what keeps the cache honest: it goes false when the adapter set
    // changes, which is exactly when a stale factory would start under-reporting. On a
    // hybrid laptop -- the configuration these captures come from -- that is not
    // hypothetical.
    //
    // Deliberately never released. It is a process-lifetime singleton, and releasing a COM
    // object from a static destructor runs after COM may already have been torn down. A
    // single leaked factory in a debug-only probe is the cheaper side of that trade.
    static IDXGIFactory1* factory = nullptr;
    if (factory != nullptr && factory->IsCurrent() == FALSE) {
        factory->Release();
        factory = nullptr;
    }
    if (factory == nullptr
        && (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))
            || factory == nullptr)) {
        factory = nullptr;
        return samples;
    }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        if (adapter == nullptr) {
            continue;
        }
        AdapterVideoMemorySample sample;
        sample.index = static_cast<int>(index);
        DXGI_ADAPTER_DESC1 desc;
        ZeroMemory(&desc, sizeof(desc));
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            sample.description = QString::fromWCharArray(desc.Description).trimmed();
            sample.luid = QStringLiteral("0x%1%2")
                              .arg(static_cast<quint32>(desc.AdapterLuid.HighPart), 8, 16, QLatin1Char('0'))
                              .arg(static_cast<quint32>(desc.AdapterLuid.LowPart), 8, 16, QLatin1Char('0'));
            sample.vendorId = static_cast<quint32>(desc.VendorId);
            sample.deviceId = static_cast<quint32>(desc.DeviceId);
            sample.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            sample.dedicatedVideoMemoryMb = bytesToMb(static_cast<quint64>(desc.DedicatedVideoMemory));
            sample.sharedSystemMemoryMb = bytesToMb(static_cast<quint64>(desc.SharedSystemMemory));
        }
        IDXGIAdapter3* adapter3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(
                __uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3)))
            && adapter3 != nullptr) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info;
            ZeroMemory(&info, sizeof(info));
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                sample.queried = true;
                sample.localBudgetMb = bytesToMb(info.Budget);
                sample.localUsageMb = bytesToMb(info.CurrentUsage);
                sample.localReservedMb = bytesToMb(info.CurrentReservation);
            }
            ZeroMemory(&info, sizeof(info));
            if (SUCCEEDED(
                    adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info))) {
                sample.queried = true;
                sample.nonLocalBudgetMb = bytesToMb(info.Budget);
                sample.nonLocalUsageMb = bytesToMb(info.CurrentUsage);
                sample.nonLocalReservedMb = bytesToMb(info.CurrentReservation);
            }
            adapter3->Release();
        }
        samples.append(sample);
        adapter->Release();
        adapter = nullptr;
    }
    // No factory->Release() here: the factory outlives this call by design, see above.
    // The per-adapter interfaces above are still released each pass -- those are cheap to
    // re-enumerate and holding them would pin adapters across a device change.
#endif
    return samples;
}

QString formatAdapterVideoMemoryPayload(const AdapterVideoMemorySample& sample)
{
    // over_budget is the actual alarm: once CurrentUsage exceeds the Budget DXGI grants
    // this process, the driver starts evicting to system memory and every frame pays a
    // PCIe re-upload. Precomputed here so an operator can grep `local_over_budget=1`
    // without doing arithmetic across two fields.
    const int localOverBudget =
        (sample.localBudgetMb > 0 && sample.localUsageMb > sample.localBudgetMb) ? 1 : 0;
    const int nonLocalOverBudget =
        (sample.nonLocalBudgetMb > 0 && sample.nonLocalUsageMb > sample.nonLocalBudgetMb) ? 1 : 0;
    return QStringLiteral(
               "adapter=%1 desc=\"%2\" luid=%3 vendor=0x%4 device=0x%5 software=%6 queried=%7 "
               "dedicated_mb=%8 shared_mb=%9 local_budget_mb=%10 local_usage_mb=%11 "
               "local_reserved_mb=%12 local_over_budget=%13 nonlocal_budget_mb=%14 "
               "nonlocal_usage_mb=%15 nonlocal_reserved_mb=%16 nonlocal_over_budget=%17")
        .arg(sample.index)
        .arg(sample.description.isEmpty() ? QStringLiteral("(unknown)") : sample.description)
        .arg(sample.luid.isEmpty() ? QStringLiteral("(none)") : sample.luid)
        .arg(sample.vendorId, 4, 16, QLatin1Char('0'))
        .arg(sample.deviceId, 4, 16, QLatin1Char('0'))
        .arg(sample.software ? 1 : 0)
        .arg(sample.queried ? 1 : 0)
        .arg(sample.dedicatedVideoMemoryMb)
        .arg(sample.sharedSystemMemoryMb)
        .arg(sample.localBudgetMb)
        .arg(sample.localUsageMb)
        .arg(sample.localReservedMb)
        .arg(localOverBudget)
        .arg(sample.nonLocalBudgetMb)
        .arg(sample.nonLocalUsageMb)
        .arg(sample.nonLocalReservedMb)
        .arg(nonLocalOverBudget);
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
        const quint64 sample = (*sampleCounter)++;
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("idle/resource_gauge"),
            QStringLiteral("action=sample sample=%1 uptime_ms=%2 app_state=%3 %4")
                .arg(sample)
                .arg(qMax<qint64>(0, uptimeMs))
                .arg(applicationStateName())
                .arg(processResourceGaugePayload()));

        // Per-adapter VRAM on the same cadence. The scan line is emitted even when zero
        // adapters are found so the reader can tell "probe ran, DXGI unavailable" from
        // "diagnostics never installed".
        const QList<AdapterVideoMemorySample> adapters = sampleAdapterVideoMemory();
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("idle/vram_gauge"),
            QStringLiteral("action=adapter_scan sample=%1 uptime_ms=%2 adapter_count=%3")
                .arg(sample)
                .arg(qMax<qint64>(0, uptimeMs))
                .arg(adapters.size()));
        for (const AdapterVideoMemorySample& adapter : adapters) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("idle/vram_gauge"),
                QStringLiteral("action=sample sample=%1 uptime_ms=%2 %3")
                    .arg(sample)
                    .arg(qMax<qint64>(0, uptimeMs))
                    .arg(formatAdapterVideoMemoryPayload(adapter)));
        }
    };

    QObject::connect(timer, &QTimer::timeout, timer, emitSample);
    // The first sample used to run synchronously, right here. installPeriodicProcessResourceGauge
    // is called just after the QApplication constructor, so that put a CreateDXGIFactory1 --
    // which loads the vendor graphics driver DLLs -- on the GUI thread BEFORE Qt has
    // initialised its own RHI. This branch already turned the D3D11 startup probe off by
    // default for precisely that reason; this was the same hazard left in place next to it.
    //
    // Posting it instead keeps the sample (uptime_ms just reads a little later) while
    // moving the driver load behind the event loop, where Qt has already chosen and
    // initialised its backend.
    QTimer::singleShot(0, timer, emitSample);
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
