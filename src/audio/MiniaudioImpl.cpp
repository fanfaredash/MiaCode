// Single translation unit that emits the miniaudio implementation
// symbols. Kept dedicated so any target needing miniaudio (preview
// runtime, export plan, peak-normalization spec) can link the impl
// without dragging in MiniaudioPreviewAudioBackend.cpp's preview-engine
// dependencies (SoundTouch, debug log, gameplay config, etc.).

#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio/miniaudio.h"
