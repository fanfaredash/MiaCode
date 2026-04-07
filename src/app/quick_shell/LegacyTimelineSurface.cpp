#include "LegacyTimelineSurface.h"

LegacyTimelineSurface::LegacyTimelineSurface(QObject* parent)
    : QObject(parent)
{
}

QWindow* LegacyTimelineSurface::window() const
{
    return window_;
}

void LegacyTimelineSurface::setWindow(QWindow* window)
{
    if (window_ == window) {
        return;
    }
    window_ = window;
    emit windowChanged();
}
