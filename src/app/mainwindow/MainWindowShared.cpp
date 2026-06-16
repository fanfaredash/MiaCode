#include "MainWindowShared.h"

#include "MainWindow.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/MiniaudioFileAccess.h"
#include "common/WaveformCache.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QSvgRenderer>
#include <QPolygonF>
#include <QScreen>
#include <QSignalBlocker>
#include <QStringList>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QWindow>
#include <QtMath>

#include <cmath>

namespace miacode::mainwindow::shared {

const QList<double> kEditorLineSpacingFactorOptions{
    0.0, 1.0, 1.5, 2.0, 3.0, 5.0,
};

namespace {

const QList<double> kPreviewPlaybackRateOptions{
    0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0,
};

QString normalizeLanguageToken(QString token)
{
    token = token.trimmed().toLower();
    token.replace('-', '_');
    return token;
}

QString cssRgba(const QColor& color, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(qBound(0, alpha, 255));
}

QString cssRgb(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

QColor difficultyColor(int difficultyId)
{
    switch (difficultyId) {
    case 1:
        return QColor("#69A6FF");
    case 2:
        return QColor("#78C85A");
    case 3:
        return QColor("#DCC548");
    case 4:
        return QColor("#E35C50");
    case 5:
        return QColor("#7A4FD1");
    case 6:
        return QColor("#D548B6");
    case 7:
        return QColor("#E29A46");
    default:
        return QColor("#8A8F98");
    }
}

QPixmap makeDifficultyBadgePixmap(int difficultyId)
{
    const QColor fill = difficultyColor(difficultyId);
    const qreal dpr = qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
    QImage image(static_cast<int>(14 * dpr), static_cast<int>(14 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawRoundedRect(QRectF(2.0 * dpr, 2.0 * dpr, 10.0 * dpr, 10.0 * dpr), 3.0 * dpr, 3.0 * dpr);
    painter.end();
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QPointF pointOnArcPrecise(const QRectF& rect, qreal deg)
{
    const qreal rad = qDegreesToRadians(deg);
    const qreal cx = rect.center().x();
    const qreal cy = rect.center().y();
    const qreal rx = rect.width() * 0.5;
    const qreal ry = rect.height() * 0.5;
    return QPointF(
        cx + rx * std::cos(rad),
        cy - ry * std::sin(rad)
    );
}

QPointF tangentOnArcPrecise(const QRectF& rect, qreal deg, qreal sweepDeg)
{
    const qreal rad = qDegreesToRadians(deg);
    const qreal rx = rect.width() * 0.5;
    const qreal ry = rect.height() * 0.5;
    QPointF tangent(
        -rx * std::sin(rad),
        -ry * std::cos(rad)
    );
    if (sweepDeg < 0.0) {
        tangent = -tangent;
    }
    const qreal len = std::hypot(tangent.x(), tangent.y());
    if (len < 1e-6) {
        return QPointF(1.0, 0.0);
    }
    return QPointF(tangent.x() / len, tangent.y() / len);
}

QPixmap makeTransformRotate45Pixmap(const QColor& color, bool clockwise)
{
    constexpr qreal logicalSize = 20.0;
    constexpr qreal dpr = 2.0;
    QPixmap pixmap(static_cast<int>(logicalSize * dpr), static_cast<int>(logicalSize * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF arcRect(3.4, 3.4, 13.2, 13.2);
    const qreal startDeg = clockwise ? 42.0 : 138.0;
    const qreal arcSweepDeg = clockwise ? -236.0 : 236.0;

    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(arcRect, qRound(startDeg * 16.0), qRound(arcSweepDeg * 16.0));

    const qreal endDeg = startDeg + arcSweepDeg;
    const QPointF tip = pointOnArcPrecise(arcRect, endDeg);
    const QPointF dir = tangentOnArcPrecise(arcRect, endDeg, arcSweepDeg);
    const QPointF normal(-dir.y(), dir.x());
    constexpr qreal headLength = 5.2;
    constexpr qreal headWidth = 4.2;
    const QPointF baseCenter = tip;
    const QPointF headTip = baseCenter + dir * headLength;
    QPolygonF head;
    head << headTip
         << (baseCenter + normal * (headWidth * 0.5))
         << (baseCenter - normal * (headWidth * 0.5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
    painter.end();
    return pixmap;
}

}  // namespace

double normalizeEditorLineSpacingFactor(double factor)
{
    if (kEditorLineSpacingFactorOptions.isEmpty()) {
        return kEditorLineSpacingFactorDefault;
    }
    double best = kEditorLineSpacingFactorOptions.first();
    double bestDiff = qAbs(best - factor);
    for (double candidate : kEditorLineSpacingFactorOptions) {
        const double diff = qAbs(candidate - factor);
        if (diff < bestDiff) {
            best = candidate;
            bestDiff = diff;
        }
    }
    return best;
}

QString editorLineSpacingFactorLabel(double factor)
{
    if (qFuzzyCompare(factor + 1.0, 1.0)) {
        return QStringLiteral("0x");
    }
    const QString text = QString::number(factor, 'f', qFuzzyCompare(factor, qRound(factor)) ? 0 : 1);
    return text + QStringLiteral("x");
}

int nearestPreviewPlaybackRateIndex(double rate)
{
    if (kPreviewPlaybackRateOptions.isEmpty()) {
        return -1;
    }
    int bestIndex = 0;
    double bestDiff = qAbs(kPreviewPlaybackRateOptions.first() - rate);
    for (int index = 1; index < kPreviewPlaybackRateOptions.size(); ++index) {
        const double diff = qAbs(kPreviewPlaybackRateOptions[index] - rate);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = index;
        }
    }
    return bestIndex;
}

double steppedPreviewPlaybackRate(double rate, int direction)
{
    const int currentIndex = nearestPreviewPlaybackRateIndex(rate);
    if (currentIndex < 0) {
        return qMax(0.25, rate);
    }
    const int targetIndex = qBound(0, currentIndex + direction, kPreviewPlaybackRateOptions.size() - 1);
    return kPreviewPlaybackRateOptions[targetIndex];
}

QString uiText(const QString& key, const QString& fallback)
{
    const QString localized = UiText::text(key);
    return localized.isEmpty() ? fallback : localized;
}

QByteArray autosaveContentSignature(const QString& text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}

QString resolveProjectDataDirectoryPath(const QString& filePath)
{
    return miacode::waveform::projectDataDirectoryPathForFile(filePath);
}

void appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs)
{
    miacode::debug_log::appendStartupTimingStage(stage, elapsedMs, deltaMs);
}

void centerDialogOnAnchor(QDialog* dialog, QWidget* parent)
{
    if (dialog == nullptr) {
        return;
    }
    dialog->adjustSize();
    const bool debugDialogAnchor = miacode::debug_options::runtimeDebugOutputEnabled();
    const auto appendDialogAnchorLog = [debugDialogAnchor](const QString& text) {
        if (!debugDialogAnchor) {
            return;
        }
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("dialog/anchor"),
            text
        );
    };

    QWidget* anchorWidget = parent != nullptr ? parent->window() : nullptr;
    QRect anchorRect;
    QScreen* targetScreen = nullptr;
    QString anchorSource;
    if (anchorWidget != nullptr) {
        if (auto* mainWindow = qobject_cast<MainWindow*>(anchorWidget);
            mainWindow != nullptr
            && mainWindow->quickShellRootWindowFrameGeometryAvailable()) {
            anchorRect = mainWindow->quickShellRootWindowFrameGeometry();
            targetScreen = QGuiApplication::screenAt(anchorRect.center());
            anchorSource = QStringLiteral("quick_shell_root_window");
        }
    }
    if (anchorWidget != nullptr
        && !anchorRect.isValid()
        && anchorWidget->isVisible()
        && !anchorWidget->windowState().testFlag(Qt::WindowMinimized)
        && !anchorWidget->windowFlags().testFlag(Qt::Tool)) {
        anchorRect = anchorWidget->frameGeometry();
        if (anchorWidget->windowHandle() != nullptr) {
            targetScreen = anchorWidget->windowHandle()->screen();
        }
        anchorSource = QStringLiteral("parent_window");
    }

    const auto tryFindTopLevelWindow = [dialog](bool requireActive, bool allowToolWindows) -> QWindow* {
        const auto windows = QGuiApplication::topLevelWindows();
        for (QWindow* window : windows) {
            if (window == nullptr
                || window == dialog->windowHandle()
                || !window->isVisible()
                || window->visibility() == QWindow::Hidden
                || window->visibility() == QWindow::Minimized) {
                continue;
            }
            const Qt::WindowFlags flags = window->flags();
            if (!allowToolWindows && (flags.testFlag(Qt::Tool) || flags.testFlag(Qt::Popup))) {
                continue;
            }
            if (requireActive && !window->isActive()) {
                continue;
            }
            return window;
        }
        return nullptr;
    };

    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(true, false); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
            targetScreen = topLevelWindow->screen();
            anchorSource = QStringLiteral("active_non_tool_qwindow");
        }
    }
    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(false, false); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
            targetScreen = topLevelWindow->screen();
            anchorSource = QStringLiteral("visible_non_tool_qwindow");
        }
    }
    if (!anchorRect.isValid()) {
        if (QWidget* activeWidget = QApplication::activeWindow();
            activeWidget != nullptr
            && activeWidget != dialog
            && activeWidget->isVisible()
            && !activeWidget->windowState().testFlag(Qt::WindowMinimized)
            && !activeWidget->windowFlags().testFlag(Qt::Tool)) {
            anchorRect = activeWidget->frameGeometry();
            anchorWidget = activeWidget;
            if (activeWidget->windowHandle() != nullptr) {
                targetScreen = activeWidget->windowHandle()->screen();
            }
            anchorSource = QStringLiteral("active_qwidget");
        }
    }
    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(false, true); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
            targetScreen = topLevelWindow->screen();
            anchorSource = QStringLiteral("visible_tool_qwindow");
        }
    }
    if (!anchorRect.isValid()) {
        if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
            anchorRect = screen->availableGeometry();
            targetScreen = screen;
            anchorSource = QStringLiteral("primary_screen");
        }
    }
    appendDialogAnchorLog(
        QString("title=%1 source=%2 rect=[%3,%4 %5x%6] parent_vis=%7 parent_title=%8")
            .arg(dialog->windowTitle())
            .arg(anchorSource.isEmpty() ? QStringLiteral("none") : anchorSource)
            .arg(anchorRect.x())
            .arg(anchorRect.y())
            .arg(anchorRect.width())
            .arg(anchorRect.height())
            .arg(parent != nullptr && parent->window() != nullptr && parent->window()->isVisible() ? 1 : 0)
            .arg(parent != nullptr && parent->window() != nullptr ? parent->window()->windowTitle() : QStringLiteral("(null)"))
    );
    if (anchorRect.isValid()) {
        QPoint targetTopLeft(
            anchorRect.center().x() - dialog->width() / 2,
            anchorRect.center().y() - dialog->height() / 2
        );
        if (targetScreen == nullptr) {
            targetScreen = QGuiApplication::screenAt(anchorRect.center());
        }
        if (targetScreen == nullptr && anchorWidget != nullptr && anchorWidget->windowHandle() != nullptr) {
            targetScreen = anchorWidget->windowHandle()->screen();
        }
        if (targetScreen != nullptr) {
            const QRect avail = targetScreen->availableGeometry();
            targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog->width() + 1));
            targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog->height() + 1));
        }
        appendDialogAnchorLog(
            QString("title=%1 target=[%2,%3 %4x%5] screen=%6")
                .arg(dialog->windowTitle())
                .arg(targetTopLeft.x())
                .arg(targetTopLeft.y())
                .arg(dialog->width())
                .arg(dialog->height())
                .arg(targetScreen != nullptr ? targetScreen->name() : QStringLiteral("(null)"))
        );
        dialog->move(targetTopLeft);
    }
}

