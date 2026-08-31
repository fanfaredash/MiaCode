#include "../../MainWindow.h"
#include "MainWindow.WindowSection.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "../validation/MainWindow.ValidationSection.h"

#include "common/DebugLog.h"
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

miacode::chart_transform::ChartNormalizationOptions MainWindow::chartNormalizeOptions() const
{
    return miacode::chart_transform::ChartNormalizationOptions{
        true,
        chartNormalizeReduceTo384Grid_,
        chartNormalizeSplitEveryFourMeasures_,
        chartNormalizeSyntax_,
        chartNormalizeSectionMeasureCount_};
}

void MainWindow::setChartNormalizeOptions(
    const miacode::chart_transform::ChartNormalizationOptions& options)
{
    chartNormalizeStartAtNewMeasure_ = true;
    chartNormalizeReduceTo384Grid_ = options.reduceTo384Grid;
    chartNormalizeSplitEveryFourMeasures_ = options.splitEveryFourMeasures;
    chartNormalizeSectionMeasureCount_ = options.sectionMeasureCount;
    chartNormalizeSyntax_ = options.syntax;
    savePortableState();
}

miacode::v2::UiRequestService* MainWindow::uiRequestService() const
{
    return uiRequests_;
}

miacode::v2::JobProgressService* MainWindow::jobProgressService() const
{
    return jobProgress_;
}

// ---- miacode::v2::PreviewSurface ----
// Thin forwarders. The shell* names on the window are v1 QuickShell history;
// the appearance VALUES already live in miacode::v2::PreviewAppearanceState, so
// what forwards here is everything that needs the running preview.

bool MainWindow::playing() const { return shellPreviewPlaying(); }
double MainWindow::positionSeconds() const { return shellPreviewPositionSeconds(); }
double MainWindow::durationSeconds() const { return shellPreviewDurationSeconds(); }
double MainWindow::lowerBoundSeconds() const { return shellPreviewLowerBoundSeconds(); }
void MainWindow::togglePlayback() { toggleShellPreviewPlayback(); }
void MainWindow::stop() { stopShellPreview(); }
void MainWindow::seek(double second) { seekShellPreview(second); }
void MainWindow::beginScrub() { beginShellPreviewScrub(); }

void MainWindow::updateScrub(double second, bool centerView)
{
    updateShellPreviewScrub(second, centerView);
}

void MainWindow::endScrub(double second, bool centerView)
{
    endShellPreviewScrub(second, centerView);
}

void MainWindow::setPlaybackRate(double rate) { setShellPreviewRate(rate); }
void MainWindow::nudgePlaybackRate(int direction) { nudgeShellPreviewRate(direction); }
QString MainWindow::playbackRateLabel() const { return shellPreviewSpeedLabel(); }

QObject* MainWindow::previewRuntimeObject() const { return shellPreviewRuntimeObject(); }
QObject* MainWindow::stageMediaHostObject() const { return shellPreviewStageMediaHostObject(); }
double MainWindow::canvasAspectRatio() const { return shellPreviewCanvasAspectRatio(); }
QStringList MainWindow::statsTexts() const { return shellPreviewStatsTexts(); }

void MainWindow::setMuriRenderMode(RenderMode mode) { setMuriRenderMode(mode, true); }
void MainWindow::toggleMuriRenderMode() { toggleShellMuriRenderMode(); }

QStringList MainWindow::availableSkinDirectoryNames() const
{
    return availablePreviewSkinDirectoryNames();
}

QString MainWindow::skinDisplayName(const QString& directoryName) const
{
    return previewSkinDisplayName(directoryName);
}

QString MainWindow::resolveSkinDir() const { return resolvePreviewSkinDir(); }
QString MainWindow::resolveSkinRootDir() const { return resolvePreviewSkinRootDir(); }
QString MainWindow::resolveCustomOutlineDir() const { return resolvePreviewCustomOutlineDir(); }

void MainWindow::applyOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                                     bool persistState)
{
    applyPreviewOutlineVariant(variant, useAutoSelection, persistState);
}

QVariantMap MainWindow::renderSettings() const { return previewRenderSettings(); }

void MainWindow::setRenderSetting(const QString& key, const QVariant& value)
{
    setPreviewRenderSetting(key, value);
}

