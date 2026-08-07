#include <QCoreApplication>
#include <QFile>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/ProcessDiagnostics.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QList<QTimer*> resourceGaugeTimers(QObject* owner)
{
    QList<QTimer*> result;
    const QList<QTimer*> timers = owner->findChildren<QTimer*>(
        QString(), Qt::FindDirectChildrenOnly);
    for (QTimer* timer : timers) {
        if (timer->objectName() == QStringLiteral("MiaCodePeriodicResourceGauge")) {
            result.push_back(timer);
        }
    }
    return result;
}

QString readRuntimeLog()
{
    QFile file(miacode::debug_log::runtimeLogPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

int main(int argc, char* argv[])
{
    QTemporaryDir logDir;
    if (!logDir.isValid()) {
        return 2;
    }
    qputenv("MIACODE_LOG_DIR", logDir.path().toUtf8());
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    miacode::debug_options::setDebugModeEnabled(false);
    miacode::diag::installPeriodicProcessResourceGauge(&app);
    ok &= require(resourceGaugeTimers(&app).isEmpty(),
                  QStringLiteral("debug-off install creates no timer"), err);

    miacode::debug_options::setDebugModeEnabled(true);
    miacode::diag::installPeriodicProcessResourceGauge(&app);
    QList<QTimer*> timers = resourceGaugeTimers(&app);
    ok &= require(timers.size() == 1,
                  QStringLiteral("debug-on install creates one timer"), err);
    QTimer* timer = timers.isEmpty() ? nullptr : timers.constFirst();
    if (timer != nullptr) {
        ok &= require(timer->isActive(), QStringLiteral("resource timer is active"), err);
        ok &= require(timer->interval() == 30000,
                      QStringLiteral("resource timer interval is 30 seconds"), err);
    }

    miacode::diag::installPeriodicProcessResourceGauge(&app);
    ok &= require(resourceGaugeTimers(&app).size() == 1,
                  QStringLiteral("install is idempotent"), err);

    // The baseline sample is POSTED, not taken inline: install runs just after the
    // QApplication constructor, and on Windows the sample opens a DXGI factory, which loads
    // the vendor graphics driver DLLs -- doing that on the GUI thread before Qt initialises
    // its own RHI is the hazard this branch already disabled the D3D11 startup probe for.
    //
    // Hence this processEvents(): without it the baseline never runs and the assertion
    // below fails. That is the only leverage this spec has on the deferral. It cannot also
    // assert the negative ("install emitted nothing yet"), because the async writer keeps
    // its QFile handle open and buffered, so a mid-run read of the log observes nothing
    // either way -- an attempted negative assertion here passed against a deliberately
    // reverted, inline implementation, i.e. it could not fail. Rather than keep a green
    // check that proves nothing, the guard against reverting to an inline first sample is
    // the comment at the call site, not this spec.
    QCoreApplication::processEvents();

    if (timer != nullptr) {
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
    }

    // The async writer intentionally keeps QFile handles buffered. Permanent
    // shutdown closes them so this separate reader observes the complete bytes.
    miacode::debug_log::shutdownAsyncLogWriter();
    const QString log = readRuntimeLog();
    ok &= require(log.contains(QStringLiteral("[runtime/idle/resource_gauge]"))
                      && log.contains(QStringLiteral("action=sample sample=0")),
                  QStringLiteral("first event-loop turn emits baseline sample"), err);
    ok &= require(log.contains(QStringLiteral("action=sample sample=1")),
                  QStringLiteral("timer emits next sample"), err);

    // Per-adapter VRAM gauge. The DXGI enumeration itself is Windows-only, but the gauge
    // must always emit an adapter_scan line so a log reader can distinguish "the probe ran
    // and DXGI reported nothing" from "the diagnostics build was never installed".
    ok &= require(log.contains(QStringLiteral("[runtime/idle/vram_gauge]"))
                      && log.contains(QStringLiteral("action=adapter_scan sample=0")),
                  QStringLiteral("gauge emits a per-adapter VRAM scan line"), err);
#ifndef Q_OS_WIN
    ok &= require(miacode::diag::sampleAdapterVideoMemory().isEmpty(),
                  QStringLiteral("non-windows adapter sampling is empty"), err);
    ok &= require(miacode::diag::adapterProcessVideoMemoryUsageKb(nullptr) == -1,
                  QStringLiteral("non-windows adapter usage reports unavailable"), err);
#endif

    // Payload format: pinned here because an operator greps these keys and the offline
    // analysis reads them positionally-by-name. over_budget is the eviction alarm and is
    // derived, so it gets explicit coverage in both directions.
    miacode::diag::AdapterVideoMemorySample sample;
    sample.index = 1;
    sample.description = QStringLiteral("NVIDIA GeForce MX450");
    sample.luid = QStringLiteral("0x0000000000012345");
    sample.vendorId = 0x10de;
    sample.deviceId = 0x1f97;
    sample.queried = true;
    sample.dedicatedVideoMemoryMb = 2048;
    sample.localBudgetMb = 1800;
    sample.localUsageMb = 1900;
    sample.nonLocalBudgetMb = 4096;
    sample.nonLocalUsageMb = 128;
    const QString payload = miacode::diag::formatAdapterVideoMemoryPayload(sample);
    ok &= require(payload.contains(QStringLiteral("adapter=1"))
                      && payload.contains(QStringLiteral("desc=\"NVIDIA GeForce MX450\""))
                      && payload.contains(QStringLiteral("luid=0x0000000000012345"))
                      && payload.contains(QStringLiteral("vendor=0x10de")),
                  QStringLiteral("adapter payload identifies the adapter"), err);
    ok &= require(payload.contains(QStringLiteral("local_budget_mb=1800"))
                      && payload.contains(QStringLiteral("local_usage_mb=1900"))
                      && payload.contains(QStringLiteral("nonlocal_budget_mb=4096"))
                      && payload.contains(QStringLiteral("nonlocal_usage_mb=128")),
                  QStringLiteral("adapter payload reports budget vs usage per segment"), err);
    ok &= require(payload.contains(QStringLiteral("local_over_budget=1"))
                      && payload.contains(QStringLiteral("nonlocal_over_budget=0")),
                  QStringLiteral("over-budget flag is derived per segment"), err);
    ok &= require(payload.contains(QStringLiteral("dedicated_mb=2048"))
                      && payload.contains(QStringLiteral("queried=1")),
                  QStringLiteral("adapter payload reports capacity and query success"), err);

    miacode::diag::AdapterVideoMemorySample unknown;
    const QString unknownPayload = miacode::diag::formatAdapterVideoMemoryPayload(unknown);
    ok &= require(unknownPayload.contains(QStringLiteral("desc=\"(unknown)\""))
                      && unknownPayload.contains(QStringLiteral("queried=0"))
                      && unknownPayload.contains(QStringLiteral("local_over_budget=0")),
                  QStringLiteral("unqueried adapter degrades without false alarms"), err);

    miacode::debug_options::setDebugModeEnabled(false);
    qunsetenv("MIACODE_LOG_DIR");

    if (ok) {
        out << "Process diagnostics spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
