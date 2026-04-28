#include <QCoreApplication>
#include <QTextStream>

#include "audio/BassPreviewDebugLogRouting.h"

namespace {

using miacode::preview_audio::bass::BassDebugOperation;
using miacode::preview_audio::bass::BassDebugRoute;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyRouteNames(QTextStream& err)
{
    if (!require(
            miacode::preview_audio::bass::bassDebugRouteName(BassDebugRoute::Init) == QStringLiteral("bass_init"),
            QStringLiteral("init route should use bass_init"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview_audio::bass::bassDebugRouteName(BassDebugRoute::Transport) == QStringLiteral("bass_transport"),
            QStringLiteral("transport route should use bass_transport"),
            err)) {
        return false;
    }
    return true;
}

bool verifySteadyStateRouting(QTextStream& err)
{
    using miacode::preview_audio::bass::bassDebugRouteForOperation;

    if (!require(
            bassDebugRouteForOperation(BassDebugOperation::PauseExact, false) == BassDebugRoute::Transport,
            QStringLiteral("pause exact should route to transport logs"),
            err)) {
        return false;
    }
    if (!require(
            bassDebugRouteForOperation(BassDebugOperation::RetainedSeek, false) == BassDebugRoute::Transport,
            QStringLiteral("retained seek should route to transport logs"),
            err)) {
        return false;
    }
    if (!require(
            bassDebugRouteForOperation(BassDebugOperation::RetainedReset, false) == BassDebugRoute::Transport,
            QStringLiteral("retained reset should route to transport logs"),
            err)) {
        return false;
    }
    if (!require(
            bassDebugRouteForOperation(BassDebugOperation::ConfigureBackgroundTrack, false) == BassDebugRoute::Transport,
            QStringLiteral("steady-state background track reposition should route to transport logs"),
            err)) {
        return false;
    }
    if (!require(
            bassDebugRouteForOperation(BassDebugOperation::ConfigureBackgroundTrack, true) == BassDebugRoute::Init,
            QStringLiteral("init-window background track configuration should route to init logs"),
            err)) {
        return false;
    }
    if (!require(
            bassDebugRouteForOperation(BassDebugOperation::AnchorTransport, false) == BassDebugRoute::Init,
            QStringLiteral("anchor transport should remain init-only"),
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

    if (!verifyRouteNames(err)) {
        return 1;
    }
    if (!verifySteadyStateRouting(err)) {
        return 1;
    }

    out << "bass_preview_debug_log_routing_spec ok" << Qt::endl;
    return 0;
}
