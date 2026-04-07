#include "LegacyChartEditorSurface.h"

LegacyChartEditorSurface::LegacyChartEditorSurface(QObject* parent)
    : QObject(parent)
{
}

QWindow* LegacyChartEditorSurface::window() const
{
    return window_;
}

void LegacyChartEditorSurface::setWindow(QWindow* window)
{
    if (window_ == window) {
        return;
    }
    window_ = window;
    emit windowChanged();
}
