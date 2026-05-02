#pragma once

// Phase 2 of the v2-refactor: Compositor walks an ordered list of
// IPreviewSource instances and lets each one contribute its
// descriptors to a shared PreviewDCompFrameStateSnapshot. The
// PreviewDCompSurface's monolithic snapshot-build code (the giant
// onRuntimeFrameStateChanged → buildAndPublishSnapshot path) becomes
// a thin compositor invocation in Phase 2c.
//
// Direct equivalent in OBS Studio: the libobs source-iteration loop
// in obs_render_main_view (libobs/obs-display.c). They walk the
// active scene's source list, call video_render on each, and the
// gs_* graphics state forms the output. Ours is the same shape but
// terminates at a snapshot rather than at a swap chain — the render
// thread later consumes the snapshot.
//
// Differences from OBS:
//   - OBS has nested scenes, transitions, transforms-per-source. Ours
//     is a flat z-ordered list. Simpler, faster traversal, no support
//     for the editor patterns (transitions etc.) we don't need.
//   - OBS's compositor mutates global gs_* state; ours just appends
//     to an out-parameter. The compositor itself is therefore
//     thread-affine to the GUI thread but otherwise stateless.
//   - OBS supports dynamic source add/remove during a session. Ours
//     allows registerSource any time but doesn't remove (sources
//     live for the lifetime of the compositor; Phase 3+ gates them
//     via isEnabled).

#include "render/backend_d3d11/PreviewDCompFrameStateSnapshot.h"
#include "render/source.h"

#include <memory>
#include <vector>

namespace miacode::render {

class Compositor
{
public:
    Compositor() = default;
    ~Compositor() = default;

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    Compositor(Compositor&&) = default;
    Compositor& operator=(Compositor&&) = default;

    // Register a source. Sources are owned by the compositor.
    // Re-sorted by zOrder() on every register so iteration order is
    // deterministic regardless of registration sequence. Phase 2 does
    // all registration during Compositor construction (one-shot); a
    // future phase that supports dynamic add/remove will need a
    // different invariant.
    //
    // OBS equivalent: obs_source_create + obs_scene_add. Theirs has
    // a release/destroy lifecycle managed by reference counting; ours
    // is unique_ptr ownership.
    void registerSource(std::unique_ptr<IPreviewSource> source);

    // Walk all enabled sources in z-order, calling
    // contributeToSnapshot on each. The output snapshot is the
    // accumulated draw list the render thread will later consume.
    //
    // The caller is responsible for setting snapshot.revision and
    // any non-source-controlled fields (playheadSeconds, sceneLogicalSize)
    // before invoking. The compositor only fills the source-driven
    // fields (sprites, circles, arcs, fireworks, batches, retainedImages).
    //
    // OBS equivalent: obs_render_main_view's source-iteration loop,
    // collapsed into a single call.
    void buildSnapshot(
        const PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot);

    // Number of registered sources. Diagnostic / test helper only.
    int sourceCount() const { return static_cast<int>(sources_.size()); }

private:
    // Sorted by IPreviewSource::zOrder(). Stable sort: sources with
    // equal z keep registration order, which matters for layers like
    // touch_hold + slide_motion that share a z and need a deterministic
    // tiebreak.
    std::vector<std::unique_ptr<IPreviewSource>> sources_;
};

}  // namespace miacode::render
