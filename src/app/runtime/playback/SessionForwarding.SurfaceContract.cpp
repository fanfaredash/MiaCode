// Session::-owned surface-contract accessors, split out of SurfaceContract.cpp.
//
// Stage 4.9d-6: PlaybackCoordinator's implementation TUs are being separated
// from the Session assembly so the coordinator can eventually link on its
// own (see the Result Packet for the link-probe evidence). SurfaceContract.cpp
// now holds only PlaybackCoordinator::-owned surface/bottom-tabs logic (which
// has its own independent state_/ui_-backed implementation of the bottom-tabs
// machinery); this file holds the Session::-owned mirror — chart-normalize
// settings, the two narrow service accessors, and the legacy bottom-tabs
// group that Widgets call sites still reach through Session — that used to
// share that TU.

#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"

#include "UiText.h"
#include "core/chart/transform/ChartNormalization.h"

#include <QtWidgets>

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
    syncQuickShellBottomTabsProxyRoute();
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

bool Session::quickShellBottomTabsProxyActive() const
{
    return backendActive_ && quickShellBottomTabsProxy_ != nullptr;
}

QString Session::bottomTabsFallbackLabel(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return UiText::text(QStringLiteral("window.timeline"));
    case BottomTabsTabId::Validation:
        return UiText::text(QStringLiteral("window.syntax"));
    case BottomTabsTabId::Muri:
        return UiText::text(QStringLiteral("window.muri"));
    case BottomTabsTabId::Unknown:
        break;
    }
    return QString();
}

QWidget* Session::bottomTabsPageForTab(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return nullptr;
    case BottomTabsTabId::Validation:
        return errorList_;
    case BottomTabsTabId::Muri:
        return muriList_;
    case BottomTabsTabId::Unknown:
        break;
    }
    return nullptr;
}

QTabWidget* Session::bottomTabsContainerForTab(BottomTabsTabId tabId) const
{
    if (tabId == BottomTabsTabId::Unknown) {
        return nullptr;
    }
    if (tabId == BottomTabsTabId::Timeline) {
        return nullptr;
    }
    if (quickShellBottomTabsProxyActive() && tabId != BottomTabsTabId::Timeline) {
        return quickShellBottomTabsProxy_;
    }
    return bottomTabs_;
}

