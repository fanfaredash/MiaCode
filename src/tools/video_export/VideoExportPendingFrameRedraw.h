#pragma once

#include <deque>
#include <utility>
#include <vector>

namespace miacode::video_export {

// Redraws every frame the offscreen readback pipeline still holds, in
// submission order, appending one payload per frame to `redrawnFrames`.
//
// The pipelined readback keeps one or two already-submitted frames in flight
// (PBO ping-pong plus the convert worker's extra depth). When a pipeline step
// fails we reset the backend and fall back to synchronous rendering, and those
// in-flight frames then have no other way out: the raw video pipe carries pixel
// bytes only, so a frame that is never delivered does not leave a gap that a
// later stage could notice or repair — it shifts every subsequent frame one
// slot earlier, and the chart runs ahead of the audio and background for the
// rest of the export.
//
// `renderFrame(pending, &payload)` redraws one pending frame synchronously and
// returns false when it cannot. The redraw has to use the parameters the frame
// was submitted with (its own export time, HUD flags and intro state), not the
// current frame's, which is why the backlog carries them. A failed redraw is
// reported to the caller so the export fails instead of shipping a stream that
// is short by exactly the frames nobody can see.
template <typename PendingFrame, typename ReadyFrame, typename RenderFrameFn>
bool redrawPendingPipelineFrames(
    std::deque<PendingFrame>* pendingFrames,
    const RenderFrameFn& renderFrame,
    std::vector<ReadyFrame>* redrawnFrames)
{
    if (pendingFrames == nullptr || redrawnFrames == nullptr) {
        return false;
    }
    while (!pendingFrames->empty()) {
        PendingFrame pending = std::move(pendingFrames->front());
        pendingFrames->pop_front();
        ReadyFrame payload;
        if (!renderFrame(pending, &payload)) {
            return false;
        }
        redrawnFrames->push_back(std::move(payload));
    }
    return true;
}

}  // namespace miacode::video_export
