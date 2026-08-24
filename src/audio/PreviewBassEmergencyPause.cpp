#include "PreviewBassEmergencyPause.h"

#include <chrono>
#include <mutex>

#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
#include "bass.h"
#endif

namespace miacode::preview_audio {

namespace {

qint64 steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct ActiveBassOutput {
    std::mutex mutex;
    int deviceIndex = -1;
};

ActiveBassOutput& activeBassOutput()
{
    static ActiveBassOutput output;
    return output;
}

}  // namespace

void PreviewBassEmergencyPause::arm(int outputDeviceIndex)
{
#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
    ActiveBassOutput& output = activeBassOutput();
    const std::lock_guard lock(output.mutex);
    output.deviceIndex = outputDeviceIndex;
#else
    Q_UNUSED(outputDeviceIndex);
#endif
}

void PreviewBassEmergencyPause::disarm()
{
#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
    ActiveBassOutput& output = activeBassOutput();
    const std::lock_guard lock(output.mutex);
    output.deviceIndex = -1;
#endif
}

BassEmergencyPauseResult PreviewBassEmergencyPause::pauseActiveOutput()
{
    BassEmergencyPauseResult result;
    result.startedMonotonicNs = steadyNowNs();

#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
    ActiveBassOutput& output = activeBassOutput();
    const std::lock_guard lock(output.mutex);
    result.available = output.deviceIndex >= 0;
    result.outputDeviceIndex = output.deviceIndex;
    if (!result.available) {
        result.finishedMonotonicNs = steadyNowNs();
        return result;
    }

    result.attempted = true;
    const DWORD previousDevice = BASS_GetDevice();
    if (!BASS_SetDevice(static_cast<DWORD>(output.deviceIndex))) {
        result.nativeErrorCode = static_cast<int>(BASS_ErrorGetCode());
    } else if (BASS_Pause()) {
        result.paused = true;
    } else {
        result.nativeErrorCode = static_cast<int>(BASS_ErrorGetCode());
    }

    // BASS selects devices per calling thread. Restore the callback thread's prior
    // selection when it had one; failures here cannot revive the already-paused output.
    if (previousDevice != static_cast<DWORD>(-1)
        && previousDevice != static_cast<DWORD>(output.deviceIndex)) {
        BASS_SetDevice(previousDevice);
    }
#endif

    result.finishedMonotonicNs = steadyNowNs();
    return result;
}

}  // namespace miacode::preview_audio
