// Contract regression for the Widgets-free job-progress surface.
//
// Links Qt6::Core only: reaching for QProgressDialog again fails the build.

#include "app/v2/JobProgressService.h"

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
    miacode::v2::JobProgressService service;
    QSignalSpy changed(&service, &miacode::v2::JobProgressService::changed);

    bool ok = require(!service.active() && service.percent() == 0,
                      QStringLiteral("an idle service reports no job"), err);

    service.begin(QStringLiteral("Pack as ZIP"), QStringLiteral("Preparing"), true);
    ok &= require(service.active() && service.cancellable() && !service.cancelRequested()
                      && service.title() == QStringLiteral("Pack as ZIP")
                      && service.label() == QStringLiteral("Preparing")
                      && service.percent() == 0 && changed.count() == 1,
                  QStringLiteral("begin publishes the job in one notification"), err);

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
                      && !service.indeterminate(),
                  QStringLiteral("end clears the job back to idle"), err);

    const int afterEnd = changed.count();
    service.end();
    service.report(50, QStringLiteral("ignored"));
    ok &= require(changed.count() == afterEnd && service.percent() == 0,
                  QStringLiteral("reporting or ending while idle is a no-op"), err);
    return ok;
}

bool verifyCooperativeCancel(QTextStream& err)
{
    miacode::v2::JobProgressService service;
    QSignalSpy cancellations(&service, &miacode::v2::JobProgressService::cancellationRequested);

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

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const bool ok = verifyLifecycle(err) && verifyCooperativeCancel(err);
    if (ok) {
        QTextStream out(stdout);
        out << "job_progress_service_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
