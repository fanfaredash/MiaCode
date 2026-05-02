#pragma once

// Phase 2 of the v2-refactor: IPreviewSource is the source side of the
// OBS-style source/compositor architecture. Each renderable thing
// (chart layers, timeline layers, the FPS HUD, the static stage
// background, the libmpv video background in Phase 4) implements this
// single-method interface and registers with a `Compositor`. The
// compositor walks registered sources in z-order and lets each one
// emit its sprite/circle/arc/firework descriptors into a shared
// `PreviewDCompFrameStateSnapshot`.
//
// Direct equivalent in OBS Studio: `obs_source_info` in libobs/obs-source.h.
// Their version is C with function pointers (id, type, output_flags,
// create/destroy, video_render, video_tick, audio_render, …) because
// it has to support dynamic plugin loading; ours is C++ virtual because
// we only ever load sources from this single binary. The functional
// shape — "z-ordered list of things that produce frame data, walked
// each tick" — is identical.
//
// Differences from OBS:
//   - OBS sources produce GPU output (gs_effect calls, gs_texture
//     handles). Ours produce *descriptors* — POD structs the render
//     thread later submits to D3D11. The render thread reads a
//     snapshot built entirely on the GUI thread, which lets the
//     compositor walk be lock-free and the GPU submission be a single
//     batched flush.
//   - OBS sources have lifecycle settings, audio output, capture
//     flags, and arbitrary user data. Ours are stateless wrappers
//     around the existing buildPreview*LayerState functions.
//   - OBS has a global source registry (`obs_source_create` looks up
//     by id). Ours is local — each Compositor owns its own
//     unique_ptr<IPreviewSource> vector.

#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewLayerOrder.h"
#include "render/backend_d3d11/PreviewDCompFrameStateSnapshot.h"
#include "timeline/TimelineSceneState.h"

#include <QImage>
#include <QRectF>
#include <QSize>

namespace miacode::render {

// Read-only context passed to every IPreviewSource each compositor
// walk. References-by-const so sources can't accidentally mutate the
// shared frame state. Cheap to construct (one struct copy of refs +
// scalars) so the surface can build a fresh one per snapshot.
//
// OBS equivalent: the per-source obs_source_t pointer (state) plus
// the global obs core's frame timing. Ours collapses both into a
// single value type.
struct PreviewBuildContext {
    const miacode::preview::scene::PreviewFrameState& frameState;
    QSize  logicalSize;
    QRectF stageRect;
    QRectF playfieldRect;
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags;
    double playheadSeconds = 0.0;
    // Effective device pixel ratio of the host QQuickWindow. Sources
    // that rasterise text (HudSource) need this so font hinting and
    // anti-aliasing land at physical pixel boundaries — without it,
    // a logical-size QImage gets bilinear-upscaled by DPR through the
    // D3D11 sampler and the text looks blurry on a HiDPI display.
    qreal  devicePixelRatio = 1.0;

    // Phase 2d — timeline state. Non-null only when the compositor is
    // running for a RenderView that includes timeline layers. The
    // chart-only preview path (current default) sets this to nullptr,
    // and timeline sources skip with a single null-check. Pointer is
    // non-owning — its lifetime is the host window/scene's lifetime.
    const miacode::timeline::TimelineSceneState* timelineState = nullptr;

    // Phase 4c-8 — image-mode background override. Non-null when the
    // chart has a resolved bg.{jpg,png,jpeg} that needs to be painted
    // by DComp's StageBackgroundSource (because the QML
    // PreviewStageMediaItem beneath the DComp HWND is occluded by the
    // layered window's per-window opacity). Pre-existing v2-refactor
    // composition gap; this field is the route to fixing it without
    // restructuring the QML/DComp HWND layering.
    QImage stageBackgroundImageOverride;
};

// Single-method (effectively) interface every renderable thing
// implements. Sources are registered with a Compositor, sorted by
// zOrder, walked every snapshot build.
//
// Implementations should:
//   - Be cheap to construct/destroy (sources are owned by unique_ptr;
//     no fancy lifecycles).
//   - Not retain the PreviewBuildContext beyond contributeToSnapshot's
//     scope (the frameState reference is to a GUI-thread-owned
//     PreviewRuntime member that the source must NOT cache).
//   - Append descriptors to the snapshot — never overwrite or mutate
//     existing entries. The Compositor relies on the convention that
//     each source only adds; if a source needs to clear, it shouldn't
//     be a source.
//
// OBS equivalent: every member of obs_source_info that's relevant for
// drawing — id, video_render, video_tick, get_width/height, and the
// per-source state pointer.
class IPreviewSource {
public:
    virtual ~IPreviewSource() = default;

    // Z-order: smaller values render first (back-to-front). The chart
    // skin background is z=0; the FPS HUD is z=top. See
    // PreviewLayerOrder.h for the canonical numerical assignments
    // we'll mirror in the source set.
    //
    // OBS equivalent: scene-item z-order, exposed via
    // obs_sceneitem_get_order_position.
    virtual int zOrder() const = 0;

    // Per-frame gating. Layer-flag-driven (e.g., MuriRenderOptions
    // can hide individual layers). Cheap — single bitand expected.
    //
    // OBS equivalent: combination of obs_source_active and the
    // `enabled` field, plus user-driven scene-item visibility.
    virtual bool isEnabled(const PreviewBuildContext& ctx) const = 0;

    // The hot path. Reads `ctx`, appends sprite/circle/arc/firework
    // descriptors to `snapshot`. Must be cheap (~0.05–0.2 ms typical;
    // the surface's tick_build_stats budget is ~5 ms total across all
    // sources). Idempotent — multiple calls with the same context
    // must produce the same snapshot delta, because PreviewRuntime
    // can emit frameStateChanged more than once per tick.
    //
    // OBS equivalent: video_render(void *data, gs_effect_t *effect),
    // except OBS issues GPU draw calls directly while we just append
    // descriptors that the render thread later batches into D3D11
    // calls.
    virtual void contributeToSnapshot(
        const PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) = 0;
};

}  // namespace miacode::render
