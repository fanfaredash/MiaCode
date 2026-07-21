#include "UiTheme.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QAbstractItemModel>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QToolButton>
#include <QWidgetAction>
#include <QStyleHints>

#include "app/ui/AppBackgroundPainter.h"

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

QString cssSurface(const QColor& color, int alpha)
{
    return miacode::ui::appBackgroundIsActiveForTheme()
        ? cssRgba(color, alpha)
        : css(color);
}

QString cssSurfaceWhenBackgroundActive(const QColor& activeColor, const QColor& inactiveColor, int alpha)
{
    return miacode::ui::appBackgroundIsActiveForTheme()
        ? cssRgba(activeColor, alpha)
        : css(inactiveColor);
}

QString cssWhenBackgroundDisabled(const QColor& color)
{
    return miacode::ui::appBackgroundIsActiveForTheme()
        ? QStringLiteral("transparent")
        : css(color);
}

int backgroundOverlayAlphaProperty(const char* propertyName, int fallback)
{
    if (qApp == nullptr) {
        return fallback;
    }
    return qBound(0, qApp->property(propertyName).toInt(), 255);
}

void repolish(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// The popup proxy style must NOT be constructed with the default (null) base:
// QProxyStyle then wraps the *desktop* style (QWindows11Style on Windows 11),
// not the Fusion style the app runs with. QWindows11Style::polish makes the
// QComboBoxPrivateContainer a translucent frameless window and paints its own
// WinUI3 rounded panel (radius 4, system-scheme colors) underneath the QSS
// radius-8 panel; the two disagree by 1px anchored at the top-left, leaving a
// solid wedge outside the rounded curve at the bottom-right corner.
class ComboBoxPopupLimitStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    QRect subControlRect(
        ComplexControl control,
        const QStyleOptionComplex* option,
        SubControl subControl,
        const QWidget* widget = nullptr) const override
    {
        QRect rect = QProxyStyle::subControlRect(control, option, subControl, widget);
        if (control == QStyle::CC_ComboBox
            && subControl == QStyle::SC_ComboBoxEditField
            && qobject_cast<const QComboBox*>(widget) != nullptr) {
            rect.adjust(10, 0, -2, 0);
        }
        return rect;
    }

    QRect subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const override
    {
        QRect rect = QProxyStyle::subElementRect(element, option, widget);
        if (element == QStyle::SE_ComboBoxLayoutItem && qobject_cast<const QComboBox*>(widget) != nullptr) {
            rect.adjust(10, 0, -2, 0);
        }
        return rect;
    }

    int styleHint(
        StyleHint hint,
        const QStyleOption* option = nullptr,
        const QWidget* widget = nullptr,
        QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == QStyle::SH_ComboBox_Popup) {
            return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

// Paints the rounded dropdown panel (menu background + border) directly on a
// translucent popup window, beneath the item view/content and scrollbar. This
// must be the ONE painter of the panel shape: the popup contents keep their
// background/border transparent, and the scrollbar track stays transparent so
// this panel shows through instead of leaving a see-through hole in the layered
// window. Colors are read at paint time, so theme switches apply without
// rewiring.
class DialogDropdownPopupPanelPainter final : public QObject {
public:
    explicit DialogDropdownPopupPanelPainter(QWidget* container)
        : QObject(container), container_(container)
    {
        container_->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == container_ && event->type() == QEvent::Paint) {
            const UiTheme::Colors& c = UiTheme::colors();
            QPainter painter(container_);
            painter.setRenderHint(QPainter::Antialiasing, true);
            const QRectF frame = QRectF(container_->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
            painter.setPen(QPen(c.menuBorder, 1.0));
            painter.setBrush(c.menuBg);
            // 7.5 inner radius ~= the QSS border-radius of 8 measured on the
            // outer edge of the 1px border.
            painter.drawRoundedRect(frame, 7.5, 7.5);
        }
        return false;
    }

private:
    QWidget* container_;
};

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
        QColor("#F5F5F5"),                  // timelineWindow
        QColor("#F3F5F8"),                  // timelineHeader
        QColor("#E8E8E8"),                  // timelineSidebar
        QColor("#F7F8FA"),                  // timelineBase
        QColor("#C7D2DF"),                  // timelineBorder
        QColor("#5A86D8"),                  // timelineGridMajor
        QColor(122, 154, 204, 165),         // timelineGridSubdivision
        QColor(174, 188, 204, 110),         // timelineGridMinor
        QColor(251, 251, 251, 190),         // timelineLaneEven
        QColor(242, 242, 242, 190),         // timelineLaneOdd
        QColor("#4D5C6D"),                  // timelineLabel
        QColor("#9AA7B6"),                  // timelineAxis
        // Waveform renders as a translucent filled silhouette ABOVE the grid.
        // Deep teal-green; the blue grid lines show through its alpha. Distinct
        // from the warm playhead/cursor markers. Alpha 135 is the base for the
        // 1.0x brightness setting — the brightness slider scales this alpha
        // (opacity), keeping the hue fixed (see adjustedTimelineWaveformColor).
        QColor(44, 156, 130, 135),          // timelineWaveStroke
        QColor("#FFC90E"),                  // timelinePlayhead
        QColor("#B85C4D"),                  // timelineCursor
        QColor("#F28C28"),                  // timelineLimit
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
        QColor("#1A2027"),                  // timelineWindow
        QColor("#1D232B"),                  // timelineHeader
        QColor("#171D24"),                  // timelineSidebar
        QColor("#202833"),                  // timelineBase
        QColor("#4A5C70"),                  // timelineBorder
        QColor("#7FA5D8"),                  // timelineGridMajor
        QColor(120, 152, 198, 140),         // timelineGridSubdivision
        QColor(95, 120, 155, 80),           // timelineGridMinor
        QColor(34, 42, 52, 210),            // timelineLaneEven
        QColor(29, 36, 45, 210),            // timelineLaneOdd
        QColor("#C8D5E5"),                  // timelineLabel
        QColor("#8091A5"),                  // timelineAxis
        // Waveform renders as a translucent filled silhouette ABOVE the grid.
        // Aquamarine-mint family (matches the light theme's waveform tint); the
        // blue grid lines show through its alpha. Distinct from the warm
        // playhead/cursor markers. Alpha 160 is the base for the 1.0x
        // brightness setting — the brightness slider scales this alpha
        // (opacity), keeping the hue fixed (see adjustedTimelineWaveformColor).
        QColor(95, 214, 180, 160),          // timelineWaveStroke
        QColor("#FFC90E"),                  // timelinePlayhead
        QColor("#C96B5B"),                  // timelineCursor
        QColor("#FF9B4A"),                  // timelineLimit
    };
    return theme;
}

}  // namespace

namespace UiTheme {

ResolvedTheme resolvedTheme()
{
    switch (UiText::preferredTheme()) {
    case UiText::ThemePreference::Light:
        return ResolvedTheme::Light;
    case UiText::ThemePreference::Dark:
        return ResolvedTheme::Dark;
    case UiText::ThemePreference::System:
    default:
        break;
    }

    if (QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()); app != nullptr) {
        if (QStyleHints* hints = app->styleHints(); hints != nullptr) {
            if (hints->colorScheme() == Qt::ColorScheme::Dark) {
                return ResolvedTheme::Dark;
            }
            if (hints->colorScheme() == Qt::ColorScheme::Light) {
                return ResolvedTheme::Light;
            }
        }
        const QColor window = app->palette().color(QPalette::Window);
        if (window.isValid() && window.lightness() < 128) {
            return ResolvedTheme::Dark;
        }
    }

