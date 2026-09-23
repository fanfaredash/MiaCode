#pragma once

namespace miacode::preview_audio {

// Windows: BASS_Init(-1) follows the system default endpoint unless
// BASS_CONFIG_DEV_DEFAULT is disabled, and the preview engine instead binds each
// lifetime to a concrete Core Audio endpoint. The setting is process-wide and stops
// accepting changes at the process's first BASS device enumeration or BASS_Init --
// any device, any thread; BASS_Free does not reopen it. The preview engine is not
// reliably that first caller (an uncached chart's waveform decode inits the no-sound
// device earlier), so main() calls this before anything can reach BASS, and later
// callers receive the cached result of that single attempt.
//
// Returns whether default-device following is disabled; on failure errorCode
// receives the BASS error code. Other platforms do not bind concrete endpoints, so
// this is a no-op that returns true there.
bool disableBassDefaultDeviceEntry(int* errorCode = nullptr);

}  // namespace miacode::preview_audio
