#include "MainEntrypoints.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QString>

#include <exception>
#include <new>

namespace miacode::app::entry {

void addSharedCliDebugOption(QCommandLineParser& parser)
{
    // main() already enables debug mode before CLI dispatch. We still declare
    // this option here so subcommand parsers accept forwarded "--debug".
    parser.addOption(QCommandLineOption(
        QStringLiteral("debug"),
        QStringLiteral("Enable debug mode and debug-only log output.")
    ));
    // P3 — hidden internal GPU device-policy overrides. Declared so the CLI
    // export / export-worker parsers accept them without erroring; the values
    // are consumed by miacode::gpu (raw-arg scan), not read back off the parser.
    parser.addOption(QCommandLineOption(
        QStringLiteral("gpu-policy"),
        QStringLiteral("Internal GPU device policy (auto_high_performance|platform_default|software)."),
        QStringLiteral("policy")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("gpu-adapter-luid"),
        QStringLiteral("Internal GPU adapter override, <high>:<low>."),
        QStringLiteral("luid")
    ));
}

QString currentExceptionDetail()
{
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return QStringLiteral("std::bad_alloc");
    } catch (const std::exception& ex) {
        const QString what = QString::fromUtf8(ex.what()).trimmed();
        return what.isEmpty() ? QStringLiteral("std::exception") : what;
    } catch (...) {
        return QStringLiteral("unknown non-std exception");
    }
}

}  // namespace miacode::app::entry
