#include "../../MainWindow.h"
#include "MainWindow.WindowSection.h"
#include "../timeline/MainWindow.TimelineSection.h"

#include "common/DebugLog.h"
#include "TimelineView.h"
#include "UiText.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QtWidgets>

void MainWindow::setQuickShellBackendActive(bool active)
{
    if (quickShellBackendActive_ == active) {
        return;
    }
    quickShellBackendActive_ = active;
    quickShellUiFocusBridgeMode_ = active;
    quickTimelineSurfaceReady_ = false;
    pendingQuickTimelineCursorSync_ = false;
    pendingQuickTimelineCursorSecond_ = 0.0;
    pendingQuickTimelineCursorCenterView_ = false;
    syncQuickShellBottomTabsProxyRoute();
    if (windowSection_ != nullptr) {
        windowSection_->updateBottomTabsDeviceHeight();
    }
}

bool MainWindow::shellTimelineSurfaceReady() const
{
    return quickTimelineSurfaceReady_;
}

void MainWindow::noteQuickTimelineSurfaceReady()
{
    if (quickTimelineSurfaceReady_) {
        return;
    }
    quickTimelineSurfaceReady_ = true;
    if (runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("quick_shell/backend"),
            QStringLiteral("action=quick_timeline_ready pending_cursor_sync=%1 second=%2 center=%3")
                .arg(pendingQuickTimelineCursorSync_ ? 1 : 0)
                .arg(pendingQuickTimelineCursorSecond_, 0, 'f', 6)
                .arg(pendingQuickTimelineCursorCenterView_ ? 1 : 0)
        );
    }
    if (timelineSection_ != nullptr) {
        timelineSection_->flushDeferredTimelineBridgeState();
    }
}

bool MainWindow::quickShellBottomTabsProxyActive() const
{
    return quickShellBackendActive_ && quickShellBottomTabsProxy_ != nullptr;
}

QString MainWindow::bottomTabsFallbackLabel(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return UiText::isChineseUi() ? QStringLiteral("时间轴") : QStringLiteral("Timeline");
    case BottomTabsTabId::Validation:
        return UiText::isChineseUi() ? QStringLiteral("语法") : QStringLiteral("Syntax");
    case BottomTabsTabId::Muri:
        return UiText::isChineseUi() ? QStringLiteral("无理") : QStringLiteral("Muri");
    case BottomTabsTabId::Unknown:
        break;
    }
    return QString();
}

QWidget* MainWindow::bottomTabsPageForTab(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        if (timelineWidgetlessQuickRoute_) {
            return nullptr;
        }
        return timelineView_;
    case BottomTabsTabId::Validation:
        return errorList_;
    case BottomTabsTabId::Muri:
        return muriList_;
    case BottomTabsTabId::Unknown:
        break;
    }
    return nullptr;
}

QTabWidget* MainWindow::bottomTabsContainerForTab(BottomTabsTabId tabId) const
{
    if (tabId == BottomTabsTabId::Unknown) {
        return nullptr;
    }
    if (tabId == BottomTabsTabId::Timeline && timelineWidgetlessQuickRoute_) {
        return nullptr;
    }
    if (quickShellBottomTabsProxyActive() && tabId != BottomTabsTabId::Timeline) {
        return quickShellBottomTabsProxy_;
    }
    return bottomTabs_;
}

int MainWindow::bottomTabsTabIndex(BottomTabsTabId tabId) const
{
    const QTabWidget* container = bottomTabsContainerForTab(tabId);
    const QWidget* targetWidget = bottomTabsPageForTab(tabId);
    if (container == nullptr || targetWidget == nullptr) {
        return -1;
    }
    return container->indexOf(const_cast<QWidget*>(targetWidget));
}

MainWindow::BottomTabsTabId MainWindow::bottomTabsTabIdFromString(const QString& tabId) const
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