QColor previewFullscreenOverlayIconColor()
{
    return QColor(QStringLiteral("#F8FAFC"));
}

QFont editorFont(int pointSize)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) {
        for (const QString& family : QStringList{"SF Mono", "Menlo", "Monaco", "Noto Sans Mono", "JetBrains Mono"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0 && QFontInfo(font).fixedPitch()) {
                break;
            }
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(pointSize > 0 ? pointSize : 13);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    static const QString embeddedConsolasFamily = []() -> QString {
        const int fontId = QFontDatabase::addApplicationFont(":/fonts/consola.ttf");
        if (fontId < 0) {
            return QString();
        }
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (families.isEmpty()) {
            return QString();
        }
        return families.first();
    }();

    QFont font;
    if (!embeddedConsolasFamily.isEmpty()) {
        font.setFamily(embeddedConsolasFamily);
    } else {
        font.setFamily("Consolas");
        if (!QFontInfo(font).exactMatch()) {
            font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(pointSize > 0 ? pointSize : 10);
    return font;
#endif
}

QFont timelineHeaderLineNumberFont(int pointSize)
{
    Q_UNUSED(pointSize);
    return editorFont(11);
}

int blockSpacingPixelsForPointSize(int pointSize, double spacingFactor)
{
    const int baseSpacing = qBound(1, qRound(static_cast<double>(pointSize) * 0.18), 6);
    return qMax(0, qRound(static_cast<double>(baseSpacing) * qMax(0.0, spacingFactor)));
}

void applyBlockSpacingToTextEdit(QTextEdit* editor, int blockSpacingPixels)
{
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }
    QSignalBlocker blocker(editor);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setBottomMargin(static_cast<qreal>(qMax(0, blockSpacingPixels)));
    cursor.mergeBlockFormat(format);
    cursor.endEditBlock();
}

QFont uiOutputFont()
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(12);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (UiText::isChineseUi()) {
        for (const QString& family : QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
    }
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPointSize(11);
    return font;
#endif
}

QFont uiAccentFont(int pointSize, QFont::Weight weight)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (UiText::isChineseUi()) {
        for (const QString& family : QStringList{"PingFang SC", "Hiragino Sans GB"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
    }
    font.setPointSize(pointSize + ((pointSize <= 11) ? 1 : 0));
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font;
    QStringList familyCandidates;
    if (UiText::isChineseUi()) {
        familyCandidates << "Microsoft YaHei UI" << "Microsoft YaHei" << "PingFang SC" << "Noto Sans CJK SC";
    }
    familyCandidates << "Segoe UI Variable Text" << "Segoe UI";
    for (const QString& family : familyCandidates) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#endif
}

QFont uiMonoFont(int pointSize, QFont::Weight weight)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) {
        for (const QString& family : QStringList{"SF Mono", "Menlo", "Monaco", "Noto Sans Mono", "JetBrains Mono"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0 && QFontInfo(font).fixedPitch()) {
                break;
            }
        }
    }
    font.setPointSize(pointSize + 1);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font;
    for (const QString& family : QStringList{"Cascadia Mono", "JetBrains Mono", "Cascadia Code", "Consolas"}) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
#endif
}

QString previewFullscreenControlCardStyleSheet()
{
    const QColor overlayText = previewFullscreenOverlayIconColor();
    const QColor accent = UiTheme::colors().accent;
    return QStringLiteral(
        "QFrame#PreviewControlCard {"
        " background: rgba(0, 0, 0, 188);"
        " border: 1px solid rgba(255, 255, 255, 42);"
        " border-radius: 18px;"
        "}"
        "QFrame#PreviewControls {"
        " background: transparent;"
        " border: none;"
        "}"
        "QToolButton#PreviewControlButton {"
        " color: %1;"
        " padding: 7px 10px;"
        " min-height: 32px;"
        " border: 1px solid rgba(255, 255, 255, 40);"
        " border-radius: 9px;"
        " background: rgba(255, 255, 255, 18);"
        " font-weight: 600;"
        "}"
        "QToolButton#PreviewControlButton:hover {"
        " background: rgba(255, 255, 255, 32);"
        " border-color: rgba(255, 255, 255, 76);"
        "}"
        "QToolButton#PreviewControlButton:pressed {"
        " background: rgba(255, 255, 255, 46);"
        "}"
        "QSlider::groove:horizontal {"
        " height: 6px;"
        " background: rgba(255, 255, 255, 42);"
        " border-radius: 3px;"
        "}"
        "QSlider::sub-page:horizontal {"
        " background: %2;"
        " border-radius: 3px;"
        "}"
        "QSlider::add-page:horizontal {"
        " background: rgba(255, 255, 255, 22);"
        " border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        " width: 12px;"
        " margin: -4px 0;"
        " border-radius: 6px;"
        " background: %1;"
        " border: 1px solid rgba(15, 23, 42, 102);"
        "}"
    )
        .arg(cssRgb(overlayText))
        .arg(cssRgb(accent));
}

QString previewFullscreenHintStyleSheet()
{
    return QStringLiteral(
        "QLabel {"
        " background: rgba(0, 0, 0, 188);"
        " color: #F8FAFC;"
        " border: 1px solid rgba(255, 255, 255, 52);"
        " border-radius: 16px;"
        " padding: 10px 16px;"
        " font-size: 14px;"
        " font-weight: 600;"
        "}"
    );
}

QString previewPlaybackRateToastStyleSheet()
{
    return QStringLiteral(
        "QWidget {"
        " background: rgba(0, 0, 0, 204);"
        " border: 1px solid rgba(255, 255, 255, 56);"
        " border-radius: 18px;"
        "}"
        "QLabel {"
        " background: transparent;"
        " color: #F8FAFC;"
        " border: none;"
        "}"
    );
}

QString outlineCollapseButtonStyleSheet()
{
    const UiTheme::Colors& colors = UiTheme::colors();
    return QStringLiteral(
        "QToolButton {"
        " color: %1;"
        " background: %2;"
        " border: none;"
        " border-left: 1px solid %3;"
        " border-right: 1px solid %3;"
        " padding: 0px;"
        " font-size: 12px;"
        " font-weight: 700;"
        "}"
        "QToolButton:hover {"
        " background: %4;"
        "}"
        "QToolButton:pressed {"
        " background: %5;"
        "}"
    )
        .arg(cssRgb(colors.iconSecondary))
        .arg(cssRgb(colors.cardAltBg))
        .arg(cssRgb(colors.border))
        .arg(cssRgb(colors.menuHoverBg))
        .arg(cssRgb(colors.borderSoft));
}

QString previewFullscreenPauseButtonStyleSheet(bool active)
{
    const QColor overlayText = previewFullscreenOverlayIconColor();
    const QColor accent = UiTheme::colors().accent;
    if (active) {
        return QStringLiteral(
            "QToolButton {"
            " color: %1;"
            " padding: 7px 10px;"
            " min-height: 32px;"
            " border: 1px solid %2;"
            " border-radius: 9px;"
            " background: %3;"
            " font-weight: 700;"
            "}"
            "QToolButton:hover { background: %4; }"
        )
            .arg(cssRgb(overlayText))
            .arg(cssRgba(accent, 220))
            .arg(cssRgba(accent, 208))
            .arg(cssRgba(accent, 255));
    }
    return QStringLiteral(
        "QToolButton {"
        " color: %1;"
        " padding: 7px 10px;"
        " min-height: 32px;"
        " border: 1px solid rgba(255, 255, 255, 40);"
        " border-radius: 9px;"
        " background: rgba(255, 255, 255, 18);"
        " font-weight: 600;"
        "}"
        "QToolButton:hover {"
        " background: rgba(255, 255, 255, 32);"
        " border-color: rgba(255, 255, 255, 76);"
        "}"
    )
        .arg(cssRgb(overlayText));
}

QIcon makeMenuSelectionCheckIcon(const QColor& color, bool visible)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    if (visible) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color, 1.8);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawLine(QPointF(5.0, 7.4), QPointF(7.7, 10.1));
        painter.drawLine(QPointF(7.7, 10.1), QPointF(12.4, 4.4));
    }
    return QIcon(pixmap);
}

