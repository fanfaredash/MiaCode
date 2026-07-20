#pragma once

#include <QColor>
#include <QString>

namespace miacode::preview::scene {

inline bool touchPadAuthoringMouseButtonSupported(Qt::MouseButton button)
{
    return button == Qt::LeftButton || button == Qt::RightButton;
}

inline bool touchPadAuthoringUsesBacktickSeparator(Qt::MouseButton button)
{
    return button == Qt::RightButton;
}

inline QString normalizedTouchPadAuthoringToken(const QString& pad)
{
    return pad.trimmed().toUpper();
}

inline bool beginTouchPadAuthoringGesture(QString* hovered, QString* pressed, const QString& pad)
{
    const QString normalized = normalizedTouchPadAuthoringToken(pad);
    if (hovered == nullptr || pressed == nullptr || normalized.isEmpty()) {
        return false;
    }
    *hovered = normalized;
    *pressed = normalized;
    return true;
}

inline void moveTouchPadAuthoringGesture(QString* hovered, QString* pressed, const QString& pad)
{
    if (hovered == nullptr || pressed == nullptr) {
        return;
    }
    const QString normalized = normalizedTouchPadAuthoringToken(pad);
    *hovered = normalized;
    if (!pressed->isEmpty() && *pressed != normalized) {
        pressed->clear();
    }
}

inline QString finishTouchPadAuthoringGesture(QString* hovered, QString* pressed, const QString& pad)
{
    if (hovered == nullptr || pressed == nullptr) {
        return QString();
    }
    const QString normalized = normalizedTouchPadAuthoringToken(pad);
    const bool matches = !pressed->isEmpty() && *pressed == normalized;
    *hovered = normalized;
    pressed->clear();
    return matches ? normalized : QString();
}

struct TouchPadAuthoringVisualStyle {
    QColor fill;
    QColor stroke;
};

inline TouchPadAuthoringVisualStyle touchPadAuthoringVisualStyle(bool pressed)
{
    return pressed
        ? TouchPadAuthoringVisualStyle{QColor(210, 225, 255, 112), QColor(180, 210, 255, 176)}
        : TouchPadAuthoringVisualStyle{QColor(255, 255, 255, 58), QColor(255, 255, 255, 96)};
}

} // namespace miacode::preview::scene
