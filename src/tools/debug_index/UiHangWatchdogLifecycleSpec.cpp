#include <QCoreApplication>
#include <QTextStream>

#include "common/DebugOptions.h"
#include "common/UiHangWatchdog.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    // CLI export workers create a QGuiApplication but do not enter app.exec().
    // The watchdog must therefore have an explicit, idempotent shutdown path;
    // waiting for aboutToQuit would leave its global std::thread joinable at
    // process exit and make the std::thread destructor call std::terminate.
    miacode::debug_options::setDebugModeEnabled(true);
    miacode::hang_watchdog::installGuiHeartbeat(&app);
    miacode::hang_watchdog::shutdownGuiHeartbeat();
    miacode::hang_watchdog::shutdownGuiHeartbeat();
    miacode::debug_options::setDebugModeEnabled(false);

    out << "UI hang watchdog lifecycle spec passed." << Qt::endl;
    return 0;
}
