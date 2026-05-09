#pragma once

#include "UiText.h"

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

class QApplication;
class QMenu;

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
    QColor timelineWaveFill;
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
QPalette timelineViewportPalette();
QString timelineZoomButtonStyleSheet();
QIcon timelineZoomButtonIcon(const QColor& strokeColor, const QString& sign);
QString timelineZoomButtonText(double scale);
QString timelineCheckBoxStyleSheet();
QString editorTextEditStyleSheet();
QString editorShellStyleSheet();
QString editorFindBarStyleSheet();
QString metadataPageStyleSheet();
QString metadataEmptyHintLabelStyleSheet();
QString outlineListStyleSheet();
QString deleteDifficultyButtonStyleSheet();
QString previewPanelStyleSheet();
QString compactToolbarButtonStyleSheet();
QString pausePreviewButtonStyleSheet(bool active);
QString formSliderStyleSheet();
QString dialogSliderStyleSheet();
QString dialogComboBoxStyleSheet();
QString dialogMenuButtonStyleSheet();
QString dialogMenuLineEditStyleSheet();
QString dialogPushButtonStyleSheet(bool emphasized = false);
QString dialogIconToolButtonStyleSheet(bool active = false);
QIcon dialogTransportPlayIcon(const QColor& color);
QIcon dialogTransportPauseIcon(const QColor& color);
QIcon dialogTransportStopIcon(const QColor& color);
QString preferencesDialogStyleSheet();
QString settingsDialogStyleSheet();
QString aboutDialogStyleSheet();
QString exportDialogStyleSheet();
QString readOnlyLineEditStyleSheet();
QString collapsibleToggleStyleSheet();
QString activePlaybackButtonStyleSheet();
QString validationMessageLabelStyleSheet();

}  // namespace UiTheme
