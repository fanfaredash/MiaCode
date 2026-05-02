#include "render/PreviewDCompRenderer.h"

#include "common/DebugLog.h"
#include "common/Mmcss.h"

#include <QDateTime>
#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <cmath>

namespace {

// Monotonic, high-resolution timestamp in nanoseconds. Used for the
// `presented` signal payload and any cross-thread interval measurement
// where wall-clock drift / NTP correction would corrupt the data.
// Resolution on Windows is QueryPerformanceCounter (~100 ns); orders
// of magnitude tighter than QDateTime::currentMSecsSinceEpoch's 1 ms.
inline qint64 monotonicNanoseconds()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

namespace miacode::preview::dcomp {

namespace {

void logRenderer(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("render/backend_d3d11/renderer"),
        payload);
}

// HSV → RGB conversion for the colour-cycle test pattern. Hue cycles
// through 0..360° with a 4-second period so the user can clearly see the
// renderer is producing live frames; saturation and value held at 1.0.
void hueToRgb(double hue01, float& r, float& g, float& b)
{
    const double h = std::fmod(hue01, 1.0) * 6.0;
    const int sector = static_cast<int>(std::floor(h));
    const double f = h - sector;
    const float p = 0.0f;
    const float q = static_cast<float>(1.0 - f);
    const float t = static_cast<float>(f);
    switch (sector) {
    case 0: r = 1.0f; g = t;    b = p;    break;
    case 1: r = q;    g = 1.0f; b = p;    break;
    case 2: r = p;    g = 1.0f; b = t;    break;
    case 3: r = p;    g = q;    b = 1.0f; break;
    case 4: r = t;    g = p;    b = 1.0f; break;
    case 5: default:
            r = 1.0f; g = p;    b = q;    break;
    }
}

}  // namespace

PreviewDCompRenderer::PreviewDCompRenderer(QObject* parent)
    : QObject(parent)
{
}

PreviewDCompRenderer::~PreviewDCompRenderer()
{
    stop();
}

bool PreviewDCompRenderer::start(PreviewDCompCore* core)
{
    if (running_.load(std::memory_order_acquire)) {
        return true;  // already running
    }
    if (core == nullptr || !core->isReady()) {
        logRenderer("start_failed",
                    QStringLiteral("reason=%1")
                        .arg(core == nullptr ? QStringLiteral("null_core")
                                              : QStringLiteral("core_not_ready")));
        return false;
    }
    core_ = core;
    stopRequested_.store(false, std::memory_order_release);
    framesRendered_.store(0, std::memory_order_release);
    startTime_ = std::chrono::steady_clock::now();

    // Phase 3.1: spin up the sprite pipeline using Core's D3D11 device.
    // If pipeline init fails (shader compile error, etc.) we fall back
    // to Core::renderClear so the render thread still produces frames —
    // the user sees a magenta clear instead of the textured quad and
    // pipeline-failed shows up in the log.
    if (core_->device() != nullptr) {
        if (!pipeline_.initialise(core_->device())) {
            logRenderer("pipeline_init_failed",
                        QStringLiteral("falling_back_to_clear=1"));
        }
    }

    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this]() { renderLoop(); });
    logRenderer("started",
                QStringLiteral("pipeline_ready=%1")
                    .arg(pipeline_.isReady() ? 1 : 0));
    return true;
}

void PreviewDCompRenderer::stop()
{
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    stopRequested_.store(true, std::memory_order_release);

#ifdef Q_OS_WIN
    // The render thread is parked on WaitForSingleObject. Pulse the
    // waitable so it wakes immediately, sees stopRequested_, and exits
    // cleanly instead of blocking until the next vsync. The handle is
    // owned by Core — if Core is already torn down, frameLatencyWaitable_
    // is null in Core and we just rely on the next vsync wake-up. Either
    // way the join below completes.
    if (core_ != nullptr) {
        if (HANDLE handle = core_->frameLatencyWaitable(); handle != nullptr) {
            ::SetEvent(handle);
        }
    }
#endif

    // Phase 4e — also wake the cv in case the thread is parked on
    // setPaused(true) instead of the waitable. The cv predicate
    // re-checks stopRequested_, so the wait returns and the loop
    // breaks cleanly.
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        pauseCv_.notify_all();
    }

    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
    // Drop cached SRVs before Core releases the D3D11 device — every cached
    // ComPtr holds a ref on a device-owned resource.
    textureCache_.clear();
    pipeline_.shutdown();
    core_ = nullptr;
    logRenderer("stopped",
                QStringLiteral("frames_total=%1").arg(framesRendered_.load()));
}