    return ResolvedTheme::Light;
}

bool isDarkTheme()
{
    return resolvedTheme() == ResolvedTheme::Dark;
}

const Colors& colors()
{
    return isDarkTheme() ? darkColors() : lightColors();
}

int appBackgroundOverlayAlpha(AppBackgroundOverlayRole role, bool darkTheme)
{
    switch (role) {
    case AppBackgroundOverlayRole::Toolbar:
        return backgroundOverlayAlphaProperty(
            darkTheme ? "miacode.appBackgroundToolbarAlphaDark" : "miacode.appBackgroundToolbarAlphaLight",
            darkTheme ? 198 : 206);
    case AppBackgroundOverlayRole::StatusBar:
        return backgroundOverlayAlphaProperty(
            darkTheme ? "miacode.appBackgroundStatusAlphaDark" : "miacode.appBackgroundStatusAlphaLight",
            darkTheme ? 208 : 216);
    case AppBackgroundOverlayRole::Panel:
        return backgroundOverlayAlphaProperty(
            darkTheme ? "miacode.appBackgroundPanelAlphaDark" : "miacode.appBackgroundPanelAlphaLight",
            darkTheme ? 176 : 190);
    case AppBackgroundOverlayRole::Card:
        return backgroundOverlayAlphaProperty(
            darkTheme ? "miacode.appBackgroundCardAlphaDark" : "miacode.appBackgroundCardAlphaLight",
            darkTheme ? 184 : 196);
    case AppBackgroundOverlayRole::EditorHeader:
        return backgroundOverlayAlphaProperty(
            darkTheme ? "miacode.appBackgroundEditorHeaderAlphaDark" : "miacode.appBackgroundEditorHeaderAlphaLight",
            darkTheme ? 188 : 196);
    case AppBackgroundOverlayRole::Input:
        return backgroundOverlayAlphaProperty(
            darkTheme ? "miacode.appBackgroundInputAlphaDark" : "miacode.appBackgroundInputAlphaLight",
            darkTheme ? 196 : 204);
    case AppBackgroundOverlayRole::CodeEditor:
        return backgroundOverlayAlphaProperty(
            darkTheme ? "miacode.appBackgroundCodeEditorAlphaDark" : "miacode.appBackgroundCodeEditorAlphaLight",
            darkTheme ? 196 : 204);
    }
    return darkTheme ? 196 : 204;
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

QString applicationStyleSheet()
{
    const Colors& c = colors();
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
        .arg(cssSurface(c.toolbarBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Toolbar, c.dark)))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.menuHoverBg))
        .arg(cssSurface(c.statusBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::StatusBar, c.dark)))
        .arg(css(c.textSecondary))
        .arg(cssSurface(c.cardBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Card, c.dark)))
        .arg(cssSurface(c.panelBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Panel, c.dark)))
        .arg(css(c.menuBg))
        .arg(css(c.menuBorder));
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

QPalette timelineViewportPalette()
{
    const Colors& c = colors();
    QPalette palette;
    palette.setColor(QPalette::Window, c.timelineWindow);
    palette.setColor(QPalette::Base, c.timelineBase);
    palette.setColor(QPalette::Highlight, c.selection);
    palette.setColor(QPalette::Text, c.textPrimary);
    palette.setColor(QPalette::WindowText, c.textPrimary);
    return palette;
}

QString timelineZoomButtonStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QToolButton { color: %1; background: %2; border: 1px solid %3; border-radius: 6px; padding: 1px 8px; font-weight: 600; }"
        "QToolButton:hover { background: %4; border-color: %5; }"
        "QToolButton:pressed { background: %6; color: %7; }"
    )
        .arg(css(c.textPrimary))
        .arg(cssSurface(c.cardBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Card, c.dark)))
        .arg(css(c.borderStrong))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText));
}

QIcon timelineZoomButtonIcon(const QColor& strokeColor, const QString& sign)
{
    QPixmap iconPixmap(18, 18);
    iconPixmap.fill(Qt::transparent);
    QPainter painter(&iconPixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(strokeColor, 1.8));
    painter.drawEllipse(QRectF(2.5, 2.5, 9.0, 9.0));
    painter.drawLine(QPointF(10.5, 10.5), QPointF(15.2, 15.2));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(7);
    painter.setFont(font);
    painter.drawText(QRectF(11.5, 0.0, 6.5, 9.0), Qt::AlignCenter, sign);
    painter.end();
    return QIcon(iconPixmap);
}

QString timelineZoomButtonText(double scale)
{
    return QStringLiteral("%1%").arg(qRound(scale * 100.0));
}

QString timelineCheckBoxStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral("QCheckBox { color: %1; spacing: 6px; font-weight: 600; }")
        .arg(css(c.textPrimary));
}

QString editorTextEditStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "border: none;"
        "background: %1;"
        "color: %2;"
        "selection-background-color: %3;"
        "selection-color: %4;"
    )
        .arg(cssSurface(c.inputBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Input, c.dark)))
        .arg(css(c.textPrimary))
        .arg(css(c.selection))
        .arg(css(c.selectionText));
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
        .arg(cssSurface(c.panelBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Panel, c.dark)))
        .arg(cssSurfaceWhenBackgroundActive(
            c.toolbarBg,
            c.cardBg,
            appBackgroundOverlayAlpha(AppBackgroundOverlayRole::EditorHeader, c.dark)))
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        .arg(css(c.textSecondary))
        .arg(cssSurface(c.inputBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Input, c.dark)))
        .arg(css(c.borderSoft))
        .arg(css(c.selection))
        .arg(css(c.selectionText))
        .arg(css(c.accent));
}

QString editorFindBarStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QFrame#EditorFindBar { background: %1; border: 1px solid %2; border-radius: 10px; }"
        "QFrame#EditorFindBar QLineEdit { background: %3; border: 1px solid %4; border-radius: 6px; min-height: 22px; padding: 1px 6px; selection-background-color: %5; selection-color: %6; color: %7; }"
        "QFrame#EditorFindBar QLineEdit:focus { border-color: %8; }"
        "QFrame#EditorFindBar QToolButton, QFrame#EditorFindBar QPushButton { color: %7; min-height: 22px; padding: 0 6px; border: 1px solid %2; border-radius: 6px; background: %3; font-weight: 400; }"
        "QFrame#EditorFindBar QToolButton:hover, QFrame#EditorFindBar QPushButton:hover { background: %9; border-color: %8; }"
        "QFrame#EditorFindBar QToolButton:pressed, QFrame#EditorFindBar QPushButton:pressed { background: %10; color: %11; }"
        "QFrame#EditorFindBar QToolButton#EditorFindPrevButton, QFrame#EditorFindBar QToolButton#EditorFindNextButton { min-width: 24px; padding: 0; font-size: 12px; }"
        "QFrame#EditorFindBar QToolButton#EditorFindCloseButton { min-width: 28px; padding: 0; font-size: 15px; font-weight: 400; }"
    )
        .arg(cssRgba(c.windowBg, c.dark ? 242 : 248))
        .arg(css(c.border))
        .arg(css(c.inputBg))
        .arg(css(c.borderSoft))
        .arg(css(c.selection))
        .arg(css(c.selectionText))
        .arg(css(c.textPrimary))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText));
}