QIcon makePreviewPlayIcon(const QColor& color)
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

QIcon makePreviewStopIcon(const QColor& color)
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

QIcon makePreviewPauseIcon(const QColor& color)
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

QIcon makePreviewResumeIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(3.0, 4.0, 2.6, 12.0), 1.0, 1.0);
    painter.drawPolygon(QPolygonF{
        QPointF(7.5, 3.0),
        QPointF(16.5, 10.0),
        QPointF(7.5, 17.0),
    });
    return QIcon(pixmap);
}

QIcon makePreviewEnterFullscreenIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(7.8, 4.8), QPointF(4.8, 4.8));
    painter.drawLine(QPointF(4.8, 4.8), QPointF(4.8, 7.8));
    painter.drawLine(QPointF(12.2, 4.8), QPointF(15.2, 4.8));
    painter.drawLine(QPointF(15.2, 4.8), QPointF(15.2, 7.8));
    painter.drawLine(QPointF(4.8, 12.2), QPointF(4.8, 15.2));
    painter.drawLine(QPointF(4.8, 15.2), QPointF(7.8, 15.2));
    painter.drawLine(QPointF(12.2, 15.2), QPointF(15.2, 15.2));
    painter.drawLine(QPointF(15.2, 12.2), QPointF(15.2, 15.2));
    return QIcon(pixmap);
}

QIcon makePreviewExitFullscreenIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(8.2, 4.8), QPointF(8.2, 8.2));
    painter.drawLine(QPointF(4.8, 8.2), QPointF(8.2, 8.2));
    painter.drawLine(QPointF(11.8, 4.8), QPointF(11.8, 8.2));
    painter.drawLine(QPointF(11.8, 8.2), QPointF(15.2, 8.2));
    painter.drawLine(QPointF(4.8, 11.8), QPointF(8.2, 11.8));
    painter.drawLine(QPointF(8.2, 11.8), QPointF(8.2, 15.2));
    painter.drawLine(QPointF(11.8, 11.8), QPointF(15.2, 11.8));
    painter.drawLine(QPointF(11.8, 11.8), QPointF(11.8, 15.2));
    return QIcon(pixmap);
}

QIcon makeDifficultyBadgeIcon(int difficultyId)
{
    const QPixmap pixmap = makeDifficultyBadgePixmap(difficultyId);
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Selected, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::On);
    return icon;
}

QIcon makeOutlineCloseIcon(const QColor& color)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(3.0, 3.0), QPointF(9.0, 9.0));
    painter.drawLine(QPointF(9.0, 3.0), QPointF(3.0, 9.0));
    return QIcon(pixmap);
}

QIcon makeSettingsGearIcon(const QColor& color)
{
    // Google Material Design "settings" gear (Apache-2.0 material-icons),
    // rendered via QSvgRenderer so it matches the familiar Google glyph
    // rather than a hand-drawn shape. The path lives in a 24x24 viewBox
    // with built-in padding, so the gear reads at ~80% of the icon box —
    // its displayed size is set by the toolbar's iconSize (matched to the
    // menu-bar font in MainWindow.FrameBootstrap.cpp). Rendered into a
    // generous base pixmap; the toolbar smoothly downscales to iconSize.
    static const char kGearPath[] =
        "M19.14,12.94c0.04-0.3,0.06-0.61,0.06-0.94c0-0.32-0.02-0.64-0.07-0.94l2.03-1.58c0.18-0.14,0.23-0.41,0.12-0.61"
        "l-1.92-3.32c-0.12-0.22-0.37-0.29-0.59-0.22l-2.39,0.96c-0.5-0.38-1.03-0.7-1.62-0.94L14.4,2.81"
        "c-0.04-0.24-0.24-0.41-0.48-0.41h-3.84c-0.24,0-0.43,0.17-0.47,0.41L9.25,5.35C8.66,5.59,8.12,5.92,7.63,6.29"
        "L5.24,5.33c-0.22-0.08-0.47,0-0.59,0.22L2.74,8.87C2.62,9.08,2.66,9.34,2.86,9.48l2.03,1.58"
        "C4.84,11.36,4.8,11.69,4.8,12c0,0.31,0.02,0.64,0.07,0.94l-2.03,1.58c-0.18,0.14-0.23,0.41-0.12,0.61l1.92,3.32"
        "c0.12,0.22,0.37,0.29,0.59,0.22l2.39-0.96c0.5,0.38,1.03,0.7,1.62,0.94l0.36,2.54c0.05,0.24,0.24,0.41,0.48,0.41"
        "h3.84c0.24,0,0.44-0.17,0.47-0.41l0.36-2.54c0.59-0.24,1.13-0.56,1.62-0.94l2.39,0.96c0.22,0.08,0.47,0,0.59-0.22"
        "l1.92-3.32c0.12-0.22,0.07-0.47-0.12-0.61L19.14,12.94z M12,15.6c-1.98,0-3.6-1.62-3.6-3.6c0-1.98,1.62-3.6,3.6-3.6"
        "c1.98,0,3.6,1.62,3.6,3.6C15.6,13.98,13.98,15.6,12,15.6z";

    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
        "<path fill='%1' d='%2'/></svg>")
        .arg(color.name(QColor::HexRgb), QString::fromLatin1(kGearPath));

    QSvgRenderer renderer(svg.toUtf8());
    constexpr int kBasePx = 64;
    // The toolbar's icon box is sized for the row height (the gear is its only
    // icon), not for the gear, so the Material artwork — which already fills
    // ~80% of its viewBox — looks oversized when it fills the whole box. Render
    // it into an inset so the visible gear reads at ~60% of the icon box, in
    // line with the adjacent menu text, without shrinking the box (= toolbar
    // height).
    constexpr qreal kInset = kBasePx * 0.135;
    QPixmap pixmap(kBasePx, kBasePx);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(kInset, kInset, kBasePx - 2.0 * kInset, kBasePx - 2.0 * kInset));
    painter.end();
    return QIcon(pixmap);
}

