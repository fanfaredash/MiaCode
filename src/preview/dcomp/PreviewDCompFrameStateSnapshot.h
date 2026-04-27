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

#include "preview/scene/PreviewArcDescriptor.h"
#include "preview/scene/PreviewCircleDescriptor.h"
#include "preview/scene/PreviewSpriteDescriptor.h"

#include <QSharedPointer>
#include <QSize>
#include <QVector>

class QImage;

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

    // Phase 3.3 — Real layer-state output for the render thread to draw.
    // Descriptor pointers reference QImage objects whose lifetime is
    // either pinned by PreviewRuntime (state.assets.*) or carried in
    // retainedImages below. The render thread treats these as read-only
    // and copies them into ID3D11Texture2D backing through a content-
    // fingerprint cache (Phase 3.3b).
    miacode::preview::scene::PreviewSpriteDescriptors sprites;

    // Phase 3.5 — non-sprite primitives. Circles (Phase 3.5a, used by
    // muri_pad + muri_action) are solid-fill ellipses with optional
    // stroke. Arcs (Phase 3.5b, used by touch_hold) are clipped textured
    // quads. Both share the snapshot's flat-vector storage with sprites
    // and are ordered via `batches` below.
    miacode::preview::scene::PreviewCircleDescriptors circles;
    miacode::preview::scene::PreviewArcDescriptors arcs;

    // Z-ordered draw command list. Each batch references a contiguous
    // run inside one of the primitive vectors (sprites/circles/arcs).
    // The pipeline iterates batches in order, switching shaders/state
    // as needed. The legacy QSG path enforces z-order via its node
    // tree; this is the equivalent.
    enum class BatchType : qint8 { Sprites, Circles, Arcs };
    struct DrawBatch {
        BatchType type = BatchType::Sprites;
        qint32 firstIndex = 0;
        qint32 count = 0;
    };
    QVector<DrawBatch> batches;

    // Per-frame composited QImages produced by some layer-state builders
    // (head, judge_effect, slide_motion, touch, touch_hold, track). The
    // raw const QImage* pointers in `sprites` reference into these, so
    // the snapshot must keep the shared pointers alive until the render
    // thread is done with the frame. Without this the QImages would be
    // freed at end-of-statement on the GUI thread and the render thread
    // would dereference dangling pointers.
    QVector<QSharedPointer<QImage>> retainedImages;
};

}  // namespace miacode::preview::dcomp
