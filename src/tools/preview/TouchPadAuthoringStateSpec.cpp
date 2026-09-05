#include "core/scene/TouchPadAuthoringState.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined"
#endif

namespace {

QString readSource(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QString hovered;
    QString pressed;
    using namespace miacode::preview::scene;

    if (!touchPadAuthoringMouseButtonSupported(Qt::LeftButton)
        || !touchPadAuthoringMouseButtonSupported(Qt::RightButton)
        || touchPadAuthoringMouseButtonSupported(Qt::MiddleButton)
        || touchPadAuthoringSeparator(Qt::LeftButton, Qt::ControlModifier) != QLatin1Char('/')
        || touchPadAuthoringSeparator(
               Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier) != QLatin1Char('`')
        || touchPadAuthoringSeparator(Qt::RightButton, Qt::ControlModifier) != QLatin1Char(',')) {
        err << "FAIL: left/right/modifier routing should select slash/backtick/comma\n";
        return 1;
    }

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
    const QString sceneSource = readSource(QStringLiteral("src/preview/quick_scene/PreviewQuickSceneRoot.cpp"));
    const QString authoringStateSource = readSource(QStringLiteral("src/core/scene/TouchPadAuthoringState.h"));
    if (!sceneSource.contains(QStringLiteral("setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton)"))
        || !sceneSource.contains(QStringLiteral("touchPadAuthoringMouseButtonSupported(event->button())"))
        || !sceneSource.contains(QStringLiteral("touchPadAuthoringSeparator("))
        || !sceneSource.contains(QStringLiteral("event->button(), event->modifiers()"))
        || !sceneSource.contains(QStringLiteral("touchPadAuthoringPressedButton_ = event->button()"))
        || !sceneSource.contains(QStringLiteral("event->button() == touchPadAuthoringPressedButton_"))
        || !authoringStateSource.contains(QStringLiteral("Qt::ShiftModifier"))) {
        err << "FAIL: scene source should route left/right gestures and Ctrl+Shift pseudo-double\n";
        return 1;
    }
    // The gutter is QML now; the assertion is the same one, re-pointed. A
    // bookmarked line is marked by colouring the row, not by underlining it.
    const QString gutterSource = readSource(QStringLiteral("src/app/qml_ui/editor/LineNumberGutter.qml"));
    if (!gutterSource.contains(QStringLiteral("Theme.colors.accent.primary"))
        || !gutterSource.contains(QStringLiteral("bookmarkedLines"))
        || gutterSource.contains(QStringLiteral("ctx.lineTo"))
        || gutterSource.contains(QStringLiteral("ctx.stroke"))) {
        err << "FAIL: bookmark gutter should keep color cues without underline drawing\n";
        return 1;
    }
    QTextStream(stdout) << "touch_pad_authoring_state_spec ok\n";
    return 0;
}
