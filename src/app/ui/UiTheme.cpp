#include "UiTheme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QStyleHints>

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

const UiTheme::Colors& lightColors()
{
    static const UiTheme::Colors theme{
        false,
        QColor("#F8FAFD"),
        QColor("#F5F7FA"),
        QColor("#F7F9FC"),
        QColor("#F7F9FC"),
        QColor("#F5F7FA"),
        QColor("#FFFFFF"),
        QColor("#EDF2F8"),
        QColor("#FFFFFF"),
        QColor("#F2F5F9"),
        QColor("#000000"),
        QColor("#203040"),
        QColor("#5F6B7A"),
        QColor("#7A8796"),
        QColor("#FFFFFF"),
        QColor("#D5E0EC"),
        QColor("#CCD6E2"),
        QColor("#B8C7DA"),
        QColor("#2E77D0"),
        QColor("#3A86E8"),
        QColor("#2668B9"),
        QColor("#FFFFFF"),
        QColor("#B8CCE5"),
        QColor("#1F1F1F"),
        QColor("#FFFFFF"),
        QColor("#D7E0EB"),
        QColor("#EEF5FF"),
        QColor("#9AA5B4"),
        QColor("#F4F7FB"),
        QColor("#9CB5CE"),
        QColor("#81A2C3"),
        QColor("#2B3C4E"),
        QColor("#5D6E83"),
        QColor("#F5F5F5"),
        QColor("#F3F5F8"),
        QColor("#E8E8E8"),
        QColor("#F7F8FA"),
        QColor("#C7D2DF"),
        // Beta21-fix4 — bump timeline grid contrast to match beta19's
        // on-screen brightness. Three independent diagnostic experiments
        // (per-line vs batched material A/B, MSAA 4x, CPU QPainter into
        // texture) proved the rendering pipeline produces identical
        // pixels — so adjusting the palette is now the principled lever.
        // Reference: user-provided beta19 screenshot shows major/minor
        // lines rendering at ~RGB 160-205 on a lane background of
        // ~RGB 35-45. The previous values blended to ~128 (major) /
        // ~85 (minor), well below the reference. Light-theme values
        // mirror the same brightness boost inverted for the near-white
        // lane background — saturated dark blue at higher alpha.
        QColor("#5B7AA8"),
        QColor(60, 85, 135, 180),
        QColor(251, 251, 251, 190),
        QColor(242, 242, 242, 190),
        QColor("#4D5C6D"),
        QColor("#9AA7B6"),
        QColor(210, 224, 214, 108),
        QColor("#9CB3A6"),
        QColor("#3A7AFE"),
        QColor("#B85C4D"),
        QColor("#F28C28"),
    };
    return theme;
}

const UiTheme::Colors& darkColors()
{
    static const UiTheme::Colors theme{
        true,
        QColor("#151A20"),
        QColor("#1B2129"),
        QColor("#171C23"),
        QColor("#171C23"),
        QColor("#1B2129"),
        QColor("#232B35"),
        QColor("#1F2630"),
        QColor("#171D24"),
        QColor("#202833"),
        QColor("#050607"),
        QColor("#E6EEF8"),
        QColor("#A9B6C6"),
        QColor("#7B8798"),
        QColor("#0F141A"),
        QColor("#384656"),
        QColor("#455466"),
        QColor("#546679"),
        QColor("#4F8FEC"),
        QColor("#67A1F1"),
        QColor("#3E79D0"),
        QColor("#F7FBFF"),
        QColor("#315D9E"),
        QColor("#F7FBFF"),
        QColor("#1F2630"),
        QColor("#455466"),
        QColor("#2C3846"),
        QColor("#748091"),
        QColor("#1C232C"),
        QColor("#5A6A7B"),
        QColor("#70849A"),
        QColor("#D8E2EE"),
        QColor("#95A4B7"),
        QColor("#1A2027"),
        QColor("#1D232B"),
        QColor("#171D24"),
        QColor("#202833"),
        QColor("#4A5C70"),
        // Beta21-fix4 — see light-theme companion above. Reference
        // beta19 dark-theme screenshot shows lines at ~RGB 180-205 on
        // lane RGB ~35-45. New values blend to that range:
        //   gridMajor #B8CDE5 opaque → on-screen ~RGB(184,205,229).
        //   gridMinor (180,200,225,150) over lane (34,42,52) ≈
        //   blended ~RGB(155,170,195). Matches the bright per-comma
        //   ticks in the reference.
        QColor("#B8CDE5"),
        QColor(180, 200, 225, 150),
        QColor(34, 42, 52, 210),
        QColor(29, 36, 45, 210),
        QColor("#C8D5E5"),
        QColor("#8091A5"),
        QColor(118, 179, 140, 158),
        QColor("#5E8B6E"),
        QColor("#6FA8FF"),
        QColor("#C96B5B"),
        QColor("#FF9B4A"),
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
        "QMenuBar { background: %1; color: %2; border-bottom: 1px solid %3; }"
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
        .arg(css(c.cardBg))
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
        .arg(css(c.inputBg))
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
    return QStringLiteral(
        "QWidget { background: %1; color: %2; }"
        "QFrame#MetadataCard { background: %1; border: 1px solid %3; border-radius: 8px; }"
        "QLabel#SectionTitle { color: %2; font-weight: 700; padding-left: 4px; }"
        "QLabel#MetadataFieldLabel { color: %2; background: transparent; padding-left: 8px; }"
        "QLineEdit, QTextEdit, QPlainTextEdit { background: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 6px 8px; selection-background-color: %6; selection-color: %7; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: %8; }"
        "QToolButton, QPushButton { color: %2; border: 1px solid %3; border-radius: 6px; background: %4; padding: 4px 8px; }"
        "QToolButton:hover, QPushButton:hover { background: %9; border-color: %8; }"
    )
        .arg(css(c.cardBg))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.inputBg))
        .arg(css(c.borderSoft))
        .arg(css(c.selection))
        .arg(css(c.selectionText))
        .arg(css(c.accent))
        .arg(css(c.menuHoverBg));
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

