#include "../../MainWindow.h"
#include "../../MainWindowShared.h"

#include "AppVersion.h"
#include "BracketScopeHighlighter.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace miacode::mainwindow::shared;

namespace {

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

#ifdef Q_OS_WIN
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr DWORD kDwmwaTextColor = 36;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmwaMicaEffect = 1029;
constexpr int kDwmsbtNone = 1;
constexpr int kDwmsbtMainWindow = 2;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

bool setDwmWindowAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size)
{
    if (hwnd == nullptr || value == nullptr || size == 0) {
        return false;
    }
    static HMODULE dwmapiModule = ::LoadLibraryW(L"dwmapi.dll");
    if (dwmapiModule == nullptr) {
        return false;
    }
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        ::GetProcAddress(dwmapiModule, "DwmSetWindowAttribute")
    );
    if (setWindowAttribute == nullptr) {
        return false;
    }
    return SUCCEEDED(setWindowAttribute(hwnd, attribute, value, size));
}

COLORREF colorRefForDwm(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}

void applySystemBackdropToWidget(QWidget* widget, bool enabled, bool darkTheme)
{
    if (widget == nullptr) {
        return;
    }
    widget->winId();
    QWidget* topLevel = widget->window();
    const WId nativeId = topLevel != nullptr ? topLevel->winId() : widget->winId();
    const HWND hwnd = reinterpret_cast<HWND>(nativeId);
    if (hwnd == nullptr) {
        return;
    }

    const BOOL darkMode = darkTheme ? TRUE : FALSE;
    setDwmWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode, sizeof(darkMode));

    if (UiText::preferredTheme() == UiText::ThemePreference::System) {
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
    } else {
        const UiTheme::Colors& themeColors = UiTheme::colors();
        const bool active = topLevel != nullptr ? topLevel->isActiveWindow() : widget->isActiveWindow();
        const COLORREF borderColor = colorRefForDwm(active ? themeColors.borderStrong : themeColors.borderSoft);
        const COLORREF captionColor = colorRefForDwm(active ? themeColors.toolbarBg : themeColors.windowAltBg);
        const COLORREF textColor = colorRefForDwm(active ? themeColors.textPrimary : themeColors.textSecondary);
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &borderColor, sizeof(borderColor));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &captionColor, sizeof(captionColor));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &textColor, sizeof(textColor));
    }

    const int backdropType = enabled ? kDwmsbtMainWindow : kDwmsbtNone;
    if (!setDwmWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType))) {
        const BOOL micaEnabled = enabled ? TRUE : FALSE;
        setDwmWindowAttribute(hwnd, kDwmwaMicaEffect, &micaEnabled, sizeof(micaEnabled));
    }
}
#endif

}  // namespace

bool MainWindow::quickShellRootWindowFrameGeometryAvailable() const
{
    return quickShellRootWindowFrameGeometry_.isValid();
}

QRect MainWindow::quickShellRootWindowFrameGeometry() const
{
    return quickShellRootWindowFrameGeometry_;
}

bool MainWindow::confirmShellClose()
{
    if (!maybeSaveBeforeContinue()) {
        return false;
    }
    savePortableState();
    clearVideoExportWorkerState();
    return true;
}

void MainWindow::toggleShellPreviewPlayback()
{
    onTogglePreviewPause();
}

void MainWindow::stopShellPreview()
{
    onStopPreview();
}

void MainWindow::seekShellPreview(double second)
{
    seekPreviewToSecond(second, true);
}

void MainWindow::setShellPreviewRate(double rate)
{
    applyPreviewPlaybackRate(rate);
}

void MainWindow::setShellPreviewFullscreen(bool fullscreen)
{
    if (previewFullscreenActive_ == fullscreen) {
        return;
    }
    if (fullscreen) {
        enterPreviewFullscreen();
    } else {
        exitPreviewFullscreen();
    }
}

bool MainWindow::shellHasShortcut(const QKeySequence& sequence) const
{
    if (sequence.isEmpty()) {
        return false;
    }
    for (QAction* action : quickShellShortcutActions()) {
        if (actionMatchesShortcut(action, sequence)) {
            return true;
        }
    }
    return false;
}

