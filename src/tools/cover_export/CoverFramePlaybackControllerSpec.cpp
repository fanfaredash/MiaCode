#include "tools/cover_export/CoverFramePlaybackController.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cmath>

using miacode::cover_export::CoverFramePlaybackController;

namespace {

bool require(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) {
        out << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < 1e-9;
}

bool testPlaybackAndEnd(QTextStream& out)
{
    CoverFramePlaybackController controller;
    bool reachedEnd = false;
    QObject::connect(&controller, &CoverFramePlaybackController::reachedEnd,
                     [&reachedEnd] { reachedEnd = true; });

    controller.setDuration(1.0);
    controller.setSeconds(0.25);
    controller.play();
    if (!require(controller.playing(), QStringLiteral("play starts"), out)) return false;
    controller.advanceForElapsed(0.5);
    if (!require(near(controller.seconds(), 0.75), QStringLiteral("play advances by elapsed delta"), out)) return false;
    controller.pause();
    controller.advanceForElapsed(0.2);
    if (!require(near(controller.seconds(), 0.75), QStringLiteral("pause stops advancement"), out)) return false;

    controller.play();
    controller.advanceForElapsed(0.5);
    if (!require(near(controller.seconds(), 1.0), QStringLiteral("play clamps to duration"), out)) return false;
    if (!require(!controller.playing() && reachedEnd, QStringLiteral("reaching duration stops and signals"), out)) return false;

    controller.toggle();
    if (!require(controller.playing() && near(controller.seconds(), 0.0),
                 QStringLiteral("toggle at end restarts from zero"), out)) return false;
    controller.pause();
    return true;
}

bool testSingleAndHeldSeek(QTextStream& out)
{
    CoverFramePlaybackController controller;
    controller.setDuration(10.0);
    controller.setSeconds(5.0);

    controller.beginKeySeek(1);
    if (!require(near(controller.seconds(), 5.0 + 1.0 / 120.0),
                 QStringLiteral("one key press moves one fixed small step"), out)) return false;
    controller.advanceForElapsed(0.1);
    if (!require(near(controller.seconds(), 5.0 + 1.0 / 120.0),
                 QStringLiteral("hold threshold prevents a second tap step"), out)) return false;
    controller.advanceForElapsed(0.2);
    if (!require(controller.seconds() > 5.0 + 1.0 / 120.0,
                 QStringLiteral("held key starts continuous seek"), out)) return false;
    const double afterHold = controller.seconds();
    controller.endKeySeek();
    controller.advanceForElapsed(1.0);
    if (!require(near(controller.seconds(), afterHold),
                 QStringLiteral("key release stops continuous seek"), out)) return false;

    controller.beginKeySeek(-1);
    controller.cancelInput();
    const double afterCancel = controller.seconds();
    controller.advanceForElapsed(1.0);
    if (!require(near(controller.seconds(), afterCancel),
                 QStringLiteral("cancel stops continuous seek"), out)) return false;

    controller.setSeconds(0.0);
    controller.beginKeySeek(-1);
    if (!require(near(controller.seconds(), 0.0), QStringLiteral("seek clamps at zero"), out)) return false;
    controller.endKeySeek();
    return true;
}

bool testInvalidDuration(QTextStream& out)
{
    CoverFramePlaybackController controller;
    controller.setDuration(0.0);
    controller.play();
    if (!require(!controller.playing() && near(controller.seconds(), 0.0),
                 QStringLiteral("zero duration cannot play"), out)) return false;
    controller.setDuration(2.0);
    controller.setSeconds(20.0);
    return require(near(controller.seconds(), 2.0), QStringLiteral("seconds clamps after duration change"), out);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    bool ok = true;
    ok = testPlaybackAndEnd(out) && ok;
    ok = testSingleAndHeldSeek(out) && ok;
    ok = testInvalidDuration(out) && ok;
    if (ok) {
        out << "CoverFramePlaybackController spec passed\n";
        return 0;
    }
    return 1;
}
