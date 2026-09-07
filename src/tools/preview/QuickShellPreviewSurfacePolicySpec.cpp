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
    // The v1 shell is gone; the surviving GUI bootstrap is the v2 QML entry below.
    const QString bootstrap = readSource(QStringLiteral("src/app/qml_ui/QmlUiBootstrap.cpp"));
    return require(
               bootstrap.contains(QStringLiteral("drop/QmlChartDropBridge.h")),
               QStringLiteral("audio drop must use the QML bridge"),
               err)
        && require(
            bootstrap.contains(QStringLiteral("installDropBridge")),
            QStringLiteral("audio drop must be part of the root lifecycle gate"),
            err)
        && require(
            !bootstrap.contains(QStringLiteral("ChartDropOverlay")),
            QStringLiteral("audio drop must not construct the removed native overlay"),
            err)
        && require(
            !bootstrap.contains(QStringLiteral("PreviewDCompSurface")),
            QStringLiteral("audio drop must not restore the removed DComp surface"),
            err)
        && require(
            !bootstrap.contains(QStringLiteral("createInProcessPreviewSurface")),
            QStringLiteral("audio drop must stay on the QSG render path"),
            err)
        && require(
            bootstrap.contains(QStringLiteral("ItemAcceptsDrops")),
            QStringLiteral("audio drop must preserve the QML content-item drop flag"),
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
