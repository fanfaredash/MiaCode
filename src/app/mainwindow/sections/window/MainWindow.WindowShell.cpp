#include "MainWindow.WindowSection.h"
#include "../../MainWindowShared.h"

#include "AppVersion.h"
#include "BracketScopeHighlighter.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "UiText.h"
#include "UiTheme.h"
#include "UiNativeWindowTheme.h"
#include "../validation/MainWindow.ValidationSection.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../export/MainWindow.ExportSection.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "extensions/ExtensionManager.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace miacode::mainwindow::shared;

namespace {

// Same helper as in MainWindow.WindowRuntime.cpp — duplicated here so the
// quick-shell close path can cascade-close popups without leaking the helper
// outside its translation unit. See the longer comment over there for the
// caveat about Qt::ApplicationModal exec() blocking taskbar-initiated closes
// from reaching MainWindow's closeEvent in the first place.
int dismissOpenChildPopupDialogs(QWidget& owner)
{
    int closed = 0;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevels) {
        if (widget == nullptr || widget == &owner) {
            continue;
        }
        if (!widget->isWindow() || !widget->isVisible()) {
            continue;
        }
        if (widget->close()) {
            ++closed;
        }
    }
    return closed;
}

constexpr double kBottomTabsContentScaleMin = 0.5;
// The bottom tab can be dragged PAST 100% now. kBottomTabsContentScaleMax is only a large
// safety bound for the math — the practical ceiling is the available window space (clamped
// in setShellBottomTabsHeight so the preview panel keeps a minimum). Above 100% only the
// note GRID height keeps growing; the header ("顶部变换") and the note 素材/markers cap at 100%.
// SYNC PAIR: mirrored by kMaxContentScale in TimelineSceneStateBuilder.cpp and the
// setContentScale clamps in TimelineQuickStateBridge.cpp.
constexpr double kBottomTabsContentScaleMax = 4.0;
// The bottom tab may grow past 100%, but not without bound — cap it at this fraction of the
// whole window height so the preview area above always keeps roughly a third of the window.
constexpr double kBottomTabsMaxWindowHeightFraction = 2.0 / 3.0;
// 语法 / 无理 issue lists render at a fixed 90% of their base font — uniform, independent of
// the bottom-tab height (no longer scaled by headerScale).
constexpr double kBottomTabsIssueListFontScale = 0.9;

double clampedBottomTabsContentScale(double scale)
{
    return qBound(kBottomTabsContentScaleMin, scale, kBottomTabsContentScaleMax);
}

// The header / "顶部变换" region and the validation/无理 list fonts follow this scale. It
// tracks contentScale up to 100% (0.75 -> 1.0) and then CAPS at 1.0, so above 100% the
// header and list fonts stop growing while only the note grid keeps expanding.
double bottomTabsHeaderScaleForContentScale(double scale)
{
    const double contentScale = qMin(1.0, clampedBottomTabsContentScale(scale));
    return 0.5 + (contentScale * 0.5);
}

void scaleFont(QFont* font, double scale)
{
    if (font == nullptr) {
        return;
    }
    const qreal clampedScale = static_cast<qreal>(clampedBottomTabsContentScale(scale));
    if (font->pointSizeF() > 0.0) {
        font->setPointSizeF(qMax(1.0, font->pointSizeF() * clampedScale));
    } else if (font->pointSize() > 0) {
        font->setPointSizeF(qMax(1.0, static_cast<qreal>(font->pointSize()) * clampedScale));
    } else if (font->pixelSize() > 0) {
        font->setPixelSize(qMax(1, qRound(static_cast<qreal>(font->pixelSize()) * clampedScale)));
    }
}

void applyScaledListFont(QListWidget* list, double scale)
{
    if (list == nullptr) {
        return;
    }
    QVariant baseFontVariant = list->property("bottomTabsBaseFont");
    if (!baseFontVariant.isValid()) {
        baseFontVariant = QVariant::fromValue(list->font());
        list->setProperty("bottomTabsBaseFont", baseFontVariant);
    }
    QFont font = baseFontVariant.value<QFont>();
    scaleFont(&font, scale);
    list->setFont(font);
}

void applyScaledTabBarFont(QTabWidget* tabs, double scale)
{
    if (tabs == nullptr || tabs->tabBar() == nullptr) {
        return;
    }
    QTabBar* tabBar = tabs->tabBar();
    QVariant baseFontVariant = tabBar->property("bottomTabsBaseFont");
    if (!baseFontVariant.isValid()) {
        baseFontVariant = QVariant::fromValue(tabBar->font());
        tabBar->setProperty("bottomTabsBaseFont", baseFontVariant);
    }
    QFont font = baseFontVariant.value<QFont>();
    scaleFont(&font, scale);
    tabBar->setFont(font);
    tabBar->updateGeometry();
}

int scaledBottomTabsTabBarHeight(QTabBar* tabBar, double contentScale)
{
    if (tabBar == nullptr) {
        return 0;
    }
    tabBar->ensurePolished();
    QVariant baseHeightVariant = tabBar->property("bottomTabsBaseHeight");
    if (!baseHeightVariant.isValid()) {
        baseHeightVariant = qMax(tabBar->minimumSizeHint().height(), tabBar->sizeHint().height());
        tabBar->setProperty("bottomTabsBaseHeight", baseHeightVariant);
    }
    return qMax(
        1,
        qRound(static_cast<qreal>(baseHeightVariant.toInt())
            * static_cast<qreal>(bottomTabsHeaderScaleForContentScale(contentScale))));
}

