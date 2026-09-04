#include "UiTheme.h"

#include "ThemeVariantResolver.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleHints>

// UiTheme 已缩减到仍被真实调用的面：QApplication 全局调色板/QSS、
// 原生窗口明暗以及旧 Widgets 外壳残留代码还在引用的样式函数。
// 对话框控件构建样式（dialog*/form* 系列）与时间轴样式均已删除。

namespace {

QString css(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

QString cssRgba(const QColor& color, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

QString applicationStyleSheet()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return QStringLiteral(
        "QMenuBar { background: %1; color: %2; border-bottom: none; }"
        "QMenuBar::item { background: transparent; color: %2; padding: 4px 8px; margin: 0 0 1px 0; }"
        "QMenuBar::item:selected { background: %4; }"
        "QToolBar { background: %1; border: none; border-bottom: 1px solid %3; spacing: 4px; }"
        "QStatusBar { background: %5; color: %6; border-top: 1px solid %3; }"
        "QStatusBar QLabel { color: %6; }"
        "QTabWidget::pane { border: 1px solid %3; background: %7; }"
        "QTabBar::tab { background: %8; color: %6; border: 1px solid %3; padding: 4px 10px; }"
        "QTabBar::tab:selected { background: %7; color: %2; }"
        "QDockWidget { color: %2; }"
        "QMainWindow::separator { background: %3; width: 1px; height: 1px; }"
        "QToolTip { background: %9; color: %2; border: 1px solid %10; }"
    )
        .arg(css(c.toolbarBg))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.menuHoverBg))
        .arg(css(c.statusBg))
        .arg(css(c.textSecondary))
        .arg(css(c.cardBg))
        .arg(css(c.panelBg))
        .arg(css(c.menuBg))
        .arg(css(c.menuBorder));
}

const UiTheme::Colors& lightColors()
{
    static const UiTheme::Colors theme{
        false,                              // dark
        QColor("#F8FAFD"),                  // windowBg
        QColor("#F5F7FA"),                  // windowAltBg
        QColor("#F7F9FC"),                  // toolbarBg
        QColor("#F7F9FC"),                  // statusBg
        QColor("#F5F7FA"),                  // panelBg
        QColor("#FFFFFF"),                  // cardBg
        QColor("#EDF2F8"),                  // cardAltBg
        QColor("#FFFFFF"),                  // inputBg
        QColor("#F2F5F9"),                  // inputDisabledBg
        QColor("#000000"),                  // canvasBg
        QColor("#203040"),                  // textPrimary
        QColor("#5F6B7A"),                  // textSecondary
        QColor("#7A8796"),                  // textMuted
        QColor("#FFFFFF"),                  // textInverse
        QColor("#D5E0EC"),                  // border
        QColor("#CCD6E2"),                  // borderSoft
        QColor("#B8C7DA"),                  // borderStrong
        QColor("#2E77D0"),                  // accent
        QColor("#3A86E8"),                  // accentHover
        QColor("#2668B9"),                  // accentPressed
        QColor("#FFFFFF"),                  // accentText
        QColor("#B8CCE5"),                  // selection
        QColor("#1F1F1F"),                  // selectionText
        QColor("#FFFFFF"),                  // menuBg
        QColor("#D7E0EB"),                  // menuBorder
        QColor("#EEF5FF"),                  // menuHoverBg
        QColor("#9AA5B4"),                  // menuDisabledText
        QColor("#F4F7FB"),                  // scrollTrack
        QColor("#9CB5CE"),                  // scrollHandle
        QColor("#81A2C3"),                  // scrollHandleHover
        QColor("#2B3C4E"),                  // iconPrimary
        QColor("#5D6E83"),                  // iconSecondary
    };
    return theme;
}