QString metadataPageStyleSheet()
{
    const Colors& c = colors();
    // The page canvas *outside* the metadata card uses the darker window
    // background so the card reads as a distinct surface — matching the
    // BPM & Latency page. The broad `QWidget` rule keeps inner widgets on the
    // card background; the higher-specificity `#MetadataPage` id rule only
    // repaints the page itself.
    return QStringLiteral("QWidget#MetadataPage { background: %1; }").arg(css(c.windowBg))
        + QStringLiteral(
        "QWidget { background: %1; color: %2; }"
        "QFrame#MetadataCard { background: %1; border: 1px solid %3; border-radius: 8px; }"
        "QLabel#SectionTitle { color: %2; font-weight: 700; padding-left: 4px; }"
        "QLabel#MetadataFieldLabel { color: %2; background: transparent; padding-left: 8px; }"
        "QLineEdit, QTextEdit, QPlainTextEdit { background: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 6px 8px; selection-background-color: %6; selection-color: %7; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: %8; }"
        "QToolButton, QPushButton { color: %2; border: 1px solid %3; border-radius: 6px; background: %4; padding: 4px 8px; }"
        "QToolButton:hover, QPushButton:hover { background: %9; border-color: %8; }"
    )
        .arg(cssSurface(c.cardBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Card, c.dark)))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.inputBg))
        .arg(css(c.borderSoft))
        .arg(css(c.selection))
        .arg(css(c.selectionText))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg));
}

QString latencyDetectionPageStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QFrame#LatencyCard {"
        " background: %1;"
        " border: 1px solid %2;"
        " border-radius: 6px;"
        "}"
        "QLabel[role=\"cardTitle\"] {"
        " color: %3;"
        " font-weight: 600;"
        "}"
        "QLabel[role=\"cardHint\"] {"
        " color: %4;"
        "}"
        "QLabel[role=\"detectResult\"] {"
        " color: %5;"
        "}"
        "QPushButton#LatencyBackButton {"
        " color: %4;"
        " background: transparent;"
        " border: none;"
        " padding: 4px 6px;"
        " text-align: left;"
        "}"
        "QPushButton#LatencyBackButton:hover {"
        " color: %5;"
        "}"
        "QPushButton#LatencyMediaToolsButton {"
        " color: %3;"
        " background: %1;"
        " border: 1px solid %2;"
        " border-radius: 13px;"
        " padding: 5px 14px 5px 11px;"
        "}"
        "QPushButton#LatencyMediaToolsButton:hover {"
        " border-color: %5;"
        " color: %5;"
        "}"
    )
        .arg(cssSurface(c.cardBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Card, c.dark)))
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        .arg(css(c.textMuted))
        .arg(css(c.accent));
}

QString exportLauncherPageStyleSheet()
{
    // 2026-06-12 redesign: flat header — difficulty badges stay pill chips,
    // the sub-nav is underline-indicator text (no card frames, no hint text).
    const Colors& c = colors();
    const QColor badgeCheckedBg = c.dark ? QColor("#263344") : QColor("#EDF4FF");
    return QStringLiteral(
        // Paint the page's own canvas so it never reveals a stale/light ancestor
        // on a theme switch. The page is otherwise transparent (flat redesign),
        // which left the header/sub-nav band showing through to whatever painted
        // behind it. Mirrors the metadata page's `#MetadataPage { background }`.
        "QWidget#ExportLauncherPage { background: %7; }"
        "QLabel[role=\"disabledReason\"] {"
        " color: %4;"
        "}"
        "QToolButton[role=\"difficultyBadge\"] {"
        " color: %2;"
        " background: transparent;"
        " border: 1px solid %1;"
        " border-radius: 12px;"
        " padding: 3px 11px;"
        "}"
        "QToolButton[role=\"difficultyBadge\"]:hover {"
        " background: %5;"
        "}"
        "QToolButton[role=\"difficultyBadge\"]:checked {"
        " background: %6;"
        " border-color: %4;"
        "}"
        "QToolButton[role=\"subNavTab\"] {"
        " color: %3;"
        " background: transparent;"
        " border: none;"
        " border-bottom: 2px solid transparent;"
        " padding: 2px 2px 7px 2px;"
        "}"
        "QToolButton[role=\"subNavTab\"]:hover {"
        " color: %2;"
        "}"
        "QToolButton[role=\"subNavTab\"]:checked {"
        " color: %2;"
        " border-bottom: 2px solid %4;"
        "}"
        "QWidget#ExportHeaderRule {"
        " background: %1;"
        "}"
        // 封面/批量/打包ZIP pane action buttons (role set in makePane). Themed
        // here so they follow light/dark switches via the page stylesheet.
        "QPushButton[role=\"paneAction\"] {"
        " color: %2;"
        " background: transparent;"
        " border: 1px solid %1;"
        " border-radius: 8px;"
        " padding: 6px 16px;"
        "}"
        "QPushButton[role=\"paneAction\"]:hover {"
        " border-color: %4;"
        " color: %4;"
        "}"
        "QPushButton[role=\"paneAction\"]:pressed {"
        " background: %5;"
        "}"
        "QPushButton[role=\"paneAction\"]:disabled {"
        " color: %3;"
        " border-color: %1;"
        "}"
    )
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        .arg(css(c.textMuted))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg))
        .arg(css(badgeCheckedBg))
        .arg(css(c.windowBg));
}

QString embeddedExportTabStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QTabWidget#VideoExportTabs::pane {"
        " border: none;"
        " background: transparent;"
        "}"
        "QTabWidget#VideoExportTabs QTabBar {"
        " background: transparent;"
        "}"
        "QTabWidget#VideoExportTabs QTabBar::tab {"
        " background: transparent;"
        " border: none;"
        " border-bottom: 2px solid transparent;"
        " padding: 4px 2px 7px 2px;"
        " margin-right: 14px;"
        " color: %1;"
        " font-weight: 600;"
        "}"
        "QTabWidget#VideoExportTabs QTabBar::tab:hover:!selected {"
        " color: %2;"
        "}"
        "QTabWidget#VideoExportTabs QTabBar::tab:selected {"
        " color: %2;"
        " border-bottom: 2px solid %3;"
        "}"
        "QScrollArea#EmbeddedExportTabScroll {"
        " background: transparent;"
        " border: none;"
        "}"
    )
        .arg(css(c.textMuted))
        .arg(css(c.textPrimary))
        .arg(css(c.accent));
}


QString metadataEmptyHintLabelStyleSheet()
{
    return QStringLiteral("color: %1; background: transparent; padding-left: 6px;").arg(css(colors().textMuted));
}

QString outlineListStyleSheet()
{
    const Colors& c = colors();
    const QColor selectedBg = c.dark ? QColor("#263344") : QColor("#EDF4FF");
    return QStringLiteral(
        "QListWidget { background: %1; color: %2; border: 1px solid %3; padding: 6px; outline: none; }"
        "QListWidget::item { min-height: 28px; padding: 4px 12px; border: 1px solid transparent; border-radius: 6px; }"
        "QListWidget::item:selected { color: %2; background: %4; border-color: %5; }"
        "QListWidget::item:hover { background: %6; }"
    )
        .arg(css(c.cardBg))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(selectedBg))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg));
}