bool PreviewDCompRenderer::isRunning() const
{
    return running_.load(std::memory_order_acquire);
}

void PreviewDCompRenderer::requestResize(QSize newPixelSize, QSize displaySize)
{
    if (newPixelSize.width() <= 0 || newPixelSize.height() <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(pendingResizeMutex_);
    pendingResizeSize_ = newPixelSize;
    pendingResizeDisplaySize_ = displaySize.isValid() ? displaySize : newPixelSize;
    pendingResizeRequested_ = true;
}

qint64 PreviewDCompRenderer::framesRendered() const
{
    return framesRendered_.load(std::memory_order_acquire);
}

bool PreviewDCompRenderer::isActivelyRendering() const
{
    return running_.load(std::memory_order_acquire)
        && !paused_.load(std::memory_order_acquire);
}

void PreviewDCompRenderer::publishSnapshot(const PreviewDCompFrameStateSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshotQueue_.push_back(snapshot);
    // Drop oldest entries past the cap. Steady-state depth is 0-1; a
    // pair-cluster lands depth 2; depth 3 has margin before we start
    // shedding. Hitting 3 means the GUI is consistently outpacing the
    // render thread — rare since both are vsync-paced.
    while (snapshotQueue_.size() > static_cast<size_t>(kSnapshotQueueCapacity)) {
        snapshotQueue_.pop_front();
    }
}

void PreviewDCompRenderer::setPaused(bool paused)
{
    const bool wasPaused = paused_.exchange(paused, std::memory_order_acq_rel);
    if (wasPaused == paused) {
        return;
    }
    if (!paused) {
        // Resume: wake the render thread so it re-evaluates its loop
        // condition and rejoins the frame-latency wait.
        std::lock_guard<std::mutex> lock(pauseMutex_);
        pauseCv_.notify_all();
    }
    logRenderer(paused ? "paused" : "resumed");
}

void PreviewDCompRenderer::setPresentSyncInterval(unsigned int syncInterval)
{
    const unsigned int clamped = std::clamp<unsigned int>(syncInterval, 1U, 4U);
    const unsigned int prev = presentSyncInterval_.exchange(clamped, std::memory_order_release);
    if (prev != clamped) {
        logRenderer("present_sync_interval",
                    QStringLiteral("interval=%1").arg(clamped));
    }
}

void PreviewDCompRenderer::renderLoop()
{
#ifdef Q_OS_WIN
    if (core_ == nullptr) {
        return;
    }
    HANDLE waitable = core_->frameLatencyWaitable();
    if (waitable == nullptr) {
        logRenderer("loop_aborted", QStringLiteral("reason=null_waitable"));
        return;
    }

    // Tag the thread for diagnostic tools. Using SetThreadDescription means
    // any debugger / WPA capture shows "MiaCodeDComp" instead of an
    // anonymous worker — useful when correlating render-thread CPU usage
    // against frame pacing data.
    ::SetThreadDescription(::GetCurrentThread(), L"MiaCodeDComp");

    // Phase 4b-perf — register with MMCSS so the OS scheduler protects
    // this thread from being preempted by background work (indexing,
    // antivirus, normal-priority threads). Qt's QSG render thread does
    // the same; without it our render thread takes occasional ~22 ms
    // hits from being scheduled out for one quantum, which the user
    // sees as the HUD's `stut` count >20.
    const auto mmcssResult =
        miacode::mmcss::registerCurrentThread(QStringLiteral("Games"));
    logRenderer("mmcss_register",
                QStringLiteral("registered=%1 task=%2 reason=%3 errno=%4")
                    .arg(mmcssResult.registered ? 1 : 0)
                    .arg(mmcssResult.taskClassUsed.isEmpty()
                         ? QStringLiteral("(none)") : mmcssResult.taskClassUsed)
                    .arg(mmcssResult.skipReason.isEmpty()
                         ? QStringLiteral("(ok)") : mmcssResult.skipReason)
                    .arg(mmcssResult.lastErrorCode));

    while (!stopRequested_.load(std::memory_order_acquire)) {
        // Phase 4e — pause gate. When the host window is minimised
        // or hidden, paused_ flips to true and we park here on the
        // condition variable instead of spinning on the frame
        // latency waitable. setPaused(false) signals the cv to
        // wake us; stop() also wakes us by setting stopRequested_
        // and notifying the cv (see end of stop()).
        if (paused_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(pauseMutex_);
            pauseCv_.wait(lock, [this]() {
                return !paused_.load(std::memory_order_acquire)
                    || stopRequested_.load(std::memory_order_acquire);
            });
            if (stopRequested_.load(std::memory_order_acquire)) {
                break;
            }
            // Fall through: resume on the next iteration.
        }

        // Plan §2: block on the GPU fence so we wake exactly once per
        // vsync slot when DXGI is ready to enqueue the next present.
        // 1000 ms timeout (was INFINITE) so a stuck DXGI fence — e.g.,
        // after DXGI_ERROR_DEVICE_REMOVED where Present fails and the
        // waitable stops signalling forever — can't deadlock the render
        // thread. On WAIT_TIMEOUT we fall through to the device-removed
        // check below; if the device is still alive we loop and wait
        // again on the next iteration.
        const DWORD waitResult = ::WaitForSingleObject(waitable, 1000);

        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }

        // Detect DXGI device removal. After DEVICE_REMOVED the swap chain
        // permanently fails Present and the frame-latency waitable stops
        // signalling. Without this check the render thread would loop
        // forever on either WAIT_TIMEOUT (waitable starved) or successful
        // wakes followed by failing Presents, never producing useful
        // output. Exit the loop cleanly so the GUI thread sees the
        // renderer is no longer active and subsequent re-attach (or
        // app restart) can recreate the device.
        //
        // Observed in user logs at 06:08:30.623 during rapid progress-bar
        // drag: [preview/dcomp] op=present hr=0x887a0005 — the device was
        // lost (likely TDR or OOM from rapid texture-cache eviction) and
        // the render thread froze indefinitely. This check unsticks it.
        if (core_->isDeviceRemoved()) {
            logRenderer("device_removed_exit",
                        QStringLiteral("frames_total=%1 wait_result=0x%2")
                            .arg(framesRendered_.load())
                            .arg(static_cast<unsigned long>(waitResult), 8, 16, QChar('0')));
            // Notify the GUI thread so it can recover the device.
            // QueuedConnection is implied because the listener is on a
            // different thread; the emit returns immediately and we
            // exit the loop. The surface's slot will call stop() on
            // us (which joins this thread) → shutdown core → re-init
            // core → start a new renderer thread. Until that completes
            // the preview shows the last presented frame.
            emit deviceLost();
            break;
        }

        if (waitResult == WAIT_TIMEOUT) {
            // No vsync signal in 1 s but device is alive. Possible causes:
            //   - DWM throttled compositing because the editor window
            //     went idle / lost focus / got occluded
            //   - QML scene became invisible and DComp visual stopped
            //     getting composited (no buffer flip = no waitable signal)
            //   - Driver hiccup
            //
            // We must NOT just `continue` here, because the waitable only
            // gets re-armed by a successful Present(). A bare continue
            // would loop forever at 1 Hz, never re-arming the fence. The
            // GUI side would see the render thread "alive but doing
            // nothing" — exactly the user-reported "detach" symptom
            // ("the chart preview detaches and clicking play doesn't
            // refresh it" after the editor sits idle for a while).
            //
            // Instead, fall through to the normal render-and-present
            // path. Present will either:
            //   (a) succeed → re-arms the waitable, normal pacing resumes
            //   (b) fail → next iteration's isDeviceRemoved() catches it
            //       and we exit cleanly
            // Either way the soft-deadlock breaks. Log a counter so the
            // diagnostic shows up if this fires repeatedly.
            ++waitTimeoutCount_;
            if ((waitTimeoutCount_ % 4) == 1) {  // log every 4 timeouts
                logRenderer("wait_timeout_force_present",
                            QStringLiteral("timeouts_total=%1 frames_total=%2 paused=%3")
                                .arg(waitTimeoutCount_)
                                .arg(framesRendered_.load())
                                .arg(paused_.load() ? 1 : 0));
            }
            // Fall through to the present path.
        } else if (waitResult != WAIT_OBJECT_0) {
            logRenderer("wait_unexpected",
                        QStringLiteral("result=0x%1")
                            .arg(static_cast<unsigned long>(waitResult), 8, 16, QChar('0')));
            // Keep going — a transient wait failure shouldn't kill the
            // loop, but log it so we can find it if it correlates with
            // perceived stutter.
        } else {
            // Healthy wait — reset the timeout counter so the diagnostic
            // log only fires during sustained starvation.
            if (waitTimeoutCount_ != 0) {
                logRenderer("wait_recovered",
                            QStringLiteral("after_timeouts=%1").arg(waitTimeoutCount_));
                waitTimeoutCount_ = 0;
            }
        }

        // FPS-cap moved out of this gate (Issue #3 take 2). The previous
        // attempt — `continue` to skip a present — froze the render
        // thread because the DXGI frame-latency waitable only signals
        // after a successful Present consumes a back-buffer slot. With
        // no Present we got no further signals; the render loop spun
        // on a stuck wait, producing dcomp_total=2 across whole sessions.
        //
        // Replacement strategy: pass `syncInterval` to ::Present instead
        // of skipping. Present(N, 0) blocks for N vsyncs, so on a 120 Hz
        // display Present(2, 0) yields 60 FPS naturally, with the
        // waitable signalling once per actual present (no stuck wait).
        // The GUI computes N from the user's target FPS option and the
        // display refresh rate (it has QScreen::refreshRate()) and pushes
        // it via setPresentSyncInterval(). See PreviewDCompCore::present.

        // Apply any pending resize between wake-up and render. This is the
        // only safe spot per IDXGISwapChain::ResizeBuffers — there are no
        // outstanding presents because the waitable was just signalled.
        // Copy the request out under the lock then release it before
        // calling Core::resize(), so a fresh requestResize() from the GUI
        // thread can queue a follow-up while ResizeBuffers runs.
        QSize sizeToApply;
        QSize displayToApply;
        bool needsResize = false;
        {
            std::lock_guard<std::mutex> lock(pendingResizeMutex_);
            if (pendingResizeRequested_) {
                sizeToApply = pendingResizeSize_;
                displayToApply = pendingResizeDisplaySize_;
                pendingResizeRequested_ = false;
                needsResize = true;
            }
        }
        if (needsResize) {
            // Phase 4d-fix7-final2 — pass displaySize to resize() so the
            // post-resize VT re-apply uses the value that was paired
            // with this resize request, not whatever the GUI thread
            // has set lastVisualTransformDisplaySize_ to since.
            core_->resize(sizeToApply, displayToApply);
        }

        renderAnimatedFrame();
        framesRendered_.fetch_add(1, std::memory_order_release);
        // Record present time for the FPS-cap gate above.
        lastPresentNs_ = monotonicNanoseconds();
        // Drive vsync-aligned publishing on the GUI thread. The signal
        // crosses threads via Qt::QueuedConnection; the cost is one
        // QEvent post per frame, dwarfed by the work the GUI thread
        // does in response.
        //
        // Carries a *monotonic high-resolution* (~100 ns on Windows)
        // timestamp captured immediately after Present(1, 0) returns.
        // The GUI slot uses this to advance renderPlayheadSeconds_ by
        // the actual vsync interval rather than by the delta between
        // GUI-thread queued-event landing times — which inherits Qt's
        // queued-dispatch jitter (typically 0.5-3 ms, can spike to
        // 10 ms under contention) and would otherwise propagate into
        // the playhead and be visible as motion irregularity.
        emit presented(lastPresentNs_);
    }

    // Pair with the MMCSS registration above. The handle is per-thread;
    // unregister here so the OS reclaims the slot before this thread
    // exits. Safe to call even if registration failed.
    miacode::mmcss::unregisterCurrentThread();
#endif
}

