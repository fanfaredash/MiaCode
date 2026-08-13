#include "ChartDropOverlay.h"

#include "UiText.h"
#include "UiTheme.h"

#include <QPainter>
#include <QPainterPath>
#include <QWindow>

namespace {

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(qBound(0, alpha, 255));
    return color;
}

QPainterPath musicFileIconPath(const QRectF& bounds)
{
    const qreal fold = bounds.width() * 0.23;
    const qreal left = bounds.left();
    const qreal right = bounds.right();
    const qreal top = bounds.top();
    const qreal bottom = bounds.bottom();
    QPainterPath path;
    path.moveTo(left + 5.0, top);
    path.lineTo(right - fold, top);
    path.lineTo(right, top + fold);
    path.lineTo(right, bottom - 5.0);
    path.quadTo(right, bottom, right - 5.0, bottom);
    path.lineTo(left + 5.0, bottom);
    path.quadTo(left, bottom, left, bottom - 5.0);
    path.lineTo(left, top + 5.0);
    path.quadTo(left, top, left + 5.0, top);
    path.closeSubpath();

    QPainterPath corner;
    corner.moveTo(right - fold, top);
    corner.lineTo(right - fold, top + fold);
    corner.lineTo(right, top + fold);
    path.addPath(corner);
    return path;
}

} // namespace

ChartDropOverlay::ChartDropOverlay()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint
                  | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    // The overlay is a real native HWND. Windows OLE drag/drop may resolve the
    // final cursor target to this window even though ordinary mouse input is
    // transparent. Register it as a drop site so releasing over the visible
    // mask still reaches the application-level event filter.
    setAcceptDrops(true);
    setWindowFlag(Qt::WindowStaysOnTopHint, false);
}

void ChartDropOverlay::showForWindow(QWindow* target)
{
    if (target == nullptr || !target->isVisible() || target->visibility() == QWindow::Minimized) {
        hideOverlay();
        return;
    }
    const QRect targetGeometry = target->frameGeometry();
    if (geometry() != targetGeometry) {
        setGeometry(targetGeometry);
    }
    winId();
    if (windowHandle() != nullptr && windowHandle()->transientParent() != target) {
        windowHandle()->setTransientParent(target);
    }
    if (!isVisible()) {
        show();
    }
}

void ChartDropOverlay::hideOverlay()
{
    hide();
}

void ChartDropOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                           | QPainter::SmoothPixmapTransform, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const UiTheme::Colors& colors = UiTheme::colors();
    const QColor accent = colors.accent;
    const QColor text = colors.textPrimary;
    const QColor secondaryText = colors.textSecondary;
    const QColor veil = colors.dark ? withAlpha(accent, 42) : withAlpha(accent, 26);
    painter.fillRect(rect(), veil);

    const qreal cardWidth = qMin<qreal>(480.0, qMax<qreal>(240.0, width() - 48.0));
    const qreal cardHeight = qMin<qreal>(214.0, qMax<qreal>(164.0, height() - 48.0));
    const QRectF card(QPointF((width() - cardWidth) / 2.0, (height() - cardHeight) / 2.0),
                      QSizeF(cardWidth, cardHeight));
    QPainterPath path;
    path.addRoundedRect(card, 16, 16);
    painter.setPen(QPen(withAlpha(accent, colors.dark ? 225 : 205), 1.5));
    painter.setBrush(withAlpha(colors.cardBg, colors.dark ? 246 : 250));
    painter.drawPath(path);

    const qreal iconSize = qMin<qreal>(48.0, card.height() * 0.27);
    const QRectF iconBounds(card.center().x() - iconSize / 2.0,
                            card.top() + 22.0, iconSize, iconSize);
    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(accent, colors.dark ? 34 : 24));
    painter.drawEllipse(iconBounds.adjusted(-12.0, -12.0, 12.0, 12.0));
    painter.setPen(QPen(withAlpha(accent, 225), 1.7));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(musicFileIconPath(iconBounds));
    painter.setPen(QPen(accent, 1.7));
    const QPointF noteStem(iconBounds.center().x() + iconSize * 0.16,
                           iconBounds.top() + iconSize * 0.39);
    painter.drawLine(noteStem, QPointF(noteStem.x(), iconBounds.bottom() - 9.0));
    painter.drawLine(QPointF(noteStem.x(), iconBounds.top() + iconSize * 0.39),
                     QPointF(iconBounds.right() - 8.0, iconBounds.top() + iconSize * 0.32));
    painter.drawEllipse(QPointF(noteStem.x() - 4.0, iconBounds.bottom() - 7.0), 3.4, 2.5);

    painter.setPen(text);
    QFont font = painter.font();
    font.setPointSizeF(qBound(11.0, card.width() / 34.0, 15.0));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.drawText(card.adjusted(18, iconSize + 38, -18, -54), Qt::AlignCenter,
        UiText::text(QStringLiteral("drop_chart.create_chart_file")));
    font.setPointSizeF(qBound(9.0, card.width() / 48.0, 11.0));
    font.setWeight(QFont::Normal);
    font.setLetterSpacing(QFont::PercentageSpacing, 102.0);
    painter.setFont(font);
    painter.setPen(secondaryText);
    painter.drawText(card.adjusted(18, card.height() - 50, -18, -20), Qt::AlignCenter,
        UiText::text(QStringLiteral("drop_chart.drop_hint")));
}
