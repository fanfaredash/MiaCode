#pragma once

namespace miacode::qml_ui {

// Pure state gate for QML root-window ownership. The drop bridge is a
// non-visual QWindow event filter; the visible drag surface belongs to QML and
// is therefore deliberately not part of this gate.
class RootLifecycle final
{
public:
    bool registerRoot()
    {
        if (releaseStarted_ || rootRegistered_) {
            return false;
        }
        rootRegistered_ = true;
        return true;
    }

    bool installRootEventFilter()
    {
        if (!rootRegistered_ || releaseStarted_ || rootEventFilterInstalled_) {
            return false;
        }
        rootEventFilterInstalled_ = true;
        return true;
    }

    bool installDropBridge()
    {
        if (!rootRegistered_ || !rootEventFilterInstalled_ || releaseStarted_ || dropBridgeInstalled_) {
            return false;
        }
        dropBridgeInstalled_ = true;
        return true;
    }

    bool canShowRoot() const
    {
        return rootRegistered_ && rootEventFilterInstalled_ && dropBridgeInstalled_ && !releaseStarted_;
    }

    bool beginRelease()
    {
        if (releaseStarted_) {
            return false;
        }
        releaseStarted_ = true;
        rootRegistered_ = false;
        rootEventFilterInstalled_ = false;
        dropBridgeInstalled_ = false;
        return true;
    }

    bool hasRegisteredRoot() const { return rootRegistered_; }

private:
    bool rootRegistered_ = false;
    bool rootEventFilterInstalled_ = false;
    bool dropBridgeInstalled_ = false;
    bool releaseStarted_ = false;
};

} // namespace miacode::qml_ui
