#include <QCoreApplication>
#include <QTextStream>

#include "app/qml_ui/QmlUiRootLifecycle.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyCreationOrdering(QTextStream& err)
{
    miacode::qml_ui::RootLifecycle lifecycle;
    return require(!lifecycle.installRootEventFilter(),
                   QStringLiteral("event filter cannot precede root registration"), err)
        && require(!lifecycle.createChartDropOverlay(),
                   QStringLiteral("overlay cannot precede root registration"), err)
        && require(lifecycle.registerRoot(),
                   QStringLiteral("root registers exactly once"), err)
        && require(!lifecycle.canShowRoot(),
                   QStringLiteral("registered root stays hidden until drag route is complete"), err)
        && require(lifecycle.installRootEventFilter(),
                   QStringLiteral("MainWindow event filter follows root registration"), err)
        && require(!lifecycle.canShowRoot(),
                   QStringLiteral("root stays hidden until overlay lifecycle is ready"), err)
        && require(lifecycle.createChartDropOverlay(),
                   QStringLiteral("overlay follows root filter installation"), err)
        && require(lifecycle.canShowRoot(),
                   QStringLiteral("root can show only after its complete drag route is ready"), err)
        && require(!lifecycle.shouldMonitorChartDropOverlay(),
                   QStringLiteral("inactive overlay does not keep a geometry monitor running"), err)
        && require(lifecycle.setChartDropOverlayVisible(true)
                       && lifecycle.shouldMonitorChartDropOverlay(),
                   QStringLiteral("visible overlay starts geometry monitoring"), err)
        && require(lifecycle.setChartDropOverlayVisible(false)
                       && !lifecycle.shouldMonitorChartDropOverlay(),
                   QStringLiteral("hidden overlay stops geometry monitoring immediately"), err);
}

bool verifyRepeatShutdownIsSafe(QTextStream& err)
{
    miacode::qml_ui::RootLifecycle lifecycle;
    lifecycle.registerRoot();
    lifecycle.installRootEventFilter();
    lifecycle.createChartDropOverlay();
    lifecycle.setChartDropOverlayVisible(true);
    return require(lifecycle.beginRelease(),
                   QStringLiteral("first shutdown owns chart-drop cleanup"), err)
        && require(!lifecycle.hasRegisteredRoot() && !lifecycle.canShowRoot()
                       && !lifecycle.shouldMonitorChartDropOverlay(),
                   QStringLiteral("first shutdown leaves no root or active monitor eligible for use"), err)
        && require(!lifecycle.beginRelease(),
                   QStringLiteral("repeat shutdown performs no second cleanup"), err)
        && require(!lifecycle.registerRoot(),
                   QStringLiteral("released lifecycle cannot expose a replacement dangling root"), err);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);
    const bool ok = verifyCreationOrdering(err) && verifyRepeatShutdownIsSafe(err);
    if (ok) {
        out << "qml_ui_bootstrap_lifecycle_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
