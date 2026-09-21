// Contract regression for the Widgets-free job-progress surface.
//
// Links Qt6::Core only: reaching for QProgressDialog again fails the build.

#include "app/services/JobProgressService.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyLifecycle(QTextStream& err)
{
    miacode::JobProgressService service;
    QSignalSpy changed(&service, &miacode::JobProgressService::changed);

    bool ok = require(!service.active() && service.percent() == 0
                          && service.taskType() == miacode::JobProgressService::TaskType::Generic
                          && !service.chartExport()
                          && service.taskTypeName() == QStringLiteral("generic"),
                      QStringLiteral("an idle service reports no job"), err);

    service.begin(QStringLiteral("Pack as ZIP"), QStringLiteral("Preparing"), true);
    ok &= require(service.active() && service.cancellable() && !service.cancelRequested()
                      && service.title() == QStringLiteral("Pack as ZIP")
                      && service.label() == QStringLiteral("Preparing")
                      && service.percent() == 0 && changed.count() == 1,
                  QStringLiteral("begin publishes the job in one notification"), err);
    ok &= require(service.taskType() == miacode::JobProgressService::TaskType::Generic
                      && !service.chartExport()
                      && service.taskTypeName() == QStringLiteral("generic"),
                  QStringLiteral("the three-argument begin defaults to a generic task"), err);

    service.report(42, QStringLiteral("Packing 3/7"));
    ok &= require(service.percent() == 42 && service.label() == QStringLiteral("Packing 3/7")
                      && changed.count() == 2,
                  QStringLiteral("report updates percent and label together"), err);

    service.report(42, QStringLiteral("Packing 3/7"));
    ok &= require(changed.count() == 2,
                  QStringLiteral("an unchanged report does not re-notify the shell"), err);

    service.report(-5, QStringLiteral("under"));
    ok &= require(service.percent() == 0, QStringLiteral("percent clamps at zero"), err);
    service.report(140, QStringLiteral("over"));
    ok &= require(service.percent() == 100, QStringLiteral("percent clamps at a hundred"), err);

    service.reportIndeterminate(QStringLiteral("Encoding"));
    ok &= require(service.indeterminate() && service.label() == QStringLiteral("Encoding"),
                  QStringLiteral("a stage without measurable progress switches to indeterminate"),
                  err);
    service.report(10, QStringLiteral("Back to measured"));
    ok &= require(!service.indeterminate() && service.percent() == 10,
                  QStringLiteral("a measured report leaves indeterminate mode"), err);

    service.end();
    ok &= require(!service.active() && service.percent() == 0 && service.title().isEmpty()
                      && service.label().isEmpty() && !service.cancellable()
                      && !service.indeterminate()
                      && service.taskType() == miacode::JobProgressService::TaskType::Generic
                      && !service.chartExport()
                      && service.taskTypeName() == QStringLiteral("generic"),
                  QStringLiteral("end clears the job back to idle"), err);

    const int afterEnd = changed.count();
    service.end();
    service.report(50, QStringLiteral("ignored"));
    ok &= require(changed.count() == afterEnd && service.percent() == 0,
                  QStringLiteral("reporting or ending while idle is a no-op"), err);

    service.begin(QStringLiteral("Export chart"), QStringLiteral("Rendering"), true,
                  miacode::JobProgressService::TaskType::ChartExport);
    ok &= require(service.taskType() == miacode::JobProgressService::TaskType::ChartExport
                      && service.chartExport()
                      && service.taskTypeName() == QStringLiteral("chartExport"),
                  QStringLiteral("a chart-export begin publishes the typed task state"), err);
    const int afterChartBegin = changed.count();
    service.end();
    ok &= require(changed.count() == afterChartBegin + 1
                      && service.taskType() == miacode::JobProgressService::TaskType::Generic
                      && !service.chartExport()
                      && service.taskTypeName() == QStringLiteral("generic"),
                  QStringLiteral("ending a chart-export task restores generic state"), err);
    return ok;
}

