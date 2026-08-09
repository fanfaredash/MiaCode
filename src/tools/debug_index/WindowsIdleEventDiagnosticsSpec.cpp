#include <QString>
#include <QTextStream>

#include "app/WindowsIdleEventDiagnostics.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

constexpr qintptr packedSize(quint16 width, quint16 height)
{
    return static_cast<qintptr>(static_cast<quint32>(width)
        | (static_cast<quint32>(height) << 16));
}

}  // namespace

int main()
{
    namespace diag = miacode::app::windows_idle_diagnostics;
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    ok &= require(diag::isRelevantMessage(0x0218),
                  QStringLiteral("WM_POWERBROADCAST is relevant"), err);
    ok &= require(diag::powerBroadcastReasonName(0x0004) == QStringLiteral("suspend"),
                  QStringLiteral("suspend reason"), err);
    ok &= require(diag::powerBroadcastReasonName(0x0012) == QStringLiteral("resume_automatic"),
                  QStringLiteral("automatic resume reason"), err);
    ok &= require(diag::consoleDisplayStateName(0) == QStringLiteral("off"),
                  QStringLiteral("console display off"), err);
    ok &= require(diag::consoleDisplayStateName(1) == QStringLiteral("on"),
                  QStringLiteral("console display on"), err);
    ok &= require(diag::consoleDisplayStateName(2) == QStringLiteral("dimmed"),
                  QStringLiteral("console display dimmed"), err);

    const QString display = diag::eventPayload(0x007E, 32, packedSize(1920, 1080));
    ok &= require(display.contains(QStringLiteral("action=display_change"))
                      && display.contains(QStringLiteral("bits_per_pixel=32"))
                      && display.contains(QStringLiteral("width=1920"))
                      && display.contains(QStringLiteral("height=1080")),
                  QStringLiteral("display-change payload"), err);

    const QString locked = diag::eventPayload(0x02B1, 7, 42);
    const QString unlocked = diag::eventPayload(0x02B1, 8, 42);
    ok &= require(locked.contains(QStringLiteral("reason=session_lock"))
                      && locked.contains(QStringLiteral("session_id=42")),
                  QStringLiteral("session-lock payload"), err);
    ok &= require(unlocked.contains(QStringLiteral("reason=session_unlock")),
                  QStringLiteral("session-unlock payload"), err);

    const QString device = diag::eventPayload(0x0219, 0x8000, 0);
    ok &= require(device.contains(QStringLiteral("action=device_change"))
                      && device.contains(QStringLiteral("reason=device_arrival")),
                  QStringLiteral("device-arrival payload"), err);
    ok &= require(diag::eventPayload(0x1234, 0, 0).isEmpty(),
                  QStringLiteral("unrelated message ignored"), err);

    if (ok) {
        out << "Windows idle event diagnostics spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
