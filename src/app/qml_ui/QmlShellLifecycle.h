#pragma once

#include <QObject>
#include <QString>

class MainWindow;

namespace miacode::qml_ui {

// Root-window close, as the QML window sees it.
//
// The contract is the one QuickShellController carried: the window asks whether
// it may close, and — only if it accepted — says so, which is what lets
// bootstrap run preparePreviewForShutdown before teardown. Kept as its own
// object rather than folded into the preview or timeline session, because it is
// neither: it is the window's lifecycle.
class QmlShellLifecycle final : public QObject
{
    Q_OBJECT

public:
    explicit QmlShellLifecycle(MainWindow& backend, QObject* parent = nullptr);

    // Runs the backend's unsaved-changes flow. False means the user cancelled
    // and the window must stay open.
    Q_INVOKABLE bool confirmClose();
    Q_INVOKABLE void notifyRootCloseAccepted(const QString& source);

signals:
    void rootCloseAccepted(const QString& source);

private:
    MainWindow* backend_ = nullptr;
};

}  // namespace miacode::qml_ui
