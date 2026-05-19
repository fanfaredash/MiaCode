#pragma once

// PreviewClock — wall-clock-driven playback timeline.
//
// Mirrors MajdataPlay's MajTimer-as-master architecture: a monotonic
// stopwatch + a chart-second anchor + a playback-rate scalar.
// The chart-second seen by chart / visual / SFX-trigger code is computed
// from the wall clock, *not* from any audio-stream cursor.
//
// Why decouple chart time from the BASS cursor:
//
//   - BASS jitter / buffer underrun / tempo-stream race no longer
//     propagates into the chart timeline.
//   - The smoothing layer that currently exists to mask BASS jitter
//     becomes unnecessary (G1 Commit 4 deletes most of it).
//   - SFX triggering can shift to a per-tick `drainSfxBefore(chartSec)`
//     pattern (G1 Commit 5) without needing BASS_SYNC_POS callbacks.
//
// See docs/PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §5.1, §6.2.
//
// All methods are main-thread-only. No locks; cheap to call per tick.
// Commit 3 only introduces the class — no caller uses it yet.

#include <QElapsedTimer>

namespace miacode::preview_audio {

class PreviewClock {
public:
    PreviewClock();

    // Begin (or resume) advancing chart time starting at startChartSecond.
    // Resets the wall-clock anchor to "now". After this call,
    // currentSecond() == startChartSecond and grows with elapsed real
    // time scaled by the current rate.
    void start(double startChartSecond);

    // Freeze chart time at its current value. After this call,
    // currentSecond() stays constant until start() / seek() is called.
    void pause();

    // While paused, move the cursor to chartSecond. While playing,
    // re-anchor so that currentSecond() == chartSecond immediately
    // and continues to advance from there.
    void seek(double chartSecond);

    // Set the rate scalar (chart-sec per wall-sec). Preserves the
    // current chart-second across the rate change so the caller does
    // not need to pause first.
    void setRate(double rate);

    // Reset to a fully-stopped, chart-second-zero state. Used at
    // session-end / asset-reload.
    void reset();

    double rate() const { return rate_; }
    bool isPlaying() const { return playing_; }

    // Sample the current chart second. Cheap; safe per frame.
    double currentSecond() const;

private:
    QElapsedTimer wallClock_;
    double anchorChartSecond_ = 0.0;
    double rate_ = 1.0;
    bool playing_ = false;
};

}  // namespace miacode::preview_audio
