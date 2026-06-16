#include "BusySpinner.h"

#include <cmath>

#include <QPainter>
#include <QTimer>

namespace miacode::ui {

namespace {
constexpr int kDotCount = 12;
constexpr int kIdleIntervalMs = 1000 / 60;       // ~60 fps when the loop is free
constexpr double kTwoPi = 6.283185307179586;
constexpr double kDegreesPerMs = 360.0 / 900.0;  // one rotation every ~0.9 s

// The spinner currently animating, if any. Only one is ever active at a time
// (page switches are mutually exclusive), so a single pointer suffices for the
// dependency-free busyTick() hook.
BusySpinner* g_activeSpinner = nullptr;
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
    timer_->setInterval(kIdleIntervalMs);
    connect(timer_, &QTimer::timeout, this, [this]() { update(); });
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
        clock_.restart();
        timer_->start();
    }
    g_activeSpinner = this;
    show();
    raise();
    update();
}

void BusySpinner::stop()
{
    timer_->stop();
    if (g_activeSpinner == this) {
        g_activeSpinner = nullptr;
    }
    hide();
}

bool BusySpinner::isActive() const
{
    return timer_->isActive();
}

qreal BusySpinner::currentAngle() const
{
    if (!clock_.isValid()) {
        return 0.0;
    }
    return std::fmod(static_cast<double>(clock_.elapsed()) * kDegreesPerMs, 360.0);
}

void BusySpinner::advance()
{
    if (!timer_->isActive()) {
        return;
    }
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
    painter.rotate(currentAngle());
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

void busyTick()
{
    if (g_activeSpinner != nullptr) {
        g_activeSpinner->advance();
    }
}

}  // namespace miacode::ui
