// Contract regression for stage 4.9e-3's second playback contract.
//
// PlaybackStateAuthority is the coordinator's seam for non-command state
// writes (see PlaybackStateAuthority.h): a portable-state rate restore, a
// media-backend clock re-anchor, and a silent playhead relocation — none of
// them a response to a user transport command, so none of them may go
// through PlaybackControl.
//
// This target links Qt6::Core + Qt6::Test only, the same guarantee as the
// four PlaybackXPort specs beside it: if the header ever pulls in Session.h
// or a Widgets/QML type, this target fails to LINK, which is stronger than
// grepping for the forbidden names.

#include "app/v2/PlaybackStateAuthority.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

// A stand-in playback authority. It records exactly what each method wrote
// and how many times, and applies none of PlaybackCoordinator's own clamping
// or side effects — the contract itself promises none of those, only that
// the write reaches the implementation.
class FakeAuthority final : public miacode::v2::PlaybackStateAuthority
{
public:
    void restorePlaybackRate(double rate) override
    {
        lastRestoredRate = rate;
        ++restorePlaybackRateCount;
    }

    void reanchorObservedSecond(double second) override
    {
        lastReanchoredSecond = second;
        ++reanchorObservedSecondCount;
    }

    void repositionSilently(double second, const char* reason) override
    {
        lastRepositionedSecond = second;
        ++repositionSilentlyCount;
        lastRepositionReason = reason != nullptr ? QString::fromUtf8(reason) : QString();
    }

    double lastRestoredRate = -1.0;
    int restorePlaybackRateCount = 0;
    double lastReanchoredSecond = -1.0;
    int reanchorObservedSecondCount = 0;
    double lastRepositionedSecond = -1.0;
    int repositionSilentlyCount = 0;
    QString lastRepositionReason;
};

bool verifyImplementableWithoutSessionOrAWindow(QTextStream& err)
{
    FakeAuthority authority;
    miacode::v2::PlaybackStateAuthority& contract = authority;

    // restorePlaybackRate: a portable-state load writes the rate it read
    // back, verbatim — no clamp is part of the CONTRACT (PlaybackCoordinator
    // applies its own floor, which is its business, not the port's).
    contract.restorePlaybackRate(0.75);
    bool ok = require(authority.restorePlaybackRateCount == 1
                           && authority.lastRestoredRate == 0.75,
                       QStringLiteral("restorePlaybackRate reaches the implementation with its argument"), err);

    // reanchorObservedSecond: the media backend's reported pause position
    // reaches the implementation, independent of restorePlaybackRate above.
    contract.reanchorObservedSecond(12.5);
    ok &= require(authority.reanchorObservedSecondCount == 1
                      && authority.lastReanchoredSecond == 12.5
                      && authority.restorePlaybackRateCount == 1,
                  QStringLiteral("reanchorObservedSecond reaches the implementation without touching the rate write"), err);

    // repositionSilently: must be callable with no gating — a caller like
    // DocumentSessionHost::switchToDifficultyField depends on being able to
    // call it again immediately after a prior reset to 0, and have the new
    // value win. Two back-to-back calls both land.
    contract.repositionSilently(0.0, "spec_first");
    ok &= require(authority.repositionSilentlyCount == 1 && authority.lastRepositionedSecond == 0.0,
                  QStringLiteral("repositionSilently reaches the implementation on its first call"), err);
    contract.repositionSilently(42.0, "spec_second");
    ok &= require(authority.repositionSilentlyCount == 2 && authority.lastRepositionedSecond == 42.0,
                  QStringLiteral("a second repositionSilently call is not gated by the first"), err);
    // The reason travels with the call on purpose: writePreviewPauseSecond logs
    // backward moves so each one names its author, and that log exists because a
    // real regression was undiagnosable without it. A single hardcoded label
    // inside the implementation would give back half of what the log buys.
    ok &= require(authority.lastRepositionReason == QStringLiteral("spec_second"),
                  QStringLiteral("repositionSilently carries the caller's own reason through"), err);

    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = verifyImplementableWithoutSessionOrAWindow(err);

    if (ok) {
        QTextStream(stdout) << "playback_state_authority_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