int scaledBottomTabsTimelineContentHeight(double contentScale)
{
    const double clampedScale = clampedBottomTabsContentScale(contentScale);
    const int headerHeight = qMax(
        1,
        qRound(static_cast<qreal>(miacode::window_parity::kTimelineHeaderHeight
                + miacode::window_parity::kTimelineTopMargin)
            * static_cast<qreal>(bottomTabsHeaderScaleForContentScale(clampedScale))));
    const int laneHeight = qMax(
        1,
        qRound(static_cast<qreal>(miacode::window_parity::kTimelineLaneHeight)
            * static_cast<qreal>(clampedScale)));
    return headerHeight + miacode::window_parity::kTimelineLaneCount * laneHeight;
}

double bottomTabsContentScaleForTimelineContentHeight(int timelineHeight)
{
    const double headerBase =
        miacode::window_parity::kTimelineHeaderHeight + miacode::window_parity::kTimelineTopMargin;
    const double laneBase =
        miacode::window_parity::kTimelineLaneHeight * miacode::window_parity::kTimelineLaneCount;
    const double variableBase = headerBase * 0.5 + laneBase;
    if (variableBase <= 0.0 || laneBase <= 0.0) {
        return kBottomTabsContentScaleMax;
    }
    // Inverse of scaledBottomTabsTimelineContentHeight, which is piecewise because the
    // header term caps at 100%:
    //   scale <= 1: total = headerBase*0.5 + (headerBase*0.5 + laneBase) * scale
    //   scale  > 1: total = headerBase     + laneBase * scale            (header capped)
    // Both branches yield headerBase + laneBase at scale == 1, so the curve is continuous.
    const double linearScale = (static_cast<double>(timelineHeight) - headerBase * 0.5) / variableBase;
    if (linearScale <= 1.0) {
        return clampedBottomTabsContentScale(linearScale);
    }
    return clampedBottomTabsContentScale((static_cast<double>(timelineHeight) - headerBase) / laneBase);
}

bool actionMatchesShortcut(QAction* action, const QKeySequence& sequence)
{
    if (action == nullptr || sequence.isEmpty()) {
        return false;
    }
    QList<QKeySequence> shortcuts = action->shortcuts();
    if (shortcuts.isEmpty() && !action->shortcut().isEmpty()) {
        shortcuts.append(action->shortcut());
    }
    for (const QKeySequence& shortcut : shortcuts) {
        if (!shortcut.isEmpty() && shortcut == sequence) {
            return true;
        }
    }
    return false;
}

void repolishWidget(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    if (QStyle* style = widget->style(); style != nullptr) {
        style->unpolish(widget);
        style->polish(widget);
    }
    widget->update();
}

void refreshMenuThemeRecursive(QMenu* menu, QSet<QMenu*>* visited)
{
    if (menu == nullptr || visited == nullptr || visited->contains(menu)) {
        return;
    }
    visited->insert(menu);
    UiTheme::styleRoundedMenu(*menu);
    repolishWidget(menu);
    const QList<QAction*> actions = menu->actions();
    for (QAction* action : actions) {
        if (action == nullptr) {
            continue;
        }
        if (QMenu* submenu = action->menu(); submenu != nullptr) {
            refreshMenuThemeRecursive(submenu, visited);
        }
    }
}

void refreshMenuBarTheme(QMenuBar* menuBar, QSet<QMenu*>* visited)
{
    if (menuBar == nullptr) {
        return;
    }
    menuBar->setNativeMenuBar(false);
    menuBar->setPalette(UiTheme::applicationPalette());
    repolishWidget(menuBar);
    const QList<QAction*> actions = menuBar->actions();
    for (QAction* action : actions) {
        if (action == nullptr) {
            continue;
        }
        if (QMenu* menu = action->menu(); menu != nullptr) {
            refreshMenuThemeRecursive(menu, visited);
        }
    }
}

// Native title-bar / backdrop theming now lives in UiNativeWindowTheme
// (src/app/ui/) so tools-layer dialogs can reach it too.

}  // namespace

namespace {

constexpr auto kQuickShellTransportSeekProperty = "miacode.quick_shell_transport_seek";

// H (observability): if the requested scrub second and the position the preview
// actually settled on diverge by more than this, emit a `preview_scrub_misalign`
// line so a "thumb vs. real position" report can be classified from logs.
constexpr double kQuickShellScrubMisalignWarnSeconds = 0.05;

void appendQuickShellBackendLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/backend"),
        text
    );
}

}  // namespace

bool MainWindow::WindowSection::quickShellRootWindowFrameGeometryAvailable() const
{
    return owner_.quickShellRootWindowFrameGeometry_.isValid();
}

QRect MainWindow::WindowSection::quickShellRootWindowFrameGeometry() const
{
    return owner_.quickShellRootWindowFrameGeometry_;
}

void MainWindow::WindowSection::requestShellClose(std::function<void(bool)> onDecided)
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    // The prompt is a QML dialog, so the answer arrives later. Everything below
    // the question moved into the continuation unchanged — including the
    // ordering rule it depends on: a cancelled close must leave the world
    // untouched.
    owner_.requestLeaveDocument(
        [this, onDecided = std::move(onDecided), totalTimer](bool canClose) mutable {
            const bool confirmed = canClose && finishShellClose(totalTimer);
            if (onDecided) {
                onDecided(confirmed);
            }
        });
}

