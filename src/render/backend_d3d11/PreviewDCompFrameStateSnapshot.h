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

#include "core/scene/PreviewArcDescriptor.h"
#include "core/scene/PreviewCircleDescriptor.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"
#include "core/scene/PreviewSpriteDescriptor.h"
#include "timeline/TimelineSceneState.h"

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
    // Phase 3.5c — judge firework state. Typically zero or one entry
    // per frame (the chart's currently-firing judge effect, if any).
    QVector<miacode::preview::scene::PreviewJudgeFireworkLayerState> fireworks;

    // Phase 2d — timeline primitives. The timeline emits a richer set
    // of primitives than the chart preview (rects with arbitrary fill,
    // line segments with width, triangles for header markers, text
    // labels for lane/header callouts, glyph dots for muri markers).
    // GUI-thread compositor walk pushes these in Phase 2d; the GPU
    // pipelines that consume them are added in Phase 3 alongside the
    // RenderView migration. Until Phase 3 wires those pipelines, the
    // renderer's batch switch defaults to skipping any non-chart batch
    // type — chart preview rendering is unaffected.
    QVector<miacode::timeline::TimelineSceneRect>      timelineRects;
    QVector<miacode::timeline::TimelineSceneLine>      timelineLines;
    QVector<miacode::timeline::TimelineSceneTriangle>  timelineTriangles;
    QVector<miacode::timeline::TimelineSceneTextLabel> timelineTextLabels;
    QVector<miacode::timeline::TimelineSceneGlyph>     timelineGlyphs;
    QVector<miacode::timeline::TimelineSceneSprite>    timelineSprites;
    QVector<miacode::timeline::TimelineSceneHoldSpan>  timelineHoldSpans;

    // Z-ordered draw command list. Each batch references a contiguous
    // run inside one of the primitive vectors (sprites/circles/arcs/
    // fireworks/timelineRects/timelineLines/...). The pipeline iterates
    // batches in order, switching shaders/state as needed. The legacy
    // QSG path enforces z-order via its node tree; this is the
    // equivalent.
    //
    // Timeline-* batch types are reserved for Phase 3+. Adding them now
    // lets the timeline sources compose their batches on the GUI side;
    // the renderer skips them via the default branch until Phase 3.
    enum class BatchType : qint8 {
        Sprites, Circles, Arcs, Fireworks,
        TimelineRects, TimelineLines, TimelineTriangles,
        TimelineTextLabels, TimelineGlyphs,
        TimelineSprites, TimelineHoldSpans,
    };
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