int Session::bottomTabsTabIndex(BottomTabsTabId tabId) const
{
    const QTabWidget* container = bottomTabsContainerForTab(tabId);
    const QWidget* targetWidget = bottomTabsPageForTab(tabId);
    if (container == nullptr || targetWidget == nullptr) {
        return -1;
    }
    return container->indexOf(const_cast<QWidget*>(targetWidget));
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

void Session::syncBottomTabsCurrentTabToContainers()
{
    if (bottomTabs_ == nullptr) {
        return;
    }

    if (!quickShellBottomTabsProxyActive()) {
        const QWidget* targetPage = bottomTabsPageForTab(currentBottomTabsTabId_);
        const int index = targetPage != nullptr ? bottomTabs_->indexOf(const_cast<QWidget*>(targetPage)) : -1;
        if (index >= 0 && bottomTabs_->isTabVisible(index) && bottomTabs_->currentIndex() != index) {
            bottomTabs_->setCurrentIndex(index);
        }
        return;
    }

    if (quickShellBottomTabsProxy_ != nullptr) {
        const QWidget* targetPage = bottomTabsPageForTab(currentBottomTabsTabId_);
        const int proxyIndex = targetPage != nullptr
            ? quickShellBottomTabsProxy_->indexOf(const_cast<QWidget*>(targetPage))
            : -1;
        if (proxyIndex >= 0
            && quickShellBottomTabsProxy_->isTabVisible(proxyIndex)
            && quickShellBottomTabsProxy_->currentIndex() != proxyIndex) {
            quickShellBottomTabsProxy_->setCurrentIndex(proxyIndex);
        }
    }
}

void Session::syncQuickShellBottomTabsProxyRoute()
{
    if (bottomTabs_ == nullptr
        || quickShellBottomTabsProxy_ == nullptr
        || errorList_ == nullptr
        || muriList_ == nullptr) {
        return;
    }

    const int validationIndexInBottomTabs = bottomTabs_->indexOf(errorList_);
    const int muriIndexInBottomTabs = bottomTabs_->indexOf(muriList_);
    const bool validationVisible = validationIndexInBottomTabs >= 0
        ? bottomTabs_->isTabVisible(validationIndexInBottomTabs)
        : (quickShellBottomTabsProxy_ != nullptr
               && quickShellBottomTabsProxy_->indexOf(errorList_) >= 0
               && quickShellBottomTabsProxy_->isTabVisible(quickShellBottomTabsProxy_->indexOf(errorList_)));
    const bool muriVisible = muriIndexInBottomTabs >= 0
        ? bottomTabs_->isTabVisible(muriIndexInBottomTabs)
        : (quickShellBottomTabsProxy_ != nullptr
               && quickShellBottomTabsProxy_->indexOf(muriList_) >= 0
               && quickShellBottomTabsProxy_->isTabVisible(quickShellBottomTabsProxy_->indexOf(muriList_)));
    const QString validationLabel = validationIndexInBottomTabs >= 0
        ? bottomTabs_->tabText(validationIndexInBottomTabs)
        : bottomTabsFallbackLabel(BottomTabsTabId::Validation);
    const QString muriLabel = muriIndexInBottomTabs >= 0
        ? bottomTabs_->tabText(muriIndexInBottomTabs)
        : bottomTabsFallbackLabel(BottomTabsTabId::Muri);

    if (quickShellBottomTabsProxyActive()) {
        if (validationIndexInBottomTabs >= 0) {
            bottomTabs_->removeTab(validationIndexInBottomTabs);
        }
        const int muriIndexAfterValidationRemoval = bottomTabs_->indexOf(muriList_);
        if (muriIndexAfterValidationRemoval >= 0) {
            bottomTabs_->removeTab(muriIndexAfterValidationRemoval);
        }
        if (quickShellBottomTabsProxy_->indexOf(errorList_) < 0) {
            quickShellBottomTabsProxy_->addTab(errorList_, validationLabel);
        }
        if (quickShellBottomTabsProxy_->indexOf(muriList_) < 0) {
            quickShellBottomTabsProxy_->addTab(muriList_, muriLabel);
        }
        const int validationIndexInProxy = quickShellBottomTabsProxy_->indexOf(errorList_);
        if (validationIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->setTabVisible(validationIndexInProxy, validationVisible);
        }
        const int muriIndexInProxy = quickShellBottomTabsProxy_->indexOf(muriList_);
        if (muriIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->setTabVisible(muriIndexInProxy, muriVisible);
        }
    } else {
        const int validationIndexInProxy = quickShellBottomTabsProxy_->indexOf(errorList_);
        if (validationIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->removeTab(validationIndexInProxy);
        }
        const int muriIndexInProxy = quickShellBottomTabsProxy_->indexOf(muriList_);
        if (muriIndexInProxy >= 0) {
            quickShellBottomTabsProxy_->removeTab(muriIndexInProxy);
        }
        if (bottomTabs_->indexOf(errorList_) < 0) {
            bottomTabs_->addTab(errorList_, validationLabel);
        }
        if (bottomTabs_->indexOf(muriList_) < 0) {
            bottomTabs_->addTab(muriList_, muriLabel);
        }
        const int restoredValidationIndex = bottomTabs_->indexOf(errorList_);
        if (restoredValidationIndex >= 0) {
            bottomTabs_->setTabVisible(restoredValidationIndex, validationVisible);
        }
        const int restoredMuriIndex = bottomTabs_->indexOf(muriList_);
        if (restoredMuriIndex >= 0) {
            bottomTabs_->setTabVisible(restoredMuriIndex, muriVisible);
        }
    }

    syncBottomTabsCurrentTabToContainers();
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