bool MainWindow::WindowSection::finishShellClose(QElapsedTimer totalTimer)
{
    // Close is confirmed. Only NOW cascade-close popup chains (Preferences,
    // Keyboard Shortcuts, etc.). Running this after the unsaved-changes
    // prompt is accepted means a cancelled close leaves every sibling
    // window untouched — including the quick-shell's native bridge/
    // compositing surfaces (createBridgeSurface() hosts the editor, timeline
    // and preview as top-level QWidgets) — so the window keeps full
    // functionality. Doing the sweep before the prompt closed those surfaces
    // and a Cancel could not bring them back.
    const int dismissed = dismissOpenChildPopupDialogs(owner_);
    if (dismissed > 0) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/window"),
            QStringLiteral("confirm_shell_close_dismissed_child_dialogs=%1").arg(dismissed)
        );
    }

    // Clean exit (quick-shell route): mirror the legacy closeEvent —
    // drop the crash-recovery snapshot, delete any recovery file, and
    // remove the session marker so the next launch doesn't treat this
    // clean shutdown as an abnormal exit.
    owner_.documentSection_->cleanupCrashRecoveryForCleanExit();
    miacode::crash_recovery::clearSessionMarker();

    QElapsedTimer savePortableTimer;
    savePortableTimer.start();
    owner_.savePortableState();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("save_portable_state"),
        savePortableTimer.elapsed()
    );

    QElapsedTimer exportCleanupTimer;
    exportCleanupTimer.start();
    owner_.exportSection_->clearVideoExportWorkerState();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("clear_video_export_worker_state"),
        exportCleanupTimer.elapsed()
    );

    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("confirm_shell_close"),
        totalTimer.elapsed(),
        QStringLiteral("result=confirmed")
    );
    return true;
}

void MainWindow::WindowSection::toggleShellPreviewPlayback()
{
    owner_.onTogglePreviewPause();
}

void MainWindow::WindowSection::stopShellPreview()
{
    owner_.onStopPreview();
}

void MainWindow::WindowSection::seekShellPreview(double second)
{
    owner_.seekPreviewToSecond(second, true);
}

void MainWindow::WindowSection::beginShellPreviewScrub()
{
    appendQuickShellBackendLog(QStringLiteral("preview_scrub_begin"));
    QToolTip::hideText();
    owner_.stopPreviewHeldSeek();
    owner_.previewScrubDragging_ = true;
    owner_.previewScrubRenderElapsed_.invalidate();
    if (owner_.previewFullscreenActive_) {
        owner_.showPreviewFullscreenControls(false);
    }
    if (owner_.qtPreviewPlaying_) {
        owner_.pauseQtPreviewPlaybackExact();
    }
}

