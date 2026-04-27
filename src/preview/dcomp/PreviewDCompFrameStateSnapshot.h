#pragma once

// Phase 3.2 of the DirectComposition preview path. Defines the snapshot
// struct that crosses the GUI-thread → render-thread boundary.
//
// PreviewDCompFrameStateSnapshot is a POD value type containing only the
// fields the render thread reads each frame. The GUI thread builds a new
// snapshot whenever PreviewRuntime::frameStateChanged fires, hands it off
// via PreviewDCompRenderer::publishSnapshot under a mutex; the render
// thread copies-out the latest published snapshot at the start of each
// frame. POD-by-value plus a single mutex is the simplest correct
// pattern at this scale — locking is held for tens of nanoseconds (one
// memcpy of ~80 bytes), so even a 60Hz publish + 60Hz consume rate has
// negligible contention.
//
// Phase 3.2 carries only the minimum data needed for the placeholder
// animation (playhead drives a horizontal quad shift). Phase 3.3+ will
// extend this struct with full sprite descriptor lists from
// PreviewSceneStateBuilder — same pattern, just more data. The fields
// here are deliberately small and trivially copyable so adding more
// stays cheap.

#include <QSize>

namespace miacode::preview::dcomp {

struct PreviewDCompFrameStateSnapshot
{
    // Monotonically-increasing revision number. Lets the renderer tell
    // whether the snapshot is fresh, and lets diagnostic tooling
    // distinguish "no new state" from "render thread is stuck".
    qint64 revision = 0;

    // Audio-clock-aligned playhead in seconds, taken straight from
    // PreviewRuntime::frameState_.playheadSeconds. Phase 3.2 uses this
    // to drive a test animation; Phase 3.3+ uses it as the time
    // coordinate for sprite descriptor selection.
    double playheadSeconds = 0.0;

    // Logical (CSS-pixel) size of the QML preview region. Taken from
    // QQuickWindow::width/height × DPR or from a future placeholder's
    // geometry (Phase 4). Phase 3.2 uses this to compute the projection
    // matrix for the test quad.
    QSize sceneLogicalSize;

    // True if playback is currently advancing the playhead. Optional
    // hint for the renderer — Phase 3.2 ignores this; later phases may
    // use it to suppress animation when paused.
    bool playing = false;
};

}  // namespace miacode::preview::dcomp
