#include "preview/dcomp/PreviewDCompRenderer.h"

#include "common/DebugLog.h"

#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cmath>

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
        QStringLiteral("preview/dcomp/renderer"),
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

void PreviewDCompRenderer::requestResize(QSize newPixelSize)
{
    if (newPixelSize.width() <= 0 || newPixelSize.height() <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(pendingResizeMutex_);
    pendingResizeSize_ = newPixelSize;
    pendingResizeRequested_ = true;
}

qint64 PreviewDCompRenderer::framesRendered() const
{
    return framesRendered_.load(std::memory_order_acquire);
}

void PreviewDCompRenderer::publishSnapshot(const PreviewDCompFrameStateSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = snapshot;
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

    while (!stopRequested_.load(std::memory_order_acquire)) {
        // Plan §2: block on the GPU fence so we wake exactly once per
        // vsync slot when DXGI is ready to enqueue the next present.
        // INFINITE because stop() wakes us via SetEvent.
        const DWORD waitResult = ::WaitForSingleObject(waitable, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            logRenderer("wait_unexpected",
                        QStringLiteral("result=0x%1")
                            .arg(static_cast<unsigned long>(waitResult), 8, 16, QChar('0')));
            // Keep going — a transient wait failure shouldn't kill the
            // loop, but log it so we can find it if it correlates with
            // perceived stutter.
        }
        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }

        // Apply any pending resize between wake-up and render. This is the
        // only safe spot per IDXGISwapChain::ResizeBuffers — there are no
        // outstanding presents because the waitable was just signalled.
        // Copy the request out under the lock then release it before
        // calling Core::resize(), so a fresh requestResize() from the GUI
        // thread can queue a follow-up while ResizeBuffers runs.
        QSize sizeToApply;
        bool needsResize = false;
        {
            std::lock_guard<std::mutex> lock(pendingResizeMutex_);
            if (pendingResizeRequested_) {
                sizeToApply = pendingResizeSize_;
                pendingResizeRequested_ = false;
                needsResize = true;
            }
        }
        if (needsResize) {
            core_->resize(sizeToApply);
        }

        renderAnimatedFrame();
        framesRendered_.fetch_add(1, std::memory_order_release);
    }
#endif
}

void PreviewDCompRenderer::renderAnimatedFrame()
{
    if (core_ == nullptr) {
        return;
    }

#ifdef Q_OS_WIN
    // Phase 3.2: copy the latest published snapshot under the lock.
    // The lock is held for one struct copy (< 100 ns) — render path
    // contention is not a concern at this scale.
    PreviewDCompFrameStateSnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot = snapshot_;
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

    // Phase 3.3b/c: render the snapshot's sprite descriptors through the
    // full sprite pipeline. Texture cache resolves QImage → SRV; pipeline
    // groups by SRV and issues one Draw per texture run. If the pipeline
    // failed to initialise we fall back to a colour-cycle clear so the
    // failure mode is visible.
    if (pipeline_.isReady()) {
        pipeline_.renderSnapshot(core_->context(),
                                  core_->device(),
                                  core_->backBufferRtv(),
                                  core_->swapChainPixelSize(),
                                  snapshot,
                                  textureCache_);
        core_->present();
    } else {
        // Fallback: colour-cycle clear so we still have something on
        // screen for the user to see.
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
