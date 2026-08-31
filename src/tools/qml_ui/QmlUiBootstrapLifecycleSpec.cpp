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
        && require(!lifecycle.installDropBridge(),
                   QStringLiteral("drop bridge cannot precede root registration"), err)
        && require(lifecycle.registerRoot(),
                   QStringLiteral("root registers exactly once"), err)
        && require(!lifecycle.canShowRoot(),
                   QStringLiteral("registered root stays hidden until the drop bridge is installed"), err)
        && require(lifecycle.installRootEventFilter(),
                   QStringLiteral("root event-filter stage follows root registration"), err)
        && require(!lifecycle.canShowRoot(),
                   QStringLiteral("root stays hidden until the drop bridge is installed"), err)
        && require(lifecycle.installDropBridge(),
                   QStringLiteral("drop bridge follows root event-filter installation"), err)
        && require(lifecycle.canShowRoot(),
                   QStringLiteral("root can show with the non-visual drop route ready"), err);
}

bool verifyRepeatShutdownIsSafe(QTextStream& err)
{
    miacode::qml_ui::RootLifecycle lifecycle;
    lifecycle.registerRoot();
    lifecycle.installRootEventFilter();
    lifecycle.installDropBridge();
    return require(lifecycle.beginRelease(),
                   QStringLiteral("first shutdown owns root/drop cleanup"), err)
        && require(!lifecycle.hasRegisteredRoot() && !lifecycle.canShowRoot(),
                   QStringLiteral("first shutdown leaves no root eligible for use"), err)
        && require(!lifecycle.beginRelease(),
                   QStringLiteral("repeat shutdown performs no second cleanup"), err)
        && require(!lifecycle.registerRoot(),
                   QStringLiteral("released lifecycle cannot expose a replacement root"), err);
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
