#include "core/video/PreviewVideoSyncPolicy.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

using miacode::preview::video_sync::Action;
using miacode::preview::video_sync::Observation;
using miacode::preview::video_sync::Policy;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
    }
    return condition;
}

Observation sample(qint64 ms, double delta, bool eligible = true)
{
    Observation observation;
    observation.eligible = eligible;
    observation.authoritativeSecond = 10.0;
    observation.videoSecond = 10.0 + delta;
    observation.frameDurationSeconds = 1.0 / 30.0;
    observation.monotonicMs = ms;
    return observation;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;

    Policy jitter;
    ok &= require(jitter.observe(sample(0, -0.12)).action == Action::Maintain
                      && jitter.observe(sample(100, -0.01)).action == Action::Maintain,
                  QStringLiteral("a transient frame delay must not reanchor"), err);

    Policy sustained;
    sustained.observe(sample(0, -0.20));
    sustained.observe(sample(200, -0.21));
    ok &= require(sustained.observe(sample(320, -0.22)).action == Action::Reanchor,
                  QStringLiteral("sustained lag must reanchor"), err);
    ok &= require(sustained.observe(sample(400, -0.22)).action == Action::Maintain
                      && sustained.observe(sample(800, -0.22)).action == Action::Maintain,
                  QStringLiteral("cooldown must prevent a seek storm"), err);
    ok &= require(sustained.observe(sample(1900, -0.22)).action == Action::Reanchor
                      && sustained.observe(sample(2000, -0.22)).action == Action::Maintain,
                  QStringLiteral("persistent lag may reanchor again after cooldown and confirmation"), err);

    Policy signChange;
    signChange.observe(sample(0, -0.20));
    signChange.observe(sample(200, 0.20));
    ok &= require(signChange.observe(sample(350, 0.20)).action == Action::Maintain
                      && signChange.observe(sample(510, 0.20)).action == Action::Reanchor,
                  QStringLiteral("a delta sign change must restart confirmation"), err);

    Policy reset;
    reset.observe(sample(0, -0.20));
    reset.observe(sample(250, -0.20));
    reset.observe(sample(300, -0.20, false));
    ok &= require(reset.observe(sample(500, -0.20)).action == Action::Maintain
                      && reset.observe(sample(810, -0.20)).action == Action::Reanchor,
                  QStringLiteral("ineligible playback state must reset pending confirmation"), err);

    Policy tolerance;
    ok &= require(tolerance.observe(sample(0, 0.07)).action == Action::Maintain
                      && tolerance.observe(sample(500, 0.07)).action == Action::Maintain,
                  QStringLiteral("normal frame jitter must stay inside tolerance"), err);

    return ok ? 0 : 1;
}