QString batchExportChartDirectoryListStyleSheet()
{
    const Colors& c = colors();
    const QColor selectedBg = c.dark ? QColor("#263344") : QColor("#EDF4FF");
    return QStringLiteral(
        "QListWidget#BatchExportChartDirectoryList { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px; outline: none; }"
        "QListWidget#BatchExportChartDirectoryList::item { min-height: 28px; margin: 1px 0; padding: 4px 10px; border: 1px solid transparent; border-radius: 5px; }"
        "QListWidget#BatchExportChartDirectoryList::item:selected { color: %2; background: %4; border-color: %5; }"
        "QListWidget#BatchExportChartDirectoryList::item:!selected:hover { background: %6; border-color: %3; }"
    )
        .arg(css(c.cardBg))
        .arg(css(c.textPrimary))
        .arg(css(c.borderSoft))
        .arg(css(selectedBg))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg));
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
        .arg(cssSurface(c.panelBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Panel, c.dark)))
        .arg(css(c.border))
        .arg(css(c.canvasBg))
        .arg(css(c.borderSoft))
        .arg(cssSurface(c.cardAltBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Card, c.dark)))
        .arg(css(c.textPrimary))
        .arg(cssSurface(c.cardBg, appBackgroundOverlayAlpha(AppBackgroundOverlayRole::Card, c.dark)))
        .arg(css(c.borderSoft))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText));
}

QString compactToolbarButtonStyleSheet()
{
    return QStringLiteral(
        "QToolButton:disabled { background: transparent; border: none; }"
        "QToolButton:disabled:hover { background: transparent; border: none; }"
    );
}

QString pausePreviewButtonStyleSheet(bool active)
{
    const Colors& c = colors();
    if (active) {
        return QStringLiteral(
            "QToolButton { color: %1; padding: 5px 8px; min-height: 28px; border: 1px solid %2; border-radius: 6px; background: %2; font-weight: 600; }"
            "QToolButton:hover { background: %3; }"
        )
            .arg(css(c.accentText))
            .arg(css(c.accent))
            .arg(css(c.accentHover));
    }
    return QStringLiteral(
        "QToolButton { color: %1; padding: 5px 8px; min-height: 28px; border: 1px solid %2; border-radius: 6px; background: transparent; font-weight: 600; }"
        "QToolButton:hover { background: %3; border-color: %4; }"
    )
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent));
}

QString formSliderStyleSheet()
{
    const Colors& c = colors();
    const QColor groove = c.border;
    const QColor handleBg = c.cardBg;
    const QColor handleBorder = c.borderSoft;
    return QStringLiteral(
        "QSlider::groove:horizontal { height: 6px; background: %1; border-radius: 3px; }"
        "QSlider::add-page:horizontal { background: %1; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: %2; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 14px; margin: -4px 0; border-radius: 7px; background: %3; border: 1px solid %4; }"
        "QSlider::handle:horizontal:hover { border-color: %5; }"
        "QSlider::handle:horizontal:pressed { background: %6; border-color: %5; }"
    )
        .arg(css(groove))
        .arg(css(c.accent))
        .arg(css(handleBg))
        .arg(css(handleBorder))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg));
}

QString dialogSliderStyleSheet()
{
    const Colors& c = colors();
    if (c.dark) {
        return formSliderStyleSheet();
    }

    const QColor groove = c.selection;
    const QColor filled = c.accentHover;
    const QColor handleBg = c.cardBg;
    const QColor handleBorder = c.accent;
    const QColor handleHover = c.accentHover;
    const QColor handlePressedBg = c.menuHoverBg;
    const QColor handlePressedBorder = c.accentPressed;

    return QStringLiteral(
        "QSlider::groove:horizontal { height: 6px; background: %1; border-radius: 3px; }"
        "QSlider::add-page:horizontal { background: %1; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: %2; border-radius: 3px; }"
        "QSlider::handle:horizontal { width: 14px; margin: -4px 0; border-radius: 7px; background: %3; border: 1px solid %4; }"
        "QSlider::handle:horizontal:hover { border-color: %5; }"
        "QSlider::handle:horizontal:pressed { background: %6; border-color: %7; }"
    )
        .arg(css(groove))
        .arg(css(filled))
        .arg(css(handleBg))
        .arg(css(handleBorder))
        .arg(css(handleHover))
        .arg(css(handlePressedBg))
        .arg(css(handlePressedBorder));
}

void prepareDialogDropdownPopupWindow(QWidget* popupWindow)
{
    if (popupWindow == nullptr) {
        return;
    }
    if (popupWindow->property("miacode.dialog_dropdown_popup_panel").toBool()) {
        return;
    }

    const bool wasCreated = popupWindow->testAttribute(Qt::WA_WState_Created);
    popupWindow->setAttribute(Qt::WA_OpaquePaintEvent, false);
    popupWindow->setAttribute(Qt::WA_TranslucentBackground);
    popupWindow->setWindowFlag(Qt::FramelessWindowHint);
    popupWindow->setWindowFlag(Qt::NoDropShadowWindowHint);
    popupWindow->setAttribute(Qt::WA_WState_Created, wasCreated);
    QPalette popupPalette = popupWindow->palette();
    popupPalette.setColor(popupWindow->backgroundRole(), Qt::transparent);
    popupWindow->setPalette(popupPalette);
    new DialogDropdownPopupPanelPainter(popupWindow);
    popupWindow->setProperty("miacode.dialog_dropdown_popup_panel", true);
}

QString dialogDropdownItemButtonStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QToolButton {"
        " color: %1;"
        " background: transparent;"
        " border: 1px solid transparent;"
        " border-radius: 6px;"
        " min-height: 20px;"
        " padding: 3px 10px;"
        " text-align: left;"
        "}"
        "QToolButton:hover, QToolButton:pressed {"
        " background: %2;"
        " border: 1px solid transparent;"
        "}"
    )
        .arg(css(c.textPrimary))
        .arg(css(c.menuHoverBg));
}

QString dialogDropdownCheckBoxStyleSheet()
{
    const Colors& c = colors();
    const QColor indicatorBg = c.dark ? c.windowAltBg : QColor("#FFFFFF");
    return QStringLiteral(
        "QCheckBox {"
        " color: %1;"
        " background: transparent;"
        " border: 1px solid transparent;"
        " border-radius: 6px;"
        " min-height: 20px;"
        " padding: 3px 10px;"
        " spacing: 8px;"
        "}"
        "QCheckBox:hover {"
        " background: %2;"
        " border: 1px solid transparent;"
        "}"
        "QCheckBox::indicator {"
        " width: 14px;"
        " height: 14px;"
        " border: 1px solid %3;"
        " border-radius: 3px;"
        " background: %4;"
        "}"
        "QCheckBox::indicator:hover { border-color: %5; }"
        "QCheckBox::indicator:checked {"
        " background: %5;"
        " border-color: %5;"
        " image: url(\":/icons/checkmark.svg\");"
        "}"
        "QCheckBox::indicator:checked:hover {"
        " background: %6;"
        " border-color: %6;"
        "}"
    )
        .arg(css(c.textPrimary))
        .arg(css(c.menuHoverBg))
        .arg(css(c.borderStrong))
        .arg(css(indicatorBg))
        .arg(css(c.accent))
        .arg(css(c.accentHover));
}

QString dialogDropdownScrollBarStyleSheet(const QString& parentSelector)
{
    const Colors& c = colors();
    const QString prefix = parentSelector.trimmed().isEmpty()
        ? QStringLiteral("QScrollBar")
        : parentSelector.trimmed() + QStringLiteral(" QScrollBar");
    return QStringLiteral(
        "%1:vertical { background: transparent; width: 10px; margin: 8px 2px 8px 0; }"
        "%1::handle:vertical { background: %2; border-radius: 3px; min-height: 24px; }"
        "%1::handle:vertical:hover { background: %3; }"
        "%1::add-line:vertical, %1::sub-line:vertical { height: 0; border: none; background: transparent; }"
        "%1::add-page:vertical, %1::sub-page:vertical { background: transparent; }"
    )
        .arg(prefix)
        .arg(css(c.scrollHandle))
        .arg(css(c.scrollHandleHover));
}

