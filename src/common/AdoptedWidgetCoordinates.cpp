#include "common/AdoptedWidgetCoordinates.h"

#include <QVariant>
#include <QWidget>
#include <QWindow>

namespace {

constexpr auto kAdoptedSurfaceWindowProperty = "miacodeAdoptedSurfaceWindow";

} // namespace

namespace miacode::ui {

void bindAdoptedSurfaceWindow(QWidget* surface, QWindow* adoptedWindow)
{
    if (surface == nullptr) {
        return;
    }
    surface->setProperty(
        kAdoptedSurfaceWindowProperty,
        QVariant::fromValue(static_cast<QObject*>(adoptedWindow)));
    if (adoptedWindow != nullptr) {
        QObject::connect(adoptedWindow, &QObject::destroyed, surface, [surface](QObject* object) {
            if (surface->property(kAdoptedSurfaceWindowProperty).value<QObject*>() == object) {
                surface->setProperty(kAdoptedSurfaceWindowProperty, QVariant());
            }
        });
    }
}

AdoptedWidgetCoordinateRoute adoptedWidgetCoordinateRoute(
    const QWidget* widget,
    const QPoint& localPoint)
{
    if (widget == nullptr) {
        return {};
    }
    for (const QWidget* ancestor = widget; ancestor != nullptr; ancestor = ancestor->parentWidget()) {
        auto* adoptedWindow = qobject_cast<QWindow*>(
            ancestor->property(kAdoptedSurfaceWindowProperty).value<QObject*>());
        if (adoptedWindow != nullptr) {
            return {adoptedWindow, widget->mapTo(ancestor, localPoint)};
        }
    }
    return {};
}

QPoint mapWidgetPointToGlobal(const QWidget* widget, const QPoint& localPoint)
{
    if (widget == nullptr) {
        return {};
    }
    const AdoptedWidgetCoordinateRoute route = adoptedWidgetCoordinateRoute(widget, localPoint);
    return route.window != nullptr
        ? route.window->mapToGlobal(route.surfacePoint)
        : widget->mapToGlobal(localPoint);
}

} // namespace miacode::ui
