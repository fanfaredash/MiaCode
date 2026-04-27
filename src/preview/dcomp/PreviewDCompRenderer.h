#pragma once

// Phase 2 of the DirectComposition preview path. PreviewDCompRenderer owns a
// dedicated render thread that drives the swap chain owned by
// PreviewDCompCore. The thread blocks on the swap chain's
// FRAME_LATENCY_WAITABLE_OBJECT — a GPU fence that signals when DXGI is
// ready to enqueue the next present — so the thread wakes exactly once per
// vsync slot, not on a Qt timer or signal hop. This is the §2 frame-pacing
// mechanism in the DComp implementation plan.
//
// Phase 2 deliverable: animate the test rectangle by cycling its clear
// colour through hue rotation so a casual visual check confirms the thread
// is producing frames. Phase 3 replaces the colour cycle with the real
// sprite pipeline.

#include "preview/dcomp/PreviewDCompCore.h"
#include "preview/dcomp/PreviewDCompFrameStateSnapshot.h"
#include "preview/dcomp/PreviewDCompSpritePipeline.h"

#include <QObject>
#include <QSize>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace miacode::preview::dcomp {

class PreviewDCompRenderer : public QObject
{
    Q_OBJECT
public:
    explicit PreviewDCompRenderer(QObject* parent = nullptr);
    ~PreviewDCompRenderer() override;

    PreviewDCompRenderer(const PreviewDCompRenderer&) = delete;
    PreviewDCompRenderer& operator=(const PreviewDCompRenderer&) = delete;

    // Spin up the render thread. `core` must remain alive until stop()
    // returns. Idempotent: a running renderer is left untouched.
    bool start(PreviewDCompCore* core);

    // Stops the thread and joins it. Safe to call from the GUI thread or
    // from the destructor. Idempotent.
    void stop();

    bool isRunning() const;

    // Queue a swap-chain resize for the renderer to apply between frames.
    // Cheap, lock-protected, doesn't block the caller. The actual
    // ResizeBuffers call happens on the render thread between
    // `WaitForSingleObject` returning and the next present, which is the
    // safe spot — D3D11 doesn't allow ResizeBuffers while a present is in
    // flight, and there are no outstanding frames at that point.
    void requestResize(QSize newPixelSize);

    // Diagnostics counter. Read from any thread; written from the render
    // thread.
    qint64 framesRendered() const;

    // Phase 3.2: GUI thread calls this whenever a new snapshot is
    // available (typically from PreviewRuntime::frameStateChanged).
    // Cheap — copies ~80 bytes under a brief mutex. The render thread
    // pulls the latest snapshot at the top of each frame.
    void publishSnapshot(const PreviewDCompFrameStateSnapshot& snapshot);

private:
    void renderLoop();
    void processPendingResizeLocked();
    void renderAnimatedFrame();

    PreviewDCompCore* core_ = nullptr;
    PreviewDCompSpritePipeline pipeline_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> stopRequested_{ false };
    std::atomic<qint64> framesRendered_{ 0 };

    // Latest published frame state. The GUI thread writes via
    // publishSnapshot under snapshotMutex_; the render thread copies it
    // out at the top of each frame. POD by value, ~80 bytes — the lock
    // is held for tens of nanoseconds (one struct copy).
    mutable std::mutex snapshotMutex_;
    PreviewDCompFrameStateSnapshot snapshot_;

    // Pending-resize queue. The mutex is held only while reading or writing
    // `pendingResizeSize_` and `pendingResizeRequested_`; the renderer
    // copies the pending request out under the lock and drops the lock
    // before calling into Core::resize().
    std::mutex pendingResizeMutex_;
    QSize pendingResizeSize_;
    bool pendingResizeRequested_ = false;

    // For colour-cycle animation. Captured at start() so the cycle starts
    // at zero time on each new render thread.
    std::chrono::steady_clock::time_point startTime_;
};

}  // namespace miacode::preview::dcomp
