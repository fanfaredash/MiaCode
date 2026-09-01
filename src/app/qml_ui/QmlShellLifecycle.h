#pragma once

#include "app/v2/EditorPageRouter.h"

#include <QObject>
#include <QString>


namespace miacode::qml_ui {

// Root-window close, as the QML window sees it.
//
// The contract is the one QuickShellController carried: the window asks whether
// it may close, and — only if it accepted — says so, which is what lets
// bootstrap run preparePreviewForShutdown before teardown. Kept as its own
// object rather than folded into the preview or timeline session, because it is
// neither: it is the window's lifecycle.
//
// The question is asked, not answered on the spot. The unsaved-changes prompt
// is a QML dialog now, so a close handler cannot get a verdict without running
// a nested event loop over its own window — the window refuses the first close,
// waits for closeDecided, and closes itself again if the answer was yes.
class QmlShellLifecycle final : public QObject
{
    Q_OBJECT

public:
    // No MainWindow: the close question is a page-router operation.
    explicit QmlShellLifecycle(miacode::v2::EditorPageRouter*& routerSlot,
                               QObject* parent = nullptr);

    // Starts the backend's unsaved-changes flow. The verdict arrives on
    // closeDecided; false means the user cancelled and the window stays open.
    // A second call while one is already in flight is ignored rather than
    // stacking a second prompt on the first.
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void notifyRootCloseAccepted(const QString& source);

signals:
    void closeDecided(bool accepted);
    void rootCloseAccepted(const QString& source);

private:
    miacode::v2::EditorPageRouter** routerSlot_ = nullptr;
    miacode::v2::EditorPageRouter* router() const
    {
        return routerSlot_ != nullptr ? *routerSlot_ : nullptr;
    }
    bool closeRequestInFlight_ = false;
};

}  // namespace miacode::qml_ui