void MainWindow::WindowSection::updateShellPreviewScrub(double second, bool centerView)
{
    // Negative-time intro region: a seek into [-duration, 0) renders a static
    // intro frame instead of a chart seek (export page, 添加片头 on).
    if (owner_.handleExportIntroSliderSeek(second)) {
        return;
    }
    const double clampedSecond = qBound(0.0, second, owner_.previewDurationSeconds());
    QToolTip::hideText();
    appendQuickShellBackendLog(
        QStringLiteral("preview_scrub_update"),
        QString("second=%1 center=%2")
            .arg(clampedSecond, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    miacode::mainwindow::shared::writePreviewPauseSecond(
        owner_.qtPreviewPauseSecond_, clampedSecond, owner_.qtPreviewPlaying_, "update_shell_preview_scrub");
    if (owner_.previewFullscreenActive_) {
        owner_.showPreviewFullscreenControls(false);
    }
    const bool shouldRenderNow =
        !owner_.previewScrubRenderElapsed_.isValid()
        || owner_.previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
    if (shouldRenderNow) {
        owner_.requestPausedPreviewSeek(clampedSecond, centerView, false, false);
        owner_.previewScrubRenderElapsed_.restart();
    } else {
        owner_.schedulePreviewSeek(clampedSecond, centerView);
    }
    const double appliedSecond = owner_.pausedSeekAppliedVisualSecond_;
    const double misalignDelta = clampedSecond - appliedSecond;
    if (qAbs(misalignDelta) > kQuickShellScrubMisalignWarnSeconds) {
        appendQuickShellBackendLog(
            QStringLiteral("preview_scrub_misalign"),
            QString("phase=update handle=%1 requested=%2 applied=%3 delta=%4")
                .arg(second, 0, 'f', 6)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(appliedSecond, 0, 'f', 6)
                .arg(misalignDelta, 0, 'f', 6)
        );
    }
}

void MainWindow::WindowSection::endShellPreviewScrub(double second, bool centerView)
{
    if (owner_.handleExportIntroSliderSeek(second)) {
        owner_.stopPreviewHeldSeek();
        owner_.previewScrubDragging_ = false;
        owner_.previewScrubRenderElapsed_.invalidate();
        return;
    }
    const double clampedSecond = qBound(0.0, second, owner_.previewDurationSeconds());
    QToolTip::hideText();
    appendQuickShellBackendLog(
        QStringLiteral("preview_scrub_end"),
        QString("second=%1 center=%2")
            .arg(clampedSecond, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    owner_.stopPreviewHeldSeek();
    owner_.previewScrubDragging_ = false;
    owner_.previewScrubRenderElapsed_.invalidate();
    miacode::mainwindow::shared::writePreviewPauseSecond(
        owner_.qtPreviewPauseSecond_, clampedSecond, owner_.qtPreviewPlaying_, "end_shell_preview_scrub");
    if (owner_.previewSeekDebounceTimer_ != nullptr) {
        owner_.previewSeekDebounceTimer_->stop();
    }
    owner_.seekPreviewToSecond(clampedSecond, centerView);
    const double appliedSecond = owner_.qtPreviewPauseSecond_;
    const double misalignDelta = clampedSecond - appliedSecond;
    if (qAbs(misalignDelta) > kQuickShellScrubMisalignWarnSeconds) {
        appendQuickShellBackendLog(
            QStringLiteral("preview_scrub_misalign"),
            QString("phase=end handle=%1 requested=%2 applied=%3 delta=%4")
                .arg(second, 0, 'f', 6)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(appliedSecond, 0, 'f', 6)
                .arg(misalignDelta, 0, 'f', 6)
        );
    }
}

void MainWindow::WindowSection::setShellPreviewRate(double rate)
{
    owner_.applyPreviewPlaybackRate(rate);
}

void MainWindow::WindowSection::toggleShellMuriRenderMode()
{
    // Three exclusive preview modes, so the shell shortcut cycles rather than toggles.
    RenderMode nextMode = RenderMode::MaimuriDxStyle;
    switch (owner_.muriRenderOptions_.renderMode) {
        case RenderMode::Native:
            nextMode = RenderMode::EraseByArea;
            break;
        case RenderMode::EraseByArea:
            nextMode = RenderMode::MaimuriDxStyle;
            break;
        case RenderMode::MaimuriDxStyle:
            nextMode = RenderMode::Native;
            break;
    }
    owner_.setMuriRenderMode(nextMode);
}

void MainWindow::WindowSection::nudgeShellPreviewRate(int direction)
{
    owner_.applyPreviewPlaybackRate(
        steppedPreviewPlaybackRate(owner_.previewPlaybackRate_, direction));
}

bool MainWindow::WindowSection::stepShellPreviewBySeconds(double deltaSeconds, bool centerView)
{
    appendQuickShellBackendLog(
        QStringLiteral("preview_step_request"),
        QString("delta=%1 center=%2")
            .arg(deltaSeconds, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    if (!qIsFinite(deltaSeconds)) {
        return false;
    }
    const double nextSecond = qBound(
        0.0,
        owner_.qtPreviewPauseSecond_ + deltaSeconds,
        owner_.previewDurationSeconds()
    );
    const bool moved = qAbs(nextSecond - owner_.qtPreviewPauseSecond_) >= 1e-9;
    QToolTip::hideText();
    if (moved) {
        owner_.seekPreviewToSecond(nextSecond, centerView);
    }
    appendQuickShellBackendLog(
        QStringLiteral("preview_step_result"),
        QString("delta=%1 center=%2 moved=%3 pos=%4")
            .arg(deltaSeconds, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
            .arg(moved ? 1 : 0)
            .arg(this->shellPreviewPositionSeconds(), 0, 'f', 6)
    );
    return moved;
}

void MainWindow::WindowSection::beginShellPreviewHeldSeek(int direction, int key)
{
    appendQuickShellBackendLog(
        QStringLiteral("preview_hold_begin"),
        QString("direction=%1 key=%2").arg(direction).arg(key)
    );
    owner_.setProperty(kQuickShellTransportSeekProperty, true);
    owner_.beginPreviewHeldSeek(direction, key);
}

void MainWindow::WindowSection::stopShellPreviewHeldSeek(int key)
{
    appendQuickShellBackendLog(
        QStringLiteral("preview_hold_stop"),
        QString("key=%1").arg(key)
    );
    owner_.stopPreviewHeldSeek(key);
    owner_.setProperty(kQuickShellTransportSeekProperty, false);
}

void MainWindow::WindowSection::setShellPreviewFullscreen(bool fullscreen)
{
    if (owner_.previewFullscreenActive_ == fullscreen) {
        return;
    }
    if (fullscreen) {
        owner_.enterPreviewFullscreen();
    } else {
        owner_.exitPreviewFullscreen();
    }
}

void MainWindow::WindowSection::setShellPreviewPaneWidthRatio(double ratio)
{
    const double normalized = qBound(0.0, ratio, 1.0);
    if (normalized <= 0.0) {
        return;
    }
    if (qFuzzyCompare(owner_.previewPaneWidthRatio_ + 1.0, normalized + 1.0)) {
        return;
    }
    owner_.previewPaneWidthRatio_ = normalized;
    if (owner_.visualLayoutPersistTimer_ != nullptr) {
        owner_.visualLayoutPersistTimer_->start();
    }
}

void MainWindow::WindowSection::setShellBottomTabsHeight(int height)
{
    if (owner_.bottomTabs_ == nullptr) {
        return;
    }
    const int fullHeight = computeBottomTabsDeviceHeightForScale(kBottomTabsContentScaleMax);
    const int minHeight = computeBottomTabsDeviceHeightForScale(kBottomTabsContentScaleMin);
    if (fullHeight <= 0 || minHeight <= 0) {
        return;
    }
    // Bound the bottom-tab height past 100% to a fraction of the whole window height so the
    // preview area above always keeps roughly a third of the window. The scale-derived
    // fullHeight is only an upper safety bound.
    int effectiveMaxHeight = fullHeight;
    const int windowHeight = owner_.height();
    if (windowHeight > 0) {
        const int windowLimit =
            static_cast<int>(static_cast<double>(windowHeight) * kBottomTabsMaxWindowHeightFraction);
        if (windowLimit > minHeight) {
            effectiveMaxHeight = qMin(effectiveMaxHeight, windowLimit);
        }
    }
    const int clampedHeight =
        qBound(qMin(minHeight, effectiveMaxHeight), height, qMax(minHeight, effectiveMaxHeight));
    const int fullTimelineHeight = scaledBottomTabsTimelineContentHeight(kBottomTabsContentScaleMax);
    const int chromeHeight = qMax(0, fullHeight - fullTimelineHeight);
    const double nextScale =
        bottomTabsContentScaleForTimelineContentHeight(qMax(0, clampedHeight - chromeHeight));
    const double clampedScale = clampedBottomTabsContentScale(nextScale);
    if (!qFuzzyCompare(owner_.bottomTabsContentScale_ + 1.0, clampedScale + 1.0)) {
        owner_.bottomTabsContentScale_ = clampedScale;
        applyBottomTabsContentScale();
        // Persist the new divider height as an app-level preference. Stored as a
        // content-scale ratio (not pixels) so it stays valid across DPI/layout
        // changes; the write is debounced so a drag doesn't thrash the disk.
        if (owner_.bottomTabsContentScalePersistTimer_ != nullptr) {
            owner_.bottomTabsContentScalePersistTimer_->start();
        }
    }
    updateBottomTabsDeviceHeight();
}

void MainWindow::WindowSection::setShellBottomTabsCurrentTab(const QString& tabId)
{
    owner_.setCurrentBottomTabsTabId(tabId);
}

void MainWindow::WindowSection::navigateShellTimelineToSecond(double second)
{
    owner_.timelineSection_->onTimelineHeaderNavigateRequested(second);
}

void MainWindow::WindowSection::centerShellTimelineNavigate(double second)
{
    owner_.timelineSection_->onTimelineCenterNavigateRequested(second);
}

void MainWindow::WindowSection::wheelShellTimelineNavigate(double second)
{
    owner_.timelineSection_->onTimelineWheelNavigateRequested(second);
}

void MainWindow::WindowSection::shellTimelineDragStarted()
{
    owner_.timelineSection_->onTimelineDragStarted();
}

void MainWindow::WindowSection::shellTimelineDragFinished(double second)
{
    owner_.timelineSection_->onTimelineDragFinished(second);
}

void MainWindow::WindowSection::shellTimelineUserInteractionStarted()
{
    owner_.timelineSection_->onTimelineUserInteractionStarted();
}

void MainWindow::WindowSection::shellTimelineSurfaceReady()
{
    owner_.noteQuickTimelineSurfaceReady();
}

void MainWindow::WindowSection::shellTimelineFollowPreviewToggled(bool enabled)
{
    owner_.timelineSection_->onTimelineFollowPreviewToggled(enabled);
}

void MainWindow::WindowSection::shellTimelineViewportLockToggled(bool enabled)
{
    owner_.timelineSection_->onTimelineViewportLockToggled(enabled);
}

void MainWindow::WindowSection::shellTimelineFollowProgressToggled(bool enabled)
{
    owner_.timelineSection_->onTimelineFollowProgressToggled(enabled);
}

void MainWindow::WindowSection::shellTimelineSyncToggled(bool enabled)
{
    owner_.timelineSection_->onTimelineSyncToggled(enabled);
}

bool MainWindow::WindowSection::shellHasShortcut(const QKeySequence& sequence) const
{
    if (sequence.isEmpty()) {
        return false;
    }
    for (QAction* action : this->quickShellShortcutActions()) {
        if (actionMatchesShortcut(action, sequence)) {
            return true;
        }
    }
    return false;
}

bool MainWindow::WindowSection::shellTriggerShortcut(const QKeySequence& sequence)
{
    if (sequence.isEmpty()) {
        return false;
    }
    for (QAction* action : this->quickShellShortcutActions()) {
        if (!actionMatchesShortcut(action, sequence)) {
            continue;
        }
        if (!action->isEnabled()) {
            return true;
        }
        action->trigger();
        return true;
    }
    return false;
}

QString MainWindow::WindowSection::shellWindowTitle() const
{
    return owner_.windowTitle();
}

bool MainWindow::WindowSection::shellWorkspacePanelsSwapped() const
{
    return owner_.workspacePanelsSwapped_;
}

QString MainWindow::WindowSection::shellPreviewSpeedLabel() const
{
    QString rateText = QString::number(owner_.previewPlaybackRate_, 'f', 2);
    while (rateText.endsWith('0')) {
        rateText.chop(1);
    }
    if (rateText.endsWith('.')) {
        rateText.chop(1);
    }
    return QStringLiteral("%1x").arg(rateText);
}

bool MainWindow::WindowSection::shellMuriCheckRenderMode() const
{
    return owner_.muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle;
}

bool MainWindow::WindowSection::shellPreviewPlaying() const
{
    return owner_.qtPreviewPlaying_ || owner_.exportIntroLeadInPlaying();
}

double MainWindow::WindowSection::shellPreviewPositionSeconds() const
{
    // While the export-page intro region is shown, the transport playhead sits
    // in negative time (the intro) — report that so the QML thumb follows it.
    if (owner_.exportIntroRegionActive_) {
        return owner_.exportIntroPlayheadSeconds_;
    }
    return qMax(0.0, owner_.qtPreviewPauseSecond_);
}

double MainWindow::WindowSection::shellPreviewDurationSeconds() const
{
    return owner_.previewDurationSeconds();
}

QStringList MainWindow::WindowSection::shellPreviewStatsTexts() const
{
    const miacode::preview::scene::PreviewObjectStatsSnapshot stats =
        owner_.previewProgressStatsCache_ != nullptr
            ? owner_.previewProgressStatsCache_->snapshotAt(qMax(0.0, owner_.qtPreviewPauseSecond_))
            : miacode::preview::scene::PreviewObjectStatsSnapshot();
    const auto fmt = [](const QString& name, int played, int total) {
        return QString("%1  %2/%3")
            .arg(name.leftJustified(5, QChar(' '), true))
            .arg(played)
            .arg(total);
    };
    return QStringList{
        fmt(QStringLiteral("Tap"), stats.tapPlayed, stats.tapTotal),
        fmt(QStringLiteral("Hold"), stats.holdPlayed, stats.holdTotal),
        fmt(QStringLiteral("Slide"), stats.slidePlayed, stats.slideTotal),
        fmt(QStringLiteral("Touch"), stats.touchPlayed, stats.touchTotal),
        fmt(QStringLiteral("Break"), stats.breakPlayed, stats.breakTotal),
        fmt(QStringLiteral("Total"), stats.totalPlayed, stats.totalCount),
    };
}

double MainWindow::WindowSection::shellPreviewCanvasAspectRatio() const
{
    return owner_.normalizedPreviewCanvasAspectRatio(owner_.previewCanvasAspectRatio_);
}

quint64 MainWindow::WindowSection::shellPreviewPaneRestoreGeneration() const
{
    return owner_.previewPaneRestoreGeneration_;
}

double MainWindow::WindowSection::shellPreviewPaneWidthRatio() const
{
    return owner_.previewPaneWidthRatio_;
}

bool MainWindow::WindowSection::shellPreviewFullscreen() const
{
    return owner_.previewFullscreenActive_;
}

QObject* MainWindow::WindowSection::shellPreviewRuntimeObject() const
{
    return owner_.previewCanvas_;
}

QObject* MainWindow::WindowSection::shellPreviewStageMediaHostObject() const
{
    return owner_.previewStageMediaHost_;
}

bool MainWindow::WindowSection::shellPreviewUsesSeparateSurface() const
{
    return owner_.quickShellPreviewUsesSeparateSurface();
}

QWindow* MainWindow::WindowSection::shellPreviewCompositeWindow() const
{
    return owner_.quickShellPreviewCompositeWindow();
}

QObject* MainWindow::WindowSection::shellTimelineStateBridgeObject() const
{
    return static_cast<QObject*>(owner_.timelineQuickStateBridge_);
}

QString MainWindow::WindowSection::shellBottomTabsCurrentTabId() const
{
    return owner_.currentBottomTabsTabIdString();
}

bool MainWindow::WindowSection::shellBottomTabsVisible() const
{
    const bool anyTabVisible = owner_.bottomTabsTabVisible(MainWindow::BottomTabsTabId::Timeline)
        || owner_.bottomTabsTabVisible(MainWindow::BottomTabsTabId::Validation)
        || owner_.bottomTabsTabVisible(MainWindow::BottomTabsTabId::Muri);
    if (!anyTabVisible) {
        return false;
    }
    if (owner_.quickShellBackendActive_) {
        return true;
    }

    const QWidget* const shellBottomTabsWidget = this->shellBottomTabsWidget();
    const bool shellBottomTabsWidgetVisible =
        shellBottomTabsWidget != nullptr && shellBottomTabsWidget->isVisible();
    const bool timelineTabsWidgetVisible =
        owner_.bottomTabs_ != nullptr && owner_.bottomTabs_->isVisible();
    return shellBottomTabsWidgetVisible || timelineTabsWidgetVisible;
}

bool MainWindow::WindowSection::shellTimelineTabVisible() const
{
    return owner_.bottomTabsTabVisible(MainWindow::BottomTabsTabId::Timeline);
}

bool MainWindow::WindowSection::shellValidationTabVisible() const
{
    return owner_.bottomTabsTabVisible(MainWindow::BottomTabsTabId::Validation);
}

bool MainWindow::WindowSection::shellMuriTabVisible() const
{
    return owner_.bottomTabsTabVisible(MainWindow::BottomTabsTabId::Muri);
}

bool MainWindow::WindowSection::shellExportPageActive() const
{
    // Stop-gap for the export-page + fullscreen Intel iGPU D3D11 crash
    // (hardware video decode).
    // performSwitchToExportField() sets activeOutlineKey_ = "export" on entry and
    // every other page sets a different key, so this is true iff the export page
    // is the active workspace page. The shared preview transport binds the
    // fullscreen button's visibility to !exportPageActive so it can't be triggered
    // there until the underlying crash is fixed.
    return state_.activeOutlineKey_ == QLatin1String("export");
}

QWidget* MainWindow::WindowSection::shellWindowWidget() const
{
    return const_cast<MainWindow*>(&owner_);
}

QDockWidget* MainWindow::WindowSection::shellOutlineDockWidget() const
{
    return owner_.outlineDock_;
}

bool MainWindow::WindowSection::shellOutlineDockCollapsed() const
{
    return owner_.outlineDockCollapsed_;
}

int MainWindow::WindowSection::shellOutlineDockExpandedWidth() const
{
    return owner_.outlineDockExpandedWidth_;
}

QWidget* MainWindow::WindowSection::shellWorkspaceWidget() const
{
    return owner_.workspaceContentWidget_ != nullptr ? owner_.workspaceContentWidget_ : owner_.previewLeftColumn_;
}

QWidget* MainWindow::WindowSection::shellBottomTabsWidget() const
{
    if (owner_.quickShellBottomTabsProxyActive()) {
        return owner_.quickShellBottomTabsProxy_;
    }
    return owner_.bottomTabs_;
}

int MainWindow::WindowSection::shellBottomTabsHeight() const
{
    return this->computeBottomTabsDeviceHeight();
}

double MainWindow::WindowSection::shellBottomTabsHeaderScale() const
{
    return bottomTabsHeaderScaleForContentScale(owner_.bottomTabsContentScale_);
}

QWidget* MainWindow::WindowSection::shellPreviewPanelWidget() const
{
    return owner_.previewPanel_;
}

double MainWindow::WindowSection::shellNormalizedPreviewCanvasAspectRatio() const
{
    return owner_.normalizedPreviewCanvasAspectRatio(owner_.previewCanvasAspectRatio_);
}

void MainWindow::WindowSection::shellRefreshLayoutAfterResize()
{
    owner_.refreshLayoutAfterPageSwitch();
}

void MainWindow::WindowSection::shellSetRootWindowFrameGeometry(const QRect& geometry)
{
    owner_.quickShellRootWindowFrameGeometry_ = geometry;
    owner_.setProperty("miacode.quick_root_window_frame_geometry", geometry);
}

void MainWindow::WindowSection::shellNoteQuickUiReady()
{
    owner_.noteQuickShellStartupUiReady();
}

void MainWindow::WindowSection::applyUiTheme()
{
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        UiTheme::applyApplicationTheme(*app);
    }

    if (owner_.outlineList_ != nullptr) {
    }
    if (owner_.editorFindBar_ != nullptr) {
        owner_.editorFindBar_->setStyleSheet(UiTheme::editorFindBarStyleSheet());
    }
    if (owner_.editorFindCloseButton_ != nullptr) {
        // The ✕ is a baked QIcon, so unlike the QSS text color it doesn't follow
        // the palette on its own — re-tint it to the find bar's button text
        // color (textPrimary) so it tracks the light/dark theme.
        owner_.editorFindCloseButton_->setIcon(makeOutlineCloseIcon(UiTheme::colors().textPrimary));
    }
    if (owner_.welcomePage_ != nullptr) {
        owner_.welcomePage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (owner_.welcomeEmptyHintLabel_ != nullptr) {
        owner_.welcomeEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (owner_.metadataPage_ != nullptr) {
        owner_.metadataPage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (owner_.metadataEmptyHintLabel_ != nullptr) {
        owner_.metadataEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (owner_.metadataExtraEdit_ != nullptr) {
        if (QScrollBar* vbar = owner_.metadataExtraEdit_->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
        if (QScrollBar* hbar = owner_.metadataExtraEdit_->horizontalScrollBar()) {
            hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    // 语法 / 无理 issue lists share the code-editor's rounded scrollbar style (instead of
    // the default native bar). Re-applied here so it follows light/dark theme switches.
    for (QListWidget* issueList : {owner_.errorList_, owner_.muriList_}) {
        if (issueList == nullptr) {
            continue;
        }
        if (QScrollBar* vbar = issueList->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
        if (QScrollBar* hbar = issueList->horizontalScrollBar()) {
            hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    if (owner_.outlineList_ != nullptr) {
        owner_.outlineList_->setStyleSheet(UiTheme::outlineListStyleSheet());
        // The scroll bar was styled once at construction — re-style it here or
        // it keeps the previous theme's colors after a light/dark switch.
        if (QScrollBar* vbar = owner_.outlineList_->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    this->updateBottomTabsDeviceHeight();
    if (owner_.metadataBracketHighlighter_ != nullptr) {
        owner_.metadataBracketHighlighter_->rehighlight();
    }
    if (QWidget* editorShell = owner_.findChild<QWidget*>(QStringLiteral("EditorShell")); editorShell != nullptr) {
        editorShell->setStyleSheet(UiTheme::editorShellStyleSheet());
    }
    const UiTheme::Colors& themeColors = UiTheme::colors();
    if (owner_.editorHeaderWidget_ != nullptr) {
        owner_.editorHeaderWidget_->setAttribute(Qt::WA_StyledBackground, true);
        owner_.editorHeaderWidget_->setStyleSheet(
            QStringLiteral(
                "QFrame#EditorHeader { background: %1; border-bottom: 1px solid %2; }"
                "QLabel#EditorContext { color: %3; font-weight: 700; background: transparent; }"
                "QLabel#EditorMeta { color: %4; background: transparent; }"
                "QWidget#EditorDifficultyControls { background: transparent; }"
                "QWidget#EditorDifficultyControls QLabel { color: %4; background: transparent; }"
                "QWidget#EditorDifficultyControls QLineEdit { background: %5; color: %3; border: 1px solid %6; border-radius: 6px; padding: 4px 6px; selection-background-color: %7; selection-color: %8; }"
                "QWidget#EditorDifficultyControls QLineEdit:focus { border-color: %9; }"
            )
                .arg(themeColors.cardBg.name(QColor::HexRgb))
                .arg(themeColors.border.name(QColor::HexRgb))
                .arg(themeColors.textPrimary.name(QColor::HexRgb))
                .arg(themeColors.textSecondary.name(QColor::HexRgb))
                .arg(themeColors.inputBg.name(QColor::HexRgb))
                .arg(themeColors.borderSoft.name(QColor::HexRgb))
                .arg(themeColors.selection.name(QColor::HexRgb))
                .arg(themeColors.selectionText.name(QColor::HexRgb))
                .arg(themeColors.accent.name(QColor::HexRgb))
        );
    }
    const QString previewPanelStyle = UiTheme::previewPanelStyleSheet();
    if (owner_.previewPanel_ != nullptr) {
        owner_.previewPanel_->setStyleSheet(previewPanelStyle);
    }
    QSet<QMenu*> refreshedMenus;
    refreshMenuBarTheme(owner_.menuBar(), &refreshedMenus);
    refreshMenuThemeRecursive(owner_.toolboxMenu_, &refreshedMenus);
    const QList<QMenu*> menus = owner_.findChildren<QMenu*>();
    for (QMenu* menu : menus) {
        refreshMenuThemeRecursive(menu, &refreshedMenus);
    }

    const QColor iconColor = UiTheme::colors().iconPrimary;
    const QColor previewControlIconColor =
        owner_.previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : iconColor;
    const QColor secondaryIconColor = UiTheme::colors().iconSecondary;
    if (owner_.stopPreviewAction_ != nullptr) {
        owner_.stopPreviewAction_->setIcon(makePreviewStopIcon(previewControlIconColor));
    }
    if (owner_.settingsPlaceholderAction_ != nullptr) {
        owner_.settingsPlaceholderAction_->setIcon(makeSettingsGearIcon(secondaryIconColor));
    }
    if (owner_.previewAudioSettingsButton_ != nullptr) {
        owner_.previewAudioSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (owner_.previewVideoSettingsButton_ != nullptr) {
        owner_.previewVideoSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    owner_.applyWorkspacePanelArrangement();
    this->applySystemWindowBackdrop();
    if (owner_.syntaxCheckButton_ != nullptr) {
        owner_.syntaxCheckButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (owner_.exportVideoButton_ != nullptr) {
        owner_.exportVideoButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (owner_.outlineCollapseButton_ != nullptr) {
        owner_.outlineCollapseButton_->setStyleSheet(outlineCollapseButtonStyleSheet());
        this->updateOutlineDockCollapseButton();
    }
    if (owner_.previewFullscreenHintLabel_ != nullptr) {
        owner_.previewFullscreenHintLabel_->setStyleSheet(previewFullscreenHintStyleSheet());
    }
    if (owner_.previewFullscreenActive_
        && owner_.previewControlCard_ != nullptr
        && owner_.previewControlCard_->parentWidget() == owner_.previewFullscreenControlsWindow_) {
        owner_.previewControlCard_->setStyleSheet(previewFullscreenControlCardStyleSheet());
    } else if (owner_.previewControlCard_ != nullptr) {
        owner_.previewControlCard_->setStyleSheet(QString());
    }
    if (owner_.previewStatsCard_ != nullptr) {
        owner_.previewStatsCard_->setStyleSheet(QString());
    }
    owner_.updateEditorValidationSummary();
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->refreshMenuSelectionIcons();
    }
    owner_.updatePauseButtonAppearance();
    owner_.updatePreviewFullscreenButtonAppearance();
    owner_.update();
}

void MainWindow::WindowSection::updateOutlineDockCollapseButton()
{
    if (owner_.outlineCollapseButton_ == nullptr) {
        return;
    }
    owner_.outlineCollapseButton_->setText(owner_.outlineDockCollapsed_ ? QStringLiteral("▶") : QStringLiteral("◀"));
    owner_.outlineCollapseButton_->setToolTip(
        owner_.outlineDockCollapsed_
            ? UiText::text(QStringLiteral("window.expand_left_sidebar"))
            : UiText::text(QStringLiteral("window.collapse_left_sidebar"))
    );
}

void MainWindow::WindowSection::setOutlineDockCollapsed(bool collapsed)
{
    if (owner_.outlineDock_ == nullptr || owner_.outlineList_ == nullptr) {
        return;
    }

    constexpr int kCollapsedWidth = miacode::window_parity::kOutlineCollapsedWidth;
    constexpr int kExpandedMinWidth = miacode::window_parity::kOutlineExpandedMinWidth;
    if (collapsed) {
        const int currentWidth = owner_.outlineDock_->width();
        if (currentWidth > kCollapsedWidth) {
            owner_.outlineDockExpandedWidth_ = currentWidth;
        }
    }

    owner_.outlineDockCollapsed_ = collapsed;
    owner_.outlineList_->setVisible(!collapsed);

    const int targetWidth = collapsed ? kCollapsedWidth : qMax(kExpandedMinWidth, owner_.outlineDockExpandedWidth_);
    owner_.outlineDock_->setMinimumWidth(targetWidth);
    owner_.outlineDock_->setMaximumWidth(targetWidth);
    owner_.outlineDock_->resize(targetWidth, owner_.outlineDock_->height());
    if (QWidget* widget = owner_.outlineDock_->widget(); widget != nullptr) {
        widget->updateGeometry();
    }
    owner_.outlineDock_->updateGeometry();
    this->updateOutlineDockCollapseButton();
    if (owner_.visualLayoutPersistTimer_ != nullptr) {
        owner_.visualLayoutPersistTimer_->start();
    }
}

void MainWindow::WindowSection::applySystemWindowBackdrop(QWidget* target) const
{
#ifdef Q_OS_WIN
    if (target != nullptr) {
        UiNativeWindowTheme::applyToWidget(target);
        return;
    }
    // Theme the main window even while it is still hidden (early boot), then
    // sweep every visible top-level so theme switches restyle open dialogs
    // regardless of their parent chain.
    UiNativeWindowTheme::applyToWidget(const_cast<MainWindow*>(&owner_));
    UiNativeWindowTheme::applyToAllTopLevelWidgets();
#else
    Q_UNUSED(target);
#endif
}

int MainWindow::WindowSection::computeBottomTabsDeviceHeight() const
{
    return computeBottomTabsDeviceHeightForScale(owner_.bottomTabsContentScale_);
}

int MainWindow::WindowSection::computeBottomTabsDeviceHeightForScale(double contentScale) const
{
    if (owner_.bottomTabs_ == nullptr) {
        return 0;
    }

    owner_.bottomTabs_->ensurePolished();
    QTabBar* tabBar = owner_.bottomTabs_->tabBar();
    if (tabBar != nullptr) {
        tabBar->ensurePolished();
    }

    const int timelineHeight = scaledBottomTabsTimelineContentHeight(contentScale);
    const int tabBarHeight = scaledBottomTabsTabBarHeight(tabBar, contentScale);
    const int frameWidth = qMax(0, owner_.bottomTabs_->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, owner_.bottomTabs_));
    return miacode::window_parity::computeBottomTabsDeviceHeight(timelineHeight, tabBarHeight, frameWidth);
}

void MainWindow::WindowSection::applyBottomTabsContentScale()
{
    const double scale = clampedBottomTabsContentScale(owner_.bottomTabsContentScale_);
    owner_.bottomTabsContentScale_ = scale;
    const double headerScale = bottomTabsHeaderScaleForContentScale(scale);
    if (owner_.timelineQuickStateBridge_ != nullptr) {
        owner_.timelineQuickStateBridge_->setContentScale(scale);
    }
    applyScaledTabBarFont(owner_.bottomTabs_, headerScale);
    applyScaledTabBarFont(owner_.quickShellBottomTabsProxy_, headerScale);
    // 语法 / 无理 lists use a fixed 90% font, uniform regardless of the bottom-tab height.
    applyScaledListFont(owner_.errorList_, kBottomTabsIssueListFontScale);
    applyScaledListFont(owner_.muriList_, kBottomTabsIssueListFontScale);
    if (owner_.validationSection_ != nullptr) {
        owner_.validationSection_->scheduleWrappedListRelayout(owner_.errorList_);
        owner_.validationSection_->scheduleWrappedListRelayout(owner_.muriList_);
    }
}

void MainWindow::WindowSection::updateBottomTabsDeviceHeight()
{
    if (owner_.bottomTabs_ == nullptr) {
        return;
    }

    applyBottomTabsContentScale();
    const int targetHeight = this->computeBottomTabsDeviceHeight();
    if (targetHeight <= 0) {
        return;
    }
    if (owner_.bottomTabs_->minimumHeight() == targetHeight && owner_.bottomTabs_->maximumHeight() == targetHeight) {
        return;
    }

    owner_.bottomTabs_->setMinimumHeight(targetHeight);
    owner_.bottomTabs_->setMaximumHeight(targetHeight);
    owner_.bottomTabs_->updateGeometry();
    if (owner_.previewLeftColumn_ != nullptr) {
        owner_.previewLeftColumn_->updateGeometry();
    }
}
