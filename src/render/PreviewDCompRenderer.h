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

#include "render/backend_d3d11/PreviewDCompCore.h"
#include "render/backend_d3d11/PreviewDCompFrameStateSnapshot.h"
#include "render/backend_d3d11/PreviewDCompSpritePipeline.h"
#include "render/backend_d3d11/PreviewDCompTextureCache.h"

#include <QObject>
#include <QSize>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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
    // Phase 4d-fix7-final2 — `displaySize` is the value to use as the
    // visual transform's displaySize when re-applying after the swap
    // chain resize completes. Must travel with the resize request so
    // it stays consistent across rapid GUI-thread updates: without
    // this, a fast enlarge→shrink sequence would queue a resize for
    // size A but by the time the render thread re-applies VT, the
    // global lastVisualTransformDisplaySize_ may have been updated
    // to size B, producing scale != 1.
    void requestResize(QSize newPixelSize, QSize displaySize);

    // Diagnostics counter. Read from any thread; written from the render
    // thread.
    qint64 framesRendered() const;

    // Phase 3.2: GUI thread calls this whenever a new snapshot is
    // available (typically from PreviewRuntime::frameStateChanged).
    // Cheap — copies ~80 bytes under a brief mutex. The render thread
    // pulls the latest snapshot at the top of each frame.
    void publishSnapshot(const PreviewDCompFrameStateSnapshot& snapshot);

    // Phase 4e — pause / resume the render loop. While paused the
    // render thread blocks on a condition variable instead of waking
    // on the frame-latency waitable: zero CPU/GPU usage, no presents
    // queued. Used to throttle while the host window is minimised
    // or hidden, where the DComp visual is invisible to the user
    // anyway. Idempotent and thread-safe; OK to flip rapidly during
    // window state transitions.
    void setPaused(bool paused);

    // True if the render thread is alive AND not in the paused state. The
    // surface uses this to decide whether to publish on its own
    // (paused / scrub case) or wait for the render thread's `presented`
    // signal to drive vsync-aligned publishes (playback case).
    bool isActivelyRendering() const;

    // FPS cap. Sets the SyncInterval passed to ::Present(SyncInterval, 0).
    //   - 1 (default) = vsync rate (display refresh).
    //   - 2           = half rate (e.g., 60 FPS on 120 Hz display).
    //   - 3           = third rate.
    //   - clamped to [1, 4] internally.
    // Computed by the GUI from the user's target FPS option and the
    // display refresh rate (which only Qt's QScreen API exposes). The
    // render thread reads this atomically every present and passes it
    // to Present, so the change takes effect within one frame.
    //
    // Why not "skip presents in software": the DXGI frame-latency
    // waitable signals after a Present consumes a back-buffer slot.
    // Skipping presents starves the waitable, freezing the loop. Using
    // SyncInterval keeps Present in the cycle and lets the waitable
    // pace it correctly.
    void setPresentSyncInterval(unsigned int syncInterval);

signals:
    // Fired from the render thread after each successful Present. The
    // surface listens to this on the GUI thread (Qt::QueuedConnection)
    // and uses it as the publish trigger during playback so consecutive
    // snapshots are sampled at uniform vsync intervals — fixes the
    // playhead-delta variance caused by GUI-tick scheduler jitter when
    // the runtime's frameStateChanged was the publish trigger.
    //
    // emittedAtNs is the wall-clock time the render thread emitted the
    // signal (UTC ms × 1e6). The receiving slot subtracts its own
    // wall-clock time to compute Qt::QueuedConnection dispatch latency
    // — i.e. how long the GUI thread sat on the queued event before
    // running the slot. High dispatch latency means the GUI thread was
    // blocked by *something else* during that window.
    void presented(qint64 emittedAtNs);

    // Fired from the render thread immediately before exiting due to
    // detected D3D11 device removal (TDR / driver crash / OOM /
    // GPU power-state transition). The render thread cannot recover
    // by itself — only the GUI thread can call shutdown() + initialise()
    // on Core to get a fresh D3D11 device, and only the GUI thread can
    // restart the renderer.
    //
    // The surface listens to this on the GUI thread via Qt::QueuedConnection
    // and runs the recovery dance: stop renderer → shutdown core →
    // re-initialise core (new device + swap chain on the same popup
    // HWND) → restart renderer → republish a snapshot. If recovery
    // succeeds the user sees a brief (<200 ms) flicker; if recovery
    // fails the surface logs and stays detached, requiring an app
    // restart — same end state as before this signal existed, but
    // with diagnostic visibility.
    void deviceLost();