void styleDialogDropdownMenu(QMenu& menu)
{
    if (!menu.property("miacode.refresh_dialog_dropdown_theme_on_show").toBool()) {
        menu.setProperty("miacode.refresh_dialog_dropdown_theme_on_show", true);
        QObject::connect(&menu, &QMenu::aboutToShow, &menu, [&menu]() {
            UiTheme::styleDialogDropdownMenu(menu);
        });
    }
    menu.setPalette(applicationPalette());
    prepareDialogDropdownPopupWindow(&menu);
    const Colors& c = colors();
    menu.setStyleSheet(
        QStringLiteral(
            "QMenu { background: transparent; border: none; padding: 6px; }"
            "QMenu::item { min-height: 20px; padding: 3px 10px; margin: 1px 0; border: 1px solid transparent; border-radius: 6px; color: %1; background: transparent; }"
            "QMenu::item:selected { background: %2; color: %1; border: 1px solid transparent; outline: 0; }"
            "QMenu::item:disabled { color: %3; background: transparent; }"
            "QMenu::separator { height: 1px; margin: 7px 6px; background: %4; }"
        )
            .arg(css(c.textPrimary))
            .arg(css(c.menuHoverBg))
            .arg(css(c.menuDisabledText))
            .arg(css(c.dark ? c.borderStrong : c.border))
    );
}

QString dialogComboBoxStyleSheet()
{
    return dialogComboBoxStyleSheet(Qt::AlignCenter);
}

QString dialogComboBoxStyleSheet(Qt::Alignment textAlignment)
{
    const Colors& c = colors();
    // The drop-down subcontrol reserves 20px on the right (the arrow itself is
    // suppressed by the ::down-arrow size rule — a deliberate flat look).
    // Centred combos therefore look left-shifted unless the LEFT inset matches
    // the right side's total (border + drop-down + right padding). Balancing
    // padding-left = drop-down(20) + padding-right(10) = 30 centres the text in
    // the FULL box (verified pixel-exact). Left-aligned combos keep a small
    // 10px left inset. The right padding stays 10 in both so the text never
    // collides with the reserved drop-down column.
    const bool centered = (textAlignment & Qt::AlignHCenter) != 0;
    const QString comboPadding = centered
        ? QStringLiteral("padding: 2px 10px 2px 30px;")
        : QStringLiteral("padding: 2px 24px 2px 10px;");
    // The popup panel (rounded menu background + border) is painted by the
    // shared DialogDropdownPopupPanelPainter on the translucent container; the view
    // and its scrollbar track stay transparent on purpose so the panel shows
    // through. Giving the view an opaque background here would re-open the
    // corner artifact: the scrollbar column is not covered by the view's
    // QSS background and would turn into a see-through hole.
    return QStringLiteral(
        // 2px vertical padding (not 3) keeps the styled sizeHint equal to the
        // dialog menu buttons' so grid rows pairing the two stay level.
        "QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; %8 min-height: 24px; }"
        "QComboBox:hover { border-color: %4; }"
        "QComboBox:focus { border-color: %4; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border: none; background: transparent; }"
        "QComboBox::down-arrow { width: 10px; height: 10px; }"
        "QComboBox QAbstractItemView { background: transparent; color: %2; border: 1px solid transparent; border-radius: 8px; padding: 6px; outline: 0; show-decoration-selected: 1; selection-background-color: transparent; selection-color: %6; }"
        "QComboBox QAbstractItemView::item { min-height: 20px; padding: 3px 10px; border: 1px solid transparent; border-radius: 6px; background: transparent; }"
        "QComboBox QAbstractItemView::item:selected { background: %5; color: %6; border: 1px solid transparent; outline: 0; }"
        "QComboBox QAbstractItemView::item:focus { border: 1px solid transparent; outline: 0; }"
        "QComboBox QAbstractItemView::item:hover { background: %5; border: 1px solid transparent; }"
        "%7"
    )
        .arg(css(c.inputBg))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg))
        .arg(css(c.textPrimary))
        .arg(dialogDropdownScrollBarStyleSheet(QStringLiteral("QComboBox QAbstractItemView")))
        .arg(comboPadding);
}

void styleDialogComboBox(QComboBox* combo, int maxVisibleItems)
{
    if (combo == nullptr) {
        return;
    }
    // Alignment is stamped on the combo as a property by
    // miacode::ui::createDialogComboBox; direct callers (cover / HUD-font
    // pickers) set it too. Absent → centred (the settings/export default).
    const QVariant alignmentProperty = combo->property("miacode.combo_text_alignment");
    const Qt::Alignment textAlignment = alignmentProperty.isValid()
        ? static_cast<Qt::Alignment>(alignmentProperty.toInt())
        : Qt::Alignment(Qt::AlignCenter);

    if (maxVisibleItems > 0) {
        applyComboBoxPopupLimit(combo, maxVisibleItems);
    }
    combo->setStyleSheet(dialogComboBoxStyleSheet(textAlignment));
    if (maxVisibleItems > 0) {
        applyComboBoxPopupLimit(combo, maxVisibleItems);
    }

    // QComboBox::initStyleOption reads Qt::TextAlignmentRole off the current
    // item for the closed label; the popup delegate honors it per row. Stamp
    // every item so the alignment survives repopulation (callers re-run this
    // after clear()/addItem()).
    for (int i = 0; i < combo->count(); ++i) {
        combo->setItemData(i, static_cast<int>(textAlignment), Qt::TextAlignmentRole);
    }

    // Fix the height from the FINAL populated sizeHint (same W1 formula as the
    // dialog menu buttons). Doing it here — not once at empty-combo creation —
    // avoids a min>max clip: addSettingsField later floors minimumHeight to
    // sizeHint()+4, which can exceed a height fixed while the combo was still
    // empty (fewer items / pre-parent font) and clip the bottom border.
    combo->ensurePolished();
    combo->setFixedHeight(qMax(combo->sizeHint().height(), 30) + 4);
}

QString dialogSpinBoxStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QWidget#DialogSpinBoxFrame { background: %1; border: 1px solid %3; border-radius: 8px; }"
        "QSpinBox#DialogSpinBoxEditor { background: transparent; color: %2; border: none;"
        " padding: 0 0 0 10px; min-height: 23px; font-weight: 500; }"
        "QToolButton#DialogSpinBoxStepButton { border: none; border-radius: 0; background: transparent; padding: 0;"
        " min-width: 20px; max-width: 20px; min-height: 17px; max-height: 17px; }"
        "QToolButton#DialogSpinBoxStepButton[miacodeSpinStep=\"up\"] { border-top-right-radius: 7px; }"
        "QToolButton#DialogSpinBoxStepButton[miacodeSpinStep=\"down\"] { border-bottom-right-radius: 7px; }"
        "QToolButton#DialogSpinBoxStepButton:hover { background: %4; }"
        "QToolButton#DialogSpinBoxStepButton:pressed { background: %5; }"
        "QWidget#DialogSpinBoxFrame:disabled { background: %6; border-color: %3; }"
        "QSpinBox#DialogSpinBoxEditor:disabled { color: %7; }"
    )
        .arg(css(c.inputBg))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.menuHoverBg))
        .arg(css(c.cardAltBg))
        .arg(css(c.inputDisabledBg))
        .arg(css(c.textMuted));
}