QString MainWindow::bottomTabsTabIdString(BottomTabsTabId tabId) const
{
    switch (tabId) {
    case BottomTabsTabId::Timeline:
        return QStringLiteral("timeline");
    case BottomTabsTabId::Validation:
        return QStringLiteral("validation");
    case BottomTabsTabId::Muri:
        return QStringLiteral("muri");
    case BottomTabsTabId::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

MainWindow::BottomTabsTabId MainWindow::currentBottomTabsTabId() const
{
    return currentBottomTabsTabId_;
}

QString MainWindow::currentBottomTabsTabIdString() const
{
    return bottomTabsTabIdString(currentBottomTabsTabId());
}

bool MainWindow::bottomTabsTabVisible(BottomTabsTabId tabId) const
{
    if (tabId == BottomTabsTabId::Timeline && timelineWidgetlessQuickRoute_) {
        return timelineTabVisible_;
    }
    const QTabWidget* container = bottomTabsContainerForTab(tabId);
    if (container == nullptr) {
        return false;
    }
    const int index = container->indexOf(bottomTabsPageForTab(tabId));
    return index >= 0 && container->isTabVisible(index);
}

void MainWindow::syncBottomTabsCurrentTabToContainers()
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

void MainWindow::syncQuickShellBottomTabsProxyRoute()
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

void MainWindow::setCurrentBottomTabsTabId(BottomTabsTabId tabId)
{
    if (tabId == BottomTabsTabId::Unknown) {
        return;
    }
    // Issue #2 fix — in QuickShell mode the legacy QTabWidget is hidden
    // (the QML BottomTabsQuickHost renders the tab bar instead) and the
    // legacy widget's `isTabVisible(index)` may report false even when
    // the QML controller reports the tab as user-visible. The early
    // return on `!bottomTabsTabVisible(tabId)` then drops every click
    // from the QML tab bar — which is exactly the symptom the user
    // reported ("clicking validation/muri detect has no effect").
    // Skip the legacy visibility gate when the QuickShell backend is
    // active; visibility there is already gated at the QML level by
    // the validationTabVisible / muriTabVisible properties.
    if (!quickShellBackendActive_ && !bottomTabsTabVisible(tabId)) {
        return;
    }
    currentBottomTabsTabId_ = tabId;
    syncBottomTabsCurrentTabToContainers();
    if (tabId == BottomTabsTabId::Timeline && timelineSection_ != nullptr) {
        timelineSection_->flushDeferredTimelineBridgeState();
    }
    if (tabId != BottomTabsTabId::Timeline) {
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    }
}

void MainWindow::setCurrentBottomTabsTabId(const QString& tabId)
{
    setCurrentBottomTabsTabId(bottomTabsTabIdFromString(tabId));
}

void MainWindow::setBottomTabsTabVisible(BottomTabsTabId tabId, bool visible)
{
    if (tabId == BottomTabsTabId::Timeline && timelineWidgetlessQuickRoute_) {
        if (timelineTabVisible_ == visible) {
            return;
        }
        timelineTabVisible_ = visible;
        if (!visible && currentBottomTabsTabId() == tabId) {
            restoreBottomTabsCurrentTabAfterRefresh(BottomTabsTabId::Validation);
        }
        return;
    }
    QTabWidget* container = bottomTabsContainerForTab(tabId);
    if (container == nullptr) {
        return;
    }
    const int index = container->indexOf(bottomTabsPageForTab(tabId));
    if (index < 0 || container->isTabVisible(index) == visible) {
        return;
    }
    container->setTabVisible(index, visible);
    if (!visible && currentBottomTabsTabId() == tabId) {
        restoreBottomTabsCurrentTabAfterRefresh(BottomTabsTabId::Timeline);
    }
}

void MainWindow::restoreBottomTabsCurrentTabAfterRefresh(BottomTabsTabId preferredTabId)
{
    if (bottomTabs_ == nullptr && quickShellBottomTabsProxy_ == nullptr) {
        return;
    }

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
