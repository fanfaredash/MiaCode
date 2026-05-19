#include "PreviewClock.h"

#include <QtMath>

namespace miacode::preview_audio {

namespace {

// Match the floor that PreviewStageMediaHost::setPlaybackRate clamps to.
// Below ~0.05 the Qt 6.8 FFmpeg backend exhibits a race on slow-decode
// paths, and BASS_FX TEMPO has its own internal floor near 0.25 anyway;
// 0.05 leaves headroom for both without letting callers feed in zero.
constexpr double kMinRate = 0.05;
constexpr double kMaxRate = 4.0;

double clampRate(double r)
{
    if (!qIsFinite(r) || r < kMinRate) {
        return kMinRate;
    }
    if (r > kMaxRate) {
        return kMaxRate;
    }
    return r;
}

double sanitizeChartSecond(double s)
{
    return qIsFinite(s) ? s : 0.0;
}

}  // namespace

PreviewClock::PreviewClock() = default;

void PreviewClock::start(double startChartSecond)
{
    anchorChartSecond_ = sanitizeChartSecond(startChartSecond);
    wallClock_.start();
    playing_ = true;
}

void PreviewClock::pause()
{
    if (!playing_) {
        return;
    }
    anchorChartSecond_ = currentSecond();
    playing_ = false;
}

void PreviewClock::seek(double chartSecond)
{
    anchorChartSecond_ = sanitizeChartSecond(chartSecond);
    if (playing_) {
        wallClock_.start();
    }
}

void PreviewClock::setRate(double rate)
{
    const double clamped = clampRate(rate);
    if (playing_) {
        anchorChartSecond_ = currentSecond();
        wallClock_.start();
    }
    rate_ = clamped;
}

void PreviewClock::reset()
{
    anchorChartSecond_ = 0.0;
    rate_ = 1.0;
    playing_ = false;
    wallClock_.invalidate();
}

double PreviewClock::currentSecond() const
{
    if (!playing_ || !wallClock_.isValid()) {
        return anchorChartSecond_;
    }
    const qint64 ns = wallClock_.nsecsElapsed();
    const double elapsedSec = static_cast<double>(ns) * 1e-9;
    return anchorChartSecond_ + elapsedSec * rate_;
}

}  // namespace miacode::preview_audio