QString dialogMenuButtonStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QToolButton { min-height: 24px; padding: 2px 10px; border: 1px solid %1; border-radius: 8px; background: %2; color: %3; font-weight: 500; text-align: left; }"
        "QToolButton:hover { background: %4; border-color: %5; }"
        "QToolButton:pressed, QToolButton:checked { background: %6; border-color: %5; color: %7; }"
        "QToolButton[miacodePopupOpen=\"true\"], QToolButton[miacodePopupOpen=\"true\"]:hover, QToolButton[miacodePopupOpen=\"true\"]:pressed, QToolButton[miacodePopupOpen=\"true\"]:checked { background: %2; border-color: %1; color: %3; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }"
    )
        .arg(css(c.border))
        .arg(css(c.inputBg))
        .arg(css(c.textPrimary))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText));
}

QString dialogComboLikeButtonStyleSheet(Qt::Alignment textAlignment)
{
    const Colors& c = colors();
    // Makes a QToolButton+QMenu pseudo-dropdown (used where a real QComboBox
    // can't express the choice, e.g. the multi-select 判定效果显示) read as a
    // createDialogComboBox: same input background, border, radius, height and
    // resting look on hover/press/popup-open (only the border tints, never a
    // fill). No drop-down subcontrol here, so centred text uses symmetric
    // padding (unlike the combo, which offsets padding to counter its reserved
    // arrow column).
    const bool centered = (textAlignment & Qt::AlignHCenter) != 0;
    const QString padding = centered
        ? QStringLiteral("padding: 2px 12px;")
        : QStringLiteral("padding: 2px 12px 2px 10px;");
    const QString align = centered ? QStringLiteral("center") : QStringLiteral("left");
    return QStringLiteral(
        "QToolButton { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; %5 min-height: 24px; text-align: %6; font-weight: 400; }"
        "QToolButton:hover { border-color: %4; }"
        "QToolButton:pressed, QToolButton:checked { border-color: %4; }"
        "QToolButton[miacodePopupOpen=\"true\"], QToolButton[miacodePopupOpen=\"true\"]:hover, QToolButton[miacodePopupOpen=\"true\"]:pressed, QToolButton[miacodePopupOpen=\"true\"]:checked { background: %1; border-color: %4; color: %2; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }"
    )
        .arg(css(c.inputBg))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.accent))
        .arg(padding)
        .arg(align);
}

QString dialogMenuCheckBoxStyleSheet()
{
    return dialogDropdownCheckBoxStyleSheet();
}

void bindDialogMenuButtonPopupState(QToolButton* button, QMenu* menu)
{
    if (button == nullptr || menu == nullptr) {
        return;
    }

    QObject::connect(menu, &QMenu::aboutToShow, button, [button, menu]() {
        const int menuWidth = qMax(menu->minimumWidth(), button->width());
        menu->setMinimumWidth(menuWidth);
        // Stretch each row widget (checkbox / item button) to the full menu
        // content width so its :hover highlight covers the whole row — a
        // QWidgetAction widget otherwise only spans its own content, leaving the
        // right part of the row un-highlighted on hover (unlike a real combo
        // popup). Menu QSS pads 6px each side.
        const int rowWidth = qMax(0, menuWidth - 12);
        for (QAction* action : menu->actions()) {
            if (auto* widgetAction = qobject_cast<QWidgetAction*>(action)) {
                if (QWidget* rowWidget = widgetAction->defaultWidget()) {
                    rowWidget->setProperty("miacode.dialog_menu_row_width", rowWidth);
                    rowWidget->setMinimumWidth(rowWidth);
                    rowWidget->updateGeometry();
                    rowWidget->adjustSize();
                    rowWidget->update();
                }
            }
        }
        menu->updateGeometry();
        menu->adjustSize();
        button->setProperty("miacodePopupOpen", true);
        button->setDown(false);
        button->clearFocus();
        repolish(button);
    });
    QObject::connect(menu, &QMenu::aboutToHide, button, [button]() {
        button->setProperty("miacodePopupOpen", false);
        button->setDown(false);
        button->clearFocus();
        repolish(button);
    });
}

QString dialogMenuLineEditStyleSheet()
{
    const Colors& c = colors();
    return dialogMenuLineEditStyleSheet(c.windowAltBg);
}

QString dialogMenuLineEditStyleSheet(const QColor& backgroundColor)
{
    const Colors& c = colors();
    const bool customBackground = backgroundColor.isValid() && backgroundColor != c.inputBg;
    const QColor baseBackground = backgroundColor.isValid() ? backgroundColor : c.inputBg;
    const QColor disabledBackground = customBackground ? baseBackground : c.inputDisabledBg;
    return QStringLiteral(
        "QLineEdit { min-height: 24px; padding: 2px 10px; border: 1px solid %1; border-radius: 8px; background: %2; color: %3; font-weight: 500; }"
        "QLineEdit:hover { border-color: %4; }"
        "QLineEdit:focus { border-color: %4; background: %2; }"
        "QLineEdit:disabled { background: %5; border-color: %1; color: %6; }"
    )
        .arg(css(c.border))
        .arg(css(baseBackground))
        .arg(css(c.textPrimary))
        .arg(css(c.accent))
        .arg(css(disabledBackground))
        .arg(css(c.textMuted));
}

QString dialogPushButtonStyleSheet(bool emphasized)
{
    const Colors& c = colors();
    const QColor baseBorder = emphasized ? c.accent : c.border;
    const QColor baseBackground = emphasized ? c.accent : c.cardBg;
    const QColor baseText = emphasized ? c.accentText : c.textPrimary;
    const QColor hoverBackground = emphasized ? c.accentHover : c.menuHoverBg;
    const QColor hoverText = emphasized ? c.accentText : c.textPrimary;
    return QStringLiteral(
        "QPushButton { min-width: 92px; min-height: 30px; padding: 0 12px; border: 1px solid %1; border-radius: 8px; background: %2; color: %3; font-weight: 500; }"
        "QPushButton:hover { background: %4; border-color: %5; color: %6; }"
        "QPushButton:pressed, QPushButton:checked { background: %7; border-color: %5; color: %8; }"
        "QPushButton:disabled { background: %9; border-color: %1; color: %10; }"
    )
        .arg(css(baseBorder))
        .arg(css(baseBackground))
        .arg(css(baseText))
        .arg(css(hoverBackground))
        .arg(css(c.accent))
        .arg(css(hoverText))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText))
        .arg(css(c.inputDisabledBg))
        .arg(css(c.textMuted));
}

QString dialogAuxiliaryButtonStyleSheet()
{
    const Colors& c = colors();
    const QColor baseBackground = c.inputBg;
    const QColor hoverBackground = c.menuHoverBg;
    const QColor pressedBackground = c.dark ? c.cardAltBg : c.windowAltBg;
    return QStringLiteral(
        "QPushButton, QPushButton#DialogAuxiliaryButton, QPushButton[miacodeAuxiliaryButton=\"true\"] {"
        " min-width: 72px; min-height: 24px; padding: 3px 12px;"
        " border: 1px solid %1; border-radius: 8px; background: %2;"
        " color: %3; font-weight: 500;"
        "}"
        "QPushButton:hover, QPushButton#DialogAuxiliaryButton:hover, QPushButton[miacodeAuxiliaryButton=\"true\"]:hover {"
        " background: %4; border-color: %5; color: %3;"
        "}"
        "QPushButton:pressed, QPushButton:checked,"
        "QPushButton#DialogAuxiliaryButton:pressed, QPushButton#DialogAuxiliaryButton:checked,"
        "QPushButton[miacodeAuxiliaryButton=\"true\"]:pressed, QPushButton[miacodeAuxiliaryButton=\"true\"]:checked {"
        " background: %6; border-color: %7; color: %3;"
        "}"
        "QPushButton:disabled, QPushButton#DialogAuxiliaryButton:disabled, QPushButton[miacodeAuxiliaryButton=\"true\"]:disabled {"
        " background: %8; border-color: %1; color: %9;"
        "}"
    )
        .arg(css(c.border))
        .arg(css(baseBackground))
        .arg(css(c.textPrimary))
        .arg(css(hoverBackground))
        .arg(css(c.borderStrong))
        .arg(css(pressedBackground))
        .arg(css(c.accent))
        .arg(css(c.inputDisabledBg))
        .arg(css(c.textMuted));
}

