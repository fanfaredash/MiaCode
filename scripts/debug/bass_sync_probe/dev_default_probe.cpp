// Measures when BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE) stops being accepted.
//
// On Windows the preview engine disables BASS's "follow the default device" mode once
// per process, under std::call_once, before it binds a concrete endpoint
// (disableBassDefaultDeviceEntry in src/audio/BassPreviewAudioBackend_EngineInit.cpp).
// If any other BASS user in the process already initialized a device, for example the
// waveform cache's no-sound decode device 0, that call fails and the cached failure keeps
// the preview engine down until the process restarts.
//
// The config window is process-global, so every case must run in a fresh process:
//   ./dev_default_probe <case>
// Cases: fresh, init0, init0free, init0thread, enum, getdevice, plugin, setfirst
// The Windows tester entry point (no compiler needed) is
// scripts/debug/first_play_probe/Run_FirstPlayProbe.bat.
#include <cstdio>
#include <cstring>
#include <thread>

#include "bass.h"

namespace {

void tryDisable(const char* label)
{
    const BOOL ok = BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE);
    std::printf("RESULT %-40s set_dev_default_false=%d err=%d\n",
                label, ok ? 1 : 0, ok ? 0 : BASS_ErrorGetCode());
}

bool initNoSound()
{
    const BOOL ok = BASS_Init(0, 44100, BASS_DEVICE_NOSPEAKER, nullptr, nullptr);
    std::printf("BASS_Init(0,NOSPEAKER)=%d err=%d\n", ok ? 1 : 0, ok ? 0 : BASS_ErrorGetCode());
    return ok != FALSE;
}

HPLUGIN loadOpusPlugin()
{
#ifdef _WIN32
    return BASS_PluginLoad(reinterpret_cast<const char*>(L"bassopus.dll"), BASS_UNICODE);
#elif defined(__APPLE__)
    return BASS_PluginLoad("libbassopus.dylib", 0);
#else
    return BASS_PluginLoad("libbassopus.so", 0);
#endif
}

}  // namespace

int main(int argc, char** argv)
{
    const char* c = argc > 1 ? argv[1] : "fresh";
    std::printf("bass_version=0x%08x case=%s\n", BASS_GetVersion(), c);
    if (!std::strcmp(c, "fresh")) {
        tryDisable("fresh process");
    } else if (!std::strcmp(c, "init0")) {
        initNoSound();
        tryDisable("after BASS_Init(0), device held");
    } else if (!std::strcmp(c, "init0free")) {
        initNoSound();
        BASS_SetDevice(0);
        BASS_Free();
        tryDisable("after BASS_Init(0) + BASS_Free");
    } else if (!std::strcmp(c, "init0thread")) {
        // Production shape: the waveform pool thread initializes device 0, the audio
        // worker thread later tries to disable the default-device mode.
        std::thread([] { initNoSound(); }).join();
        tryDisable("after BASS_Init(0) on another thread");
    } else if (!std::strcmp(c, "enum")) {
        BASS_DEVICEINFO info;
        int count = 0;
        for (DWORD i = 0; BASS_GetDeviceInfo(i, &info); ++i) {
            ++count;
        }
        std::printf("enumerated=%d\n", count);
        tryDisable("after BASS_GetDeviceInfo enumeration");
    } else if (!std::strcmp(c, "getdevice")) {
        BASS_GetDevice();
        tryDisable("after BASS_GetDevice only");
    } else if (!std::strcmp(c, "plugin")) {
        const HPLUGIN plugin = loadOpusPlugin();
        std::printf("BASS_PluginLoad=%u err=%d\n", plugin, plugin ? 0 : BASS_ErrorGetCode());
        tryDisable("after BASS_PluginLoad");
    } else if (!std::strcmp(c, "setfirst")) {
        // Control for the fix direction: disable first, then a no-sound init must still
        // work and must leave the setting disabled.
        tryDisable("fresh process (first)");
        initNoSound();
        std::printf("RESULT %-40s dev_default=%u\n",
                    "config after BASS_Init(0)", BASS_GetConfig(BASS_CONFIG_DEV_DEFAULT));
    } else {
        std::fprintf(stderr, "unknown case: %s\n", c);
        return 2;
    }
    return 0;
}
