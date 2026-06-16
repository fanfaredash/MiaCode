#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QWidget>

class QTimer;

namespace miacode::ui {

// A small indeterminate circular "busy" spinner — a ring of fading dots that
// rotates while active. Used as a transient overlay that signals a
// short-but-noticeable operation is in progress (e.g. switching to the export
// page, whose embedded video panel is expensive to build).
//
// The rotation angle is derived from elapsed wall-clock time, so the ring spins
// at a constant visual speed no matter how irregularly it is repainted. That
// matters because the slow operation blocks the GUI thread (the idle timer never
// fires): callers drive frames synchronously via advance() / the free busyTick()
// hook, and time-based angles keep those irregular frames looking smooth.
//
// It is mouse-transparent and hidden in its resting state, so it never disturbs
// the widget it floats over. Parent it over the target, move() it into place,
// then call start()/stop() around the work.
class BusySpinner : public QWidget
{
    Q_OBJECT

public:
    explicit BusySpinner(QWidget* parent = nullptr);

    // Tint of the rotating dots. Defaults to the app accent colour.
    void setColor(const QColor& color);

    // Show the spinner and run its rotation animation. Idempotent.
    void start();

    // Stop the animation and hide the spinner. Idempotent.
    void stop();

    bool isActive() const;

    // Repaint *synchronously* at the current (time-derived) angle. Use this to
    // keep the spinner visibly rotating from inside a long synchronous operation
    // that blocks the GUI thread (so the timer never fires) — call it at points
    // spread through that work. No-op while the spinner is stopped.
    void advance();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal currentAngle() const;

    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    QColor color_;
};

// Nudge whichever BusySpinner is currently animating one frame (no-op if none).
// A dependency-free hook so deep, decoupled code (e.g. the export dialog
// constructor) can keep the active spinner rotating without referencing it.
void busyTick();

}  // namespace miacode::ui
