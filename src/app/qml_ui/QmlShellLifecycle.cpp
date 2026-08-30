#include "QmlShellLifecycle.h"

#include "common/DebugLog.h"
#include "mainwindow/MainWindow.h"

namespace miacode::qml_ui {

namespace {
void appendLifecycleLog(const QString& action, const QString& payload = QString())
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("qml_ui/shell_lifecycle"),
        payload.isEmpty() ? action : QStringLiteral("%1 %2").arg(action, payload),
        true);
}
}  // namespace

QmlShellLifecycle::QmlShellLifecycle(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
{
}

void QmlShellLifecycle::requestClose()
{
    if (closeRequestInFlight_) {
        // Escape, the title-bar button and ⌘Q can all arrive while the prompt
        // is up. One question is enough.
        appendLifecycleLog(QStringLiteral("confirm_close"), QStringLiteral("result=already_asking"));
        return;
    }
    if (backend_ == nullptr) {
        appendLifecycleLog(QStringLiteral("confirm_close"), QStringLiteral("result=no_backend"));
        emit closeDecided(true);
        return;
    }
    closeRequestInFlight_ = true;
    backend_->requestShellClose([this](bool confirmed) {
        closeRequestInFlight_ = false;
        appendLifecycleLog(QStringLiteral("confirm_close"),
                           QStringLiteral("result=%1").arg(confirmed ? "accepted" : "cancelled"));
        emit closeDecided(confirmed);
    });
}

void QmlShellLifecycle::notifyRootCloseAccepted(const QString& source)
{
    const QString normalized =
        source.trimmed().isEmpty() ? QStringLiteral("qml_root_close") : source.trimmed();
    appendLifecycleLog(QStringLiteral("root_close_accepted"), QStringLiteral("source=%1").arg(normalized));
    emit rootCloseAccepted(normalized);
}

}  // namespace miacode::qml_ui
