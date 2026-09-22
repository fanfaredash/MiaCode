#pragma once

#include <QtGlobal>

namespace miacode::preview::video_sync {

enum class Action {
    Maintain,
    Reanchor,
};

struct Observation {
    bool eligible = false;
    double authoritativeSecond = 0.0;
    double videoSecond = 0.0;
    double frameDurationSeconds = 0.0;
    qint64 monotonicMs = 0;
};

struct Decision {
    Action action = Action::Maintain;
    double deltaSeconds = 0.0;
    double toleranceSeconds = 0.0;
    qint64 sustainedMs = 0;
};

class Policy {
public:
    Decision observe(const Observation& observation);
    void reset();

private:
    qint64 candidateSinceMs_ = -1;
    qint64 lastObservationMs_ = -1;
    qint64 lastReanchorMs_ = -1;
    int candidateSign_ = 0;
};

}  // namespace miacode::preview::video_sync
