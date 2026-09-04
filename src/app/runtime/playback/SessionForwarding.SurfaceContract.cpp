// Session-owned surface-contract accessors, split out of SurfaceContract.cpp.
//
// Stage 4.9d-6: PlaybackCoordinator's implementation TUs are being separated
// from the Session assembly so the coordinator can eventually link on its
// own (see the Result Packet for the link-probe evidence). SurfaceContract.cpp
// now holds only PlaybackCoordinator-owned surface/bottom-tabs state logic;
// this file holds the Session-owned chart-normalize settings and the two
// narrow service accessors that used to share that TU.

#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

#include "core/chart/transform/ChartNormalization.h"

void Session::setBackendActive(bool active)
{
    if (backendActive_ == active) {
        return;
    }
    backendActive_ = active;
    uiFocusBridgeMode_ = active;
    timelineReady_ = false;
    pendingQuickTimelineCursorSync_ = false;
    pendingQuickTimelineCursorSecond_ = 0.0;
    pendingQuickTimelineCursorCenterView_ = false;
}

miacode::chart_transform::ChartNormalizationOptions Session::chartNormalizeOptions() const
{
    return miacode::chart_transform::ChartNormalizationOptions{
        true,
        chartNormalizeReduceTo384Grid_,
        chartNormalizeSplitEveryFourMeasures_,
        chartNormalizeSyntax_,
        chartNormalizeSectionMeasureCount_};
}

void Session::setChartNormalizeOptions(
    const miacode::chart_transform::ChartNormalizationOptions& options)
{
    chartNormalizeStartAtNewMeasure_ = true;
    chartNormalizeReduceTo384Grid_ = options.reduceTo384Grid;
    chartNormalizeSplitEveryFourMeasures_ = options.splitEveryFourMeasures;
    chartNormalizeSectionMeasureCount_ = options.sectionMeasureCount;
    chartNormalizeSyntax_ = options.syntax;
    savePortableState();
}

miacode::v2::UiRequestService* Session::uiRequestService() const
{
    return uiRequests_;
}

miacode::v2::JobProgressService* Session::jobProgressService() const
{
    return jobProgress_;
}

Session::BottomTabsTabId Session::bottomTabsTabIdFromString(const QString& tabId) const
{
    const QString normalized = tabId.trimmed().toLower();
    if (normalized == QStringLiteral("timeline")) {
        return BottomTabsTabId::Timeline;
    }
    if (normalized == QStringLiteral("validation")) {
        return BottomTabsTabId::Validation;
    }
    if (normalized == QStringLiteral("muri")) {
        return BottomTabsTabId::Muri;
    }
    return BottomTabsTabId::Unknown;
}

Session::BottomTabsTabId Session::currentBottomTabsTabId() const
{
    return currentBottomTabsTabId_;
}

bool Session::bottomTabsTabVisible(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return timelineTabVisible_;
    case BottomTabsTabId::Validation:
        return validationTabVisible_;
    case BottomTabsTabId::Muri:
        return muriTabVisible_;
    case BottomTabsTabId::Unknown:
        break;
    }
    return false;
}

// Stage 4.9d-4b-2e: function body moved to
// PlaybackCoordinator::setCurrentBottomTabsTabId (SurfaceContract.cpp, near
// bottomTabsTabIdToString) — this stays as a one-line forwarder per
// SessionForwarding.cpp's convention because shell/Interaction.cpp,
// document/DocumentPages.cpp and validation/ValidationRuntime.cpp still call
// it from outside playback/.
void Session::setCurrentBottomTabsTabId(BottomTabsTabId tabId)
{
    playback_->setCurrentBottomTabsTabId(tabId);
}

void Session::setCurrentBottomTabsTabId(const QString& tabId)
{
    setCurrentBottomTabsTabId(bottomTabsTabIdFromString(tabId));
}

void Session::setBottomTabsTabVisible(BottomTabsTabId tabId, bool visible)
{
    bool* flag = nullptr;
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        flag = &timelineTabVisible_;
        break;
    case BottomTabsTabId::Validation:
        flag = &validationTabVisible_;
        break;
    case BottomTabsTabId::Muri:
        flag = &muriTabVisible_;
        break;
    case BottomTabsTabId::Unknown:
        break;
    }
    if (flag == nullptr || *flag == visible) {
        return;
    }
    *flag = visible;
    if (!visible && currentBottomTabsTabId() == tabId) {
        restoreBottomTabsCurrentTabAfterRefresh(
            tabId == BottomTabsTabId::Timeline
                ? BottomTabsTabId::Validation
                : BottomTabsTabId::Timeline);
    }
    emit presentationChanged();
}

void Session::restoreBottomTabsCurrentTabAfterRefresh(BottomTabsTabId preferredTabId)
{
    const auto pickVisibleTab = [this](BottomTabsTabId tabId) -> bool {
        return bottomTabsTabVisible(tabId);
    };

    BottomTabsTabId targetTabId = BottomTabsTabId::Unknown;
    if (pickVisibleTab(preferredTabId)) {
        targetTabId = preferredTabId;
    }
    if (targetTabId == BottomTabsTabId::Unknown && pickVisibleTab(currentBottomTabsTabId())) {
        targetTabId = currentBottomTabsTabId();
    }
    if (targetTabId == BottomTabsTabId::Unknown) {
        for (const BottomTabsTabId fallbackTab : {
                 BottomTabsTabId::Timeline,
                 BottomTabsTabId::Validation,
                 BottomTabsTabId::Muri}) {
            if (pickVisibleTab(fallbackTab)) {
                targetTabId = fallbackTab;
                break;
            }
        }
    }

    if (targetTabId != BottomTabsTabId::Unknown) {
        setCurrentBottomTabsTabId(targetTabId);
    }
}
