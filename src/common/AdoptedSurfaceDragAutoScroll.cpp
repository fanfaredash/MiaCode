#include "common/AdoptedSurfaceDragAutoScroll.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QBasicTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimerEvent>
#include <QVariant>
#include <QWidget>

namespace {

constexpr auto kInstalledProperty = "miacodeAdoptedSurfaceDragAutoScroll";

#ifdef Q_OS_MACOS
// Plain QObject (no Q_OBJECT): it only overrides virtuals, so it needs no moc.
class DragAutoScrollFilter final : public QObject
{
public:
    explicit DragAutoScrollFilter(QAbstractScrollArea* area)
        : QObject(area)
        , area_(area)
    {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (forwarding_ || area_ == nullptr || event == nullptr || watched != area_->viewport()) {
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::Hide) {
            stop();
            return QObject::eventFilter(watched, event);
        }
        if (event->type() != QEvent::MouseMove) {
            return QObject::eventFilter(watched, event);
        }

        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (!mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            stop();
            return QObject::eventFilter(watched, event);
        }

        const miacode::ui::DragAutoScrollStep step = miacode::ui::planDragAutoScrollStep(
            area_->viewport()->rect(), mouseEvent->position().toPoint());
        if (step.intervalMs == 0) {
            // Back inside the viewport — nothing to take over.
            stop();
            return QObject::eventFilter(watched, event);
        }

        viewportPosition_ = mouseEvent->position().toPoint();
        globalPosition_ = mouseEvent->globalPosition().toPoint();
        modifiers_ = mouseEvent->modifiers();
        if (!timer_.isActive()) {
            timer_.start(step.intervalMs, this);
        }
        forwardClamped(step.clampedPosition);
        return true;  // swallow the out-of-viewport move; the clamped copy stands in
    }

    void timerEvent(QTimerEvent* event) override
    {
        if (event == nullptr || event->timerId() != timer_.timerId()) {
            QObject::timerEvent(event);
            return;
        }
        // The drag ends on the release we filter above; this only catches the
        // viewport going away underneath a held drag (same robustness contract
        // as QTextEdit's own autoscroll timer, which stops on release too).
        if (area_ == nullptr || area_->viewport() == nullptr || !area_->viewport()->isVisible()) {
            stop();
            return;
        }

        const miacode::ui::DragAutoScrollStep step =
            miacode::ui::planDragAutoScrollStep(area_->viewport()->rect(), viewportPosition_);
        if (step.intervalMs == 0) {
            stop();
            return;
        }

        timer_.start(step.intervalMs, this);
        if (QScrollBar* vbar = area_->verticalScrollBar(); vbar != nullptr && step.verticalStep != 0) {
            vbar->triggerAction(
                step.verticalStep < 0
                    ? QAbstractSlider::SliderSingleStepSub
                    : QAbstractSlider::SliderSingleStepAdd);
        }
        if (QScrollBar* hbar = area_->horizontalScrollBar(); hbar != nullptr && step.horizontalStep != 0) {
            hbar->triggerAction(
                step.horizontalStep < 0
                    ? QAbstractSlider::SliderSingleStepSub
                    : QAbstractSlider::SliderSingleStepAdd);
        }
        // Re-extend the selection after the scroll: the clamped point now sits
        // over the freshly revealed text, which is what "drag past the edge to
        // keep selecting" means.
        forwardClamped(step.clampedPosition);
    }

private:
    void forwardClamped(const QPoint& clampedPosition)
    {
        QWidget* viewport = area_ != nullptr ? area_->viewport() : nullptr;
        if (viewport == nullptr) {
            return;
        }
        const QPointF localPosition(clampedPosition);
        const QPointF globalPosition(globalPosition_);
        QMouseEvent clampedEvent(
            QEvent::MouseMove,
            localPosition,
            globalPosition,
            globalPosition,
            Qt::NoButton,
            Qt::LeftButton,
            modifiers_
        );
        // The clamped copy travels back through this same filter; the guard
        // keeps it from being mistaken for the user returning to the viewport.
        forwarding_ = true;
        QApplication::sendEvent(viewport, &clampedEvent);
        forwarding_ = false;
    }

    void stop()
    {
        if (timer_.isActive()) {
            timer_.stop();
        }
    }

    QAbstractScrollArea* area_ = nullptr;
    QBasicTimer timer_;
    // The last position the platform actually reported for the held pointer, in
    // viewport coordinates, plus its global twin (forwarded verbatim so the
    // watched widget's own drag bookkeeping still sees the real gesture).
    QPoint viewportPosition_;
    QPoint globalPosition_;
    Qt::KeyboardModifiers modifiers_ = Qt::NoModifier;
    bool forwarding_ = false;
};
#endif  // Q_OS_MACOS

}  // namespace

namespace miacode::ui {

DragAutoScrollStep planDragAutoScrollStep(const QRect& viewportRect, const QPoint& position)
{
    DragAutoScrollStep step;
    step.clampedPosition = position;
    if (!viewportRect.isValid()) {
        return step;
    }

    step.clampedPosition = QPoint(
        qBound(viewportRect.left(), position.x(), viewportRect.right()),
        qBound(viewportRect.top(), position.y(), viewportRect.bottom()));
    if (step.clampedPosition == position) {
        return step;  // inside the viewport — nothing to scroll
    }

    if (position.x() < viewportRect.left()) {
        step.horizontalStep = -1;
    } else if (position.x() > viewportRect.right()) {
        step.horizontalStep = 1;
    }
    if (position.y() < viewportRect.top()) {
        step.verticalStep = -1;
    } else if (position.y() > viewportRect.bottom()) {
        step.verticalStep = 1;
    }

    // Same shape as QTextEdit's own cadence (4900 / overshoot²), but floored at
    // one 60 Hz frame: Qt's formula bottoms out near 1 ms, which is far more
    // scroll steps than a frame can show and was part of what made the
    // mis-targeted autoscroll read as a strobe rather than a glide.
    const int overshoot = qMax(
        qAbs(position.x() - step.clampedPosition.x()),
        qAbs(position.y() - step.clampedPosition.y()));
    const int scaled = qMax(7, overshoot);
    step.intervalMs = qBound(16, 4900 / (scaled * scaled), 100);
    return step;
}

void installAdoptedSurfaceDragAutoScroll(QAbstractScrollArea* area)
{
#ifdef Q_OS_MACOS
    if (area == nullptr || area->viewport() == nullptr) {
        return;
    }
    if (area->property(kInstalledProperty).toBool()) {
        return;
    }
    area->setProperty(kInstalledProperty, true);
    area->viewport()->installEventFilter(new DragAutoScrollFilter(area));
#else
    Q_UNUSED(area);
    Q_UNUSED(kInstalledProperty);
#endif
}

} // namespace miacode::ui