namespace {

// Sizes the combo's popup view to show up to `maxVisibleItems` of the CURRENT
// items. Must run against the populated combo — see applyComboBoxPopupLimit for
// why it is also wired to re-run on row insertion.
void sizeComboPopupHeight(QComboBox* combo)
{
    if (combo == nullptr) {
        return;
    }
    QAbstractItemView* popupView = combo->view();
    if (popupView == nullptr) {
        return;
    }
    const int requested = qMax(1, combo->property("miacode.combo_max_visible").toInt());
    const int visibleItems = combo->count() > 0 ? qMin(requested, combo->count()) : 1;
    combo->setMaxVisibleItems(visibleItems);

    popupView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    popupView->setMinimumHeight(0);

    int rowHeight = -1;
    const int sampleRows = qMin(combo->count(), visibleItems);
    for (int row = 0; row < sampleRows; ++row) {
        rowHeight = qMax(rowHeight, popupView->sizeHintForRow(row));
    }
    if (rowHeight <= 0) {
        rowHeight = combo->fontMetrics().height() + 8;
    }

    // + the view's QSS padding (6px top + 6px bottom, from
    // "QComboBox QAbstractItemView { padding: 6px }") so the last requested row
    // isn't clipped by the panel padding — a small (e.g. 2-item) popup
    // otherwise showed only one row and forced a scrollbar.
    constexpr int kViewPadding = 6 * 2;
    popupView->setMaximumHeight(
        rowHeight * visibleItems + popupView->frameWidth() * 2 + kViewPadding + 4);
}

}  // namespace

void applyComboBoxPopupLimit(QComboBox* combo, int maxVisibleItems)
{
    if (combo == nullptr) {
        return;
    }

    combo->setProperty("miacode.combo_max_visible", maxVisibleItems > 0 ? maxVisibleItems : 1);
    if (!combo->property("miacode.combo_popup_limited").toBool()) {
        // Explicit Fusion base: a default-constructed QProxyStyle would wrap
        // the desktop style (QWindows11Style on Win11), whose popup painting
        // fights the QSS panel — see the ComboBoxPopupLimitStyle comment.
        auto* popupLimitStyle =
            new ComboBoxPopupLimitStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        popupLimitStyle->setParent(combo);
        combo->setStyle(popupLimitStyle);
        combo->setProperty("miacode.combo_popup_limited", true);

        // Combos are frequently styled while STILL EMPTY (the shared factory
        // runs before addItem), so a one-shot height would lock the popup to a
        // single row. Recompute whenever rows are inserted/removed so the popup
        // grows with the final item list, no matter when the caller styles it.
        if (QAbstractItemModel* model = combo->model()) {
            QObject::connect(model, &QAbstractItemModel::rowsInserted, combo,
                             [combo]() { sizeComboPopupHeight(combo); });
            QObject::connect(model, &QAbstractItemModel::rowsRemoved, combo,
                             [combo]() { sizeComboPopupHeight(combo); });
            QObject::connect(model, &QAbstractItemModel::modelReset, combo,
                             [combo]() { sizeComboPopupHeight(combo); });
        }
    }

    QAbstractItemView* popupView = combo->view();
    if (popupView == nullptr) {
        return;
    }

    // Make the popup container a translucent frameless window so the shared
    // dialog-dropdown panel painter is the only thing composited.
    if (QWidget* container = popupView->window();
        container != nullptr && container != combo->window()) {
        prepareDialogDropdownPopupWindow(container);
    }

    sizeComboPopupHeight(combo);
}

QString dialogIconToolButtonStyleSheet(bool active)
{
    const Colors& c = colors();
    if (active) {
        return QStringLiteral(
            "QToolButton { border: 1px solid %1; border-radius: 10px; background: %1; }"
            "QToolButton:hover { background: %2; border-color: %2; }"
            "QToolButton:pressed { background: %3; border-color: %3; }"
            "QToolButton:disabled { background: %4; border-color: %5; }"
        )
            .arg(css(c.accent))
            .arg(css(c.accentHover))
            .arg(css(c.accentPressed))
            .arg(css(c.inputDisabledBg))
            .arg(css(c.border));
    }
    return QStringLiteral(
        "QToolButton { border: 1px solid %1; border-radius: 10px; background: %2; }"
        "QToolButton:hover { background: %3; border-color: %4; }"
        "QToolButton:pressed { background: %5; border-color: %4; }"
        "QToolButton:disabled { background: %6; border-color: %1; }"
    )
        .arg(css(c.border))
        .arg(css(c.cardAltBg))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent))
        .arg(css(c.accentPressed))
        .arg(css(c.inputDisabledBg));
}

QIcon dialogTransportPlayIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{
        QPointF(5.0, 3.0),
        QPointF(15.5, 10.0),
        QPointF(5.0, 17.0),
    });
    return QIcon(pixmap);
}

QIcon dialogTransportPauseIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(5.0, 3.0, 3.5, 14.0), 1.2, 1.2);
    painter.drawRoundedRect(QRectF(11.5, 3.0, 3.5, 14.0), 1.2, 1.2);
    return QIcon(pixmap);
}

QIcon dialogTransportStopIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(4.5, 4.5, 11.0, 11.0), 1.8, 1.8);
    return QIcon(pixmap);
}

QString preferencesDialogStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QDialog { background: %1; }"
        "QGroupBox { background: %2; border: 1px solid %3; border-radius: 10px; margin-top: 12px; padding-top: 10px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }"
        "QLabel { color: %4; }"
        "QToolButton#PreferenceMenuButton { min-height: 30px; min-width: 150px; border: 1px solid %3; border-radius: 6px; padding: 4px 10px; background: %2; color: %4; font-weight: 600; text-align: left; }"
        "QToolButton#PreferenceMenuButton:hover { background: %5; border-color: %6; }"
        "QToolButton#PreferenceMenuButton:pressed, QToolButton#PreferenceMenuButton:checked { background: %7; border-color: %6; color: %8; }"
        "QPushButton { min-width: 92px; min-height: 30px; padding: 0 12px; border: 1px solid %3; border-radius: 8px; background: %2; color: %4; font-weight: 500; }"
        "QPushButton:hover { background: %5; border-color: %6; }"
        "QPushButton:pressed, QPushButton:checked { background: %7; border-color: %6; color: %8; }"
        // Round "?" help badge (welcome dialog's 中文输入法 hint). Styled here so
        // it re-themes whenever the dialog re-applies this sheet on theme toggle.
        "QLabel#WelcomeHelpBadge { color: %9; border: 1px solid %9; border-radius: 8px; font-weight: 700; font-size: 11px; }"
    )
        .arg(css(c.windowBg))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText))
        .arg(css(c.textSecondary))
        + dialogTabStripStyleSheet(c.windowBg);
}

