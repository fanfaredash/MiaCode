#include "PreviewVideoSyncPolicy.h"

#include <QtMath>

namespace miacode::preview::video_sync {
namespace {

constexpr double kDefaultFrameDurationSeconds = 1.0 / 30.0;
constexpr double kMinimumToleranceSeconds = 0.080;
constexpr double kMaximumToleranceSeconds = 0.200;
constexpr qint64 kRequiredSustainedMs = 300;
constexpr qint64 kReanchorCooldownMs = 1500;

double resolvedTolerance(double frameDurationSeconds)
{
    const double frameDuration = qIsFinite(frameDurationSeconds) && frameDurationSeconds > 0.0
        ? frameDurationSeconds
        : kDefaultFrameDurationSeconds;
    return qBound(kMinimumToleranceSeconds, frameDuration * 2.0, kMaximumToleranceSeconds);
}

}  // namespace

Decision Policy::observe(const Observation& observation)
{
    Decision decision;
    decision.toleranceSeconds = resolvedTolerance(observation.frameDurationSeconds);
    if (!observation.eligible
        || !qIsFinite(observation.authoritativeSecond)
        || !qIsFinite(observation.videoSecond)) {
        candidateSinceMs_ = -1;
        candidateSign_ = 0;
        lastObservationMs_ = observation.monotonicMs;
        return decision;
    }

    if (lastObservationMs_ >= 0 && observation.monotonicMs < lastObservationMs_) {
        reset();
    }
    lastObservationMs_ = observation.monotonicMs;
    decision.deltaSeconds = observation.videoSecond - observation.authoritativeSecond;
    if (qAbs(decision.deltaSeconds) <= decision.toleranceSeconds) {
        candidateSinceMs_ = -1;
        candidateSign_ = 0;
        return decision;
    }

    const int sign = decision.deltaSeconds < 0.0 ? -1 : 1;
    if (candidateSinceMs_ < 0 || candidateSign_ != sign) {
        candidateSinceMs_ = observation.monotonicMs;
        candidateSign_ = sign;
        return decision;
    }

    decision.sustainedMs = qMax<qint64>(0, observation.monotonicMs - candidateSinceMs_);
    const bool cooldownComplete = lastReanchorMs_ < 0
        || observation.monotonicMs - lastReanchorMs_ >= kReanchorCooldownMs;
    if (decision.sustainedMs >= kRequiredSustainedMs && cooldownComplete) {
        decision.action = Action::Reanchor;
        lastReanchorMs_ = observation.monotonicMs;
        candidateSinceMs_ = -1;
        candidateSign_ = 0;
    }
    return decision;
}

void Policy::reset()
{
    candidateSinceMs_ = -1;
    lastObservationMs_ = -1;
    lastReanchorMs_ = -1;
    candidateSign_ = 0;
}

}  // namespace miacode::preview::video_sync