const UiTheme::Colors& darkColors()
{
    static const UiTheme::Colors theme{
        true,                               // dark
        QColor("#151A20"),                  // windowBg
        QColor("#1B2129"),                  // windowAltBg
        QColor("#171C23"),                  // toolbarBg
        QColor("#171C23"),                  // statusBg
        QColor("#1B2129"),                  // panelBg
        QColor("#232B35"),                  // cardBg
        QColor("#1F2630"),                  // cardAltBg
        QColor("#171D24"),                  // inputBg
        QColor("#202833"),                  // inputDisabledBg
        QColor("#050607"),                  // canvasBg
        QColor("#E6EEF8"),                  // textPrimary
        QColor("#A9B6C6"),                  // textSecondary
        QColor("#7B8798"),                  // textMuted
        QColor("#0F141A"),                  // textInverse
        QColor("#384656"),                  // border
        QColor("#455466"),                  // borderSoft
        QColor("#546679"),                  // borderStrong
        QColor("#4F8FEC"),                  // accent
        QColor("#67A1F1"),                  // accentHover
        QColor("#3E79D0"),                  // accentPressed
        QColor("#F7FBFF"),                  // accentText
        QColor("#315D9E"),                  // selection
        QColor("#F7FBFF"),                  // selectionText
        QColor("#1F2630"),                  // menuBg
        QColor("#455466"),                  // menuBorder
        QColor("#2C3846"),                  // menuHoverBg
        QColor("#748091"),                  // menuDisabledText
        QColor("#1C232C"),                  // scrollTrack
        QColor("#5A6A7B"),                  // scrollHandle
        QColor("#70849A"),                  // scrollHandleHover
        QColor("#D8E2EE"),                  // iconPrimary
        QColor("#95A4B7"),                  // iconSecondary
    };
    return theme;
}

}  // namespace

namespace UiTheme {

ResolvedTheme resolvedTheme()
{
    return miacode::ui::ThemeVariantResolver::resolve(UiText::preferredTheme())
        == miacode::ui::ThemeVariant::Dark
        ? ResolvedTheme::Dark
        : ResolvedTheme::Light;
}

bool isDarkTheme()
{
    return resolvedTheme() == ResolvedTheme::Dark;
}

const Colors& colors()
{
    return isDarkTheme() ? darkColors() : lightColors();
}

QPalette applicationPalette()
{
    const Colors& c = colors();
    QPalette palette;
    palette.setColor(QPalette::Window, c.windowBg);
    palette.setColor(QPalette::WindowText, c.textPrimary);
    palette.setColor(QPalette::Base, c.inputBg);
    palette.setColor(QPalette::AlternateBase, c.cardAltBg);
    palette.setColor(QPalette::ToolTipBase, c.menuBg);
    palette.setColor(QPalette::ToolTipText, c.textPrimary);
    palette.setColor(QPalette::Text, c.textPrimary);
    palette.setColor(QPalette::Button, c.cardBg);
    palette.setColor(QPalette::ButtonText, c.textPrimary);
    palette.setColor(QPalette::BrightText, c.accentText);
    palette.setColor(QPalette::Highlight, c.selection);
    palette.setColor(QPalette::HighlightedText, c.selectionText);
    palette.setColor(QPalette::PlaceholderText, c.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::Base, c.inputDisabledBg);
    return palette;
}

void applyApplicationTheme(QApplication& app)
{
    app.setPalette(applicationPalette());
    app.setStyleSheet(applicationStyleSheet());
}

QString scrollBarStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QScrollBar:vertical { background: %1; width: 12px; margin: 2px; border: 1px solid %2; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background: %3; min-height: 36px; border-radius: 5px; }"
        "QScrollBar::handle:vertical:hover { background: %4; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: transparent; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: %1; height: 12px; margin: 2px; border: 1px solid %2; border-radius: 6px; }"
        "QScrollBar::handle:horizontal { background: %3; min-width: 36px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal:hover { background: %4; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; background: transparent; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"
    )
        .arg(css(c.scrollTrack))
        .arg(css(c.borderSoft))
        .arg(css(c.scrollHandle))
        .arg(css(c.scrollHandleHover));
}

void styleRoundedMenu(QMenu& menu)
{
    const Colors& c = colors();
    if (!menu.property("miacode.refresh_theme_on_show").toBool()) {
        menu.setProperty("miacode.refresh_theme_on_show", true);
        QObject::connect(&menu, &QMenu::aboutToShow, &menu, [&menu]() {
            UiTheme::styleRoundedMenu(menu);
        });
    }
    menu.setPalette(applicationPalette());
    menu.setWindowFlag(Qt::FramelessWindowHint, true);
    menu.setWindowFlag(Qt::NoDropShadowWindowHint, true);
    menu.setAttribute(Qt::WA_TranslucentBackground, true);
    menu.setStyleSheet(
        QStringLiteral(
            "QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 7px; }"
            "QMenu::item { padding: 6px 20px 6px 12px; margin: 1px 0; border-radius: 6px; color: %3; background: transparent; }"
            "QMenu::item:selected { background: %4; color: %3; }"
            "QMenu::item:disabled { color: %5; background: transparent; }"
            "QMenu::separator { height: 1px; margin: 7px 6px; background: %6; }"
        )
            .arg(cssRgba(c.menuBg, c.dark ? 246 : 245))
            .arg(css(c.menuBorder))
            .arg(css(c.textPrimary))
            .arg(css(c.menuHoverBg))
            .arg(css(c.menuDisabledText))
            .arg(css(c.dark ? c.borderStrong : c.border))
    );
}

