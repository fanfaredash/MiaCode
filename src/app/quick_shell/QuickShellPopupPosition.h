#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

namespace miacode::quick_shell {

inline QRect actionScreenRectFromSurface(
    const QPoint& surfaceGlobalOrigin,
    const QRect& actionSurfaceRect)
{
    return actionSurfaceRect.translated(surfaceGlobalOrigin);
}

// Returns the top-left screen position for a first-level menu popup. The popup
// normally opens below its menu-bar action, flips above it when the screen's
// lower edge would be crossed, and is finally clamped to the available screen.
inline QPoint popupTopLeftForAction(
    const QRect& actionScreenRect,
    const QSize& popupSize,
    const QRect& availableScreenRect)
{
    QPoint result(actionScreenRect.left(), actionScreenRect.bottom() + 1);
    if (!availableScreenRect.isValid()) {
        return result;
    }

    const int popupWidth = qMax(0, popupSize.width());
    const int popupHeight = qMax(0, popupSize.height());
    const int availableRightExclusive = availableScreenRect.right() + 1;
    const int availableBottomExclusive = availableScreenRect.bottom() + 1;

    if (result.y() + popupHeight > availableBottomExclusive) {
        result.setY(actionScreenRect.top() - popupHeight);
    }

    const int maxX = qMax(availableScreenRect.left(), availableRightExclusive - popupWidth);
    const int maxY = qMax(availableScreenRect.top(), availableBottomExclusive - popupHeight);
    result.setX(qBound(availableScreenRect.left(), result.x(), maxX));
    result.setY(qBound(availableScreenRect.top(), result.y(), maxY));
    return result;
}

}  // namespace miacode::quick_shell