QIcon makeExportAccessIcon(const QColor& color)
{
    // Outline-list export glyph (downward arrow into a tray) for the Export
    // hub entry. Drawn at 14x14 to match the same icon-size budget as the
    // metadata badge; same Antialiased / round-cap stroke style as
    // makeOutlineCloseIcon so it sits visually next to the other entries
    // without looking out of place.
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Arrow shaft + head pointing down into the tray.
    painter.drawLine(QPointF(7.0, 2.2), QPointF(7.0, 8.4));
    painter.drawLine(QPointF(4.7, 6.2), QPointF(7.0, 8.6));
    painter.drawLine(QPointF(9.3, 6.2), QPointF(7.0, 8.6));

    // Tray — open-topped box catching the arrow.
    QPainterPath tray;
    tray.moveTo(2.8, 8.6);
    tray.lineTo(2.8, 11.4);
    tray.lineTo(11.2, 11.4);
    tray.lineTo(11.2, 8.6);
    painter.drawPath(tray);

    painter.end();
    return QIcon(pixmap);
}

QIcon makeToolboxAccessIcon(const QColor& toolboxColor, const QColor& gearColor)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen toolboxPen(toolboxColor, 1.8);
    toolboxPen.setCapStyle(Qt::RoundCap);
    toolboxPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(toolboxPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(4.0, 9.0, 11.8, 7.8), 2.2, 2.2);
    painter.drawRoundedRect(QRectF(6.6, 6.0, 6.4, 3.5), 1.3, 1.3);
    painter.drawLine(QPointF(4.2, 10.9), QPointF(15.6, 10.9));

    const QPointF gearCenter(17.0, 16.4);
    QPen gearPen(gearColor, 1.35);
    gearPen.setCapStyle(Qt::RoundCap);
    gearPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(gearPen);
    painter.drawEllipse(gearCenter, 2.35, 2.35);
    for (int index = 0; index < 8; ++index) {
        const qreal angle = (index * 45.0) * 0.017453292519943295;
        const QPointF inner(gearCenter.x() + qCos(angle) * 3.1, gearCenter.y() + qSin(angle) * 3.1);
        const QPointF outer(gearCenter.x() + qCos(angle) * 4.25, gearCenter.y() + qSin(angle) * 4.25);
        painter.drawLine(inner, outer);
    }

    painter.end();
    return QIcon(pixmap);
}

QIcon makeTransformMirrorLeftRightIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(QPointF(4.2, 10.0), QPointF(15.8, 10.0));
    painter.drawLine(QPointF(5.9, 8.3), QPointF(4.2, 10.0));
    painter.drawLine(QPointF(5.9, 11.7), QPointF(4.2, 10.0));
    painter.drawLine(QPointF(14.1, 8.3), QPointF(15.8, 10.0));
    painter.drawLine(QPointF(14.1, 11.7), QPointF(15.8, 10.0));
    painter.end();
    return QIcon(pixmap);
}

QIcon makeTransformMirrorUpDownIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawLine(QPointF(10.0, 4.2), QPointF(10.0, 15.8));
    painter.drawLine(QPointF(8.3, 5.9), QPointF(10.0, 4.2));
    painter.drawLine(QPointF(11.7, 5.9), QPointF(10.0, 4.2));
    painter.drawLine(QPointF(8.3, 14.1), QPointF(10.0, 15.8));
    painter.drawLine(QPointF(11.7, 14.1), QPointF(10.0, 15.8));
    painter.end();
    return QIcon(pixmap);
}

QIcon makeTransformRotate180Icon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawArc(QRectF(3.6, 3.6, 12.8, 12.8), 35 * 16, 150 * 16);
    painter.drawArc(QRectF(3.6, 3.6, 12.8, 12.8), 215 * 16, 150 * 16);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{
        QPointF(15.9, 6.8),
        QPointF(17.6, 4.9),
        QPointF(14.9, 4.6),
    });
    painter.drawPolygon(QPolygonF{
        QPointF(4.1, 13.2),
        QPointF(2.4, 15.1),
        QPointF(5.1, 15.4),
    });
    painter.end();
    return QIcon(pixmap);
}

QIcon makeTransformRotateCcw45Icon(const QColor& color)
{
    return QIcon(makeTransformRotate45Pixmap(color, false));
}

QIcon makeTransformRotateCw45Icon(const QColor& color)
{
    return QIcon(makeTransformRotate45Pixmap(color, true));
}

QString modernScrollBarStyle()
{
    return UiTheme::scrollBarStyleSheet();
}

void styleRoundedMenu(QMenu& menu)
{
    UiTheme::styleRoundedMenu(menu);
}

qint64 fileLastModifiedMs(const QFileInfo& fileInfo)
{
    return fileInfo.exists() ? fileInfo.lastModified().toMSecsSinceEpoch() : -1;
}

double probeAudioDurationSeconds(const QString& trackPath)
{
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath)) {
        return 0.0;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    if (miacode::audio_io::decoderInitFile(trackPath, &config, &decoder) != MA_SUCCESS) {
        return 0.0;
    }

    ma_uint64 totalFrames = 0;
    const bool ok = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS
        && totalFrames > 0;
    ma_decoder_uninit(&decoder);
    if (!ok) {
        return 0.0;
    }

    return static_cast<double>(totalFrames) / 48000.0;
}

}  // namespace miacode::mainwindow::shared
