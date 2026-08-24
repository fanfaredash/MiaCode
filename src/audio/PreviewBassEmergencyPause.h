#pragma once

#include <QtGlobal>

namespace miacode::preview_audio {

// A tiny process-local control plane for the active Windows BASS output.  The Core
// Audio callback uses it to stop the old endpoint before the audio worker has an
// opportunity to drain a queued command.  Stream lifetime remains worker-owned.
struct BassEmergencyPauseResult {
    bool available = false;
    bool attempted = false;
    bool paused = false;
    int outputDeviceIndex = -1;
    int nativeErrorCode = 0;
    qint64 startedMonotonicNs = 0;
    qint64 finishedMonotonicNs = 0;
};

class PreviewBassEmergencyPause final
{
public:
    // Called only after BASS_Init has successfully bound a concrete output.
    static void arm(int outputDeviceIndex);
    // Called before the backend frees its BASS device/streams, and on teardown.
    static void disarm();
    // Safe for Core Audio's MTA callback thread. It never accesses stream handles.
    static BassEmergencyPauseResult pauseActiveOutput();
};

}  // namespace miacode::preview_audio
