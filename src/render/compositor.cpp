#include "render/compositor.h"

#include <algorithm>

namespace miacode::render {

void Compositor::registerSource(std::unique_ptr<IPreviewSource> source)
{
    if (source == nullptr) {
        return;
    }
    sources_.push_back(std::move(source));
    // Stable sort by z-order; equal-z sources retain registration
    // order. The legacy QSG path uses registration order as a
    // tiebreak for layers at the same z (e.g., touch_hold and
    // slide_motion); we preserve that behaviour.
    std::stable_sort(
        sources_.begin(),
        sources_.end(),
        [](const std::unique_ptr<IPreviewSource>& a,
           const std::unique_ptr<IPreviewSource>& b) {
            return a->zOrder() < b->zOrder();
        });
}

void Compositor::buildSnapshot(
    const PreviewBuildContext& ctx,
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    // Linear walk in z-order. Each source's contributeToSnapshot is
    // expected to be cheap (~0.05–0.2 ms typical) so the total walk
    // for ~15 sources stays well under 5 ms per frame on the GUI
    // thread — that's the existing tick_build_stats budget.
    for (const std::unique_ptr<IPreviewSource>& source : sources_) {
        if (!source->isEnabled(ctx)) {
            continue;
        }
        source->contributeToSnapshot(ctx, snapshot);
    }
}

}  // namespace miacode::render