QString dialogTabStripStyleSheet(const QColor& dialogBackground)
{
    const Colors& c = colors();
    // Standard browser-tab pattern: pane keeps a full rounded border;
    // the pane is lifted 1px so the selected tab's bottom edge sits
    // exactly on the pane's top edge, and we paint that 1px of the
    // tab in the pane's background so the joint visually merges (no
    // double line). Unselected tabs sit slightly below so the pane's
    // top border still reads as a continuous line across the empty
    // area to the right of the last tab.
    //
    // %1 = unselected tab background (caller's dialog backdrop)
    // %2 = pane / selected tab background (card color)
    // %3 = pane + tab border color
    // %4 = text color
    return QStringLiteral(
        "QTabWidget::pane { background: %2; border: 1px solid %3;"
        " border-radius: 10px; top: -1px; padding: 8px; }"
        "QTabWidget::tab-bar { left: 6px; }"
        "QTabBar { background: transparent; border: none; }"
        "QTabBar::tab { background: %1; color: %4; border: 1px solid %3;"
        " padding: 5px 12px; margin-right: 2px; min-width: 40px;"
        " border-top-left-radius: 8px; border-top-right-radius: 8px; }"
        "QTabBar::tab:selected { background: %2; color: %4; border-bottom-color: %2; }"
        "QTabBar::tab:!selected { margin-top: 2px; }"
    )
        .arg(css(dialogBackground))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.textPrimary));
}

QString settingsDialogStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QDialog { background: %1; }"
        "QGroupBox { background: %2; border: 1px solid %3; border-radius: 10px; margin-top: 12px; padding-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLabel { color: %4; }"
        "QCheckBox { color: %4; spacing: 6px; }"
    )
        .arg(css(c.windowAltBg))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        + dialogTabStripStyleSheet(c.windowAltBg);
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

QString exportDialogStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QDialog { background: %1; }"
        "QFrame#VideoExportPrimaryPanel, QFrame#VideoExportSectionPanel { background: %2; border: 1px solid %3; border-radius: 12px; }"
        "QLineEdit, QDoubleSpinBox { background: %1; color: %4; border: 1px solid %3; border-radius: 8px; padding: 2px 10px; min-height: 24px; font-weight: 500; }"
        "QLineEdit:hover, QDoubleSpinBox:hover { border-color: %7; }"
        "QLineEdit:focus, QDoubleSpinBox:focus { border-color: %7; background: %1; }"
        "QLineEdit:disabled, QDoubleSpinBox:disabled { background: %8; border-color: %3; color: %9; }"
        "QLabel { color: %4; }"
        "QCheckBox { color: %4; spacing: 6px; }"
        // "添加片头" master switch sits in a neutral rounded box matching the
        // intro option groups, distinct from the louder export action buttons.
        "QFrame#AddIntroCapsule { background: %5; border: 1px solid %6; border-radius: 8px; }"
    )
        .arg(css(c.windowAltBg))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        .arg(css(c.windowAltBg))
        .arg(css(c.border))
        .arg(css(c.accent))
        .arg(css(c.inputDisabledBg))
        .arg(css(c.textMuted))
        + dialogTabStripStyleSheet(c.windowAltBg)
        // Export-dialog-only: widen the tab pane's inset (shared default is
        // 8px) so the dense Visuals page leaves a clear margin to the rounded
        // pane border — sliders/percent labels were sitting flush against the
        // top and right edges, getting visually clipped by the 10px corner
        // radius. ID selector keeps this scoped to this dialog's tabs.
        + QStringLiteral("QTabWidget#VideoExportTabs::pane { padding: 14px; }");
}

QString designerPickerDialogStyleSheet()
{
    // Stylesheet for the "pick canonical designer" dialog. The radio button
    // indicator gets explicit fills so it stays visible in dark mode (the
    // platform default leaves the unchecked disc nearly invisible against
    // the dark card background). The checked state paints the white checkmark
    // SVG (like the metadata checkbox) rather than a solid dot, and keeps the
    // border at 1px in every state so selecting a row never resizes the
    // indicator box and shifts the label sideways. Padding is set on
    // QRadioButton, not on individual button widgets — every row aligns.
    const Colors& c = colors();
    const QColor indicatorBg = c.dark ? c.windowAltBg : QColor("#FFFFFF");
    const QColor indicatorBorder = c.borderStrong;
    return QStringLiteral(
        "QDialog { background: %1; }"
        "QLabel { color: %2; }"
        "QScrollArea { background: %3; border: 1px solid %4; border-radius: 8px; }"
        "QScrollArea > QWidget > QWidget { background: %3; }"
        "QRadioButton { color: %2; spacing: 8px; padding: 4px; }"
        "QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid %5; border-radius: 8px; background: %6; }"
        "QRadioButton::indicator:hover { border-color: %7; }"
        "QRadioButton::indicator:checked { background: %7; border: 1px solid %7; image: url(\":/icons/checkmark.svg\"); }"
        "QPushButton { min-width: 92px; min-height: 30px; padding: 0 12px; border: 1px solid %4; border-radius: 8px; background: %3; color: %2; }"
        "QPushButton:hover { background: %8; border-color: %7; }"
        "QPushButton:default, QPushButton:pressed { background: %7; border-color: %7; color: %9; }"
    )
        .arg(css(c.windowBg))
        .arg(css(c.textPrimary))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(indicatorBorder))
        .arg(css(indicatorBg))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accentText));
}

QString darkAwareCheckBoxStyleSheet()
{
    // Standalone QCheckBox styling used on the chart-info form where the
    // checkbox isn't part of a themed dialog. Mirrors the radio-indicator
    // approach in designerPickerDialogStyleSheet so the box stays visible
    // in dark mode without inheriting from a host widget. The :checked
    // state paints a white checkmark via the qrc-bundled SVG so users
    // get a recognisable glyph rather than a solid accent fill.
    const Colors& c = colors();
    const QColor indicatorBg = c.dark ? c.windowAltBg : QColor("#FFFFFF");
    return QStringLiteral(
        "QCheckBox { color: %1; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid %2; border-radius: 3px; background: %3; }"
        "QCheckBox::indicator:hover { border-color: %4; }"
        "QCheckBox::indicator:checked { background: %4; border-color: %4; image: url(\":/icons/checkmark.svg\"); }"
        "QCheckBox::indicator:checked:hover { background: %5; border-color: %5; }"
    )
        .arg(css(c.textPrimary))
        .arg(css(c.borderStrong))
        .arg(css(indicatorBg))
        .arg(css(c.accent))
        .arg(css(c.accentHover));
}

QString readOnlyLineEditStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 2px 8px; min-height: 24px; }")
        .arg(css(c.inputDisabledBg))
        .arg(css(c.textSecondary))
        .arg(css(c.border));
}

QString collapsibleToggleStyleSheet()
{
    return QStringLiteral("QToolButton { border: none; font-weight: 600; text-align: left; color: %1; }")
        .arg(css(colors().textPrimary));
}

QString activePlaybackButtonStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QToolButton { color: %1; border: 1px solid %2; border-radius: 6px; background: %2; }"
        "QToolButton:hover { background: %3; }"
    )
        .arg(css(c.accentText))
        .arg(css(c.accent))
        .arg(css(c.accentHover));
}

QString validationMessageLabelStyleSheet()
{
    return QStringLiteral("QLabel { background: transparent; color: %1; padding: 1px 2px; }")
        .arg(css(colors().textPrimary));
}

}  // namespace UiTheme
