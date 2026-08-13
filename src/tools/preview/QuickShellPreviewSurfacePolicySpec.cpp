#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"

namespace {

QString readSource(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyPolicy(QTextStream& err)
{
    if (!require(
            miacode::quick_shell::shouldUseSeparatePreviewSurface(true, true),
            QStringLiteral("quick-shell video should use the separate preview surface"),
            err)) {
        return false;
    }
    if (!require(
            !miacode::quick_shell::shouldUseSeparatePreviewSurface(true, false),
            QStringLiteral("quick-shell image/no-media should stay on the inline preview surface"),
            err)) {
        return false;
    }
    if (!require(
            !miacode::quick_shell::shouldUseSeparatePreviewSurface(false, true),
            QStringLiteral("non quick-shell preview should not use the separate preview surface policy"),
            err)) {
        return false;
    }
    return true;
}

bool verifyChartDropUsesQsgOnly(QTextStream& err)
{
    const QString bootstrap = readSource(QStringLiteral("src/app/quick_shell/QuickShellBootstrap.cpp"));
    const QString mainWindowHeader = readSource(QStringLiteral("src/app/mainwindow/MainWindow.h"));
    return require(
               bootstrap.contains(QStringLiteral("ui/ChartDropOverlay.h")),
               QStringLiteral("audio drop must keep the QuickShell overlay"),
               err)
        && require(
            bootstrap.contains(QStringLiteral("syncChartDropOverlay")),
            QStringLiteral("audio drop must keep the QuickShell overlay lifecycle"),
            err)
        && require(
            !bootstrap.contains(QStringLiteral("PreviewDCompSurface")),
            QStringLiteral("QuickShell audio drop must not restore the removed DComp surface"),
            err)
        && require(
            !bootstrap.contains(QStringLiteral("createInProcessPreviewSurface")),
            QStringLiteral("QuickShell audio drop must stay on the QSG render path"),
            err)
        && require(
            mainWindowHeader.contains(QStringLiteral("chartDropOverlayVisibleChanged")),
            QStringLiteral("audio drop must retain the MainWindow overlay signal"),
            err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyPolicy(err) || !verifyChartDropUsesQsgOnly(err)) {
        return 1;
    }

    out << "quickshell_preview_surface_policy_spec ok" << Qt::endl;
    return 0;
}
