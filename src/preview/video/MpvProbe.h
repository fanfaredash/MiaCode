#pragma once

namespace miacode::preview::video {

// Phase 0 of the v2-refactor video-background plan: confirm libmpv is
// present in the running build and log its API version to the runtime
// channel. No functional change yet — chart playback still uses the
// existing image-background path. Phase 4 brings real decode through
// MpvVideoSource.
//
// Returns true when mpv_client_api_version was successfully called and
// the major version is non-zero. False on any failure (DLL missing,
// symbol resolution failure, zero-version sentinel). The caller logs
// the outcome regardless; the boolean is for future code paths that
// want to gate libmpv-using features on probe success.
bool probeLibmpvAtStartup();

}  // namespace miacode::preview::video
