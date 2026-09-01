#include "runtime/shell/ShellHost.h"
#include "runtime/Shared.h"

#include "AppVersion.h"
#include "BracketScopeHighlighter.h"
#include "runtime/playback/PlaybackCoordinator.h"
#include "UiText.h"
#include "UiTheme.h"
#include "UiNativeWindowTheme.h"
#include "runtime/validation/ValidationHost.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/export/VideoExportHost.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace miacode::runtime::shared;

namespace {

// Same helper as in MainWindow.WindowRuntime.cpp — duplicated here so the
// quick-shell close path can cascade-close popups without leaking the helper
// outside its translation unit. See the longer comment over there for the
// caveat about Qt::ApplicationModal exec() blocking taskbar-initiated closes
// from reaching Session's closeEvent in the first place.
int dismissOpenChildPopupDialogs()
{
    int closed = 0;
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevels) {
        if (widget == nullptr) {
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

bool miacode::runtime::ShellHost::rootWindowFrameGeometryAvailable() const
{
    return session_.rootWindowFrameGeometry_.isValid();
}

QRect miacode::runtime::ShellHost::rootWindowFrameGeometry() const
{
    return session_.rootWindowFrameGeometry_;
}

void miacode::runtime::ShellHost::requestShellClose(std::function<void(bool)> onDecided)
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    // The prompt is a QML dialog, so the answer arrives later. Everything below
    // the question moved into the continuation unchanged — including the
    // ordering rule it depends on: a cancelled close must leave the world
    // untouched.
    session_.documents_->requestLeaveDocument(
        [this, onDecided = std::move(onDecided), totalTimer](bool canClose) mutable {
            const bool confirmed = canClose && finishShellClose(totalTimer);
            if (onDecided) {
                onDecided(confirmed);
            }
        });
}

bool miacode::runtime::ShellHost::finishShellClose(QElapsedTimer totalTimer)
{
    // Close is confirmed. Only NOW cascade-close popup chains (Preferences,
    // Keyboard Shortcuts, etc.). Running this after the unsaved-changes
    // prompt is accepted means a cancelled close leaves every sibling
    // window untouched — including the quick-shell's native bridge/
    // compositing surfaces (createBridgeSurface() hosts the editor, timeline
    // and preview as top-level QWidgets) — so the window keeps full
    // functionality. Doing the sweep before the prompt closed those surfaces
    // and a Cancel could not bring them back.
    const int dismissed = dismissOpenChildPopupDialogs();
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
    session_.documents_->cleanupCrashRecoveryForCleanExit();
    miacode::crash_recovery::clearSessionMarker();

    QElapsedTimer savePortableTimer;
    savePortableTimer.start();
    session_.savePortableState();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/window"),
        QStringLiteral("save_portable_state"),
        savePortableTimer.elapsed()
    );

    QElapsedTimer exportCleanupTimer;
    exportCleanupTimer.start();
    session_.videoExport_->clearVideoExportWorkerState();
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
void miacode::runtime::ShellHost::setBottomTabsHeight(int height)
{
    if (session_.bottomTabs_ == nullptr) {
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
    const int windowHeight = session_.rootWindow_ != nullptr
        ? session_.rootWindow_->height()
        : 0;
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
    if (!qFuzzyCompare(session_.bottomTabsContentScale_ + 1.0, clampedScale + 1.0)) {
        session_.bottomTabsContentScale_ = clampedScale;
        applyBottomTabsContentScale();
        // Persist the new divider height as an app-level preference. Stored as a
        // content-scale ratio (not pixels) so it stays valid across DPI/layout
        // changes; the write is debounced so a drag doesn't thrash the disk.
        if (session_.bottomTabsContentScalePersistTimer_ != nullptr) {
            session_.bottomTabsContentScalePersistTimer_->start();
        }
    }
    updateBottomTabsDeviceHeight();
}

void miacode::runtime::ShellHost::setRootWindowFrameGeometry(const QRect& geometry)
{
    session_.rootWindowFrameGeometry_ = geometry;
    session_.setProperty("miacode.quick_root_window_frame_geometry", geometry);
}

void miacode::runtime::ShellHost::noteRootWindowReady()
{
    session_.noteQuickShellStartupUiReady();
}

void miacode::runtime::ShellHost::applyUiTheme()
{
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        UiTheme::applyApplicationTheme(*app);
    }

    if (session_.outlineList_ != nullptr) {
    }
    if (session_.editorFindBar_ != nullptr) {
        session_.editorFindBar_->setStyleSheet(UiTheme::editorFindBarStyleSheet());
    }
    if (session_.editorFindCloseButton_ != nullptr) {
        // The ✕ is a baked QIcon, so unlike the QSS text color it doesn't follow
        // the palette on its own — re-tint it to the find bar's button text
        // color (textPrimary) so it tracks the light/dark theme.
        session_.editorFindCloseButton_->setIcon(makeOutlineCloseIcon(UiTheme::colors().textPrimary));
    }
    if (session_.welcomePage_ != nullptr) {
        session_.welcomePage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (session_.welcomeEmptyHintLabel_ != nullptr) {
        session_.welcomeEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (session_.metadataPage_ != nullptr) {
        session_.metadataPage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (session_.metadataEmptyHintLabel_ != nullptr) {
        session_.metadataEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (session_.metadataExtraEdit_ != nullptr) {
        if (QScrollBar* vbar = session_.metadataExtraEdit_->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
        if (QScrollBar* hbar = session_.metadataExtraEdit_->horizontalScrollBar()) {
            hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    // 语法 / 无理 issue lists share the code-editor's rounded scrollbar style (instead of
    // the default native bar). Re-applied here so it follows light/dark theme switches.
    for (QListWidget* issueList : {session_.errorList_, session_.muriList_}) {
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
    if (session_.outlineList_ != nullptr) {
        session_.outlineList_->setStyleSheet(UiTheme::outlineListStyleSheet());
        // The scroll bar was styled once at construction — re-style it here or
        // it keeps the previous theme's colors after a light/dark switch.
        if (QScrollBar* vbar = session_.outlineList_->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    this->updateBottomTabsDeviceHeight();
    if (session_.metadataBracketHighlighter_ != nullptr) {
        session_.metadataBracketHighlighter_->rehighlight();
    }
    if (QWidget* editorShell = session_.findChild<QWidget*>(QStringLiteral("EditorShell")); editorShell != nullptr) {
        editorShell->setStyleSheet(UiTheme::editorShellStyleSheet());
    }
    const UiTheme::Colors& themeColors = UiTheme::colors();
    if (session_.editorHeaderWidget_ != nullptr) {
        session_.editorHeaderWidget_->setAttribute(Qt::WA_StyledBackground, true);
        session_.editorHeaderWidget_->setStyleSheet(
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
    if (session_.previewPanel_ != nullptr) {
        session_.previewPanel_->setStyleSheet(previewPanelStyle);
    }
    QSet<QMenu*> refreshedMenus;
    refreshMenuThemeRecursive(session_.toolboxMenu_, &refreshedMenus);
    const QList<QMenu*> menus = session_.findChildren<QMenu*>();
    for (QMenu* menu : menus) {
        refreshMenuThemeRecursive(menu, &refreshedMenus);
    }

    const QColor iconColor = UiTheme::colors().iconPrimary;
    const QColor previewControlIconColor =
        session_.previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : iconColor;
    const QColor secondaryIconColor = UiTheme::colors().iconSecondary;
    if (session_.stopPreviewAction_ != nullptr) {
        session_.stopPreviewAction_->setIcon(makePreviewStopIcon(previewControlIconColor));
    }
    if (session_.settingsPlaceholderAction_ != nullptr) {
        session_.settingsPlaceholderAction_->setIcon(makeSettingsGearIcon(secondaryIconColor));
    }
    if (session_.previewAudioSettingsButton_ != nullptr) {
        session_.previewAudioSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (session_.previewVideoSettingsButton_ != nullptr) {
        session_.previewVideoSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    session_.applyWorkspacePanelArrangement();
    this->applySystemWindowBackdrop();
    if (session_.syntaxCheckButton_ != nullptr) {
        session_.syntaxCheckButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (session_.exportVideoButton_ != nullptr) {
        session_.exportVideoButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (session_.outlineCollapseButton_ != nullptr) {
        session_.outlineCollapseButton_->setStyleSheet(outlineCollapseButtonStyleSheet());
        this->updateOutlineDockCollapseButton();
    }
    if (session_.previewFullscreenHintLabel_ != nullptr) {
        session_.previewFullscreenHintLabel_->setStyleSheet(previewFullscreenHintStyleSheet());
    }
    if (session_.previewFullscreenActive_
        && session_.previewControlCard_ != nullptr
        && session_.previewControlCard_->parentWidget() == session_.previewFullscreenControlsWindow_) {
        session_.previewControlCard_->setStyleSheet(previewFullscreenControlCardStyleSheet());
    } else if (session_.previewControlCard_ != nullptr) {
        session_.previewControlCard_->setStyleSheet(QString());
    }
    if (session_.previewStatsCard_ != nullptr) {
        session_.previewStatsCard_->setStyleSheet(QString());
    }
    session_.updateEditorValidationSummary();
    session_.updatePauseButtonAppearance();
    session_.updatePreviewFullscreenButtonAppearance();
}

void miacode::runtime::ShellHost::updateOutlineDockCollapseButton()
{
    if (session_.outlineCollapseButton_ == nullptr) {
        return;
    }
    session_.outlineCollapseButton_->setText(session_.outlineDockCollapsed_ ? QStringLiteral("▶") : QStringLiteral("◀"));
    session_.outlineCollapseButton_->setToolTip(
        session_.outlineDockCollapsed_
            ? UiText::text(QStringLiteral("window.expand_left_sidebar"))
            : UiText::text(QStringLiteral("window.collapse_left_sidebar"))
    );
}

void miacode::runtime::ShellHost::setOutlineDockCollapsed(bool collapsed)
{
    if (session_.outlineDock_ == nullptr || session_.outlineList_ == nullptr) {
        return;
    }

    constexpr int kCollapsedWidth = miacode::window_parity::kOutlineCollapsedWidth;
    constexpr int kExpandedMinWidth = miacode::window_parity::kOutlineExpandedMinWidth;
    if (collapsed) {
        const int currentWidth = session_.outlineDock_->width();
        if (currentWidth > kCollapsedWidth) {
            session_.outlineDockExpandedWidth_ = currentWidth;
        }
    }

    session_.outlineDockCollapsed_ = collapsed;
    session_.outlineList_->setVisible(!collapsed);

    const int targetWidth = collapsed ? kCollapsedWidth : qMax(kExpandedMinWidth, session_.outlineDockExpandedWidth_);
    session_.outlineDock_->setMinimumWidth(targetWidth);
    session_.outlineDock_->setMaximumWidth(targetWidth);
    session_.outlineDock_->resize(targetWidth, session_.outlineDock_->height());
    if (QWidget* widget = session_.outlineDock_->widget(); widget != nullptr) {
        widget->updateGeometry();
    }
    session_.outlineDock_->updateGeometry();
    this->updateOutlineDockCollapseButton();
    if (session_.visualLayoutPersistTimer_ != nullptr) {
        session_.visualLayoutPersistTimer_->start();
    }
}

void miacode::runtime::ShellHost::applySystemWindowBackdrop(QWidget* target) const
{
#ifdef Q_OS_WIN
    if (target != nullptr) {
        UiNativeWindowTheme::applyToWidget(target);
        return;
    }
    UiNativeWindowTheme::applyToAllTopLevelWidgets();
#else
    Q_UNUSED(target);
#endif
}

int miacode::runtime::ShellHost::computeBottomTabsDeviceHeight() const
{
    return computeBottomTabsDeviceHeightForScale(session_.bottomTabsContentScale_);
}

int miacode::runtime::ShellHost::computeBottomTabsDeviceHeightForScale(double contentScale) const
{
    if (session_.bottomTabs_ == nullptr) {
        return 0;
    }

    session_.bottomTabs_->ensurePolished();
    QTabBar* tabBar = session_.bottomTabs_->tabBar();
    if (tabBar != nullptr) {
        tabBar->ensurePolished();
    }

    const int timelineHeight = scaledBottomTabsTimelineContentHeight(contentScale);
    const int tabBarHeight = scaledBottomTabsTabBarHeight(tabBar, contentScale);
    const int frameWidth = qMax(0, session_.bottomTabs_->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, session_.bottomTabs_));
    return miacode::window_parity::computeBottomTabsDeviceHeight(timelineHeight, tabBarHeight, frameWidth);
}

void miacode::runtime::ShellHost::applyBottomTabsContentScale()
{
    const double scale = clampedBottomTabsContentScale(session_.bottomTabsContentScale_);
    session_.bottomTabsContentScale_ = scale;
    const double headerScale = bottomTabsHeaderScaleForContentScale(scale);
    if (session_.timelineQuickStateBridge_ != nullptr) {
        session_.timelineQuickStateBridge_->setContentScale(scale);
    }
    applyScaledTabBarFont(session_.bottomTabs_, headerScale);
    applyScaledTabBarFont(session_.quickShellBottomTabsProxy_, headerScale);
    // 语法 / 无理 lists use a fixed 90% font, uniform regardless of the bottom-tab height.
    applyScaledListFont(session_.errorList_, kBottomTabsIssueListFontScale);
    applyScaledListFont(session_.muriList_, kBottomTabsIssueListFontScale);
    if (session_.validation_ != nullptr) {
        session_.validation_->scheduleWrappedListRelayout(session_.errorList_);
        session_.validation_->scheduleWrappedListRelayout(session_.muriList_);
    }
}

void miacode::runtime::ShellHost::updateBottomTabsDeviceHeight()
{
    if (session_.bottomTabs_ == nullptr) {
        return;
    }

    applyBottomTabsContentScale();
    const int targetHeight = this->computeBottomTabsDeviceHeight();
    if (targetHeight <= 0) {
        return;
    }
    if (session_.bottomTabs_->minimumHeight() == targetHeight && session_.bottomTabs_->maximumHeight() == targetHeight) {
        return;
    }

    session_.bottomTabs_->setMinimumHeight(targetHeight);
    session_.bottomTabs_->setMaximumHeight(targetHeight);
    session_.bottomTabs_->updateGeometry();
    if (session_.previewLeftColumn_ != nullptr) {
        session_.previewLeftColumn_->updateGeometry();
    }
}
