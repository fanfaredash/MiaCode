#pragma once

namespace miacode::qml_ui {

// Pure state gate for QML root-window ownership. It prevents exposing a root
// before MainWindow owns the drag route, and makes cleanup single-owner.
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

    bool createChartDropOverlay()
    {
        if (!rootRegistered_ || !rootEventFilterInstalled_ || releaseStarted_
            || chartDropOverlayCreated_) {
            return false;
        }
        chartDropOverlayCreated_ = true;
        return true;
    }

    bool canShowRoot() const
    {
        return rootRegistered_ && rootEventFilterInstalled_ && chartDropOverlayCreated_
            && !releaseStarted_;
    }

    bool setChartDropOverlayVisible(bool visible)
    {
        if (!canShowRoot() || chartDropOverlayVisible_ == visible) {
            return false;
        }
        chartDropOverlayVisible_ = visible;
        return true;
    }

    bool shouldMonitorChartDropOverlay() const
    {
        return canShowRoot() && chartDropOverlayVisible_;
    }

    bool beginRelease()
    {
        if (releaseStarted_) {
            return false;
        }
        releaseStarted_ = true;
        rootRegistered_ = false;
        chartDropOverlayVisible_ = false;
        return true;
    }

    bool hasRegisteredRoot() const { return rootRegistered_; }

private:
    bool rootRegistered_ = false;
    bool rootEventFilterInstalled_ = false;
    bool chartDropOverlayCreated_ = false;
    bool chartDropOverlayVisible_ = false;
    bool releaseStarted_ = false;
};

} // namespace miacode::qml_ui
