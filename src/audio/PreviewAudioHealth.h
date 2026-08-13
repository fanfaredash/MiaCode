#pragma once

#include <QString>
#include <QtGlobal>

// Preview audio underrun / buffer-health policy.
//
// Why this exists: src/audio/ had ZERO underrun, stall, starvation or buffer-level
// diagnostics. If the "stutter" users report under OBS/browser contention is AUDIBLE
// rather than visual, nothing in the existing logs can see it — every other probe in the
// repo measures the render path. The direct signal from BASS is
// BASS_ChannelIsActive(...) == BASS_ACTIVE_STALLED, which means the channel ran out of
// data and the mixer is idling: a textbook underrun.
//
// Everything BASS-specific stays in the backend TU (which owns bass.h); this header holds
// only the pure policy — activity vocabulary, stall edge detection, sampling throttle, and
// the payload format — so it is covered by a spec on machines without a sound device.
namespace miacode::preview_audio::health {

// How often the periodic buffer-health line is emitted, in authoritative playback
// seconds. Deliberately much coarser than the ~1 Hz bass_status line: buffer health is a
// slow signal and this must never become a per-frame log. Stall EDGES bypass this and log
// immediately, because an underrun that lasts less than one sampling period is exactly
// the event we are hunting.
inline constexpr double kHealthSampleIntervalSeconds = 5.0;

// Mirrors BASS_ACTIVE_*. The mapping from BASS's DWORD lives in the backend TU so this
// header never needs bass.h.
enum class ChannelActivity {
    Unknown,
    Stopped,
    Playing,
    Stalled,
    Paused,
    PausedDevice,
};

inline const char* activityName(ChannelActivity activity)
{
    switch (activity) {
    case ChannelActivity::Stopped:
        return "stopped";
    case ChannelActivity::Playing:
        return "playing";
    case ChannelActivity::Stalled:
        return "stalled";
    case ChannelActivity::Paused:
        return "paused";
    case ChannelActivity::PausedDevice:
        return "paused_device";
    case ChannelActivity::Unknown:
    default:
        return "unknown";
    }
}

inline bool isUnderrun(ChannelActivity activity)
{
    return activity == ChannelActivity::Stalled;
}

enum class StallEdge {
    None,
    Begin,
    End,
};

// Latched stall state, so a multi-second underrun produces exactly two lines (begin/end)
// plus its duration, instead of one line per poll.
struct StallTracker {
    bool stalled = false;
    quint64 stallCount = 0;
    double stallStartSecond = -1.0;
};

inline StallEdge updateStall(StallTracker* tracker, bool stalledNow, double second)
{
    if (tracker == nullptr) {
        return StallEdge::None;
    }
    if (stalledNow == tracker->stalled) {
        return StallEdge::None;
    }
    tracker->stalled = stalledNow;
    if (stalledNow) {
        tracker->stallStartSecond = second;
        tracker->stallCount += 1;
        return StallEdge::Begin;
    }
    return StallEdge::End;
}

// Milliseconds the current (or just-ended) stall has lasted; -1 when never stalled.
inline qint64 stallDurationMs(const StallTracker& tracker, double second)
{
    if (tracker.stallStartSecond < 0.0) {
        return -1;
    }
    const double elapsed = second - tracker.stallStartSecond;
    return elapsed > 0.0 ? static_cast<qint64>(elapsed * 1000.0) : 0;
}

// Throttle for the periodic line. `lastLoggedSecond < 0` means "never logged".
// Playback can be seeked backwards, which makes the delta negative; treat that as due so
// a seek does not suppress sampling until the playhead catches up again.
inline bool shouldLogHealth(
    double second,
    double lastLoggedSecond,
    double intervalSeconds = kHealthSampleIntervalSeconds)
{
    if (lastLoggedSecond < 0.0) {
        return true;
    }
    const double delta = second - lastLoggedSecond;
    return delta < 0.0 || delta >= intervalSeconds;
}

// Buffer / device numbers read straight out of BASS. `-1` marks a field the backend could
// not read (engine not initialised, channel handle zero, unsupported device).
struct BufferSnapshot {
    qint64 minBufferMs = -1;      // BASS_INFO::minbuf — device's minimum safe buffer
    qint64 initLatencyMs = -1;    // BASS_INFO::latency; 0 unless BASS_DEVICE_LATENCY at init
    qint64 configBufferMs = -1;   // BASS_CONFIG_BUFFER — playback buffer length
    qint64 updatePeriodMs = -1;   // BASS_CONFIG_UPDATEPERIOD — update-thread cadence
    qint64 updateThreads = -1;    // BASS_CONFIG_UPDATETHREADS
    qint64 deviceFreq = -1;       // BASS_INFO::freq
    qint64 bufferedBytes = -1;    // BASS_ChannelGetData(BASS_DATA_AVAILABLE) on the mixer
    qint64 bufferedMs = -1;       // the above converted through BASS_ChannelBytes2Seconds
};

// Build the periodic buffer-health payload.
//
// `mmcssRegisteredOnAudioThreads` records whether MiaCode registers any BASS thread with
// MMCSS. It is false: the only MMCSS registration in the default code path is on the QSG
// render thread (preview/quick_scene/PreviewQuickSceneRoot.cpp). Logging that explicitly
// turns a "we think so" into evidence — under CPU contention from OBS/x264 an
// unprotected audio thread is a live suspect, and the log should say plainly that it is
// unprotected rather than leaving the reader to grep the source.
inline QString healthPayload(
    quint64 transactionId,
    double second,
    ChannelActivity mixerActivity,
    ChannelActivity backgroundActivity,
    const StallTracker& tracker,
    const BufferSnapshot& buffers,
    bool mmcssRegisteredOnAudioThreads,
    const QString& appMmcssTaskClass)
{
    return QStringLiteral(
               "bass_audio_health txn=%1 second=%2 mixer_active=%3 bgm_active=%4 underrun=%5 "
               "stall_count=%6 stall_ms=%7 buffered_ms=%8 buffered_bytes=%9 config_buffer_ms=%10 "
               "min_buffer_ms=%11 update_period_ms=%12 update_threads=%13 init_latency_ms=%14 "
               "device_freq=%15 bass_mmcss_registered_by_app=%16 app_mmcss_task_class=%17")
        .arg(transactionId)
        .arg(second, 0, 'f', 3)
        .arg(QLatin1String(activityName(mixerActivity)))
        .arg(QLatin1String(activityName(backgroundActivity)))
        .arg((isUnderrun(mixerActivity) || isUnderrun(backgroundActivity)) ? 1 : 0)
        .arg(tracker.stallCount)
        .arg(stallDurationMs(tracker, second))
        .arg(buffers.bufferedMs)
        .arg(buffers.bufferedBytes)
        .arg(buffers.configBufferMs)
        .arg(buffers.minBufferMs)
        .arg(buffers.updatePeriodMs)
        .arg(buffers.updateThreads)
        .arg(buffers.initLatencyMs)
        .arg(buffers.deviceFreq)
        .arg(mmcssRegisteredOnAudioThreads ? 1 : 0)
        .arg(appMmcssTaskClass.isEmpty() ? QStringLiteral("(none)") : appMmcssTaskClass);
}

// Build the immediate stall-edge payload. Emitted the moment BASS reports STALLED and
// again when it recovers, so a sub-sampling-interval underrun still leaves a trace.
inline QString stallEdgePayload(
    StallEdge edge,
    quint64 transactionId,
    double second,
    ChannelActivity mixerActivity,
    ChannelActivity backgroundActivity,
    const StallTracker& tracker)
{
    return QStringLiteral(
               "bass_audio_stall edge=%1 txn=%2 second=%3 mixer_active=%4 bgm_active=%5 "
               "stall_count=%6 stall_ms=%7")
        .arg(edge == StallEdge::Begin ? QStringLiteral("begin") : QStringLiteral("end"))
        .arg(transactionId)
        .arg(second, 0, 'f', 3)
        .arg(QLatin1String(activityName(mixerActivity)))
        .arg(QLatin1String(activityName(backgroundActivity)))
        .arg(tracker.stallCount)
        .arg(stallDurationMs(tracker, second));
}

}  // namespace miacode::preview_audio::health
