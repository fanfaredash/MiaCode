#pragma once

#include <QString>
#include <QtGlobal>

// Preview audio OUTPUT-buffer discontinuity probe.
//
// Why this exists: bass_audio_health (PreviewAudioHealth.h) aggregates over a
// multi-second window, so a click/pop lasting a few milliseconds cannot show up
// in its numbers -- see docs/audit/PREVIEW_AUDIO_MASTER_MIXER_STALL_REVIEW_ZH.md.
// This probe instead looks at the master mixer's OUTPUT waveform itself, one
// block at a time, from a BASS DSP callback that runs on BASS's own mixing
// thread. Everything below is a pure, stateless-per-call transform on caller-
// supplied state, so it is covered by a spec on machines without a sound
// device. The DSP glue that actually touches bass.h, walks the interleaved
// float buffer, and pushes events into the lock-free ring lives in the BASS
// backend TU (BassPreviewAudioBackend_EngineInit.cpp) because a DSP callback
// runs on the audio thread and must not log, lock, or allocate -- see that
// TU's comment for the exact constraints.
//
// Candidate failure modes this measures for
// (docs/audit/PREVIEW_AUDIO_MASTER_MIXER_STALL_REVIEW_ZH.md and the deeper
// second-pass review it was extended with):
//  - kind=step: a sample-to-sample jump too large to be ordinary program
//    content. Catches BOTH a device-buffer underrun splice (silence/glue
//    dropped in) and a SoundTouch tempo-stream overlap-add seam, since both
//    leave a discontinuity in the final output waveform.
//  - kind=clip: consecutive samples pinned at/above the float format ceiling.
//    Catches gain-stage clipping (Sample.h's normalize ceiling is 4.0x and
//    clampSampleVolume's is 2.0x, with no limiter downstream).
//  - kind=late_callback: this DSP invocation's wall-clock arrival is later
//    than the previous block's nominal duration predicts. A buffer-
//    granularity underrun probe, far finer than bass_audio_health's 5s
//    sampling window or its ~1 Hz advance-ratio estimate.
namespace miacode::preview_audio::output_glitch {

// A sample-to-sample delta this large is not ordinary program content at any
// reasonable mix level; it is either a hard splice or full-scale noise. Roughly
// a quarter of full scale in a single sample period (1/48000 s at the mix rate).
inline constexpr float kDefaultStepThreshold = 0.25f;

// "At or above the format ceiling", with a hair of margin for float rounding.
inline constexpr float kDefaultClipThreshold = 0.999f;

// A DSP callback firing more than this late relative to the block duration it
// just delivered is treated as a buffer-granularity underrun candidate.
inline constexpr qint64 kDefaultLateCallbackThresholdMs = 2;

enum class GlitchKind : quint8 {
    Step,
    Clip,
    LateCallback,
};

inline const char* glitchKindName(GlitchKind kind)
{
    switch (kind) {
    case GlitchKind::Step:
        return "step";
    case GlitchKind::Clip:
        return "clip";
    case GlitchKind::LateCallback:
        return "late_callback";
    default:
        return "unknown";
    }
}

// Per-channel state carried across DSP callback invocations. A step can
// straddle a block boundary, so the previous block's last sample must be kept.
struct StepDetectorState {
    float lastSample = 0.0f;
    bool hasLastSample = false;
};

// Detects |x[n] - x[n-1]| > threshold. Pure state transition: reads/writes only
// the state and the sample given -- no globals, no I/O, no allocation.
inline bool updateStepDetector(
    StepDetectorState* state,
    float sample,
    float threshold,
    float* outMagnitude)
{
    if (state == nullptr) {
        return false;
    }
    bool hit = false;
    if (state->hasLastSample) {
        const float delta = sample - state->lastSample;
        const float magnitude = delta < 0.0f ? -delta : delta;
        if (magnitude > threshold) {
            hit = true;
            if (outMagnitude != nullptr) {
                *outMagnitude = magnitude;
            }
        }
    }
    state->lastSample = sample;
    state->hasLastSample = true;
    return hit;
}

// Per-channel run-length state for the clipping probe. `startFrame` is the
// mixer-relative frame index (a cumulative counter the DSP glue owns) where the
// current run began, so a run that straddles a block boundary is still reported
// with its true start once it closes.
struct ClipRunState {
    bool active = false;
    quint64 startFrame = 0;
    quint32 length = 0;
};

// Result of feeding one sample into the clip-run tracker: `runEnded` is true
// exactly on the sample after a run drops back under threshold (the falling
// edge), at which point startFrame/length describe the run that just closed.
struct ClipRunUpdate {
    bool runEnded = false;
    quint64 startFrame = 0;
    quint32 length = 0;
};

inline ClipRunUpdate updateClipRun(
    ClipRunState* state,
    float sample,
    float threshold,
    quint64 currentFrame)
{
    ClipRunUpdate update;
    if (state == nullptr) {
        return update;
    }
    const float magnitude = sample < 0.0f ? -sample : sample;
    const bool clipping = magnitude >= threshold;
    if (clipping) {
        if (!state->active) {
            state->active = true;
            state->startFrame = currentFrame;
            state->length = 0;
        }
        ++state->length;
    } else if (state->active) {
        update.runEnded = true;
        update.startFrame = state->startFrame;
        update.length = state->length;
        state->active = false;
        state->length = 0;
    }
    return update;
}

// Forces a still-open run closed, e.g. at DSP teardown, so a clip run that is
// still ringing when playback stops is not silently lost. Same result shape as
// updateClipRun's runEnded case.
inline ClipRunUpdate flushClipRun(ClipRunState* state)
{
    ClipRunUpdate update;
    if (state == nullptr || !state->active) {
        return update;
    }
    update.runEnded = true;
    update.startFrame = state->startFrame;
    update.length = state->length;
    state->active = false;
    state->length = 0;
    return update;
}

// Buffer-granularity underrun probe. `previousArrivalNs < 0` means "no
// previous block to compare against" (first callback, or the tracker was just
// reset) and the call is not comparable. `previousBlockFrames`/`sampleRateHz`
// describe how long the PREVIOUS block's audio should take to consume;
// comparing that to how long actually elapsed before THIS callback arrived is
// what makes this a buffer-level probe rather than the file/multi-second-level
// bass_audio_health one.
struct LateCallbackProbe {
    bool valid = false;
    qint64 lateMs = 0;  // wall-clock arrival minus expected arrival, in ms.
};

inline LateCallbackProbe computeLateCallback(
    qint64 previousArrivalNs,
    quint32 previousBlockFrames,
    quint32 sampleRateHz,
    qint64 currentArrivalNs)
{
    LateCallbackProbe probe;
    if (previousArrivalNs < 0 || sampleRateHz == 0 || previousBlockFrames == 0) {
        return probe;
    }
    const qint64 expectedBlockNs =
        (static_cast<qint64>(previousBlockFrames) * 1000000000LL)
        / static_cast<qint64>(sampleRateHz);
    const qint64 expectedArrivalNs = previousArrivalNs + expectedBlockNs;
    probe.valid = true;
    probe.lateMs = (currentArrivalNs - expectedArrivalNs) / 1000000LL;
    return probe;
}

inline bool isLate(const LateCallbackProbe& probe, qint64 thresholdMs)
{
    return probe.valid && probe.lateMs > thresholdMs;
}

// One glitch event: pushed into the lock-free ring by the DSP callback (see
// PreviewAudioOutputGlitchRing.h) and, on the worker-thread drain side, handed
// straight to glitchEventPayload below. Kept as one POD shape end to end so
// there is exactly one place that knows the field meanings.
//
// `channel` is not meaningful for LateCallback (left at its default 0).
// `magnitude` holds the step delta for Step, and is unused (0) for Clip; for
// LateCallback it holds the lateness in milliseconds. `length` holds the clip
// run length in samples for Clip, and the just-delivered block's frame count
// for LateCallback; unused (0) for Step.
struct GlitchEvent {
    GlitchKind kind = GlitchKind::Step;
    quint8 channel = 0;
    quint64 frame = 0;
    double magnitude = 0.0;
    quint32 length = 0;
};

// Formats one drained event for the audio debug log. Field naming mirrors
// bass_status/bass_audio_health's key=value style. `sampleRateHz == 0` renders
// mixer_pos as -1.0000 (unreadable, matches PreviewAudioHealth.h's -1 sentinel
// convention) rather than dividing by zero.
inline QString glitchEventPayload(
    quint64 transactionId,
    quint32 sampleRateHz,
    const GlitchEvent& event)
{
    const double positionSeconds = sampleRateHz > 0
        ? static_cast<double>(event.frame) / static_cast<double>(sampleRateHz)
        : -1.0;
    switch (event.kind) {
    case GlitchKind::Step:
        return QStringLiteral(
                   "bass_output_glitch kind=step txn=%1 mixer_pos=%2 channel=%3 magnitude=%4")
            .arg(transactionId)
            .arg(positionSeconds, 0, 'f', 4)
            .arg(event.channel)
            .arg(event.magnitude, 0, 'f', 4);
    case GlitchKind::Clip:
        return QStringLiteral(
                   "bass_output_glitch kind=clip txn=%1 mixer_pos=%2 channel=%3 samples=%4")
            .arg(transactionId)
            .arg(positionSeconds, 0, 'f', 4)
            .arg(event.channel)
            .arg(event.length);
    case GlitchKind::LateCallback:
    default:
        return QStringLiteral(
                   "bass_output_glitch kind=late_callback txn=%1 mixer_pos=%2 late_ms=%3 block_frames=%4")
            .arg(transactionId)
            .arg(positionSeconds, 0, 'f', 4)
            .arg(static_cast<qint64>(event.magnitude))
            .arg(event.length);
    }
}

inline QString glitchDroppedPayload(quint64 transactionId, quint64 droppedCount)
{
    return QStringLiteral("bass_output_glitch_dropped txn=%1 count=%2")
        .arg(transactionId)
        .arg(droppedCount);
}

}  // namespace miacode::preview_audio::output_glitch