void MainWindow::refreshSurfaces() { refreshPreviewSurfaces(); }

PreviewAudioSettings MainWindow::audioSettings() const { return currentPreviewAudioSettings(); }

void MainWindow::applyAudioSettings(const PreviewAudioSettings& settings)
{
    applyPreviewAudioSettingsFromUi(settings);
}

void MainWindow::saveAudioSettingsAsSoftwareDefault()
{
    savePreviewAudioSettingsAsSoftwareDefault();
}

void MainWindow::restoreAudioSettingsFromSoftwareDefault()
{
    restorePreviewAudioSettingsFromSoftwareDefault();
}

// ---- miacode::v2::TimelineSurface ----
// Thin forwarders. The shell* names on the window date from the v1 QuickShell
// controller; the interface does not carry that history forward.

QObject* MainWindow::timelineStateBridge() const
{
    return shellTimelineStateBridgeObject();
}

bool MainWindow::timelineSurfaceReady() const
{
    return shellTimelineSurfaceReady();
}

void MainWindow::navigateToSecond(double second)
{
    navigateShellTimelineToSecond(second);
}

void MainWindow::centerOnSecond(double second)
{
    centerShellTimelineNavigate(second);
}

void MainWindow::wheelNavigateToSecond(double second)
{
    wheelShellTimelineNavigate(second);
}

void MainWindow::timelineDragStarted()
{
    shellTimelineDragStarted();
}

void MainWindow::timelineDragFinished(double second)
{
    shellTimelineDragFinished(second);
}

void MainWindow::timelineUserInteractionStarted()
{
    shellTimelineUserInteractionStarted();
}

void MainWindow::setFollowPreviewEnabled(bool enabled)
{
    shellTimelineFollowPreviewToggled(enabled);
}

QString MainWindow::bottomTabsCurrentTabId() const
{
    return shellBottomTabsCurrentTabId();
}

void MainWindow::setBottomTabsCurrentTabId(const QString& tabId)
{
    setShellBottomTabsCurrentTab(tabId);
}

bool MainWindow::bottomTabsVisible() const
{
    return shellBottomTabsVisible();
}

bool MainWindow::timelineTabVisible() const
{
    return shellTimelineTabVisible();
}

bool MainWindow::muriTabVisible() const
{
    return shellMuriTabVisible();
}

bool MainWindow::validationTabVisible() const
{
    return shellValidationTabVisible();
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

QWidget* MainWindow::bottomTabsPageForTab(BottomTabsTabId tabId) const
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

QTabWidget* MainWindow::bottomTabsContainerForTab(BottomTabsTabId tabId) const
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
    if (tabId == BottomTabsTabId::Timeline) {
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
    // (the QML BottomTabBar renders the tab bar instead) and the
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
    const BottomTabsTabId previousTabId = currentBottomTabsTabId_;
    currentBottomTabsTabId_ = tabId;
    syncBottomTabsCurrentTabToContainers();
    if (tabId == BottomTabsTabId::Muri && validationSection_ != nullptr) {
        validationSection_->flushPendingMuriDiagnosticsPanelRefresh();
    }
    if (tabId == BottomTabsTabId::Timeline && timelineSection_ != nullptr) {
        timelineSection_->flushDeferredTimelineBridgeState();
    }
    if (tabId != BottomTabsTabId::Timeline) {
        scheduleWrappedListRelayout(errorList_);
        scheduleWrappedListRelayout(muriList_);
    }
    if (previousTabId != tabId) {
        emit shellPresentationChanged();
    }
}

void MainWindow::setCurrentBottomTabsTabId(const QString& tabId)
{
    setCurrentBottomTabsTabId(bottomTabsTabIdFromString(tabId));
}

void MainWindow::setBottomTabsTabVisible(BottomTabsTabId tabId, bool visible)
{
    if (tabId == BottomTabsTabId::Timeline) {
        if (timelineTabVisible_ == visible) {
            return;
        }
        timelineTabVisible_ = visible;
        if (!visible && currentBottomTabsTabId() == tabId) {
            restoreBottomTabsCurrentTabAfterRefresh(BottomTabsTabId::Validation);
        }
        emit shellPresentationChanged();
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
    emit shellPresentationChanged();
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
