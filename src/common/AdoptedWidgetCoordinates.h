#pragma once

#include <QPoint>

class QWidget;
class QWindow;

namespace miacode::ui {

struct AdoptedWidgetCoordinateRoute {
    QWindow* window = nullptr;
    QPoint surfacePoint;
};

void bindAdoptedSurfaceWindow(QWidget* surface, QWindow* adoptedWindow);

AdoptedWidgetCoordinateRoute adoptedWidgetCoordinateRoute(
    const QWidget* widget,
    const QPoint& localPoint);

QPoint mapWidgetPointToGlobal(const QWidget* widget, const QPoint& localPoint);

// The inverse of mapWidgetPointToGlobal().  On an adopted QuickShell surface,
// QWidget::mapFromGlobal() still follows the orphan native panel on macOS;
// resolve through the adopted QWindow instead.
QPoint mapGlobalPointToWidget(const QWidget* widget, const QPoint& globalPoint);

} // namespace miacode::ui
