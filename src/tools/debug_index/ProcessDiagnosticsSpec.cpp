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

    if (timer != nullptr) {
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
    }

    // The async writer intentionally keeps QFile handles buffered. Permanent
    // shutdown closes them so this separate reader observes the complete bytes.
    miacode::debug_log::shutdownAsyncLogWriter();
    const QString log = readRuntimeLog();
    ok &= require(log.contains(QStringLiteral("[runtime/idle/resource_gauge]"))
                      && log.contains(QStringLiteral("action=sample sample=0")),
                  QStringLiteral("install emits baseline sample"), err);
    ok &= require(log.contains(QStringLiteral("action=sample sample=1")),
                  QStringLiteral("timer emits next sample"), err);

    miacode::debug_options::setDebugModeEnabled(false);
    qunsetenv("MIACODE_LOG_DIR");

    if (ok) {
        out << "Process diagnostics spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
