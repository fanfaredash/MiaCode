#include <QString>
#include <QTextStream>

#include "app/ProcessIdentityFields.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);

    using miacode::app::entry::formatProcessIdentityBuildFields;
    bool ok = true;
    ok &= require(
        formatProcessIdentityBuildFields(
            QStringLiteral("1.1.0-beta.7"),
            QStringLiteral("677a962574bd"),
            QStringLiteral("0"))
            == QStringLiteral(
                "version=1.1.0-beta.7 git_revision=677a962574bd git_dirty=0"),
        QStringLiteral("clean build fields"),
        err);
    ok &= require(
        formatProcessIdentityBuildFields(
            QStringLiteral("1.1.0-beta.7"),
            QStringLiteral("677a962574bd"),
            QStringLiteral("1"))
            .endsWith(QStringLiteral("git_dirty=1")),
        QStringLiteral("dirty build fields"),
        err);
    ok &= require(
        formatProcessIdentityBuildFields(
            QStringLiteral("1.1.0-beta.7"),
            QStringLiteral("unknown"),
            QStringLiteral("unknown"))
            .endsWith(QStringLiteral("git_revision=unknown git_dirty=unknown")),
        QStringLiteral("source archive build fields"),
        err);

    if (ok) {
        out << "Process identity fields spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
