#pragma once

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <Qt>

class QWheelEvent;

namespace miacode::input_shortcut {

enum class WheelDirection {
    None,
    Up,
    Down,
};

struct Gesture {
    QString canonicalText;
    QKeySequence keySequence;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    WheelDirection wheelDirection = WheelDirection::None;

    bool isValid() const;
    bool isWheel() const;
};

Gesture parseGesture(const QString& text);
QString normalizeGestureText(const QString& text);
QString gestureDisplayText(const QString& canonicalText);
QStringList normalizeGestureTexts(const QStringList& texts);
QList<QKeySequence> keyboardSequencesFromGestureTexts(const QStringList& texts);
QString wheelGestureText(Qt::KeyboardModifiers modifiers, WheelDirection direction);
WheelDirection wheelDirectionFromEvent(const QWheelEvent* event);
bool wheelEventMatchesGesture(const QWheelEvent* event, const QString& canonicalText);
bool wheelEventMatchesAnyGesture(const QWheelEvent* event, const QStringList& canonicalTexts);

}  // namespace miacode::input_shortcut