QIcon menuSelectionCheckIcon(bool visible)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    if (visible) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(colors().accent, 1.8);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawLine(QPointF(5.0, 7.4), QPointF(7.7, 10.1));
        painter.drawLine(QPointF(7.7, 10.1), QPointF(12.4, 4.4));
    }
    return QIcon(pixmap);
}

QString editorShellStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QWidget#EditorShell { background: %1; }"
        "QFrame#EditorHeader { background: %2; border-bottom: 1px solid %3; }"
        "QLabel#EditorContext { color: %4; font-weight: 700; }"
        "QLabel#EditorMeta { color: %5; }"
        "QWidget#EditorDifficultyControls QLabel { color: %5; }"
        "QWidget#EditorDifficultyControls QLineEdit { background: %6; color: %4; border: 1px solid %7; border-radius: 6px; padding: 4px 6px; selection-background-color: %8; selection-color: %9; }"
        "QWidget#EditorDifficultyControls QLineEdit:focus { border-color: %10; }"
    )
        .arg(css(c.panelBg))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        .arg(css(c.textSecondary))
        .arg(css(c.inputBg))
        .arg(css(c.borderSoft))
        .arg(css(c.selection))
        .arg(css(c.selectionText))
        .arg(css(c.accent));
}

QString previewPanelStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QWidget#PreviewPanel { background: %1; border-left: 1px solid %2; }"
        "QFrame#PreviewCanvasFrame { background: %3; border: 1px solid %4; }"
        "QFrame#PreviewControlCard, QFrame#PreviewStatsCard { background: %5; border: 1px solid %2; border-radius: 10px; }"
        "QFrame#PreviewControls, QFrame#PreviewStats { background: transparent; border: none; }"
        "QLabel#PreviewStatChip { color: %6; background: %7; border: 1px solid %8; border-radius: 9px; padding: 2px 8px; font-weight: 600; }"
        "QLabel#PreviewStatChipTotal { color: %6; background: %5; border: 1px solid %2; border-radius: 9px; padding: 2px 8px; font-weight: 700; }"
        "QToolButton#PreviewControlButton { color: %6; padding: 5px 8px; min-height: 28px; border: 1px solid %2; border-radius: 6px; background: transparent; font-weight: 600; }"
        "QToolButton#PreviewControlButton:hover { background: %9; border-color: %10; }"
        "QToolButton#PreviewControlButton:pressed { background: %11; color: %12; }"
        "QSlider::groove:horizontal { height: 6px; background: %2; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: %10; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -4px 0; border-radius: 6px; background: %7; border: 1px solid %8; }"
    )
        .arg(css(c.panelBg))
        .arg(css(c.border))
        .arg(css(c.canvasBg))
        .arg(css(c.borderSoft))
        .arg(css(c.cardAltBg))
        .arg(css(c.textPrimary))
        .arg(css(c.cardBg))
        .arg(css(c.borderSoft))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText));
}

QString aboutDialogStyleSheet()
{
    const Colors& c = colors();
    const QColor versionBg = c.dark ? QColor("#21344A") : QColor("#EAF2FC");
    const QColor versionBorder = c.dark ? QColor("#47648A") : QColor("#C7DBF5");
    return QStringLiteral(
        "QDialog { background: %1; }"
        "QFrame#AboutCard { background: %2; border: 1px solid %3; border-radius: 10px; }"
        "QLabel#AboutIcon { background: %4; border: 1px solid %3; border-radius: 10px; padding: 6px; }"
        "QLabel#AboutTitle { color: %5; font-size: 26px; font-weight: 700; }"
        "QLabel#AboutVersion { color: %6; background: %7; border: 1px solid %8; border-radius: 10px; padding: 2px 8px; }"
        "QLabel#AboutKey { color: %9; }"
        "QLabel#AboutValue { color: %5; font-weight: 600; }"
        "QPushButton { min-width: 92px; min-height: 30px; border: 1px solid %3; border-radius: 6px; background: %2; color: %5; }"
        "QPushButton:hover { background: %10; border-color: %6; }"
        "QPushButton:pressed, QPushButton:checked { background: %11; border-color: %6; color: %12; }"
    )
        .arg(css(c.windowBg))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.cardAltBg))
        .arg(css(c.textPrimary))
        .arg(css(c.accent))
        .arg(css(versionBg))
        .arg(css(versionBorder))
        .arg(css(c.textSecondary))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText));
}

}  // namespace UiTheme
