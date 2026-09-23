#include "PreviewBassDefaultDevice.h"

#include <QtGlobal>

#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
#include <mutex>

#include "bass.h"
#endif

namespace miacode::preview_audio {

#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
namespace {

struct DefaultDeviceEntryState {
    std::once_flag once;
    bool disabled = false;
    int errorCode = BASS_OK;
};

DefaultDeviceEntryState& defaultDeviceEntryState()
{
    static DefaultDeviceEntryState state;
    return state;
}

}  // namespace
#endif

bool disableBassDefaultDeviceEntry(int* errorCode)
{
#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
    // One attempt per process: once the window has closed a retry cannot succeed,
    // and it would overwrite the error code of the attempt that mattered.
    DefaultDeviceEntryState& state = defaultDeviceEntryState();
    std::call_once(state.once, [&state] {
        state.disabled = BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE) != FALSE;
        if (!state.disabled) {
            state.errorCode = static_cast<int>(BASS_ErrorGetCode());
        }
    });
    if (errorCode != nullptr) {
        *errorCode = state.errorCode;
    }
    return state.disabled;
#else
    if (errorCode != nullptr) {
        *errorCode = 0;
    }
    return true;
#endif
}

}  // namespace miacode::preview_audio
