#include "BusySpinner.h"

#include <cmath>

#include <QPainter>
#include <QTimer>

namespace miacode::ui {

namespace {
constexpr int kDotCount = 12;
constexpr int kTickIntervalMs = 1000 / 30;  // ~30 fps
constexpr int kDegreesPerTick = 30;
constexpr double kTwoPi = 6.283185307179586;
}  // namespace

BusySpinner::BusySpinner(QWidget* parent)
    : QWidget(parent)
    , color_(QColor(0x6C, 0x5C, 0xE7))  // app accent; overridable via setColor()
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(16, 16);
    hide();

    timer_ = new QTimer(this);
    timer_->setInterval(kTickIntervalMs);
    connect(timer_, &QTimer::timeout, this, [this]() {
        angle_ = (angle_ + kDegreesPerTick) % 360;
        update();
    });
}

void BusySpinner::setColor(const QColor& color)
{
    if (color.isValid() && color != color_) {
        color_ = color;
        update();
    }
}

void BusySpinner::start()
{
    if (!timer_->isActive()) {
        timer_->start();
    }
    show();
    raise();
    update();
}

void BusySpinner::stop()
{
    timer_->stop();
    hide();
}

bool BusySpinner::isActive() const
{
    return timer_->isActive();
}

void BusySpinner::advance()
{
    if (!timer_->isActive()) {
        return;
    }
    angle_ = (angle_ + kDegreesPerTick) % 360;
    // repaint() (not update()) so the new frame is drawn now, without waiting for
    // the blocked event loop to deliver a paint event.
    repaint();
}

void BusySpinner::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds(rect());
    const qreal radius = qMin(bounds.width(), bounds.height()) / 2.0;
    const qreal dotRadius = radius * 0.18;
    const qreal orbit = radius - dotRadius - 0.5;

    painter.translate(bounds.center());
    painter.rotate(angle_);
    painter.setPen(Qt::NoPen);

    for (int i = 0; i < kDotCount; ++i) {
        const qreal t = static_cast<qreal>(i) / kDotCount;
        QColor c = color_;
        // Leading dot is brightest; the trail fades out behind it.
        c.setAlphaF(0.15 + 0.85 * t);
        painter.setBrush(c);
        const qreal a = t * kTwoPi;
        const QPointF p(orbit * std::cos(a), orbit * std::sin(a));
        painter.drawEllipse(p, dotRadius, dotRadius);
    }
}

}  // namespace miacode::ui
