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
};

ResolvedTheme resolvedTheme();
bool isDarkTheme();
const Colors& colors();
QPalette applicationPalette();
void applyApplicationTheme(QApplication& app);

// Canonical checked-state indicator for QAction-backed menus. Pass visible=false
// to preserve the icon slot without painting the checkmark.
QIcon menuSelectionCheckIcon(bool visible = true);
QString scrollBarStyleSheet();
void styleRoundedMenu(QMenu& menu);
QString editorShellStyleSheet();
QString previewPanelStyleSheet();
QString aboutDialogStyleSheet();

}  // namespace UiTheme