QString deleteDifficultyButtonStyleSheet()
{
    return QStringLiteral(
        "QToolButton { border: none; border-radius: 5px; background: transparent; }"
        "QToolButton:hover { background: %1; }"
    ).arg(css(colors().menuHoverBg));
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

QString dialogComboBoxStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 3px 24px 3px 10px; min-height: 24px; }"
        "QComboBox:hover { border-color: %4; }"
        "QComboBox:focus { border-color: %4; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 20px; border: none; background: transparent; }"
        "QComboBox::down-arrow { width: 10px; height: 10px; }"
        "QComboBox QAbstractItemView { background: %5; color: %2; border: 1px solid %6; border-radius: 8px; padding: 6px; outline: 0; show-decoration-selected: 1; selection-background-color: transparent; selection-color: %8; }"
        "QComboBox QAbstractItemView::item { min-height: 20px; padding: 3px 10px; border: 1px solid transparent; border-radius: 6px; background: transparent; }"
        "QComboBox QAbstractItemView::item:selected { background: %7; color: %8; border: 1px solid transparent; outline: 0; }"
        "QComboBox QAbstractItemView::item:focus { border: 1px solid transparent; outline: 0; }"
        "QComboBox QAbstractItemView::item:hover { background: %7; border: 1px solid transparent; }"
    )
        .arg(css(c.inputBg))
        .arg(css(c.textPrimary))
        .arg(css(c.border))
        .arg(css(c.accent))
        .arg(css(c.menuBg))
        .arg(css(c.menuBorder))
        .arg(css(c.menuHoverBg))
        .arg(css(c.textPrimary));
}

QString dialogMenuButtonStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QToolButton { min-height: 24px; padding: 2px 10px; border: 1px solid %1; border-radius: 8px; background: %2; color: %3; font-weight: 500; text-align: left; }"
        "QToolButton:hover { background: %4; border-color: %5; }"
        "QToolButton:pressed, QToolButton:checked { background: %6; border-color: %5; color: %7; }"
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

QString dialogMenuLineEditStyleSheet()
{
    const Colors& c = colors();
    return QStringLiteral(
        "QLineEdit { min-height: 24px; padding: 2px 10px; border: 1px solid %1; border-radius: 8px; background: %2; color: %3; font-weight: 500; }"
        "QLineEdit:hover { border-color: %4; }"
        "QLineEdit:focus { border-color: %4; background: %2; }"
    )
        .arg(css(c.border))
        .arg(css(c.inputBg))
        .arg(css(c.textPrimary))
        .arg(css(c.accent));
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
        "QToolButton#PreferenceMenuButton { min-height: 30px; min-width: 180px; border: 1px solid %3; border-radius: 6px; padding: 4px 10px; background: %2; color: %4; font-weight: 600; text-align: left; }"
        "QToolButton#PreferenceMenuButton:hover { background: %5; border-color: %6; }"
        "QToolButton#PreferenceMenuButton:pressed, QToolButton#PreferenceMenuButton:checked { background: %7; border-color: %6; color: %8; }"
        "QPushButton { min-width: 92px; min-height: 30px; padding: 0 12px; border: 1px solid %3; border-radius: 8px; background: %2; color: %4; font-weight: 500; }"
        "QPushButton:hover { background: %5; border-color: %6; }"
        "QPushButton:pressed, QPushButton:checked { background: %7; border-color: %6; color: %8; }"
    )
        .arg(css(c.windowBg))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.textPrimary))
        .arg(css(c.menuHoverBg))
        .arg(css(c.accent))
        .arg(css(c.accentPressed))
        .arg(css(c.accentText));
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
        .arg(css(c.textPrimary));
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
        "QLineEdit, QDoubleSpinBox { background: %2; color: %4; border: 1px solid %3; border-radius: 8px; padding: 2px 8px; min-height: 24px; }"
        "QLabel { color: %4; }"
        "QCheckBox { color: %4; spacing: 6px; }"
    )
        .arg(css(c.windowAltBg))
        .arg(css(c.cardBg))
        .arg(css(c.border))
        .arg(css(c.textPrimary));
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
