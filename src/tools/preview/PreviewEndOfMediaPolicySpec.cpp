#include <QCoreApplication>
#include <QTextStream>

#include "core/video/PreviewEndOfMediaPolicy.h"

namespace {

using miacode::preview::video::classifyEndOfMedia;
using miacode::preview::video::EndOfMediaClass;
using miacode::preview::video::EndOfMediaFacts;
using miacode::preview::video::endOfMediaClassName;
using miacode::preview::video::endOfMediaShouldRecover;
using miacode::preview::video::kEndOfMediaNaturalSlackSeconds;

bool expect(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
        return false;
    }
    return true;
}

EndOfMediaFacts facts(double duration, double decoded, double interval, double expected)
{
    EndOfMediaFacts value;
    value.durationSeconds = duration;
    value.decodedSeconds = decoded;
    value.frameIntervalSeconds = interval;
    value.expectedSeconds = expected;
    return value;
}

// The 0.333 s pv.mp4 from docs/audit/PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md.
// A legitimately tiny PV must stay NATURAL: it keeps its last frame and never
// gets reloaded, no matter how much of the chart is left to play.
bool testShortPvIsNatural(QTextStream& err)
{
    bool ok = true;
    const EndOfMediaFacts tenFrameClip = facts(0.333333, 0.300000, 1.0 / 30.0, 0.311328);
    ok &= expect(classifyEndOfMedia(tenFrameClip) == EndOfMediaClass::Natural,
                 "a 0.333 s PV reaching its own end is a natural end",
                 err);
    ok &= expect(!endOfMediaShouldRecover(tenFrameClip),
                 "a natural end never triggers recovery",
                 err);

    // Same clip, but the chart is 113 s long, so there is a huge amount of chart
    // left. Remaining CHART time must not influence the decision at all.
    const EndOfMediaFacts atChartStart = facts(0.333333, 0.333333, 1.0 / 30.0, 0.0);
    ok &= expect(!endOfMediaShouldRecover(atChartStart),
                 "remaining chart time does not turn a natural end into a stale one",
                 err);
    return ok;
}

// The captured field case: 121.066 s PV, EndOfMedia after 1.446 s of playback with
// the last decoded pts at 1.267 s.
bool testCapturedStaleEvent(QTextStream& err)
{
    bool ok = true;
    const EndOfMediaFacts captured = facts(121.066, 1.267, 1.0 / 30.0, 1.446);
    ok &= expect(classifyEndOfMedia(captured) == EndOfMediaClass::Stale,
                 "the captured 121 s / 1.267 s event classifies as stale",
                 err);
    ok &= expect(endOfMediaShouldRecover(captured),
                 "a stale event with ~120 s of PV left is worth recovering",
                 err);
    return ok;
}

// Rounding / tail slack: a decoder that stops a hair before the container duration
// has still reached the end.
bool testNaturalSlack(QTextStream& err)
{
    bool ok = true;
    const double duration = 60.0;
    const EndOfMediaFacts justInside =
        facts(duration, duration - (kEndOfMediaNaturalSlackSeconds * 0.5), 1.0 / 60.0, duration);
    ok &= expect(classifyEndOfMedia(justInside) == EndOfMediaClass::Natural,
                 "a sub-slack shortfall is natural",
                 err);

    const EndOfMediaFacts justOutside = facts(duration, duration - 5.0, 1.0 / 60.0, 10.0);
    ok &= expect(classifyEndOfMedia(justOutside) == EndOfMediaClass::Stale,
                 "a five-second shortfall is stale",
                 err);

    // A 2 fps source: one frame is 0.5 s, so three frames of slack must dominate
    // the fixed floor or every low-fps clip would look stale at its real end.
    const EndOfMediaFacts lowFps = facts(20.0, 18.8, 0.5, 20.0);
    ok &= expect(classifyEndOfMedia(lowFps) == EndOfMediaClass::Natural,
                 "frame-interval slack scales for low frame-rate sources",
                 err);
    return ok;
}

// Unknown must never recover: recovering on missing information is how a broken
// file turns into an endless reload loop.
bool testUnknownNeverRecovers(QTextStream& err)
{
    bool ok = true;
    ok &= expect(classifyEndOfMedia(facts(0.0, 1.2, 1.0 / 30.0, 1.4)) == EndOfMediaClass::Unknown,
                 "no reported duration is unknown, not stale",
                 err);
    ok &= expect(classifyEndOfMedia(facts(121.0, -1.0, 0.0, 0.0)) == EndOfMediaClass::Unknown,
                 "no decoded frame at all is unknown, not stale",
                 err);
    ok &= expect(!endOfMediaShouldRecover(facts(0.0, -1.0, 0.0, 0.0)),
                 "unknown never recovers",
                 err);
    return ok;
}

// A stale event raised when the timeline has already run past the PV's end needs
// no recovery — the visible result is the same last frame either way.
bool testStaleNearEndDoesNotRecover(QTextStream& err)
{
    bool ok = true;
    const EndOfMediaFacts nearEnd = facts(121.066, 100.0, 1.0 / 30.0, 120.9);
    ok &= expect(classifyEndOfMedia(nearEnd) == EndOfMediaClass::Stale,
                 "a large decode shortfall is still stale near the end",
                 err);
    ok &= expect(!endOfMediaShouldRecover(nearEnd),
                 "no recovery when the transport has effectively passed the PV end",
                 err);
    return ok;
}

bool testClassNames(QTextStream& err)
{
    bool ok = true;
    ok &= expect(QString::fromLatin1(endOfMediaClassName(EndOfMediaClass::Natural))
                     == QStringLiteral("natural"),
                 "natural class name",
                 err);
    ok &= expect(QString::fromLatin1(endOfMediaClassName(EndOfMediaClass::Stale))
                     == QStringLiteral("stale"),
                 "stale class name",
                 err);
    ok &= expect(QString::fromLatin1(endOfMediaClassName(EndOfMediaClass::Unknown))
                     == QStringLiteral("unknown"),
                 "unknown class name",
                 err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    bool ok = true;
    ok &= testShortPvIsNatural(err);
    ok &= testCapturedStaleEvent(err);
    ok &= testNaturalSlack(err);
    ok &= testUnknownNeverRecovers(err);
    ok &= testStaleNearEndDoesNotRecover(err);
    ok &= testClassNames(err);

    if (!ok) {
        return 1;
    }
    out << "preview_end_of_media_policy_spec ok" << Qt::endl;
    return 0;
}