void PreviewDCompRenderer::renderAnimatedFrame()
{
    if (core_ == nullptr) {
        return;
    }

#ifdef Q_OS_WIN
    // Pop the front of the snapshot queue (FIFO). When the queue is
    // empty (GUI hasn't published since last consume — happens at start
    // and during paused state), reuse the last consumed snapshot so the
    // renderer keeps presenting valid content instead of flashing to a
    // default-constructed frame.
    //
    // The lock is held for one std::move + a possible deque pop — tens
    // of nanoseconds. Render path contention is not a concern at this
    // scale; snapshotMutex_ is uncontended in steady state because the
    // GUI publish runs to completion between vsync waits.
    PreviewDCompFrameStateSnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        if (!snapshotQueue_.empty()) {
            snapshot = std::move(snapshotQueue_.front());
            snapshotQueue_.pop_front();
            lastConsumedSnapshot_ = snapshot;
        } else {
            snapshot = lastConsumedSnapshot_;
        }
    }

    // Phase 3.3a diagnostic: log sprite/image counts every ~60 frames
    // so we can verify the snapshot is carrying the layer-state output.
    // The threshold check on first non-zero sprite count fires once
    // when chart content first reaches the render thread.
    static thread_local qint64 s_lastLoggedSpriteCount = -1;
    if (snapshot.sprites.size() != s_lastLoggedSpriteCount
        && (framesRendered_.load() % 60) == 0) {
        s_lastLoggedSpriteCount = snapshot.sprites.size();
        logRenderer("snapshot_sprite_count",
                    QStringLiteral("revision=%1 sprites=%2 retained_images=%3 playhead=%4")
                        .arg(snapshot.revision)
                        .arg(snapshot.sprites.size())
                        .arg(snapshot.retainedImages.size())
                        .arg(snapshot.playheadSeconds, 0, 'f', 3));
    }

    // Phase 4-perf-fix — visual motion smoothness tracking. Compare
    // each unique snapshot's playhead against the previous unique
    // snapshot's playhead; an even cadence (each delta close to 1
    // frame at the target fps) means smooth motion, large
    // deltas/stddev mean visible jitter. Stats accumulate over a
    // rolling window and log periodically so subsequent code changes
    // can be regression-checked against the recorded baseline.
    if (snapshot.revision != lastSnapshotRevision_
        && lastSnapshotRevision_ != -1) {
        const double currentMs = snapshot.playheadSeconds * 1000.0;
        if (lastPlayheadMs_ >= 0.0) {
            const double delta = currentMs - lastPlayheadMs_;
            // Exclude reverse seeks, pause→play resumes (large jumps),
            // and the first-frame nonsense.
            if (delta > 0.0 && delta < 100.0) {
                playheadDeltaSumMs_ += delta;
                playheadDeltaSqSumMs_ += delta * delta;
                playheadDeltaMaxMs_ = std::max(playheadDeltaMaxMs_, delta);
                playheadDeltaMinMs_ = std::min(playheadDeltaMinMs_, delta);
                playheadDeltaSampleCount_ += 1;
            }
        }
        lastPlayheadMs_ = currentMs;
    }
    lastSnapshotRevision_ = snapshot.revision;

    // Log every ~60 unique snapshots (1 second at 60 Hz) so short
    // playback sessions still get data. Accumulate a fresh window
    // after each log so the metric reflects recent behaviour rather
    // than a session average that hides recent regressions.
    if (playheadDeltaSampleCount_ >= 60) {
        const double avg = playheadDeltaSumMs_
            / static_cast<double>(playheadDeltaSampleCount_);
        const double meanSq = playheadDeltaSqSumMs_
            / static_cast<double>(playheadDeltaSampleCount_);
        const double variance = std::max(0.0, meanSq - avg * avg);
        const double stddev = std::sqrt(variance);
        logRenderer("playhead_delta_stats",
                    QStringLiteral("avg_ms=%1 min_ms=%2 max_ms=%3 stddev_ms=%4 samples=%5")
                        .arg(avg, 0, 'f', 3)
                        .arg(playheadDeltaMinMs_, 0, 'f', 3)
                        .arg(playheadDeltaMaxMs_, 0, 'f', 3)
                        .arg(stddev, 0, 'f', 3)
                        .arg(playheadDeltaSampleCount_));
        playheadDeltaSumMs_ = 0.0;
        playheadDeltaSqSumMs_ = 0.0;
        playheadDeltaMaxMs_ = 0.0;
        playheadDeltaMinMs_ = 1.0e9;
        playheadDeltaSampleCount_ = 0;
    }

    // Phase 3.3b/c: render the snapshot's sprite descriptors through the
    // full sprite pipeline. Texture cache resolves QImage → SRV; pipeline
    // groups by SRV and issues one Draw per texture run. If the pipeline
    // failed to initialise we fall back to a colour-cycle clear so the
    // failure mode is visible.
    const unsigned int syncInterval =
        presentSyncInterval_.load(std::memory_order_acquire);
    if (pipeline_.isReady()) {
        pipeline_.renderSnapshot(core_->context(),
                                  core_->device(),
                                  core_->backBufferRtv(),
                                  core_->swapChainPixelSize(),
                                  snapshot,
                                  textureCache_);
        core_->present(syncInterval);
    } else {
        // Fallback: colour-cycle clear so we still have something on
        // screen for the user to see. renderClear internally calls
        // present(1) — fine, the fallback isn't perf-critical.
        const auto now = std::chrono::steady_clock::now();
        const double seconds =
            std::chrono::duration<double>(now - startTime_).count();
        constexpr double kCyclePeriodSeconds = 4.0;
        const double hue = std::fmod(seconds / kCyclePeriodSeconds, 1.0);
        float r = 0.0f, g = 0.0f, b = 0.0f;
        hueToRgb(hue, r, g, b);
        core_->renderClear(r, g, b, 1.0f);
    }
#endif
}

}  // namespace miacode::preview::dcomp