bool MainWindow::shellTriggerShortcut(const QKeySequence& sequence)
{
    if (sequence.isEmpty()) {
        return false;
    }
    for (QAction* action : quickShellShortcutActions()) {
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

QString MainWindow::shellWindowTitle() const
{
    return windowTitle();
}

bool MainWindow::shellWorkspacePanelsSwapped() const
{
    return workspacePanelsSwapped_;
}

QString MainWindow::shellPreviewSpeedLabel() const
{
    return previewSpeedButton_ != nullptr ? previewSpeedButton_->text() : QStringLiteral("1x");
}

bool MainWindow::shellPreviewPlaying() const
{
    return qtPreviewPlaying_;
}

double MainWindow::shellPreviewPositionSeconds() const
{
    return previewSlider_ != nullptr
        ? static_cast<double>(previewSlider_->value()) / 1000.0
        : qtPreviewPauseSecond_;
}

double MainWindow::shellPreviewDurationSeconds() const
{
    return previewDurationSeconds();
}

bool MainWindow::shellPreviewFullscreen() const
{
    return previewFullscreenActive_;
}

QObject* MainWindow::shellPreviewRuntimeObject() const
{
    return previewCanvas_;
}

QObject* MainWindow::shellPreviewStageMediaHostObject() const
{
    return previewStageMediaHost_;
}

bool MainWindow::shellPreviewUsesSeparateSurface() const
{
    return quickShellPreviewUsesSeparateSurface();
}

QWindow* MainWindow::shellPreviewCompositeWindow() const
{
    return quickShellPreviewCompositeWindow();
}

QWidget* MainWindow::shellWindowWidget() const
{
    return const_cast<MainWindow*>(this);
}

QDockWidget* MainWindow::shellOutlineDockWidget() const
{
    return outlineDock_;
}

bool MainWindow::shellOutlineDockCollapsed() const
{
    return outlineDockCollapsed_;
}

int MainWindow::shellOutlineDockExpandedWidth() const
{
    return outlineDockExpandedWidth_;
}

QWidget* MainWindow::shellWorkspaceWidget() const
{
    return previewLeftColumn_;
}

QWidget* MainWindow::shellPreviewPanelWidget() const
{
    return previewPanel_;
}

QString MainWindow::shellPreviewPanelStyleSheet() const
{
    return previewPanel_ != nullptr ? previewPanel_->styleSheet() : QString();
}

QWidget* MainWindow::shellPreviewControlCardWidget() const
{
    return previewControlCard_;
}

QWidget* MainWindow::shellPreviewStatsCardWidget() const
{
    return previewStatsCard_;
}

QLabel* MainWindow::shellPreviewTotalStatsLabel() const
{
    return previewTotalStatsLabel_;
}

QGridLayout* MainWindow::shellPreviewStatsGridLayout() const
{
    return previewStatsGridLayout_;
}

int MainWindow::shellPreviewStatsMinimumPanelHeight(int panelWidth) const
{
    return previewStatsMinimumHeightForPanelWidth(panelWidth);
}

int MainWindow::shellUpdatePreviewStatsLayout(int hostWidth)
{
    return updatePreviewStatsLayoutMode(hostWidth);
}

double MainWindow::shellNormalizedPreviewCanvasAspectRatio() const
{
    return normalizedPreviewCanvasAspectRatio(previewCanvasAspectRatio_);
}

void MainWindow::shellRefreshLayoutAfterResize()
{
    refreshLayoutAfterPageSwitch();
}

void MainWindow::shellSetRootWindowFrameGeometry(const QRect& geometry)
{
    quickShellRootWindowFrameGeometry_ = geometry;
    setProperty("miacode.quick_root_window_frame_geometry", geometry);
}

void MainWindow::shellNoteQuickUiReady()
{
    noteQuickShellStartupUiReady();
}

void MainWindow::applyUiTheme()
{
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        UiTheme::applyApplicationTheme(*app);
    }

    if (editorWidget_ != nullptr) {
        editorWidget_->setStyleSheet(UiTheme::editorTextEditStyleSheet());
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(editorWidget_)) {
            if (QScrollBar* vbar = scrollArea->verticalScrollBar()) {
                vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
            if (QScrollBar* hbar = scrollArea->horizontalScrollBar()) {
                hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
        }
    }
    if (editorFindBar_ != nullptr) {
        editorFindBar_->setStyleSheet(UiTheme::editorFindBarStyleSheet());
    }
    if (welcomePage_ != nullptr) {
        welcomePage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (welcomeEmptyHintLabel_ != nullptr) {
        welcomeEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (metadataPage_ != nullptr) {
        metadataPage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (metadataEmptyHintLabel_ != nullptr) {
        metadataEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (metadataExtraEdit_ != nullptr) {
        if (QScrollBar* vbar = metadataExtraEdit_->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
        if (QScrollBar* hbar = metadataExtraEdit_->horizontalScrollBar()) {
            hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    if (outlineList_ != nullptr) {
        outlineList_->setStyleSheet(UiTheme::outlineListStyleSheet());
    }
    if (deleteDifficultyButton_ != nullptr) {
        deleteDifficultyButton_->setStyleSheet(UiTheme::deleteDifficultyButtonStyleSheet());
        deleteDifficultyButton_->setIcon(makeOutlineCloseIcon(UiTheme::colors().iconSecondary));
    }
    if (timelineView_ != nullptr) {
        timelineView_->refreshTheme();
    }
    updateBottomTabsDeviceHeight();
    if (chartBracketHighlighter_ != nullptr) {
        chartBracketHighlighter_->rehighlight();
    }
    if (metadataBracketHighlighter_ != nullptr) {
        metadataBracketHighlighter_->rehighlight();
    }
    if (QWidget* editorShell = findChild<QWidget*>(QStringLiteral("EditorShell")); editorShell != nullptr) {
        editorShell->setStyleSheet(UiTheme::editorShellStyleSheet());
    }
    const UiTheme::Colors& themeColors = UiTheme::colors();
    if (editorHeaderWidget_ != nullptr) {
        editorHeaderWidget_->setAttribute(Qt::WA_StyledBackground, true);
        editorHeaderWidget_->setStyleSheet(
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
    if (previewPanel_ != nullptr) {
        previewPanel_->setStyleSheet(previewPanelStyle);
    }
    const QList<QMenu*> menus = findChildren<QMenu*>();
    for (QMenu* menu : menus) {
        if (menu != nullptr) {
            UiTheme::styleRoundedMenu(*menu);
        }
    }

    const QColor iconColor = UiTheme::colors().iconPrimary;
    const QColor previewControlIconColor =
        previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : iconColor;
    const QColor secondaryIconColor = UiTheme::colors().iconSecondary;
    if (stopPreviewAction_ != nullptr) {
        stopPreviewAction_->setIcon(makePreviewStopIcon(previewControlIconColor));
    }
    if (settingsPlaceholderAction_ != nullptr) {
        settingsPlaceholderAction_->setIcon(makeSettingsGearIcon(secondaryIconColor));
    }
    if (previewAudioSettingsButton_ != nullptr) {
        previewAudioSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (previewVideoSettingsButton_ != nullptr) {
        previewVideoSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    applyWorkspacePanelArrangement();
    applySystemWindowBackdrop();
    if (syntaxCheckButton_ != nullptr) {
        syntaxCheckButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (exportVideoButton_ != nullptr) {
        exportVideoButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (outlineCollapseButton_ != nullptr) {
        outlineCollapseButton_->setStyleSheet(outlineCollapseButtonStyleSheet());
        updateOutlineDockCollapseButton();
    }
    if (previewFullscreenHintLabel_ != nullptr) {
        previewFullscreenHintLabel_->setStyleSheet(previewFullscreenHintStyleSheet());
    }
    if (previewFullscreenActive_
        && previewControlCard_ != nullptr
        && previewControlCard_->parentWidget() == previewFullscreenControlsWindow_) {
        previewControlCard_->setStyleSheet(previewFullscreenControlCardStyleSheet());
    } else if (previewControlCard_ != nullptr) {
        previewControlCard_->setStyleSheet(QString());
    }
    if (previewStatsCard_ != nullptr) {
        previewStatsCard_->setStyleSheet(QString());
    }
    updateEditorValidationSummary();
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
    update();
}

void MainWindow::updateOutlineDockCollapseButton()
{
    if (outlineCollapseButton_ == nullptr) {
        return;
    }
    outlineCollapseButton_->setText(outlineDockCollapsed_ ? QStringLiteral("▶") : QStringLiteral("◀"));
    outlineCollapseButton_->setToolTip(
        outlineDockCollapsed_
            ? (UiText::isChineseUi() ? QStringLiteral("展开左侧字段栏") : QStringLiteral("Expand left sidebar"))
            : (UiText::isChineseUi() ? QStringLiteral("折叠左侧字段栏") : QStringLiteral("Collapse left sidebar"))
    );
}

void MainWindow::setOutlineDockCollapsed(bool collapsed)
{
    if (outlineDock_ == nullptr || outlineList_ == nullptr) {
        return;
    }

    constexpr int kCollapsedWidth = miacode::window_parity::kOutlineCollapsedWidth;
    constexpr int kExpandedMinWidth = miacode::window_parity::kOutlineExpandedMinWidth;
    if (collapsed) {
        const int currentWidth = outlineDock_->width();
        if (currentWidth > kCollapsedWidth) {
            outlineDockExpandedWidth_ = currentWidth;
        }
    }

    outlineDockCollapsed_ = collapsed;
    outlineList_->setVisible(!collapsed);
    if (collapsed) {
        updateDifficultyDeleteButton(false);
    }

    const int targetWidth = collapsed ? kCollapsedWidth : qMax(kExpandedMinWidth, outlineDockExpandedWidth_);
    outlineDock_->setMinimumWidth(targetWidth);
    outlineDock_->setMaximumWidth(targetWidth);
    outlineDock_->resize(targetWidth, outlineDock_->height());
    if (QWidget* widget = outlineDock_->widget(); widget != nullptr) {
        widget->updateGeometry();
    }
    outlineDock_->updateGeometry();
    updateOutlineDockCollapseButton();
}

void MainWindow::applySystemWindowBackdrop(QWidget* target) const
{
#ifdef Q_OS_WIN
    if (target != nullptr) {
        applySystemBackdropToWidget(target, true, UiTheme::isDarkTheme());
        return;
    }
    applySystemBackdropToWidget(const_cast<MainWindow*>(this), true, UiTheme::isDarkTheme());
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* topLevel : topLevels) {
        if (topLevel == nullptr
            || topLevel == this
            || topLevel->parentWidget() != this
            || !topLevel->isVisible()
            || topLevel->windowState().testFlag(Qt::WindowMinimized)) {
            continue;
        }
        applySystemBackdropToWidget(topLevel, true, UiTheme::isDarkTheme());
    }
#else
    Q_UNUSED(target);
#endif
}

int MainWindow::computeBottomTabsDeviceHeight() const
{
    if (bottomTabs_ == nullptr || timelineView_ == nullptr) {
        return 0;
    }

    bottomTabs_->ensurePolished();
    timelineView_->ensurePolished();
    QTabBar* tabBar = bottomTabs_->tabBar();
    if (tabBar != nullptr) {
        tabBar->ensurePolished();
    }

    const int timelineHeight = qMax(timelineView_->minimumHeight(), timelineView_->minimumSizeHint().height());
    const int tabBarHeight = tabBar != nullptr
        ? qMax(tabBar->minimumSizeHint().height(), tabBar->sizeHint().height())
        : 0;
    const int frameWidth = qMax(0, bottomTabs_->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, bottomTabs_));
    return miacode::window_parity::computeBottomTabsDeviceHeight(timelineHeight, tabBarHeight, frameWidth);
}

void MainWindow::updateBottomTabsDeviceHeight()
{
    if (bottomTabs_ == nullptr) {
        return;
    }

    const int targetHeight = computeBottomTabsDeviceHeight();
    if (targetHeight <= 0) {
        return;
    }
    if (bottomTabs_->minimumHeight() == targetHeight && bottomTabs_->maximumHeight() == targetHeight) {
        return;
    }

    bottomTabs_->setMinimumHeight(targetHeight);
    bottomTabs_->setMaximumHeight(targetHeight);
    bottomTabs_->updateGeometry();
    if (previewLeftColumn_ != nullptr) {
        previewLeftColumn_->updateGeometry();
    }
}

