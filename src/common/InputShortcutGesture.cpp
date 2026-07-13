#include "common/InputShortcutGesture.h"

#include <QWheelEvent>

namespace miacode::input_shortcut {
namespace {

Qt::KeyboardModifiers shortcutModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

QString modifierText(Qt::KeyboardModifiers modifiers)
{
    QStringList parts;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        parts.append(QStringLiteral("Ctrl"));
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        parts.append(QStringLiteral("Shift"));
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        parts.append(QStringLiteral("Alt"));
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        parts.append(QStringLiteral("Meta"));
    }
    return parts.join(QStringLiteral("+"));
}

QString wheelDirectionToken(WheelDirection direction)
{
    switch (direction) {
    case WheelDirection::Up:
        return QStringLiteral("WheelUp");
    case WheelDirection::Down:
        return QStringLiteral("WheelDown");
    case WheelDirection::None:
        break;
    }
    return QString();
}

WheelDirection parseWheelDirectionToken(QString token)
{
    token = token.trimmed().toLower();
    token.remove(QLatin1Char(' '));
    if (token == QStringLiteral("wheelup")) {
        return WheelDirection::Up;
    }
    if (token == QStringLiteral("wheeldown")) {
        return WheelDirection::Down;
    }
    return WheelDirection::None;
}

bool appendModifierToken(const QString& token, Qt::KeyboardModifiers* modifiers)
{
    if (modifiers == nullptr) {
        return false;
    }
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("ctrl")) {
        *modifiers |= Qt::ControlModifier;
        return true;
    }
    if (normalized == QStringLiteral("shift")) {
        *modifiers |= Qt::ShiftModifier;
        return true;
    }
    if (normalized == QStringLiteral("alt")) {
        *modifiers |= Qt::AltModifier;
        return true;
    }
    if (normalized == QStringLiteral("meta")) {
        *modifiers |= Qt::MetaModifier;
        return true;
    }
    return false;
}

Gesture parseWheelGesture(const QString& text)
{
    const QStringList parts = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }

    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    WheelDirection direction = WheelDirection::None;
    for (const QString& part : parts) {
        const WheelDirection maybeDirection = parseWheelDirectionToken(part);
        if (maybeDirection != WheelDirection::None) {
            if (direction != WheelDirection::None) {
                return {};
            }
            direction = maybeDirection;
            continue;
        }
        if (!appendModifierToken(part, &modifiers)) {
            return {};
        }
    }
    if (direction == WheelDirection::None) {
        return {};
    }

    Gesture gesture;
    gesture.modifiers = modifiers;
    gesture.wheelDirection = direction;
    gesture.canonicalText = wheelGestureText(modifiers, direction);
    return gesture;
}

QKeySequence normalizeKeySequence(const QKeySequence& sequence)
{
    const QString portable = sequence.toString(QKeySequence::PortableText);
    if (portable == QStringLiteral("Ctrl+Shift++")) {
        return QKeySequence(QStringLiteral("Ctrl+Shift+="));
    }
    if (portable == QStringLiteral("Ctrl+Shift+_")) {
        return QKeySequence(QStringLiteral("Ctrl+Shift+-"));
    }
    return sequence;
}

}  // namespace

bool Gesture::isValid() const
{
    return !canonicalText.isEmpty();
}

bool Gesture::isWheel() const
{
    return wheelDirection != WheelDirection::None;
}

Gesture parseGesture(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    Gesture wheel = parseWheelGesture(trimmed);
    if (wheel.isValid()) {
        return wheel;
    }

    const QKeySequence sequence = normalizeKeySequence(QKeySequence(trimmed));
    if (sequence.isEmpty()) {
        return {};
    }

    Gesture gesture;
    gesture.keySequence = sequence;
    gesture.canonicalText = sequence.toString(QKeySequence::PortableText);
    return gesture;
}

QString normalizeGestureText(const QString& text)
{
    return parseGesture(text).canonicalText;
}

QString gestureDisplayText(const QString& canonicalText)
{
    const Gesture gesture = parseGesture(canonicalText);
    if (!gesture.isValid()) {
        return QString();
    }
    if (gesture.isWheel()) {
        const QString modifiers = modifierText(gesture.modifiers);
        const QString wheel = gesture.wheelDirection == WheelDirection::Up
            ? QStringLiteral("Wheel Up")
            : QStringLiteral("Wheel Down");
        return modifiers.isEmpty() ? wheel : QStringLiteral("%1+%2").arg(modifiers, wheel);
    }
    return gesture.keySequence.toString(QKeySequence::NativeText);
}

QStringList normalizeGestureTexts(const QStringList& texts)
{
    QStringList normalized;
    for (const QString& text : texts) {
        const QString canonical = normalizeGestureText(text);
        if (!canonical.isEmpty() && !normalized.contains(canonical)) {
            normalized.append(canonical);
        }
    }
    return normalized;
}

QList<QKeySequence> keyboardSequencesFromGestureTexts(const QStringList& texts)
{
    QList<QKeySequence> sequences;
    for (const QString& text : texts) {
        const Gesture gesture = parseGesture(text);
        if (!gesture.keySequence.isEmpty() && !sequences.contains(gesture.keySequence)) {
            sequences.append(gesture.keySequence);
        }
    }
    return sequences;
}

QString wheelGestureText(Qt::KeyboardModifiers modifiers, WheelDirection direction)
{
    const QString directionToken = wheelDirectionToken(direction);
    if (directionToken.isEmpty()) {
        return QString();
    }
    const QString modifiersText = modifierText(shortcutModifiers(modifiers));
    return modifiersText.isEmpty() ? directionToken : QStringLiteral("%1+%2").arg(modifiersText, directionToken);
}

WheelDirection wheelDirectionFromEvent(const QWheelEvent* event)
{
    if (event == nullptr) {
        return WheelDirection::None;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        delta = event->pixelDelta().x();
    }
    if (delta > 0) {
        return WheelDirection::Up;
    }
    if (delta < 0) {
        return WheelDirection::Down;
    }
    return WheelDirection::None;
}

bool wheelEventMatchesGesture(const QWheelEvent* event, const QString& canonicalText)
{
    const Gesture gesture = parseGesture(canonicalText);
    if (event == nullptr || !gesture.isWheel()) {
        return false;
    }
    return wheelDirectionFromEvent(event) == gesture.wheelDirection
        && shortcutModifiers(event->modifiers()) == gesture.modifiers;
}

bool wheelEventMatchesAnyGesture(const QWheelEvent* event, const QStringList& canonicalTexts)
{
    for (const QString& canonicalText : canonicalTexts) {
        if (wheelEventMatchesGesture(event, canonicalText)) {
            return true;
        }
    }
    return false;
}

}  // namespace miacode::input_shortcut
