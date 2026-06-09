#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include "core/scene/PreviewFrameState.h"
#include "preview/runtime/PreviewSceneAssetRepository.h"

class QQuickWindow;
class PreviewQuickSceneRoot;
struct VideoExportTask;

namespace miacode::cover_export {

// In-process, offscreen renderer of a SINGLE chart frame at an arbitrary time T,
// for the cover composer's "chart frame" layer. Mirrors CoverComposerView's
// in-process capture rule: a bare QQuickWindow hosting a PreviewQuickSceneRoot
// (the SAME C++ chart scene the live preview + video export use), shown off-
// screen at opacity 0, captured with grabWindow() on the process RHI (D3D11 on
// Windows). It is NOT the export worker (that forces OpenGL globally + is a whole
// subprocess — using it to grab one frame for a GUI scrubber would be absurd).
//
//   ⚠ Same two hard constraints as CoverComposerView (each cost a crash to
//   learn — keep both): NO QQuickView (re-registers QtQuick modules → crash in
//   the packaged layout), NO forced OpenGL / manual QRhi (in-process the app's
//   RHI is D3D11; forcing OpenGL hard-crashes). PreviewQuickSceneRoot is a plain
//   C++ QQuickItem, so no QQmlEngine is needed at all.
//
// The "arbitrary T" decoupling that looked scary in the handoff is trivial here:
// the heavy per-chart data — note markers, skin/judge sprite atlases, muri
// report, render settings — is loaded ONCE in bootstrap() from the export task;
// each renderAt() only moves the playhead and re-grabs the WARM scene, so
// scrubbing the frame-picker never re-parses. (This is exactly what the video
// export does per output frame; cf. VideoExportQuickRenderBackend::bootstrap +
// applyExportFrameTick — only playheadSeconds changes frame to frame.)
//
// Layers: kPreviewExportOverlayRenderLayers (everything EXCEPT the song-
// background media), captured over transparent, so the playfield (outline ring +
// notes + judge) composites cleanly as a layer over the cover's own background.
//
// GUI-thread only; not thread-safe.
class SceneFrameRenderer
{
public:
    SceneFrameRenderer();
    ~SceneFrameRenderer();
    SceneFrameRenderer(const SceneFrameRenderer&) = delete;
    SceneFrameRenderer& operator=(const SceneFrameRenderer&) = delete;

    // Load the skin + build the base frame state from the export task. Returns
    // false (and sets *errorMessage) only if the skin assets can't load. A task
    // with no note markers still bootstraps (it would render an empty playfield);
    // callers that need notes should gate on task.noteMarkers beforehand.
    bool bootstrap(const VideoExportTask& task, QString* errorMessage = nullptr);
    bool isReady() const { return ready_; }

    // Total content duration (seconds) carried by the bootstrapped task — the
    // natural max for a frame-picker slider.
    double contentDurationSeconds() const { return contentDurationSeconds_; }

    // Render the chart at playhead `seconds` into a square `sidePx`×`sidePx`
    // ARGB32 image with a transparent background. Returns a null QImage + sets
    // *errorMessage on failure.
    QImage renderAt(double seconds, int sidePx, QString* errorMessage = nullptr);

    // The bootstrapped base frame state. The cover composer's LIVE edit scene (an
    // in-QML PreviewQuickSceneRoot — A2) reads THIS SAME pointer so scrubbing /
    // playback only moves the playhead with zero readback (the offscreen grab in
    // renderAt() is reserved for the export path). Stays valid for this renderer's
    // lifetime — keep the renderer alive while any live scene references it, and
    // detach the live scene's pointer before destroying the renderer.
    const miacode::preview::scene::PreviewFrameState* frameState() const { return &frameState_; }

    // Move the shared playhead WITHOUT grabbing, for the live edit scene. The
    // export path still calls renderAt() to grab a still at the chosen time.
    void setPlayheadSeconds(double seconds) { frameState_.playheadSeconds = seconds; }
    double playheadSeconds() const { return frameState_.playheadSeconds; }

private:
    bool ensureWindow(QString* errorMessage);

    miacode::preview::runtime::PreviewSceneAssetRepository assets_;
    miacode::preview::scene::PreviewFrameState frameState_;
    QQuickWindow* window_ = nullptr;
    PreviewQuickSceneRoot* sceneRoot_ = nullptr;   // owned by window_'s content item
    bool ready_ = false;
    bool warmedUp_ = false;
    double contentDurationSeconds_ = 0.0;
    // Monotonic so a re-bootstrap always changes frameState_.sceneContentRevision
    // and the scene root's prepared cache rebuilds (a hardcoded constant would let
    // a second bootstrap keep the prior chart's prepared notes).
    quint64 sceneContentRevision_ = 0;
};

}  // namespace miacode::cover_export
