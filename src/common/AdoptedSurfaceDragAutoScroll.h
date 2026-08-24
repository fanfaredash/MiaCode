#pragma once

#include <QPoint>
#include <QRect>

class QAbstractScrollArea;

namespace miacode::ui {

// Drag-selection autoscroll for scroll areas hosted on an adopted QuickShell
// surface.
//
// Qt drives its own drag autoscroll from QCursor::pos() routed through
// QWidget::mapFromGlobal() — the mapping that, on macOS, still resolves through
// the neutralized orphan NSPanel once QML has adopted the surface's NSView (the
// same defect AdoptedWidgetCoordinates.h works around for popups). Incoming
// events carry correct widget-local coordinates, so a drag looks fine until the
// pointer leaves the viewport and Qt's timer arms: from then on every tick
// synthesizes a mouse move on the wrong line and fights the real gesture, and
// the selection strobes. Viewport margins make that easy to hit — a text
// editor's line-number gutter is outside the viewport, so dragging left far
// enough to grab whole lines is enough.
//
// installAdoptedSurfaceDragAutoScroll() takes the gesture over: out-of-viewport
// moves are swallowed and re-sent clamped to the viewport (which is also what
// stops Qt from arming its timer), and the scrolling is stepped from a timer fed
// by real event coordinates, so dragging past an edge still scrolls and keeps
// selecting. No-op off macOS — every other platform maps globals correctly and
// keeps Qt's own path.

struct DragAutoScrollStep {
    // `position` pulled back inside the viewport.
    QPoint clampedPosition;
    int horizontalStep = 0;  // -1 left, 0 none, +1 right
    int verticalStep = 0;    // -1 up, 0 none, +1 down
    // 0 when the pointer is inside the viewport (no autoscroll); otherwise the
    // tick interval, accelerating with the overshoot like Qt's own cadence.
    int intervalMs = 0;
};

// Pure geometry, compiled on every platform so it stays unit-testable
// (plain_code_editor_spec).
DragAutoScrollStep planDragAutoScrollStep(const QRect& viewportRect, const QPoint& position);

// Idempotent; the installed helper lives as a child of `area`.
void installAdoptedSurfaceDragAutoScroll(QAbstractScrollArea* area);

} // namespace miacode::ui
