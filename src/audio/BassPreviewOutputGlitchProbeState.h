#pragma once

#include <array>

#include <QtGlobal>

#include "PreviewAudioOutputGlitchProbe.h"
#include "PreviewAudioOutputGlitchRing.h"

// Audio-thread-owned state for the master-mixer output-glitch DSP callback
// (see PreviewAudioOutputGlitchProbe.h for what it measures and why). This
// header has no bass.h dependency -- BassPreviewAudioBackend.h includes it
// unconditionally so the state can be a plain member even when
// MIACODE_HAS_BASS_AUDIO is not defined; the DSP glue that actually touches
// bass.h and mutates this state lives in BassPreviewAudioBackend_EngineInit.cpp.
namespace miacode::audio::bass_detail {

// Bound to a fixed stereo layout. miacode::preview_audio::kMixChannels
// (PreviewAudioMixConfig.h) is the single source of truth for the mixer's
// actual channel count; the DSP glue clamps against both so a future mixer
// reconfiguration cannot walk past this array.
inline constexpr int kOutputGlitchProbeMaxChannels = 2;

// Every field here is written ONLY from the BASS DSP callback thread except
// `ring`, which is the lock-free handoff to the worker thread that drains it
// (see PreviewAudioOutputGlitchRing.h's own header comment for the
// producer/consumer split). The backend re-creates this struct wholesale on
// each engine (re)init, so a rebuilt mixer starts every tracker fresh.
struct OutputGlitchProbeState {
    std::array<miacode::preview_audio::output_glitch::StepDetectorState, kOutputGlitchProbeMaxChannels> step{};
    std::array<miacode::preview_audio::output_glitch::ClipRunState, kOutputGlitchProbeMaxChannels> clip{};
    quint64 frameCursor = 0;
    qint64 lastCallbackArrivalNs = -1;
    quint32 lastBlockFrames = 0;
    quint32 sampleRateHz = 0;
    int channelCount = 0;
    miacode::preview_audio::output_glitch::GlitchRing ring;

    // `ring` holds atomics (see GlitchRing::reset's own comment on why it is
    // reset in place rather than copy-assigned), which makes this struct as a
    // whole non-copyable/non-movable by default too. The backend re-arms this
    // in place at attach/detach instead of assigning a fresh instance over it.
    void reset()
    {
        step.fill(miacode::preview_audio::output_glitch::StepDetectorState());
        clip.fill(miacode::preview_audio::output_glitch::ClipRunState());
        frameCursor = 0;
        lastCallbackArrivalNs = -1;
        lastBlockFrames = 0;
        sampleRateHz = 0;
        channelCount = 0;
        ring.reset();
    }
};

}  // namespace miacode::audio::bass_detail
