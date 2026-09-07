#include "tools/cover_export/CoverFramePlaybackController.h"

#include "common/PreviewInteractionConfig.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace miacode::cover_export {

CoverFramePlaybackController::CoverFramePlaybackController(QObject* parent)
    : QObject(parent)
{
    tickTimer_.setInterval(kTickIntervalMs);
    tickTimer_.setTimerType(Qt::PreciseTimer);
    connect(&tickTimer_, &QTimer::timeout, this, &CoverFramePlaybackController::onTick);
}

void CoverFramePlaybackController::setDuration(double duration)
{
    const double next = std::isfinite(duration) ? std::max(0.0, duration) : 0.0;
    if (qFuzzyCompare(duration_, next)) {
        if (seconds_ > duration_) {
            setSecondsClamped(duration_);
        }
        return;
    }
    duration_ = next;
    emit durationChanged();
    if (seconds_ > duration_) {
        setSecondsClamped(duration_);
    }
    if (duration_ <= 0.0) {
        setPlaying(false);
    }
    stopTimerIfIdle();
}

void CoverFramePlaybackController::setSeconds(double seconds)
{
    setSecondsClamped(seconds);
}

void CoverFramePlaybackController::setSecondsClamped(double seconds)
{
    const double finiteSeconds = std::isfinite(seconds) ? seconds : 0.0;
    const double next = qBound(0.0, finiteSeconds, duration_);
    if (qFuzzyCompare(seconds_, next)) {
        return;
    }
    seconds_ = next;
    emit secondsChanged();
}

void CoverFramePlaybackController::setPlaying(bool playing)
{
    if (playing_ == playing) {
        return;
    }
    playing_ = playing;
    emit playingChanged();
}

void CoverFramePlaybackController::play()
{
    if (duration_ <= 0.0) {
        setPlaying(false);
        return;
    }
    endKeySeek();
    if (seconds_ >= duration_) {
        setSecondsClamped(0.0);
    }
    setPlaying(true);
    ensureTimerRunning();
}

void CoverFramePlaybackController::pause()
{
    setPlaying(false);
    stopTimerIfIdle();
}

void CoverFramePlaybackController::toggle()
{
    if (playing_) {
        pause();
    } else {
        play();
    }
}

void CoverFramePlaybackController::seekBy(double deltaSeconds)
{
    cancelInput();
    pause();
    setSecondsClamped(seconds_ + (std::isfinite(deltaSeconds) ? deltaSeconds : 0.0));
}

void CoverFramePlaybackController::beginKeySeek(int direction)
{
    const int nextDirection = direction < 0 ? -1 : (direction > 0 ? 1 : 0);
    if (nextDirection == 0) {
        return;
    }
    if (keySeeking_ && keyDirection_ == nextDirection) {
        return;
    }
    pause();
    endKeySeek();
    keySeeking_ = true;
    keyDirection_ = nextDirection;
    keyHeldSeconds_ = 0.0;
    setSecondsClamped(seconds_ + nextDirection * miacode::preview_interaction::kSeekSingleStepSeconds);
    ensureTimerRunning();
}

void CoverFramePlaybackController::endKeySeek()
{
    keySeeking_ = false;
    keyDirection_ = 0;
    keyHeldSeconds_ = 0.0;
    stopTimerIfIdle();
}

void CoverFramePlaybackController::cancelInput()
{
    endKeySeek();
}

void CoverFramePlaybackController::advanceForElapsed(double deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) {
        return;
    }

    if (playing_) {
        const double next = seconds_ + deltaSeconds;
        if (next >= duration_) {
            setSecondsClamped(duration_);
            setPlaying(false);
            stopTimerIfIdle();
            emit reachedEnd();
        } else {
            setSecondsClamped(next);
        }
        return;
    }

    if (!keySeeking_ || keyDirection_ == 0 || duration_ <= 0.0) {
        return;
    }

    const double previousHeldSeconds = keyHeldSeconds_;
    keyHeldSeconds_ += deltaSeconds;
    const double thresholdSeconds = static_cast<double>(kHoldThresholdMs) / 1000.0;
    const double previousContinuousSeconds = std::max(0.0, previousHeldSeconds - thresholdSeconds);
    const double continuousSeconds = std::max(0.0, keyHeldSeconds_ - thresholdSeconds);
    const double activeDelta = continuousSeconds - previousContinuousSeconds;
    if (activeDelta <= 0.0) {
        return;
    }
    const double heldSeconds = std::max(0.0, keyHeldSeconds_ - thresholdSeconds);
    const double rate = miacode::preview_interaction::heldSeekPlaybackRate(heldSeconds);
    setSecondsClamped(seconds_ + keyDirection_ * activeDelta * rate);
}

void CoverFramePlaybackController::ensureTimerRunning()
{
    tickClock_.start();
    if (!tickTimer_.isActive()) {
        tickTimer_.start();
    }
}

void CoverFramePlaybackController::stopTimerIfIdle()
{
    if (!playing_ && !keySeeking_) {
        tickTimer_.stop();
        tickClock_.invalidate();
    }
}

void CoverFramePlaybackController::onTick()
{
    if (!playing_ && !keySeeking_) {
        stopTimerIfIdle();
        return;
    }
    const qint64 elapsedMs = tickClock_.isValid() ? tickClock_.restart() : kTickIntervalMs;
    advanceForElapsed(std::max<qint64>(1, elapsedMs) / 1000.0);
}

}  // namespace miacode::cover_export