bool verifyCooperativeCancel(QTextStream& err)
{
    miacode::JobProgressService service;
    QSignalSpy cancellations(&service, &miacode::JobProgressService::cancellationRequested);

    service.requestCancel();
    bool ok = require(!service.cancelRequested(),
                      QStringLiteral("cancelling an idle service does nothing"), err);

    service.begin(QStringLiteral("Job"), QStringLiteral("Working"), false);
    service.requestCancel();
    ok &= require(!service.cancelRequested(),
                  QStringLiteral("a job declared uncancellable cannot be cancelled"), err);

    const quint64 cancellable = service.begin(QStringLiteral("Job"), QStringLiteral("Working"), true);
    service.requestCancel();
    ok &= require(service.cancelRequested() && service.active(),
                  QStringLiteral("cancel only raises a flag; the job stays active until it stops itself"),
                  err);
    ok &= require(cancellations.count() == 1
                      && cancellations.at(0).at(0).toULongLong() == cancellable,
                  QStringLiteral("cancellation names the job it belongs to, so one job cannot cancel another"),
                  err);

    service.requestCancel();
    ok &= require(cancellations.count() == 1,
                  QStringLiteral("a second cancel on the same job does not re-fire"), err);

    // The critical one: a cancel left over from the previous job must not abort
    // the next one at its first checkpoint.
    const quint64 next = service.begin(QStringLiteral("Next job"), QStringLiteral("Working"), true);
    ok &= require(!service.cancelRequested() && next != cancellable,
                  QStringLiteral("a new job starts uncancelled, under a fresh token"), err);
    return ok;
}

// The comic surface is driven by the task type, so the boundary between a
// chart-export job and every other job has to stay exact — including the case
// where one job replaces another without an idle gap in between.
bool verifyTaskTypeBoundary(QTextStream& err)
{
    miacode::JobProgressService service;

    // The comic region reads the job identity from QML, so the token has to be a
    // readable, notifying property rather than a plain C++ accessor.
    const QMetaObject* const meta = &miacode::JobProgressService::staticMetaObject;
    const int tokenIndex = meta->indexOfProperty("token");
    const int chartIndex = meta->indexOfProperty("chartExport");
    bool ok = require(tokenIndex >= 0 && chartIndex >= 0
                          && meta->property(tokenIndex).isReadable()
                          && meta->property(tokenIndex).hasNotifySignal()
                          && meta->property(chartIndex).isReadable()
                          && meta->property(chartIndex).hasNotifySignal(),
                      QStringLiteral("the job token and chart-export flag are readable QML properties"), err);
    ok &= require(tokenIndex < 0 || QString::fromLatin1(meta->property(tokenIndex).notifySignal().name())
                           == QStringLiteral("changed"),
                  QStringLiteral("the job token notifies on the shared changed signal"), err);

    const quint64 firstExport = service.begin(
        QStringLiteral("Export chart"), QStringLiteral("Rendering"), true,
        miacode::JobProgressService::TaskType::ChartExport);
    ok &= require(service.active() && service.chartExport()
                      && service.taskTypeName() == QStringLiteral("chartExport"),
                  QStringLiteral("a chart export publishes the typed surface"), err);

    // Re-entry: a second chart export replaces the first without an end(), so
    // neither active nor chartExport changes value. The raised token is what
    // tells a consumer a different job now owns the surface.
    const quint64 secondExport = service.begin(
        QStringLiteral("Export chart"), QStringLiteral("Rendering again"), true,
        miacode::JobProgressService::TaskType::ChartExport);
    ok &= require(service.active() && service.chartExport()
                      && secondExport != firstExport && secondExport > firstExport,
                  QStringLiteral("a replacing chart export keeps the type but raises the token"), err);
    ok &= require(service.percent() == 0 && service.label() == QStringLiteral("Rendering again")
                      && !service.cancelRequested(),
                  QStringLiteral("a replacing job restarts clean at zero percent"), err);

    // A generic job taking the surface over must stand the comic region down.
    service.begin(QStringLiteral("Pack as ZIP"), QStringLiteral("Preparing"), true);
    ok &= require(service.active() && !service.chartExport()
                      && service.taskTypeName() == QStringLiteral("generic"),
                  QStringLiteral("a generic job taking over clears the chart-export flag"), err);

    // And a chart export taking it back must raise it again.
    const quint64 thirdExport = service.begin(
        QStringLiteral("Export chart"), QStringLiteral("Final pass"), true,
        miacode::JobProgressService::TaskType::ChartExport);
    ok &= require(service.chartExport() && thirdExport > secondExport,
                  QStringLiteral("a chart export taking over raises the flag again"), err);

    service.end();
    ok &= require(!service.active() && !service.chartExport()
                      && service.taskType() == miacode::JobProgressService::TaskType::Generic
                      && service.token() == thirdExport,
                  QStringLiteral("ending a chart export returns to the idle generic state"), err);

    service.end();
    ok &= require(!service.chartExport(),
                  QStringLiteral("a repeated end() never claims the comic surface"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const bool ok = verifyLifecycle(err) && verifyCooperativeCancel(err)
        && verifyTaskTypeBoundary(err);
    if (ok) {
        QTextStream out(stdout);
        out << "job_progress_service_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
