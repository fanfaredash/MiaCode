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
