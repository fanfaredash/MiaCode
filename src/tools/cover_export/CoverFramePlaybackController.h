#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace miacode::cover_export {

// UI-independent transport for one cover chart-frame layer. The timer only
// wakes the controller; elapsed time is authoritative so a delayed event loop
// cannot make playback depend on the number of timer callbacks.
class CoverFramePlaybackController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double seconds READ seconds NOTIFY secondsChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)

public:
    explicit CoverFramePlaybackController(QObject* parent = nullptr);

    bool playing() const { return playing_; }
    double seconds() const { return seconds_; }
    double duration() const { return duration_; }

    void setDuration(double duration);
    void setSeconds(double seconds);

    void play();
    void pause();
    void toggle();
    void seekBy(double deltaSeconds);

    // The first call performs one fixed small step. Further movement only
    // begins after the hold threshold; QML filters auto-repeat key events.
    void beginKeySeek(int direction);
    void endKeySeek();
    void cancelInput();

    // `deltaSeconds` is the elapsed duration since the previous call. It is
    // public so the deterministic dev-tool spec can exercise the exact same
    // state machine used by the QTimer callback.
    void advanceForElapsed(double deltaSeconds);

signals:
    void playingChanged();
    void secondsChanged();
    void durationChanged();
    void reachedEnd();

private:
    static constexpr int kTickIntervalMs = 16;
    static constexpr int kHoldThresholdMs = 150;

    void setSecondsClamped(double seconds);
    void setPlaying(bool playing);
    void ensureTimerRunning();
    void stopTimerIfIdle();
    void onTick();

    QTimer tickTimer_;
    QElapsedTimer tickClock_;
    bool playing_ = false;
    bool keySeeking_ = false;
    int keyDirection_ = 0;
    double keyHeldSeconds_ = 0.0;
    double seconds_ = 0.0;
    double duration_ = 0.0;
};

}  // namespace miacode::cover_export
