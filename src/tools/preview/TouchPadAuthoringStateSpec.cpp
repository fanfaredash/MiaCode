#include "core/scene/TouchPadAuthoringState.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QString hovered;
    QString pressed;
    using namespace miacode::preview::scene;

    if (!beginTouchPadAuthoringGesture(&hovered, &pressed, QStringLiteral("a1"))
        || hovered != QLatin1String("A1") || pressed != hovered) {
        err << "FAIL: press should normalize and store hovered/pressed pad\n";
        return 1;
    }
    moveTouchPadAuthoringGesture(&hovered, &pressed, QStringLiteral("B2"));
    if (hovered != QLatin1String("B2") || !pressed.isEmpty()) {
        err << "FAIL: moving away should cancel pressed state\n";
        return 1;
    }
    beginTouchPadAuthoringGesture(&hovered, &pressed, QStringLiteral("C"));
    if (finishTouchPadAuthoringGesture(&hovered, &pressed, QStringLiteral("C")) != QLatin1String("C")
        || !finishTouchPadAuthoringGesture(&hovered, &pressed, QStringLiteral("C")).isEmpty()) {
        err << "FAIL: matching release should complete once\n";
        return 1;
    }
    beginTouchPadAuthoringGesture(&hovered, &pressed, QStringLiteral("D1"));
    moveTouchPadAuthoringGesture(&hovered, &pressed, QString());
    if (!hovered.isEmpty() || !pressed.isEmpty()) {
        err << "FAIL: lifecycle clear should remove hover and cancel press\n";
        return 1;
    }
    const auto hoverStyle = touchPadAuthoringVisualStyle(false);
    const auto pressedStyle = touchPadAuthoringVisualStyle(true);
    if (pressedStyle.fill.alpha() <= hoverStyle.fill.alpha()
        || pressedStyle.stroke.alpha() <= hoverStyle.stroke.alpha()
        || pressedStyle.fill.lightness() >= hoverStyle.fill.lightness()
        || pressedStyle.stroke.lightness() >= hoverStyle.stroke.lightness()) {
        err << "FAIL: pressed visual should be darker/stronger than hover\n";
        return 1;
    }
    QTextStream(stdout) << "touch_pad_authoring_state_spec ok\n";
    return 0;
}
