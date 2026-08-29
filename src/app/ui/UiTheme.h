#pragma once

#include "UiText.h"

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

class QApplication;
class QComboBox;
class QMenu;
class QToolButton;
class QWidget;

namespace UiTheme {

enum class ResolvedTheme {
    Light,
    Dark,
};

struct Colors {
    bool dark = false;
    QColor windowBg;
    QColor windowAltBg;
    QColor toolbarBg;
    QColor statusBg;
    QColor panelBg;
    QColor cardBg;
    QColor cardAltBg;
    QColor inputBg;
    QColor inputDisabledBg;
    QColor canvasBg;
    QColor textPrimary;
    QColor textSecondary;
    QColor textMuted;
    QColor textInverse;
    QColor border;
    QColor borderSoft;
    QColor borderStrong;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentText;
    QColor selection;
    QColor selectionText;
    QColor menuBg;
    QColor menuBorder;
    QColor menuHoverBg;
    QColor menuDisabledText;
    QColor scrollTrack;
    QColor scrollHandle;
    QColor scrollHandleHover;
    QColor iconPrimary;
    QColor iconSecondary;
    QColor timelineWindow;
    QColor timelineHeader;
    QColor timelineSidebar;
    QColor timelineBase;
    QColor timelineBorder;
    QColor timelineGridMajor;
    QColor timelineGridSubdivision;
    QColor timelineGridMinor;
    QColor timelineLaneEven;
    QColor timelineLaneOdd;
    QColor timelineLabel;
    QColor timelineAxis;
    QColor timelineWaveStroke;
    QColor timelinePlayhead;
    QColor timelineCursor;
    QColor timelineLimit;
};

ResolvedTheme resolvedTheme();
bool isDarkTheme();
const Colors& colors();
QPalette applicationPalette();
void applyApplicationTheme(QApplication& app);

QString applicationStyleSheet();
QString scrollBarStyleSheet();
void styleRoundedMenu(QMenu& menu);
// Canonical checked-state indicator for QAction-backed menus. Pass visible=false
// to preserve the icon slot without painting the checkmark.
QIcon menuSelectionCheckIcon(bool visible = true);
QPalette timelineViewportPalette();
QString timelineZoomButtonStyleSheet();
QIcon timelineZoomButtonIcon(const QColor& strokeColor, const QString& sign);
QString timelineZoomButtonText(double scale);
QString timelineCheckBoxStyleSheet();
QString editorTextEditStyleSheet();
QString editorShellStyleSheet();
QString editorFindBarStyleSheet();
QString metadataPageStyleSheet();
QString latencyDetectionPageStyleSheet();
QString exportLauncherPageStyleSheet();
// Embedded video-export panel only (export page): flat underline tab bar +
// transparent per-tab scroll viewports. The modal dialog keeps its default
// boxed tabs.
QString embeddedExportTabStyleSheet();
QString metadataEmptyHintLabelStyleSheet();
QString outlineListStyleSheet();
// Compact, rounded row treatment for the batch queue's numbered chart-folder
// list. Kept separate from the editor outline list because the two surfaces
// have different density and selection semantics.
QString batchExportChartDirectoryListStyleSheet();
QString previewPanelStyleSheet();
QString compactToolbarButtonStyleSheet();
QString pausePreviewButtonStyleSheet(bool active);
QString formSliderStyleSheet();
QString dialogSliderStyleSheet();
QString dialogComboBoxStyleSheet();
QString dialogComboBoxStyleSheet(Qt::Alignment textAlignment);
void styleDialogComboBox(QComboBox* combo, int maxVisibleItems = 0);
void prepareDialogDropdownPopupWindow(QWidget* popupWindow);
QString dialogDropdownItemButtonStyleSheet();
QString dialogDropdownCheckBoxStyleSheet();
QString dialogDropdownScrollBarStyleSheet(const QString& parentSelector = QString());
void styleDialogDropdownMenu(QMenu& menu);
QString dialogSpinBoxStyleSheet();
QString dialogMenuButtonStyleSheet();
// QToolButton styled to match a createDialogComboBox closed box (for
// multi-select pseudo-dropdowns like 判定效果显示). Default centered.
QString dialogComboLikeButtonStyleSheet(Qt::Alignment textAlignment = Qt::AlignCenter);
QString dialogMenuCheckBoxStyleSheet();
QString dialogMenuLineEditStyleSheet();
QString dialogMenuLineEditStyleSheet(const QColor& backgroundColor);
QString dialogPushButtonStyleSheet(bool emphasized = false);
QString dialogAuxiliaryButtonStyleSheet();
void bindDialogMenuButtonPopupState(QToolButton* button, QMenu* menu);
void applyComboBoxPopupLimit(QComboBox* combo, int maxVisibleItems = 12);
QString dialogIconToolButtonStyleSheet(bool active = false);
QIcon dialogTransportPlayIcon(const QColor& color);
QIcon dialogTransportPauseIcon(const QColor& color);
QIcon dialogTransportStopIcon(const QColor& color);
QString preferencesDialogStyleSheet();
QString settingsDialogStyleSheet();
// Reusable QTabWidget/QTabBar styling for dialog tab strips. Append
// to a dialog's own stylesheet so callers don't have to re-derive the
// same selectors. Used by both the render-settings dialog (视频/游戏)
// and the preferences dialog (外观/编辑器/性能/快捷键) so the two read
// as one consistent pattern. Pass the dialog's own backdrop color so
// unselected tabs read as a piece of the dialog chrome rather than a
// foreign color band.
QString dialogTabStripStyleSheet(const QColor& dialogBackground);
QString aboutDialogStyleSheet();
QString exportDialogStyleSheet();
QString designerPickerDialogStyleSheet();
QString darkAwareCheckBoxStyleSheet();
QString readOnlyLineEditStyleSheet();
QString collapsibleToggleStyleSheet();
QString activePlaybackButtonStyleSheet();
QString validationMessageLabelStyleSheet();

}  // namespace UiTheme
