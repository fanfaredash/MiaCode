#include <QCoreApplication>
#include <QTextStream>

#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"

namespace {

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

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyPolicy(err)) {
        return 1;
    }

    out << "quickshell_preview_surface_policy_spec ok" << Qt::endl;
    return 0;
}
