#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include <QtGlobal>

#include "PreviewAudioOutputGlitchProbe.h"

// Lock-free single-producer/single-consumer ring buffer carrying
// output_glitch::GlitchEvent from the BASS DSP callback (producer, BASS's own
// mixing thread) to PreviewAudioWorker (consumer, drained once per health
// sample -- see BassPreviewAudioBackend::drainOutputGlitchEvents). Fixed-
// capacity POD storage: no allocation, no locks, no exceptions on either side,
// so pushing from the audio-callback thread cannot violate the constraints
// documented at the top of PreviewAudioOutputGlitchProbe.h.
namespace miacode::preview_audio::output_glitch {

class GlitchRing
{
public:
    // A drained-once-per-second cadence only needs to absorb bursts within that
    // window. Sized well above the volumes exercised in
    // PreviewAudioOutputGlitchProbeSpec's clip/step stress cases; a full ring
    // only means the worker missed some events in a single drain window, not
    // that any state is corrupted -- droppedCount() below still reports it.
    static constexpr std::size_t kCapacity = 512;

    // Producer side. Call only from the BASS DSP callback thread.
    bool tryPush(const GlitchEvent& event)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t nextHead = advance(head);
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        events_[head] = event;
        head_.store(nextHead, std::memory_order_release);
        return true;
    }

    // Consumer side. Call only from PreviewAudioWorker's thread.
    bool tryPop(GlitchEvent* out)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        *out = events_[tail];
        tail_.store(advance(tail), std::memory_order_release);
        return true;
    }

    // Consumer side. Reads-and-clears so each drain reports only events dropped
    // since the previous drain, not a running total.
    quint64 takeDroppedCount()
    {
        return droppedCount_.exchange(0, std::memory_order_relaxed);
    }

    // Drops any unread events and resets bookkeeping. Only safe to call when the
    // producer thread cannot be pushing concurrently -- i.e. after the DSP has
    // been detached (see BassPreviewAudioBackend::detachOutputGlitchProbe), not
    // from inside the DSP callback itself. The atomics here make this class
    // non-copyable/non-movable by default (see the deleted operator= this
    // method exists to stand in for), which is intentional: a torn copy while
    // the audio thread might still be pushing would be a real bug, so the
    // backend resets the ring in place instead of assigning a fresh one over it.
    void reset()
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        droppedCount_.store(0, std::memory_order_relaxed);
    }

private:
    static std::size_t advance(std::size_t index)
    {
        const std::size_t next = index + 1;
        return next == kCapacity ? 0 : next;
    }

    std::array<GlitchEvent, kCapacity> events_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<quint64> droppedCount_{0};
};

}  // namespace miacode::preview_audio::output_glitch
