// Standalone miniaudio implementation TU for the latency_offset_batch dev tool.
//
// The main app gets the miniaudio implementation from
// src/audio/MiniaudioPreviewAudioBackend.cpp, but this CLI links only
// LatencyAnalysis.cpp (which calls ma_decoder_*), so it needs its own copy of
// the implementation. Decode-only — MA_NO_DEVICE_IO drops the platform audio
// backends (WASAPI/CoreAudio/...) and their link dependencies, since we never
// open a playback/capture device here.

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#include "../../../third_party/miniaudio/miniaudio.h"