private:
    void renderLoop();
    void processPendingResizeLocked();
    void renderAnimatedFrame();

    PreviewDCompCore* core_ = nullptr;
    PreviewDCompSpritePipeline pipeline_;
    // Phase 3.3b: QImage* → ID3D11ShaderResourceView cache. Render-thread
    // owned; populated lazily as snapshot descriptors arrive. Cleared on
    // stop() before Core's D3D11 device is released.
    PreviewDCompTextureCache textureCache_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> stopRequested_{ false };
    std::atomic<qint64> framesRendered_{ 0 };

    // Snapshot mailbox. The GUI thread enqueues via publishSnapshot under
    // snapshotMutex_; the render thread pops the front entry per frame.
    //
    // Capacity > 1 is the fix for queued-event clusters on the GUI side:
    // when two presented-driven publishes land in rapid succession (Qt's
    // QueuedConnection sometimes coalesces sub-ms between events when the
    // GUI thread alternates busy/idle), the render thread used to see
    // only the latest one in a single-slot mailbox — the intermediate
    // snapshot was overwritten before being consumed, and the renderer's
    // playhead_delta_stats logged a 2-vsync delta as a single sample.
    // With FIFO depth N, those clusters become N consecutive 1-vsync
    // deltas instead, and the render thread renders both frames in
    // sequence (one per vsync).
    //
    // When the queue overflows (GUI consistently faster than vsync, very
    // rare since both are paced to the display rate), drop oldest to keep
    // the latest state visible. When the queue is empty (render thread is
    // ahead, e.g. the GUI thread missed a publish), the render thread
    // falls back to the most recently consumed snapshot — same as the
    // single-slot behaviour at idle.
    static constexpr int kSnapshotQueueCapacity = 3;
    mutable std::mutex snapshotMutex_;
    std::deque<PreviewDCompFrameStateSnapshot> snapshotQueue_;
    PreviewDCompFrameStateSnapshot lastConsumedSnapshot_;

    // Pending-resize queue. The mutex is held only while reading or writing
    // `pendingResizeSize_` and `pendingResizeRequested_`; the renderer
    // copies the pending request out under the lock and drops the lock
    // before calling into Core::resize().
    std::mutex pendingResizeMutex_;
    QSize pendingResizeSize_;
    QSize pendingResizeDisplaySize_;  // Phase 4d-fix7-final2 — see header
    bool pendingResizeRequested_ = false;

    // For colour-cycle animation. Captured at start() so the cycle starts
    // at zero time on each new render thread.
    std::chrono::steady_clock::time_point startTime_;

    // Phase 4e — render-loop pause gate. paused_ is the truth source
    // (atomic so stop() can read it without the cv mutex); pauseMutex_
    // / pauseCv_ park the render thread when paused_ flips to true and
    // wake it when it flips to false.
    std::atomic<bool> paused_{ false };
    std::mutex pauseMutex_;
    std::condition_variable pauseCv_;

    // Phase 4-perf-fix — visual motion smoothness tracking. The render
    // thread compares each unique snapshot's playhead against the
    // previous unique snapshot's playhead and accumulates the delta
    // distribution. Periodic log entries record avg/max/stddev so
    // subsequent builds can be checked for motion-smoothness
    // regressions (the user-visible "clock instability" we just
    // fixed). See logRenderer("playhead_delta_stats", ...).
    qint64 lastSnapshotRevision_ = -1;
    double lastPlayheadMs_ = -1.0;
    double playheadDeltaSumMs_ = 0.0;
    double playheadDeltaSqSumMs_ = 0.0;
    double playheadDeltaMaxMs_ = 0.0;
    double playheadDeltaMinMs_ = 1.0e9;
    qint64 playheadDeltaSampleCount_ = 0;

    // FPS cap (see setPresentSyncInterval). Read from the render thread,
    // written from any thread. 1 = vsync rate; 2 = half; etc.
    std::atomic<unsigned int> presentSyncInterval_{ 1 };
    qint64 lastPresentNs_ = 0;

    // Diagnostic counter — incremented every time WaitForSingleObject hits
    // its 1-second timeout (waitable starved). Reset on a successful
    // wake-up. Logged periodically while >0. Used to chase down the
    // "preview detached after idle / lost focus" pattern: DWM can throttle
    // compositing of unfocused windows, which starves the swap chain's
    // FRAME_LATENCY_WAITABLE_OBJECT, which previously turned the render
    // thread into a 1 Hz spinner that never re-armed the fence. Now we
    // fall through to a forced Present on timeout to break the soft
    // deadlock — this counter records how often that path fires.
    qint64 waitTimeoutCount_ = 0;
};

}  // namespace miacode::preview::dcomp
