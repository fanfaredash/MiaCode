#include "MainWindow.WindowSection.h"
#include "../../MainWindowShared.h"

#include "AppVersion.h"
#include "BracketScopeHighlighter.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "../export/MainWindow.ExportSection.h"
#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewProgressStatsCache.h"

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

namespace {

constexpr auto kQuickShellTransportSeekProperty = "miacode.quick_shell_transport_seek";

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

bool MainWindow::WindowSection::confirmShellClose()
{
    if (!owner_.maybeSaveBeforeContinue()) {
        return false;
    }
    owner_.savePortableState();
    owner_.exportSection_->clearVideoExportWorkerState();
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
        owner_.stopQtPreviewPlayback(true);
    }
}

void MainWindow::WindowSection::updateShellPreviewScrub(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, owner_.previewDurationSeconds());
    QToolTip::hideText();
    appendQuickShellBackendLog(
        QStringLiteral("preview_scrub_update"),
        QString("second=%1 center=%2")
            .arg(clampedSecond, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    owner_.qtPreviewPauseSecond_ = clampedSecond;
    if (owner_.previewFullscreenActive_) {
        owner_.showPreviewFullscreenControls(false);
    }
    const bool shouldRenderNow =
        !owner_.previewScrubRenderElapsed_.isValid()
        || owner_.previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
    if (shouldRenderNow) {
        if (owner_.previewSeekDebounceTimer_ != nullptr) {
            owner_.previewSeekDebounceTimer_->stop();
        }
        owner_.seekPreviewToSecond(clampedSecond, centerView);
        owner_.previewScrubRenderElapsed_.restart();
    } else {
        owner_.schedulePreviewSeek(clampedSecond, centerView);
    }
}

void MainWindow::WindowSection::endShellPreviewScrub(double second, bool centerView)
{
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
    owner_.qtPreviewPauseSecond_ = clampedSecond;
    if (owner_.previewSeekDebounceTimer_ != nullptr) {
        owner_.previewSeekDebounceTimer_->stop();
    }
    owner_.seekPreviewToSecond(clampedSecond, centerView);
}

void MainWindow::WindowSection::setShellPreviewRate(double rate)
{
    owner_.applyPreviewPlaybackRate(rate);
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

bool MainWindow::WindowSection::shellPreviewPlaying() const
{
    return owner_.qtPreviewPlaying_;
}

double MainWindow::WindowSection::shellPreviewPositionSeconds() const
{
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
    return owner_.previewLeftColumn_;
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

    if (owner_.editorWidget_ != nullptr) {
        owner_.editorWidget_->setStyleSheet(UiTheme::editorTextEditStyleSheet());
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(owner_.editorWidget_)) {
            if (QScrollBar* vbar = scrollArea->verticalScrollBar()) {
                vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
            if (QScrollBar* hbar = scrollArea->horizontalScrollBar()) {
                hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
        }
    }
    if (owner_.editorFindBar_ != nullptr) {
        owner_.editorFindBar_->setStyleSheet(UiTheme::editorFindBarStyleSheet());
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
    if (owner_.outlineList_ != nullptr) {
        owner_.outlineList_->setStyleSheet(UiTheme::outlineListStyleSheet());
    }
    if (owner_.deleteDifficultyButton_ != nullptr) {
        owner_.deleteDifficultyButton_->setStyleSheet(UiTheme::deleteDifficultyButtonStyleSheet());
        owner_.deleteDifficultyButton_->setIcon(makeOutlineCloseIcon(UiTheme::colors().iconSecondary));
    }
    if (owner_.timelineView_ != nullptr) {
        owner_.timelineView_->refreshTheme();
    }
    this->updateBottomTabsDeviceHeight();
    if (owner_.chartBracketHighlighter_ != nullptr) {
        owner_.chartBracketHighlighter_->rehighlight();
    }
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
    refreshMenuThemeRecursive(owner_.exportVideoMenu_, &refreshedMenus);
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
            ? (UiText::isChineseUi() ? QStringLiteral("展开左侧字段栏") : QStringLiteral("Expand left sidebar"))
            : (UiText::isChineseUi() ? QStringLiteral("折叠左侧字段栏") : QStringLiteral("Collapse left sidebar"))
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
    if (collapsed) {
        owner_.updateDifficultyDeleteButton(false);
    }

    const int targetWidth = collapsed ? kCollapsedWidth : qMax(kExpandedMinWidth, owner_.outlineDockExpandedWidth_);
    owner_.outlineDock_->setMinimumWidth(targetWidth);
    owner_.outlineDock_->setMaximumWidth(targetWidth);
    owner_.outlineDock_->resize(targetWidth, owner_.outlineDock_->height());
    if (QWidget* widget = owner_.outlineDock_->widget(); widget != nullptr) {
        widget->updateGeometry();
    }
    owner_.outlineDock_->updateGeometry();
    this->updateOutlineDockCollapseButton();
}

void MainWindow::WindowSection::applySystemWindowBackdrop(QWidget* target) const
{
#ifdef Q_OS_WIN
    if (target != nullptr) {
        applySystemBackdropToWidget(target, true, UiTheme::isDarkTheme());
        return;
    }
    applySystemBackdropToWidget(const_cast<MainWindow*>(&owner_), true, UiTheme::isDarkTheme());
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* topLevel : topLevels) {
        if (topLevel == nullptr
            || topLevel == &owner_
            || topLevel->parentWidget() != &owner_
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

int MainWindow::WindowSection::computeBottomTabsDeviceHeight() const
{
    if (owner_.bottomTabs_ == nullptr || owner_.timelineView_ == nullptr) {
        return 0;
    }

    owner_.bottomTabs_->ensurePolished();
    owner_.timelineView_->ensurePolished();
    QTabBar* tabBar = owner_.bottomTabs_->tabBar();
    if (tabBar != nullptr) {
        tabBar->ensurePolished();
    }

    const int timelineHeight = qMax(owner_.timelineView_->minimumHeight(), owner_.timelineView_->minimumSizeHint().height());
    const int tabBarHeight = tabBar != nullptr
        ? qMax(tabBar->minimumSizeHint().height(), tabBar->sizeHint().height())
        : 0;
    const int frameWidth = qMax(0, owner_.bottomTabs_->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, owner_.bottomTabs_));
    return miacode::window_parity::computeBottomTabsDeviceHeight(timelineHeight, tabBarHeight, frameWidth);
}

void MainWindow::WindowSection::updateBottomTabsDeviceHeight()
{
    if (owner_.bottomTabs_ == nullptr) {
        return;
    }

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

