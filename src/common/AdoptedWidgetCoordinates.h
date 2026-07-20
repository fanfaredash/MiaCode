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

} // namespace miacode::ui
